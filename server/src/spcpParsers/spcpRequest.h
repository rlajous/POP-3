#ifndef PROJECT_REQUEST_H
#define PROJECT_REQUEST_H

#include <glob.h>
#include <stdint.h>
#include "../utils/buffer.h"

/**  The SPCP request is formed as follows:
 *
 *      +-----+-------+----------+----------+...+----------+----------+
 *      | CMD | NARGS | ARGLEN 1 |   ARG 1  |   | ARGLEN N |   ARG N  |
 *      +-----+-------+----------+----------+...+----------+----------+
 *      |  1  |   1   |    1    | Variable |   |    1     | Variable |
 *      +-----+-------+----------+----------+...+----------+----------+
 *
 *   Where:
 *
 *        o  CMD
 *           o  USER                        X'00'
 *           o  PASS                        X'01'
 *           o  GET CONCURRENT CONNECTIONS  X'02'
 *           o  GET TRANSFERRED BYTES       X'03'
 *           o  GET HISTORICAL ACCESSES     X'04'
 *           o  GET ACTIVE TRANSFORMATION   X'05'
 *           o  QUIT                        X'06'
 *           o  SET BUFFER SIZE             X'07'
 *           o  CHANGE TRANSFORMATION       X'08'
 *           o  SET TIMEOUTS                X'09'
 *
 *
 *        o  NARGS      number of arguments sent
 *        o  ARGLEN N   length of the nth argument
 *        o  ARG N      the nth argument
 *
 */



/** Estados del parser de request spcp */
enum spcp_request_state {
    spcp_request_cmd,
    spcp_request_nargs,
    spcp_request_arg_size,
    spcp_request_arg,

    spcp_request_done,

    spcp_request_error_invalid_command,
    spcp_request_error_invalid_arguments,
    spcp_request_error,
};
/** Posibles respuestas del servidor spcp*/
enum spcp_response_status {
    spcp_success                    = 0x00,
    spcp_auth_err                   = 0x01,
    spcp_invalid_command            = 0x02,
    spcp_invalid_arguments          = 0x03,
    spcp_err                        = 0x04,
};
/** posibles comandos del servidor spcp*/
enum spcp_request_cmd {
    spcp_user                       = 0x00,
    spcp_pass                       = 0x01,
    spcp_concurrent_connections     = 0x02,
    spcp_transfered_bytes           = 0x03,
    spcp_historical_accesses        = 0x04,
    spcp_active_transformation      = 0x05,
    spcp_set_buffer_size            = 0x06,
    spcp_change_transformation      = 0x07,
    spcp_quit                       = 0x09,
};

/** Estructira qie representa un request
 * del servidor spcp*/
struct spcp_request {
    enum spcp_request_cmd cmd;
    uint8_t *arg0;
    uint8_t *arg1;
    size_t arg0_size;
    size_t arg1_size;
};

/**Definicion del parser de request spcp*/
struct spcp_request_parser {
    /** El request que esta siendo parseado*/
    struct spcp_request         request;
    /** Estado del parser*/
    enum   spcp_request_state   state;
    /** cantidad de argumentos */
    uint8_t nargs;
    /** argumentos ya leidos */
    uint8_t nargs_i;
    /** ultimo narg parseado */
    uint8_t arg_size;
    /** bytes del argumento ya leidos*/
    uint8_t arg_size_i;


};
/** Consume el request del buffer pasandolo por el parser*/
extern enum spcp_request_state
spcp_request_consume(buffer *b, struct spcp_request_parser *p, bool *errored);

/** Inicializa el parser de request spcp */
extern void
spcp_request_parser_init(struct spcp_request_parser *p);

/** Serializa al buffer una respuesta apropiada al status
 * de respuesta que se pasa por argumento*/
extern int
spcp_no_data_request_marshall(buffer *b, uint8_t status);

/** Serializa al buffer una respuesta apropiada al status
 * de respuesta agregando la data que se pasa por parametro*/
extern int
spcp_data_request_marshall(buffer *b, uint8_t status, char *data, size_t data_len);

/** Cierra el parser de request*/
extern void
spcp_request_close(struct spcp_request_parser *p);

/** En base al estado del parser indica si se termino de parsear un
 * request*/
extern bool
spcp_request_is_done(enum spcp_request_state st, bool *errored);

#endif