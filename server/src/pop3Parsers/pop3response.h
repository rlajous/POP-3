#ifndef PROJECT_RESPONSE_H
#define PROJECT_RESPONSE_H

#include <stdint.h>
#include "../utils/buffer.h"
#include "pop3request.h"
/**El response parser se ocupa del parseo de cada respuesta
 * en base a si es uni o multi linea (lo cual se detecto
 * previamente en el request parser) así pudiendo determinar
 * el fin de una respuesta y manejar servidores orignes con
 * y sin soporte de pipelining
 * */



/**Definición de los estados del parser de respuesta.
 * */
enum response_state {
    response_detect_status,
    response_new_line,
    response_dot,
    response_dot_cr,
    response_byte,
    response_cr,
    response_done,
    response_error,
};

struct response_parser {
    enum response_state response_state;
    bool pop3_response_success;
    struct request* request;
};

/**Inicializa el parser con un request, el parser utiliza el request
 * para poder determinar si la respuesta sera multilinea o unilinea*/
void
response_parser_init(struct response_parser *p, struct request *request);

/** Función que indica si se termino de parsear el response en base al
 * estado del parser.*/
bool
response_is_done(enum response_state st, bool *errored);

/**consume la respuesta depositada en el buffer hacia el parser*/
enum response_state
response_consume(buffer *rb, struct response_parser *p);

/** Consume un byte*/
enum response_state
response_parser_feed(struct response_parser *p, uint8_t c);

/** Cierra el parser de respuesta*/
void
response_close(struct response_parser *p);

#endif //PROJECT_RESPONSE_H
