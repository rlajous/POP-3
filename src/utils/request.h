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
    quit,
    uidl,

};

enum request_state {
    request_cmd,
    request_arg,
    request_CR,
    request_done,

    request_error,

};

struct request {
    enum pop3_req_cmd   cmd;
    char *              arg;
};

struct request_parser {
    struct request *request;
    enum request_state state;
    /** Buffer para el comando */
    char cmd_buffer[4];
    /** cuantos bytes tenemos que leer */
    uint8_t n;
    /** cuantos bytes ya leimos */
    uint8_t i;
};

#endif request_H
