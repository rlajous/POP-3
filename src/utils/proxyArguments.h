#ifndef PROXYARGUMENTS_H
#define PROXYARGUMENTS_H

#include <stdint.h>

typedef struct args {
    /** Origin address and port */
    char * origin_address;
    uint16_t origin_port;

    /** Pop3 listen address and port */
    char * pop3_address;
    uint16_t pop3_port;

    /** SCTP configuration address and port */
    char * config_address;
    uint16_t config_port;

    /** Filters */
    //TODO: Add MediaTypes
    char * command;
    char * message;
    char * filter_error_file;
} args;

typedef args * arguments;

arguments
parse_arguments(int argc, char * const * argv);

#endif
