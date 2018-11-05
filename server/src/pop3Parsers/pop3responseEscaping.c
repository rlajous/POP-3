//
// Created by francisco on 03/11/18.
//

#include <string.h>
#include <stdlib.h>
#include "pop3responseEscaping.h"
#include "pop3response.h"


enum response_state
e_new_line(struct escape_response_parser *p, const uint8_t c, buffer *b) {
    if(c == '.'){
        buffer_write(b, c);
    }
    if(c == '\r'){
        return response_cr;
    }
    return response_byte;

}

enum response_state
e_byte(struct escape_response_parser *p, const uint8_t c) {
    if(c == '\r') {
        return response_cr;
    } else {
        return response_byte;
    }
}

enum response_state
e_cr(struct escape_response_parser *p, const uint8_t c) {
    if(c == '\n'){
        return response_new_line;
    } else {
        return response_byte;
    }
}

extern enum response_state
escape_response_parser_feed(struct escape_response_parser *p, const uint8_t c, buffer *b) {
    enum response_state next;

    switch (p->response_state) {
        case response_new_line:
            next = e_new_line(p, c, b);
            break;
        case response_byte:
            next = e_byte(p, c);
            break;
        case response_cr:
            next = e_cr(p, c);
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
escape_response_parser_init(struct escape_response_parser *p, size_t response_size) {
    p->response_state = response_new_line;
    p->response_size_i = 0;
    p->response_size_n = response_size;
}

extern bool
escape_response_is_done(struct escape_response_parser *p) {
    return p->response_size_n <= p->response_size_i;
}


enum response_state
escape_response_consume(buffer *rb, buffer *wb, struct escape_response_parser *p, bool *errored) {
    enum response_state st = p->response_state;
    size_t n;
    buffer_write_ptr(wb, &n);
    //TODO: Cuidado que pueden quedar cosas colgadas en el readbuffer. Probablemente debemos loopear exteriormente.
    while(buffer_can_read(rb) && n > 2){
        const  uint8_t c = buffer_read(rb);
        st = escape_response_parser_feed(p, c, wb);
        buffer_write(wb, c);
        p->response_size_i++;
        buffer_write_ptr(wb, &n);
    }
    return st;
}

void
escape_response_close(struct escape_response_parser *p) {
    //nada que hacer
}

int
response_add_termination(buffer *wb) {
    size_t n;
    uint8_t *ptr = buffer_write_ptr(wb, &n);
    if(n < 5){
        return -1;
    } else {
        memcpy(ptr, "\r\n.\r\n", 5);
    }
    return 5;
}

