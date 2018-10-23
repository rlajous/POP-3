#ifndef PROJECT_RESPONSE_H
#define PROJECT_RESPONSE_H

#include <stdint.h>
#include "buffer.h"
enum response_state {
    response_unidentified,
    response_success,
    response_error,
};

struct response {
    bool success;
};

struct response_parser {
    enum response_state response_state;
    bool multi;
};

void
response_parser_init(struct response_parser *p);

bool
response_is_done(enum response_state st, bool *errored);

enum response_state
response_consume(buffer *rb, buffer *wb, struct response_parser *p, bool *errored);

void
response_close(struct response_parser *p);

#endif //PROJECT_RESPONSE_H
