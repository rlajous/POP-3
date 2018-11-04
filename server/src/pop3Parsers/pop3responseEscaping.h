//
// Created by francisco on 03/11/18.
//

#ifndef PROJECT_POP3RESPONSEDESCAPING_H
#define PROJECT_POP3RESPONSEDESCAPING_H

#include "pop3response.h"
struct escape_response_parser {
    enum response_state response_state;
    size_t response_size_i;
    size_t response_size_n;
};

extern void
escape_response_parser_init(struct escape_response_parser *p, size_t response_size);

extern bool
escape_response_is_done(size_t response_length, struct escape_response_parser *p);

enum response_state
escape_response_consume(buffer *rb, buffer *wb, struct escape_response_parser *p, bool *errored);

int
response_add_termination(buffer *wb);

void
escape_response_close(struct escape_response_parser *p);

#endif //PROJECT_POP3RESPONSEDESCAPING_H
