#include "parser.h"
#include "mime_chars.h"
#include "mime_boundary_key.h"

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
    ERROR,
};

///////////////////////////////////////////////////////////////////////////////
// Acciones

static void
value(struct parser_event *ret, const uint8_t c) {
    ret->type    = BOUNDARY_KEY_VALUE;
    ret->n       = 1;
    ret->data[0] = c;
}

static void
fin(struct parser_event *ret, const uint8_t c) {
    ret->type    = BOUNDARY_KEY_VALUE_END;
    ret->n       = 1;
    ret->data[0] = c;
}

static void
unexpected(struct parser_event *ret, const uint8_t c) {
    ret->type    = BOUNDARY_KEY_UNEXPECTED;
    ret->n       = 1;
    ret->data[0] = c;
}

///////////////////////////////////////////////////////////////////////////////
// Transiciones

static struct parser_state_transition ST_KEY_VALUE[] =  {
    {.when = '\"',       .dest = VALUE,           .act1 = fin,       },
    {.when = ANY,        .dest = VALUE,           .act1 = value,     },
};

static struct parser_state_transition ST_ERROR[] =  {
    {.when = ANY,        .dest = ERROR,           .act1 = unexpected,},
};

///////////////////////////////////////////////////////////////////////////////
// Declaración formal

static struct parser_state_transition *states [] = {    
    ST_KEY_VALUE,
    ST_ERROR,
};

#define N(x) (sizeof(x)/sizeof((x)[0]))

static size_t states_n [] = {
    N(ST_KEY_VALUE),
    N(ST_ERROR),
};

static struct parser_definition definition = {
    .states_count = N(states),
    .states       = states,
    .states_n     = states_n,
    .start_state  = VALUE,
};

struct parser_definition * 
mime_boundary_key_parser(void) {
    return &definition;
}

const char *
mime_boundary_key_event(enum mime_boundary_key_event_type type) {
    const char *ret;

    switch(type) {
        case BOUNDARY_KEY_VALUE:
            ret = "value(c)";
            break;
        case BOUNDARY_KEY_VALUE_END:
            ret = "value_end(\")";
            break;
        case BOUNDARY_KEY_UNEXPECTED:
            ret = "unexpected(c)";
            break;
    }
    return ret;
}

