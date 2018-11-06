#ifndef MSG_H_be03ad0cccde0231647a6c699e44f0d753baf4de
#define MSG_H_be03ad0cccde0231647a6c699e44f0d753baf4de

/**
 * mime_boundary_key.c - tokenizador de valor de boundary.
 *
 */
#include "parser.h"

/** tipo de eventos de un value de content-type */
struct parser;
enum mime_boundary_key_event_type {
    
    /* leyendo el valor de boundary */
    BOUNDARY_KEY_VALUE,
    
    /* se termino de leer el valor de boundary. payload: caracter. */
    BOUNDARY_KEY_VALUE_END,

    /* se recibió un caracter que no se esperaba */
    BOUNDARY_KEY_UNEXPECTED,
};

/** la definición del parser */
struct parser_definition * mime_boundary_key_parser(void);

const char *
mime_boundary_key_event(enum mime_boundary_key_event_type type);

#endif
