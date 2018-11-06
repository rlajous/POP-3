//
// Created by francisco on 03/11/18.
//

#ifndef PROJECT_POP3RESPONSEESCAPING_H
#define PROJECT_POP3RESPONSEESCAPING_H

#include "pop3response.h"
struct escape_response_parser {
    enum response_state response_state;
};
/** Inicializa el parser de reescapado */
extern void
escape_response_parser_init(struct escape_response_parser *p);

/** Lee la respeusta sin escapado de pop3 del read buffer
 *  y escribe en el writebuffer la respeusta reescapada*/
enum response_state
escape_response_consume(buffer *rb, buffer *wb, struct escape_response_parser *p);

#endif //PROJECT_POP3RESPONSEDESCAPING_H
