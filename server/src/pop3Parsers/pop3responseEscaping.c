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
escape_response_parser_init(struct escape_response_parser *p) {
    p->response_state = response_new_line;
}


enum response_state
escape_response_consume(buffer *rb, buffer *wb, struct escape_response_parser *p) {
    enum response_state st = p->response_state;
    const  uint8_t c = buffer_read(rb);
    st = escape_response_parser_feed(p, c, wb);
    buffer_write(wb, c);
    return st;
}