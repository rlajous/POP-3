#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include "request.h"
#include "buffer.h"

#define MAX_ARG_LENGTH 40

static void
remaining_set(struct request_parser* p, const int n) {
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
request_identify_cmd(struct request_parser* p){

    for(int i = 0; i < 9 ; i++){
        if(0 == strcmp(p->cmd_buffer, POP3_CMDS_INFO[i].string_representation)){
            p->request->cmd = POP3_CMDS_INFO[i].request_cmd;
            return true;
        }
    }
    return false;
}

enum request_state
request_parse_cmd(struct request_parser* p, const uint8_t c){

    if(c == ' '){
        if(request_identify_cmd(p)){
            remaining_set(p, MAX_ARG_LENGTH);
            return request_arg;
        }
        return request_error;
    }
    /** recieve a CR*/
    if(c == '\r'){
        if(request_identify_cmd(p) && POP3_CMDS_INFO[p->request->cmd].min_args == 0) {
            return request_CR;
        }
        return request_error;
    }
    /** LF without CR*/
    if(c == '\n'){
        return request_error;
    }
    /** Arguments have a max length of 4 chars */
    if(remaining_is_done(p)){
        return request_error;
    }
    
    char append = (char)tolower(c);
    strcat(p->cmd_buffer, &append);
    p->i++;
    return request_cmd;
}

bool
validate_nargs(struct request_parser *p){
    return p->request->nargs >= POP3_CMDS_INFO[p->request->cmd].min_args;
}
 
enum request_state
request_parse_arg(struct request_parser *p, const uint8_t c){
    /** recieve a CF */
    if(c == '\r'){
        if(validate_nargs(p)){
            return request_CR;
        }
        return  request_missing_args_error;
    }

    /** LF without CR*/
    if(c == '\n'){
        return request_error;
    }

    if(p->request->nargs == POP3_CMDS_INFO[p->request->cmd].nargs){
        return request_arg;
    }
    if(c == ' '){
        p->request->nargs++;
        p->request->arg[p->request->nargs-1][p->i] = '\0';
        remaining_set(p, 40);
        return request_arg;
    }
    if(!remaining_is_done(p)){
        char current_char = (char)c;
        p->request->arg[p->request->nargs-1][p->i++] = current_char;
        p->i++;
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