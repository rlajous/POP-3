#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include "pop3request.h"
#include "../utils/buffer.h"


#define N(x) (sizeof(x)/sizeof((x)[0]))

static void
remaining_set(struct request_parser* p, const uint8_t n) {
    p->i = 0;
    p->n = n;
}

static int
remaining_is_done(struct request_parser* p) {
    return p->i >= p->n;
}

/**
 * Identifies the parsed command
 * returns:
 *      - true if parsed argument is valid
 *      - false otherwise
 */
bool
request_identify_cmd(struct request_parser* p) {

    for(int i = 0; i <  N(POP3_CMDS_INFO); i++){
        if(0 == strcmp(p->cmd_buffer, POP3_CMDS_INFO[i].string_representation)){
            p->request.cmd = POP3_CMDS_INFO[i].request_cmd;
            p->request.multi = POP3_CMDS_INFO[i].multi;
            return true;
        }
    }
    return false;
}

bool
has_minimum_nargs(struct request_parser *p) {
    return p->request.nargs >= POP3_CMDS_INFO[p->request.cmd].min_args;
}

bool has_maximum_nargs(struct request_parser *p) {
    return p->request.nargs > POP3_CMDS_INFO[p->request.cmd].max_args;
}

void
arg_dependant_multi(struct request_parser *p) {
    if(p->request.cmd == list || p->request.cmd == uidl){
        p->request.multi = false;
    }
}

enum request_state
request_parse_cmd(struct request_parser* p, const uint8_t c) {

    if(c == ' '){
        if(request_identify_cmd(p)){
            remaining_set(p, MAX_ARG_LENGTH);
            return request_arg;
        }
    }
    /** recieve a CR*/
    if(c == '\r'){
        request_identify_cmd(p);
        remaining_set(p, 1);
        return request_CR;

    }

    /** Arguments have a max length of 4 chars */
    if(!remaining_is_done(p)){
        char append = (char)tolower(c);
        p->cmd_buffer[p->i++] = append;
    }

    return request_cmd;
}
 
enum request_state
request_parse_arg(struct request_parser *p, const uint8_t c){
    /** recieve a CF */
    if(c == '\r'){
        p->request.nargs++;
        p->request.arg[p->request.nargs-1][p->i] = '\0';
        if(has_minimum_nargs(p)){
            return request_CR;
        }
    }

    if(has_maximum_nargs(p)){
        return request_arg;
    }
    if(c == ' '){
        p->request.nargs++;
        p->request.arg[p->request.nargs-1][p->i] = '\0';
        remaining_set(p, MAX_ARG_LENGTH);
        arg_dependant_multi(p);
        return request_arg;
    }
    if(!remaining_is_done(p)){
        char current_char = (char)c;
        p->request.arg[p->request.nargs][p->i++] = current_char;
        p->request.argsize[p->request.nargs]++;
        return request_arg;
    } else {
        return request_arg;
    }
}

enum request_state
request_parse_LF(struct request_parser* p, const uint8_t c){
    if(c == '\n'){
        return request_done;
    }
    return request_error;
}

extern enum request_state
request_parser_feed(struct request_parser* p, const uint8_t c){
    enum request_state next;

    switch(p->state){
        case request_cmd:
            next = request_parse_cmd(p, c);
            break;
        case request_arg:
            next = request_parse_arg(p, c);
            break;
        case request_CR:
            next = request_parse_LF(p, c);
            break;
        case request_done:
        case request_error:
        case request_missing_args_error:
            next = p->state;
            break;
        default:
            next = request_error;
            break;
    }

    return p->state = next;
}

extern void
request_parser_init(struct request_parser * p) {
    p->state = request_cmd;
    remaining_set(p, MAX_CMD_LENGTH);
    memset(&p->request, 0, sizeof(p->request));
    memset(p->cmd_buffer, 0, sizeof(*(p->cmd_buffer)));
    p->request.cmd    = unknown;
    p->request.multi  = false;
    p->request.nargs  = 0;
    p->request.length = 0;
}

extern bool
request_is_done(const enum request_state st, bool* errored) {
    if(st >= request_error && errored != 0) {
        *errored = true;
    }
    return st >= request_done;
}

static bool
request_process(struct request_parser *p, struct request_queue *q) {
    //TODO: RETURN ERROR
    queue_request(q, &p->request);
    return true;
}

extern enum request_state
request_consume(buffer *rb, buffer *wb, struct request_parser *p, bool *errored, struct request_queue *q) {
    enum request_state st = p->state;
    while(buffer_can_read(rb)) {
        if(!buffer_can_write(wb)) {
            break;
        }
        const uint8_t c = buffer_read(rb);
        st = request_parser_feed(p, c);
        p->request.length++;
        buffer_write(wb, c);
        if(request_is_done(st, errored)) {
            *errored &= request_process(p, q);
            request_parser_init(p);
        }
    }
    return st;
}

extern void
request_close(struct request_parser *p) {
    //Creo que no hay nada que hacer.
}

//TODO(fran): habría que implementar el marshall?