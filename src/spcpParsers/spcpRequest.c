//
// Created by francisco on 25/10/18.
//

#include <string.h>
#include <stdint.h>
#include <stdbool.h>
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
remaining_arg_set(struct spcp_request_parser *p, const uint8_t n) {
    p->narg_i = 0;
    p->narg = n;
}

static int
remaining_arg_is_done(struct spcp_request_parser *p) {
    return p->narg_i >= p->narg;
}

extern enum spcp_request_state
request_validate(struct spcp_request_parser *p) {
    //TODO: validate the command based on the inputs
}

extern void
spcp_request_parser_init(struct spcp_request_parser *p){
    p->state = request_cmd;
    memset(p->request, 0, sizeof(*(p->request)));
}


extern enum spcp_request_state
parse_request_cmd(struct spcp_request_parser *p, const uint8_t c) {
    if(c <= 0x09){
        p->request->cmd = c;
        return request_nargs;
    }
    return request_error_invalid_command;
}

extern enum spcp_request_state
parse_request_nargs(struct spcp_request_parser *p, const uint8_t c) {
    remaining_args_set(p, c);
    return request_narg;
}

extern enum spcp_request_state
parse_request_narg(struct spcp_request_parser *p, const uint8_t c) {
    remaining_arg_set(p, c);
    return request_narg;
}

extern enum spcp_request_state
parse_request_arg(struct spcp_request_parser *p, const uint8_t c) {

    //parse arg REMEMBER TO INCREMENT INDEXES

    if(remaining_arg_is_done(p)) {
        if(remaining_args_is_done(p)){
            return request_validate(p);
        } else {
            return request_narg;
        }
    }
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
        case request_narg:
            next = parse_request_narg(p, c);
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
    if(st >= request_error && errored != 0) {
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

extern int
spcp_request_marshall(buffer *b, char *data){
    return -1;
}

enum spcp_response_status
spcp_cmd_resolve(struct spcp_request *request) {

}