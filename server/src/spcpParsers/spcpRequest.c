//
// Created by francisco on 25/10/18.
//

#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include "spcpRequest.h"
#include "../utils/buffer.h"

#define N(x) (sizeof(x)/sizeof((x)[0]))

static void
remaining_args_set(struct spcp_request_parser *p, const uint8_t n) {
    p->nargs_i = 0;
    p->nargs = n;
}

static int
remaining_args_is_done(struct spcp_request_parser *p) {
    return p->nargs_i >= p->nargs;
}


static void
arg_size_set(struct spcp_request_parser *p, const uint8_t n) {
    p->arg_size_i = 0;
    p->arg_size = n;
}

static int
arg_is_done(struct spcp_request_parser *p) {
    return p->arg_size_i >= p->arg_size;
}

extern void
spcp_request_parser_init(struct spcp_request_parser *p) {
    p->state = spcp_request_cmd;
    memset(&p->request, 0, sizeof(p->request));
}


static enum spcp_request_state
parse_request_cmd(struct spcp_request_parser *p, const uint8_t c) {
    if(c <= 0x09 && c != 0x08){
        p->request.cmd = c;
        return spcp_request_nargs;
    }
    return spcp_request_error_invalid_command;
}

static enum spcp_request_state
parse_request_nargs(struct spcp_request_parser *p, const uint8_t c) {
    remaining_args_set(p, c);
    if(remaining_args_is_done(p)){
       return spcp_request_done;
    }
    return spcp_request_arg_size;
}

static enum spcp_request_state
parse_request_arg_size(struct spcp_request_parser *p, const uint8_t c) {
    arg_size_set(p, c);
    if(p->nargs_i == 0) {
        p->request.arg0 = malloc(c+1);
        p->request.arg0_size = c;
    } else if( p->nargs_i == 1) {
        p->request.arg1 = malloc(c+1);
        p->request.arg1_size = c;
    }
    return spcp_request_arg;
}

static enum spcp_request_state
request_validate(struct spcp_request_parser *p);

static enum spcp_request_state
parse_request_arg(struct spcp_request_parser *p, const uint8_t c) {

    if(p->nargs_i == 0) {
        p->request.arg0[p->arg_size_i++] = c;
    } else if( p->nargs_i == 1) {
        p->request.arg1[p->arg_size_i++] = c;
    }

    if(arg_is_done(p)) {
        if(p->nargs_i == 0) {
            p->request.arg0[p->arg_size_i] = '\0';
        } else if( p->nargs_i == 1) {
            p->request.arg1[p->arg_size_i] = '\0';
        }

        p->nargs_i++;
        if(remaining_args_is_done(p)){
            return spcp_request_done;
        } else {
            return spcp_request_arg_size;
        }
    }
    return spcp_request_arg;
}

extern enum spcp_request_state
spcp_request_parser_feed(struct spcp_request_parser *p, const uint8_t c){
    enum spcp_request_state next;

    switch(p->state){
        case spcp_request_cmd:
            next = parse_request_cmd(p, c);
            break;
        case spcp_request_nargs:
            next = parse_request_nargs(p, c);
            break;
        case spcp_request_arg_size:
            next = parse_request_arg_size(p, c);
            break;
        case spcp_request_arg:
            next = parse_request_arg(p, c);
            break;
        case spcp_request_done:
        case spcp_request_error_invalid_command:
        case spcp_request_error_invalid_arguments:
        case spcp_request_error:
            next = p->state;
            break;
        default:
            next = spcp_request_error;
            break;
    }
    p->state = next;
    return next;
}

extern bool
spcp_request_is_done(const enum spcp_request_state st, bool *errored) {
    if (st >= spcp_request_error && errored != 0) {
        *errored = true;
    }
    return st >= spcp_request_done;
}

extern enum spcp_request_state
spcp_request_consume(buffer *b, struct spcp_request_parser *p, bool *errored) {
    enum spcp_request_state st = p->state;

    while(buffer_can_read(b)) {
        const uint8_t c = buffer_read(b);
        st = spcp_request_parser_feed(p, c);
        if(spcp_request_is_done(st, errored)) {
            break;
        }
    }
    return st;
}

extern int
spcp_data_request_marshall(buffer *b, uint8_t status, char *data, size_t data_len){
    uint8_t *ptr;
    size_t  count;
    ptr = buffer_write_ptr(b, &count);
    if(count < data_len + 2)
        return -1;

    buffer_write(b, status);
    buffer_write(b, (uint8_t)data_len);
    memcpy(ptr + 2, data, data_len);
    buffer_write_adv(b, data_len + 2);

    return data_len + 2;
}

extern int
spcp_no_data_request_marshall(buffer *b, uint8_t status){
    size_t n;
    uint8_t *buff = buffer_write_ptr(b, &n);
    if(n < 1){
        return -1;
    }
    buff[0] = status;
    buffer_write_adv(b, 1);
    return 1;
}

extern void
spcp_request_close(struct spcp_request_parser *p) {
    if(p->request.arg0 != NULL)
        free(p->request.arg0);
    if(p->request.arg1 != NULL)
        free(p->request.arg1);
}
