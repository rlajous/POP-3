#ifndef MIME_BODY_H_be03ad0bccde0231647a6c699e14f0d753saf4dd
#define MIME_BODY_H_be03ad0bccde0231647a6c699e14f0d753saf4dd

/**
 * mime_body.c - tokenizador del body.
 *
 */
#include "parser.h"

/** tipo de eventos de un body */
struct parser;
enum mime_body_event_type {
    
    /* primera lectura del valor */
    BODY_VALUE0,
    
    /* leyendo el valor */
    BODY_VALUE,
    
    /* se termino de leer el body. payload: caracter. */
    BODY_END,
    
    /* no tenemos idea de qué hacer hasta que venga el proximo caracter */
    BODY_WAIT,

    /* se recibió un caracter que no se esperaba */
    BODY_UNEXPECTED,
};

/** la definición del parser */
struct parser_definition * mime_body_parser(void);

const char *
mime_body_event(enum mime_body_event_type type);

#endif
