#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include "request.h"
#include "buffer.h"

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

    for(int i = 0; i < 9 ; i++){
        if(0 == strcmp(p->cmd_buffer, POP3_CMDS_INFO[i].string_representation)){
            p->request->cmd = POP3_CMDS_INFO[i].request_cmd;
            return true;
        }
    }
    return false;
}

bool
has_minimum_nargs(struct request_parser *p) {
    return p->request->nargs >= POP3_CMDS_INFO[p->request->cmd].min_args;
}

bool has_maximum_nargs(struct request_parser *p) {
    return p->request->nargs >= POP3_CMDS_INFO[p->request->cmd].max_args;
}

enum request_state
request_parse_cmd(struct request_parser* p, const uint8_t c) {

    /** Arguments have a max length of 4 chars */
    if(remaining_is_done(p)){
        return request_length_error;
    }

    if(c == ' '){
        if(request_identify_cmd(p)){
            remaining_set(p, MAX_ARG_LENGTH);
            return request_arg;
        }
        return request_invalid_cmd_error;
    }
    /** recieve a CR*/
    if(c == '\r'){
        if(!request_identify_cmd(p)) {
            return request_invalid_cmd_error;
        }
        if(!has_minimum_nargs(p)) {
            return request_missing_args_error;
        }
        remaining_set(p, 1);
        return request_CR;

    }
    /** LF without CR*/
    if(c == '\n'){
        return request_invalid_termination_error;
    }
    
    char append = (char)tolower(c);
    p->cmd_buffer[p->i++] = append;
    return request_cmd;
}
 
enum request_state
request_parse_arg(struct request_parser *p, const uint8_t c){
    /** recieve a CF */
    if(c == '\r'){
        if(has_minimum_nargs(p)){
            return request_CR;
        }
        return  request_missing_args_error;
    }

    /** LF without CR*/
    if(c == '\n'){
        return request_invalid_termination_error;
    }

    if(has_maximum_nargs(p)){
        return request_arg;
    }
    if(c == ' '){
        p->request->nargs++;
        p->request->arg[p->request->nargs-1][p->i] = '\0';
        remaining_set(p, MAX_ARG_LENGTH);
        return request_arg;
    }
    if(!remaining_is_done(p)){
        char current_char = (char)c;
        p->request->arg[p->request->nargs-1][p->i++] = current_char;
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
    memset(p->request, 0, sizeof(*(p->request)));
    memset(p->cmd_buffer, 0, sizeof(*(p->cmd_buffer)));
}

//TODO(fran): no se bien que es esto del errored
extern bool
request_is_done(const enum request_state st, bool* errored) {
    if(st >= request_error && errored != 0) {
        *errored = true;
    }
    return st >= request_done;
}

extern enum request_state
request_consume(buffer *b, struct request_parser *p, bool *errored){
    enum request_state st = p->state;
    while(buffer_can_read(b)){
        const uint8_t c = buffer_read(b);
        st = request_parser_feed(p, c);
        if(request_is_done(st, errored)) {
            break;
        }
    }
    return st;
}

extern void
request_close(struct request_parser *p){
    //Creo que no hay nada que hacer.
}

//TODO(fran): habría que implementar el marshall