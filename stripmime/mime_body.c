#include "parser.h"
#include "mime_chars.h"
#include "mime_body.h"

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
    BODY,
    BODY_CR,
    ERROR,
};

///////////////////////////////////////////////////////////////////////////////
// Acciones

static void
value0(struct parser_event *ret, const uint8_t c) {
    ret->type    = BODY_VALUE0;
    ret->n       = 1;
    ret->data[0] = c;
}

static void
value1(struct parser_event *ret, const uint8_t c) {
    ret->type    = BODY_VALUE;
    ret->n       = 0;
}

static void
value(struct parser_event *ret, const uint8_t c) {
    ret->type    = BODY_VALUE;
    ret->n       = 1;
    ret->data[0] = c;
}

static void
value_cr(struct parser_event *ret, const uint8_t c) {
    value(ret, '\r');
}

static void
wait(struct parser_event *ret, const uint8_t c) {
    ret->type    = BODY_WAIT;
    ret->n       = 0;
}

static void
unexpected(struct parser_event *ret, const uint8_t c) {
    ret->type    = BODY_UNEXPECTED;
    ret->n       = 1;
    ret->data[0] = c;
}

///////////////////////////////////////////////////////////////////////////////
// Transiciones

static struct parser_state_transition ST_BODY[] =  {
    {.when = '\r',                 .dest = BODY_CR,       .act1 = wait,      },
    {.when = TOKEN_CHAR,           .dest = BODY,          .act1 = value,     },
    {.when = TOKEN_EXTENDED_CHAR,  .dest = BODY,          .act1 = value,     },
    {.when = ANY,                  .dest = ERROR,         .act1 = unexpected,},
};

static struct parser_state_transition ST_BODY_CR[] =  {
    {.when = '\n',       .dest = BODY,         .act1 = value_cr,
                                               .act2 = value,     },
    {.when = ANY,        .dest = ERROR,        .act1 = unexpected,},
};

static struct parser_state_transition ST_ERROR[] =  {
    {.when = ANY,        .dest = ERROR,         .act1 = unexpected,},
};

///////////////////////////////////////////////////////////////////////////////
// Declaración formal

static struct parser_state_transition *states [] = {
        ST_BODY,
        ST_BODY_CR,
        ST_ERROR,
};

#define N(x) (sizeof(x)/sizeof((x)[0]))

static size_t states_n [] = {        
        N(ST_BODY),
        N(ST_BODY_CR),
        N(ST_ERROR),
};

static struct parser_definition definition = {
        .states_count = N(states),
        .states       = states,
        .states_n     = states_n,
        .start_state  = BODY,
};

struct parser_definition *
mime_body_parser(void) {
    return &definition;
}

const char *
mime_body_event(enum mime_body_event_type type) {
    const char *ret;

    switch(type) {
        case BODY_VALUE:
            ret = "body(c)";
            break;
        case BODY_WAIT:
            ret = "wait(c)";
            break;
        case BODY_UNEXPECTED:
            ret = "error(c)";
            break;
    }
    return ret;
}

