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

#endif //PROJECT_POP3RESPONSEDESCAPING_H
