#include "parser.h"
#include "mime_chars.h"
#include "mime_ctransfer_value.h"

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
    VALUE,
};

///////////////////////////////////////////////////////////////////////////////
// Acciones

static void
value(struct parser_event *ret, const uint8_t c) {
    ret->type    = CTRANSFER_VALUE;
    ret->n       = 1;
    ret->data[0] = c;
}

static void
unexpected(struct parser_event *ret, const uint8_t c) {
    ret->type    = CTRANSFER_UNEXPECTED;
    ret->n       = 1;
    ret->data[0] = c;
}

///////////////////////////////////////////////////////////////////////////////
// Transiciones

static struct parser_state_transition ST_VALUE[] =  {
        {.when = ANY,        .dest = VALUE,         .act1 = value,},
};

///////////////////////////////////////////////////////////////////////////////
// Declaración formal

static struct parser_state_transition *states [] = {
        ST_VALUE,
};

#define N(x) (sizeof(x)/sizeof((x)[0]))

static size_t states_n [] = {        
        N(ST_VALUE),
};

static struct parser_definition definition = {
        .states_count = N(states),
        .states       = states,
        .states_n     = states_n,
        .start_state  = VALUE,
};

struct parser_definition *
mime_ctransfer_value_parser(void) {
    return &definition;
}

const char *
mime_ctransfer_value_event(enum mime_ctransfer_value_event_type type) {
    const char *ret;

    switch(type) {
        case CTRANSFER_VALUE:
            ret = "value(c)";
            break;
        case CTRANSFER_UNEXPECTED:
            ret = "error(c)";
            break;
    }
    return ret;
}

