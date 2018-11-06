#include "parser.h"
#include "mime_chars.h"
#include "mime_boundary_border_end.h"

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
    VALUE_HYPHEN,
    VALUE_HYPHEN_HYPHEN,
    VALUE_HYPHEN_HYPHEN_CR,
    VALUE_CR,
    ERROR,
    ERROR_CR,
};

///////////////////////////////////////////////////////////////////////////////
// Acciones

static void
value(struct parser_event *ret, const uint8_t c) {
    ret->type    = BOUNDARY_BORDER_END_VALUE;
    ret->n       = 1;
    ret->data[0] = c;
}

static void
value_hyphen(struct parser_event *ret, const uint8_t c) {
    value(ret, '-');
}

static void
value_cr(struct parser_event *ret, const uint8_t c) {
    value(ret, '\r');
}

static void
end_hyphen(struct parser_event *ret, const uint8_t c) {
    ret->type    = BOUNDARY_BORDER_END_VALUE_END_HYPHENS;
    ret->n       = 1;
    ret->data[0] = c;
}

static void
end_crlf(struct parser_event *ret, const uint8_t c) {
    ret->type    = BOUNDARY_BORDER_END_VALUE_END_CRLF;
    ret->n       = 1;
    ret->data[0] = c;
}

static void
wait(struct parser_event *ret, const uint8_t c) {
    ret->type    = BOUNDARY_BORDER_END_WAIT;
    ret->n       = 1;
    ret->data[0] = c;
}

static void
unexpected(struct parser_event *ret, const uint8_t c) {
    ret->type    = BOUNDARY_BORDER_END_UNEXPECTED;
    ret->n       = 1;
    ret->data[0] = c;
}

static void
unexpected_cr(struct parser_event *ret, const uint8_t c) {
    unexpected(ret, '\r');
}

static void
unexpected_crlf(struct parser_event *ret, const uint8_t c) {
    ret->type    = BOUNDARY_BORDER_END_UNEXPECTED_CRLF;
    ret->n       = 1;
    ret->data[0] = c;
}

///////////////////////////////////////////////////////////////////////////////
// Transiciones

static struct parser_state_transition ST_BORDER_END_VALUE[] =  {
    {.when = '-',        .dest = VALUE_HYPHEN,    .act1 = wait,      },
    {.when = '\r',       .dest = VALUE_CR,        .act1 = wait,      },
    {.when = ANY,        .dest = ERROR,           .act1 = unexpected,},
};

static struct parser_state_transition ST_BORDER_END_VALUE_HYPHEN[] =  {
    {.when = '-',        .dest = VALUE_HYPHEN_HYPHEN, .act1 = wait,},
    {.when = ANY,        .dest = ERROR,               .act1 = unexpected,},
};

static struct parser_state_transition ST_BORDER_END_VALUE_HYPHEN_HYPHEN[] =  {
    {.when = '\r',       .dest = VALUE_HYPHEN_HYPHEN_CR, .act1 = wait,},
    {.when = ANY,        .dest = ERROR,                  .act1 = unexpected,},
};

static struct parser_state_transition ST_BORDER_END_VALUE_HYPHEN_HYPHEN_CR[] =  {
    {.when = '\n',       .dest = VALUE_HYPHEN_HYPHEN_CR, .act1 = end_hyphen,},
    {.when = ANY,        .dest = ERROR,                  .act1 = unexpected,},
};

static struct parser_state_transition ST_BORDER_END_VALUE_CR[] =  {
    {.when = '\n',       .dest = VALUE_CR,        .act1 = end_crlf,  },
    {.when = ANY,        .dest = ERROR,           .act1 = unexpected,},
};

static struct parser_state_transition ST_ERROR[] =  {
    {.when = '\r',       .dest = ERROR_CR,        .act1 = wait,      },
    {.when = ANY,        .dest = ERROR,           .act1 = unexpected,},
};

static struct parser_state_transition ST_ERROR_CR[] =  {
    {.when = '\n',       .dest = ERROR_CR,        .act1 = unexpected_cr,
                                                  .act2 = unexpected_crlf, },
    {.when = ANY,        .dest = ERROR,           .act1 = unexpected,      },
};

///////////////////////////////////////////////////////////////////////////////
// Declaración formal

static struct parser_state_transition *states [] = {    
    ST_BORDER_END_VALUE,
    ST_BORDER_END_VALUE_HYPHEN,
    ST_BORDER_END_VALUE_HYPHEN_HYPHEN,
    ST_BORDER_END_VALUE_HYPHEN_HYPHEN_CR,
    ST_BORDER_END_VALUE_CR,
    ST_ERROR,
    ST_ERROR_CR,
};

#define N(x) (sizeof(x)/sizeof((x)[0]))

static size_t states_n [] = {
    N(ST_BORDER_END_VALUE),
    N(ST_BORDER_END_VALUE_HYPHEN),
    N(ST_BORDER_END_VALUE_CR),
    N(ST_ERROR),
    N(ST_ERROR_CR),
};

static struct parser_definition definition = {
    .states_count = N(states),
    .states       = states,
    .states_n     = states_n,
    .start_state  = VALUE,
};

struct parser_definition * 
mime_boundary_border_end_parser(void) {
    return &definition;
}

const char *
mime_boundary_border_end_event(enum mime_boundary_border_end_event_type type) {
    const char *ret;

    switch(type) {
        case BOUNDARY_BORDER_END_VALUE:
            ret = "value(c)";
            break;
        case BOUNDARY_BORDER_END_VALUE_END_HYPHENS:
            ret = "value_end(--)";
            break;
        case BOUNDARY_BORDER_END_VALUE_END_CRLF:
            ret = "value_end(\\r\\n)";
            break;
        case BOUNDARY_BORDER_END_UNEXPECTED:
            ret = "unexpected(c)";
            break;
        case BOUNDARY_BORDER_END_UNEXPECTED_CRLF:
            ret = "unexpected_crlf(c)";
            break;
        case BOUNDARY_BORDER_END_WAIT:
            ret = "wait(c)";
            break;
    }
    return ret;
}

