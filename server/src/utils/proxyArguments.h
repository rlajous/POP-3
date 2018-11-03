#ifndef PROXYARGUMENTS_H
#define PROXYARGUMENTS_H

#include <stdint.h>

typedef struct args {
    /** Origin address and port */
    char    *origin_address;
    uint16_t origin_port;

    /** Pop3 listen address and port */
    char    *pop3_address;
    uint16_t pop3_port;

    /** SCTP configuration address and port */
    char    *spcp_address;
    uint16_t spcp_port;

    /** Filters */
    char *media_types;
    char *command;
    char *message;
    char *filter_error_file;

    char *version;
} args;

typedef args * arguments;

arguments
parse_arguments(int argc, char * const * argv);

void
destroy_arguments(arguments args);

extern char *
modify_string(char * old_string, const char * new_string);

#endif
