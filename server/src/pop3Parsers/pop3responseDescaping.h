//
// Created by francisco on 04/11/18.
//

#ifndef PROJECT_POP3RESPONSEDESCAPING_H
#define PROJECT_POP3RESPONSEDESCAPING_H

#include "pop3response.h"
struct descape_response_parser {
    enum response_state response_state;
    size_t response_size_i;
    size_t response_size_n;
};

extern void
descape_response_parser_init(struct descape_response_parser *p);

extern bool
descape_response_is_done(struct descape_response_parser *p);

enum response_state
descape_response_consume(buffer *rb, buffer *wb, struct descape_response_parser *p);

void
descape_response_close(struct descape_response_parser *p);

#endif //PROJECT_POP3RESPONSEDESCAPING_H
