//
// Created by francisco on 04/11/18.
//

#include <stddef.h>
#include <stdint.h>
#include "pop3responseDescaping.h"
#include "../utils/buffer.h"
#include "pop3response.h"

enum response_state
d_new_line(struct descape_response_parser *p, const uint8_t c, buffer *b) {
    if(c == '.'){
        return response_dot;
    }
    else {
        buffer_write(b,c);
        return response_byte;
    }
}

enum response_state
d_dot(struct descape_response_parser *p, const uint8_t c, buffer *b) {
    if(c == '\r') {
        //add cr to buffer
        return response_dot_cr;
    } else {
        buffer_write(b,c);
        return response_byte;
    }
}

enum response_state
d_dot_cr(struct descape_response_parser *p, const uint8_t c, buffer *b) {
    if(c == '\n') {
        return response_done;
    } else {
        buffer_write(b, '\r');
        buffer_write(b,c);
        return response_byte;
    }
}

enum response_state
d_byte(struct descape_response_parser *p, const uint8_t c, buffer *b) {
    buffer_write(b,c);
    if(c == '\r') {
        return response_cr;
    } else {
        return response_byte;
    }
}

enum response_state
d_cr(struct descape_response_parser *p, const uint8_t c, buffer *b) {
    buffer_write(b,c);
    if(c == '\n'){
        return response_new_line;
    } else {
        return response_byte;
    }
}


extern enum response_state
descape_response_parser_feed(struct descape_response_parser *p, const uint8_t c, buffer *b) {
    enum response_state next;

    switch (p->response_state) {
        case response_new_line:
            next = d_new_line(p, c, b);
            break;
        case response_dot:
            next = d_dot(p, c, b);
            break;
        case response_dot_cr:
            next = d_dot_cr(p, c, b);
            break;
        case response_byte:
            next = d_byte(p, c, b);
            break;
        case response_cr:
            next = d_cr(p, c, b);
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
descape_response_parser_init(struct descape_response_parser *p) {
    p->response_state = response_new_line;
}

extern bool
descape_response_is_done(struct descape_response_parser *p) {
    return p->response_state == response_done;
}


enum response_state
descape_response_consume(buffer *rb, buffer *wb, struct descape_response_parser *p) {
    enum response_state st = p->response_state;
    const  uint8_t c = buffer_read(rb);
    st = descape_response_parser_feed(p, c, wb);
    return st;
}

void
descape_response_close(struct descape_response_parser *p) {
    //nada que hacer
}