#include "parser.h"
#include "mime_chars.h"
#include "mime_value.h"

/**
 * RFC822:
 *
 * field       =  field-name ":" [ field-body ] CRLF
 *
 * field-name  =  1*<any CHAR, excluding CTLs, SPACE, and ":">
 *
 * field-body  =  field-body-contents
 *               [CRLF LWSP-char field-body]
 *
 * field-body-contents =
 *              <the ASCII characters making up the field-body, as
 *               defined in the following sections, and consisting
 *               of combinations of atom, quoted-string, and
 *               specials tokens, or else consisting of texts>
 *
 * linear-white-space =  1*([CRLF] LWSP-char)  ; semantics = SPACE
 *                                             ; CRLF => folding
 */
enum state {
    MIME_VALUE0,
    MIME_VALUE,
    ERROR,
};

///////////////////////////////////////////////////////////////////////////////
// Acciones

static void
value(struct parser_event *ret, const uint8_t c) {
    ret->type    = VALUE;
    ret->n       = 1;
    ret->data[0] = c;
}

static void
fin(struct parser_event *ret, const uint8_t c) {
    ret->type    = VALUE_END;
    ret->n       = 1;
    ret->data[0] = c;
}

static void
wait(struct parser_event *ret, const uint8_t c) {
    ret->type    = WAIT; // Un estado que no hace nada, puede no existir
    ret->n       = 1;
    ret->data[0] = c;
}

static void
unexpected(struct parser_event *ret, const uint8_t c) {
    ret->type    = UNEXPECTED;
    ret->n       = 1;
    ret->data[0] = c;
}

///////////////////////////////////////////////////////////////////////////////
// Transiciones

static struct parser_state_transition ST_VALUE0[] =  {
        {.when = ';',        .dest = ERROR,         .act1 = unexpected,},
        {.when = '\r',       .dest = ERROR,         .act1 = unexpected,},
        {.when = TOKEN_LWSP, .dest = MIME_VALUE0,   .act1 = wait,      },
        {.when = TOKEN_CHAR, .dest = MIME_VALUE,    .act1 = value,     },
        {.when = ANY,        .dest = ERROR,         .act1 = unexpected,},
};

static struct parser_state_transition ST_VALUE[] =  {
        {.when = ';',        .dest = MIME_VALUE,    .act1 = fin,       },
        {.when = TOKEN_LWSP, .dest = MIME_VALUE,    .act1 = value,     },
        {.when = TOKEN_CHAR, .dest = MIME_VALUE,    .act1 = value,     },
        {.when = ANY,        .dest = ERROR,         .act1 = unexpected,},
};

static struct parser_state_transition ST_ERROR[] =  {
        {.when = ANY,        .dest = ERROR,           .act1 = unexpected,},
};

///////////////////////////////////////////////////////////////////////////////
// Declaración formal

static struct parser_state_transition *states [] = {
        ST_VALUE0,
        ST_VALUE,
        ST_ERROR,
};

#define N(x) (sizeof(x)/sizeof((x)[0]))

static size_t states_n [] = {        
        N(ST_VALUE0),
        N(ST_VALUE),
        N(ST_ERROR),
};

static struct parser_definition definition = {
        .states_count = N(states),
        .states       = states,
        .states_n     = states_n,
        .start_state  = MIME_VALUE0,
};

struct parser_definition *
mime_value_parser(void) {
    return &definition;
}

const char *
mime_value_event(enum mime_value_event_type type) {
    const char *ret;

    switch(type) {
        case VALUE:
            ret = "value(c)";
            break;
        case VALUE_END:
            ret = "value_end(c)";
            break;
        case WAIT:
            ret = "wait(c)";
            break;
        case UNEXPECTED:
            ret = "error(c)";
            break;
    }
    return ret;
}

