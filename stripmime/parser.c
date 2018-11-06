#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#include "parser.h"
#include "parser_utils.h"
#include "mime_chars.h"

/* CDT del parser */
struct parser {
    /** tipificación para cada caracter */
    const unsigned     *classes;
    /** definición de estados */
    struct parser_definition *def;

    /* estado actual */
    unsigned            state;

    /* evento que se retorna */
    struct parser_event e1;
    /* evento que se retorna */
    struct parser_event e2;
};

void
parser_destroy(struct parser *p) {
    if(p != NULL) {
        free(p);
    }
}

void
boundary_parser_init(struct parser *parser, struct parser_definition *def) {
    parser->def = def;
    parser->state = def->start_state;
}

struct parser *
parser_init(const unsigned *classes,
            struct parser_definition *def) {
    struct parser *ret = malloc(sizeof(*ret));
    if(ret != NULL) {
        memset(ret, 0, sizeof(*ret));
        ret->classes = classes;
        if(def != NULL) {
            ret->def   = def;
            ret->state = def->start_state;
        }
    }
    return ret;
}

void
parser_reset(struct parser *p) {
    if(p->def != NULL) {
        p->state   = p->def->start_state;
    }
    memset(&(p->e1), 0, sizeof(p->e1));
    memset(&(p->e2), 0, sizeof(p->e2));
}

const struct parser_event *
parser_feed(struct parser *p, const uint8_t c) {
    const unsigned type = p->classes[c];

    p->e1.next = p->e2.next = 0;

    const struct parser_state_transition *state = p->def->states[p->state];
    const size_t n                              = p->def->states_n[p->state];
    bool matched   = false;

    for(unsigned i = 0; i < n ; i++) {
        const int when = state[i].when;
        if (state[i].when <= 0xFF) {
            matched = (c == when);
        } else if(state[i].when == ANY) {
            matched = true;
        } else if(state[i].when > 0xFF) {
            matched = (type & when);
        } else {
            matched = false;
        }

        if(matched) {
            state[i].act1(&p->e1, c);
            if(state[i].act2 != NULL) {
                p->e1.next = &p->e2;
                state[i].act2(&p->e2, c);
            }
            p->state = state[i].dest;
            break;
        }
    }
    return &p->e1;
}


static const unsigned classes[0xFF] = {0x00};

const unsigned *
parser_no_classes(void) {
    return classes;
}

