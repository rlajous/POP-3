//
// Created by francisco on 23/10/18.
//

#include <string.h>
#include <stdlib.h>
#include "pop3response.h"
#include "pop3request.h"


enum response_state
detect_status(struct response_parser *p, const uint8_t c) {
    if(c == '+'){
        p->pop3_response_success = true;
    } else if (c == '-') {
        p->pop3_response_success = false;
        p->request->multi = false;
    }
    return response_byte;
}

enum response_state
new_line(struct response_parser *p, const uint8_t c) {
    if(c == '.'){
        return response_dot;
    }
    else {
        return response_byte;
    }
}

enum response_state
dot(struct response_parser *p, const uint8_t c) {
    if(c == '\r') {
        return response_dot_cr;
    } else {
        return response_byte;
    }
}

enum response_state
dot_cr(struct response_parser *p, const uint8_t c) {
    if(c == '\n') {
        return response_done;
    } else {
        return response_byte;
    }
}

enum response_state
byte(struct response_parser *p, const uint8_t c) {
    if(c == '\r') {
        return response_cr;
    } else {
        return response_byte;
    }
}

enum response_state
cr(struct response_parser *p, const uint8_t c) {
    if(c == '\n'){
        if(p->request->multi) {
            return response_new_line;
        } else {
            return response_done;
        }
    } else {
        return response_byte;
    }
}

extern enum response_state
response_parser_feed(struct response_parser *p, const uint8_t c) {
    enum response_state next;

    switch (p->response_state) {
        case response_detect_status:
            next = detect_status(p, c);
            break;
        case response_new_line:
            next = new_line(p, c);
            break;
        case response_dot:
            next = dot(p, c);
            break;
        case response_dot_cr:
            next = dot_cr(p, c);
            break;
        case response_byte:
            next = byte(p, c);
            break;
        case response_cr:
            next = cr(p, c);
            break;
        case response_done:
            next = response_done;
            break;
        default:
            next = response_error;
    }

    return p->response_state =  next;
}

extern void
response_parser_init(struct response_parser *p, struct request* request) {
    p->response_state = response_detect_status;
    p->pop3_response_success = false;
    p->request = request;
}

extern bool
response_is_done(enum response_state st, bool *errored) {

    return st == response_done;
}


enum response_state
response_consume(buffer *rb, struct response_parser *p){
    const  uint8_t c = buffer_parse(rb);
    enum response_state st = response_parser_feed(p, c);
    return st;
}



void
response_close(struct response_parser *p) {
    free(p->request);
}