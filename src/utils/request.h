#ifndef REQUEST_H
#define REQUEST_H

#include <stdint.h>

enum pop3_req_cmd{
    stat,
    list,
    retr,
    dele,
    noop,
    rset,
};

enum request_state {
    request_cmd;
    request_arg;

    request_done;

    request_error;

};

struct request {
    enum pop3_req_cmd   cmd;
    uint8_t             arg;

};


#endif request_H
