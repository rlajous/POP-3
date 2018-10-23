//
// Created by francisco on 23/10/18.
//

#include "response.h"

extern void
response_parser_init(struct response_parser *p) {

}

extern bool
response_is_done(enum response_state st, bool *errored) {

}

enum response_state
response_consume(buffer *rb, buffer *wb, struct response_parser *p, bool *errored){

}

void
response_close(struct response_parser *p) {

}