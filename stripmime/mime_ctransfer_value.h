#ifndef MIME_MSG_H_be03ad0cccde0231647a6c699e44f0d753ebf4dd
#define MIME_MSG_H_be03ad0cccde0231647a6c699e44f0d753ebf4dd

/**
 * mime_value.c - tokenizador generico de valores que terminan en ';'.
 *
 */
#include "parser.h"

/** tipo de eventos de un value */
struct parser;
enum mime_ctransfer_value_event_type {
    
    /* leyendo el valor */
    MIME_CTRANSFER_VALUE,
    
    /* no tenemos idea de qué hacer hasta que venga el proximo caracter */
    MIME_CTRANSFER_WAIT,

    /* se recibió un caracter que no se esperaba */
    MIME_CTRANSFER_UNEXPECTED,
};

/** la definición del parser */
const struct parser_definition * mime_ctransfer_value_parser(void);

const char *
mime_ctransfer_value_event(enum mime_ctransfer_value_event_type type);

#endif
