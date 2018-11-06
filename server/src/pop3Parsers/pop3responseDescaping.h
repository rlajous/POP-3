//
// Created by francisco on 04/11/18.
//

#ifndef PROJECT_POP3RESPONSEDESCAPING_H
#define PROJECT_POP3RESPONSEDESCAPING_H
#include "pop3response.h"

/**
 * Este parser se encarga del desescapado de las
 * respuestas multilineas que deben ser transformadas
 * */


struct descape_response_parser {
    enum response_state response_state;
};
/** Inicializa el parser de desescapdo */
extern void
descape_response_parser_init(struct descape_response_parser *p);

/** Indica si se termino de desescapar una respuesta */
extern bool
descape_response_is_done(struct descape_response_parser *p);

/** Consume del buffer de lectura la respuesta a desescapar
 * y escribe en el buffer de escritura la respuesta desescapada*/
enum response_state
descape_response_consume(buffer *rb, buffer *wb, struct descape_response_parser *p);

/** Cierra el buffer de desescapado */
void
descape_response_close(struct descape_response_parser *p);

#endif //PROJECT_POP3RESPONSEDESCAPING_H
