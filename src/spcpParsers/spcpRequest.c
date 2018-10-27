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
    p->state = request_cmd;
    memset(p->request, 0, sizeof(*(p->request)));
}


static enum spcp_request_state
parse_request_cmd(struct spcp_request_parser *p, const uint8_t c) {
    if(c <= 0x09){
        p->request->cmd = c;
        return request_nargs;
    }
    return request_error_invalid_command;
}

static enum spcp_request_state
parse_request_nargs(struct spcp_request_parser *p, const uint8_t c) {
    remaining_args_set(p, c);
    return request_arg_size;
}

static enum spcp_request_state
parse_request_arg_size(struct spcp_request_parser *p, const uint8_t c) {
    arg_size_set(p, c);
    if(p->nargs_i == 0) {
        p->request->arg0 = malloc(c);
        p->request->arg0_size = c;
    } else if( p->nargs_i == 1) {
        p->request->arg1 = malloc(c);
        p->request->arg1_size = c;
    }
    return request_arg;
}

static enum spcp_request_state
request_validate(struct spcp_request_parser *p);

static enum spcp_request_state
parse_request_arg(struct spcp_request_parser *p, const uint8_t c) {

    if(p->nargs_i == 0) {
        p->request->arg0[p->arg_size_i++] = c;
    } else if( p->nargs_i == 1) {
        p->request->arg1[p->arg_size_i++] = c;
    }

    if(arg_is_done(p)) {
        if(remaining_args_is_done(p)){
            return request_validate(p);
        } else {
            p->nargs_i++;
            return request_arg_size;
        }
    }
    return request_arg;
}

extern enum spcp_request_state
spcp_request_parser_feed(struct spcp_request_parser *p, const uint8_t c){
    enum spcp_request_state next;

    switch(p->state){
        case request_cmd:
            next = parse_request_cmd(p, c);
            break;
        case request_nargs:
            next = parse_request_nargs(p, c);
            break;
        case request_arg_size:
            next = parse_request_arg_size(p, c);
            break;
        case request_arg:
            next = parse_request_arg(p, c);
            break;
        case request_done:
        case request_error_invalid_command:
        case request_error_invalid_arguments:
        case request_error:
            next = p->state;
            break;
        default:
            next = request_error;
            break;
    }
    return next;
}

extern bool
spcp_request_is_done(const enum spcp_request_state st, bool *errored) {
    if (st >= request_error && errored != 0) {
        *errored = true;
    }
    return st >= request_done;
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

static enum spcp_request_state
request_validate(struct spcp_request_parser *p) {
    //TODO: validate the command based on the inputs
}

enum spcp_response_status
spcp_cmd_resolve(struct spcp_request *request) {

}

extern int
spcp_data_request_marshall(buffer *b, uint8_t status, char *data){
    return -1;
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


