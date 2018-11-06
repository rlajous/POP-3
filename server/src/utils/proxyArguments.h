#ifndef PROXYARGUMENTS_H
#define PROXYARGUMENTS_H

#include <stdint.h>

/**
 * Estructura que define los argumentos del proxy
 * */
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

/** Parsea los argumentos y los deposita en
 * la estructura de argumentos*/
arguments
parse_arguments(int argc, char * const * argv);

/**  Libera la memoria alocada a los argumentos */
void
destroy_arguments(arguments args);

/** Funcion de utilidad para reemplazar el viejo string por uno nuevo*/
extern char *
modify_string(char * old_string, const char * new_string);

#endif
