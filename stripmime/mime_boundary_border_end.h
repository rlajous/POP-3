#ifndef MSG_H_be03ad0cccde0231647a6c699q42f0d753baf4de
#define MSG_H_be03ad0cccde0231647a6c699q42f0d753baf4de

/**
 * mime_boundary_border_end.c - analizador de tipo de border.
 *
 */
#include "parser.h"

/** tipo de eventos de un value de content-type */
struct parser;
enum mime_boundary_border_end_event_type {
    
    /* leyendo el valor de boundary */
    BOUNDARY_BORDER_END_VALUE,
    
    /* no se sabe que hacer con la informacion actual. payload: caracter. */
    BOUNDARY_BORDER_END_WAIT,
    
    /* se termino de leer el valor de boundary. payload: caracter. */
    BOUNDARY_BORDER_END_VALUE_END_CRLF,

    /* se termino de leer el valor de boundary. payload: caracter. */
    BOUNDARY_BORDER_END_VALUE_END_HYPHENS,

    /* se recibió un caracter que no se esperaba */
    BOUNDARY_BORDER_END_UNEXPECTED,

    /* se recibió un caracter que no se esperaba pero que contiene un CRLF al final */
    BOUNDARY_BORDER_END_UNEXPECTED_CRLF,
};

/** la definición del parser */
struct parser_definition * mime_boundary_border_end_parser(void);

const char *
mime_boundary_border_end_event(enum mime_boundary_border_end_event_type type);

#endif
