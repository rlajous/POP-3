#ifndef REQUEST_H
#define REQUEST_H

#include <stdint.h>

enum pop3_req_cmd{
    stat = 0,
    list,
    retr,
    dele,
    noop,
    rset,
    quit,
    uidl,
    top,
    apop,
};

enum request_state {
    request_cmd,
    request_arg,
    request_CR,
    request_done,

    request_error,
    request_missing_args_error,

};

struct request {
    enum pop3_req_cmd   cmd;
    char                arg[2][41];
    //DEBE ser inicializado en 1;
    uint8_t             nargs;
};
/** Estructura es consultada por el parser para obtener
 * Informacion sobre el comando*/
struct pop3_cmd_data {
    char * string_representation;
    enum pop3_req_cmd request_cmd;
    uint8_t nargs;
    uint8_t min_args;
};

//TODO(fran) : No estoy seguro si ponerle static o no
/** Array de datos sobre cada comando pop3 usado por el parser.
 *  El indice del array se correlaciona con el valor numérico del
 *  enum de pop3_req_cmd */
static struct pop3_cmd_data POP3_CMDS_INFO[] ={
        { .request_cmd = stat,
          .string_representation = "stat",
          .nargs = 0,
          .min_args = 0,
        },
        { .request_cmd = list,
          .string_representation = "list",
          .nargs = 1,
          .min_args = 0,
        },
        { .request_cmd = retr,
          .string_representation = "retr",
          .nargs = 1,
          .min_args = 1,
        },
        { .request_cmd = dele,
          .string_representation = "dele",
          .nargs = 1,
          .min_args = 1,
        },
        { .request_cmd = noop,
          .string_representation = "noop",
          .nargs = 0,
          .min_args = 0,
        },
        { .request_cmd = rset,
          .string_representation = "rset",
          .nargs = 0,
          .min_args = 0,
        },
        { .request_cmd = quit,
          .string_representation = "quit",
          .nargs = 0,
          .min_args = 0,
        },
        { .request_cmd = uidl,
          .string_representation = "uidl",
          .nargs = 1,
          .min_args = 0,
        },
        { .request_cmd = top,
          .string_representation = "top",
          .nargs = 2,
          .min_args = 2,
        },
        { .request_cmd = apop,
          .string_representation = "apop",
          .nargs = 2,
          .min_args = 2,
        },

};

struct request_parser {
    struct request *request;
    enum request_state state;
    /** Buffer para el comando */
    char cmd_buffer[4];
    /** cuantos bytes tenemos que leer */
    uint8_t n;
    /** cuantos bytes ya leimos */
    uint8_t i;
};

#endif request_H
