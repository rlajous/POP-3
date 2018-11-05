#ifndef PROJECT_RESPONSE_H
#define PROJECT_RESPONSE_H

#include <stdint.h>
#include "../utils/buffer.h"
#include "pop3request.h"

enum response_state {
    response_detect_status,
    response_new_line,
    response_dot,
    response_dot_cr,
    response_byte,
    response_cr,
    response_done,
    response_error,
};

struct response_parser {
    enum response_state response_state;
    bool pop3_response_success;
    struct request* request;
};

void
response_parser_init(struct response_parser *p, struct request *request);

bool
response_is_done(enum response_state st, bool *errored);

enum response_state
response_consume(buffer *rb, struct response_parser *p);

enum response_state
response_parser_feed(struct response_parser *p, uint8_t c);

void
response_close(struct response_parser *p);

#endif //PROJECT_RESPONSE_H
