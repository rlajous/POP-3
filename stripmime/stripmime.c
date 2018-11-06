#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>

#include "parser.h"
#include "parser_utils.h"
#include "pop3_multi.h"
#include "mime_chars.h"
#include "mime_msg.h"
#include "mime_value.h"
#include "mime_ctransfer_value.h"
#include "mime_body.h"
#include "mime_boundary_key.h"
#include "mime_boundary_border_end.h"

#define MAX_BOUNDARIES 1024
#define MAX_BLACKLIST 1024
#define MAX_STRING_LENGTH 1024

struct parser_definition boundary_parser_definition;

/*
 * imprime información de debuging sobre un evento.
 *
 * @param p        prefijo (8 caracteres)
 * @param namefnc  obtiene el nombre de un tipo de evento
 * @param e        evento que se quiere imprimir
 */
static void
debug(const char *p,
      const char * (*namefnc)(unsigned),
      const struct parser_event* e) {
    if (e->n == 0) {
        fprintf(stderr, "%-8s: %-14s\n", p, namefnc(e->type));
    } else {
        for (int j = 0; j < e->n; j++) {
            const char* name = (j == 0) ? namefnc(e->type)
                                        : "";
            if (e->data[j] <= ' ') {
                fprintf(stderr, "%-8s: %-14s 0x%02X\n", p, name, e->data[j]);
            } else {
                fprintf(stderr, "%-8s: %-14s %c\n", p, name, e->data[j]);
            }
        }
    }
}

/* mantiene el estado durante el parseo */
struct ctx {
    /* delimitador respuesta multi-línea POP3 */
    struct parser* multi;
    /* delimitador mensaje "tipo-rfc 822" */
    struct parser* msg;
    /* detector de field-name "Content-Type" */
    struct parser* ctype_header;
    /* detector de field-name "Content-Transfer" */
    struct parser* ctransfer_header;
    /* tokenizador de content-type value */
    struct parser* ctype_value;
    /* detector de boundary name ("boundary=") */
    struct parser* boundary_name;
    /* parser de body */
    struct parser* body;
    /* detector de boundary key (la ultima guardada)*/
    struct parser* boundary_key;
    /* detector de boundary border */
    struct parser* boundary_border;
    /* detector del final de boundary border.
     * Diferencia si es un border comun, un 
     * final de boundary o si no es valido
     */
    struct parser* boundary_border_end;

    /* ¿hemos detectado si el field-name que estamos procesando refiere
     * a Content-Type?. Utilizando dentro msg para los field-name.
     */
    bool *msg_content_type_field_detected;
    /* ¿hemos detectado si el field-name que estamos procesando refiere
     * a Content-Transfer-Encoding?. Utilizando dentro msg para los field-name.
     */
    bool *msg_content_transfer_field_detected;
    /* ¿hemos terminado de guardar el value del 
     * content-type?
     */
    bool *msg_content_type_value_stored;
    /* ¿hemos detectado dentro del Content-Type en el que estamos
     * a "boundary="?
     */
    bool *msg_boundary_name_detected;
    /* ¿hemos terminado de guardar el value del 
     * boundary?
     */
    bool *msg_boundary_key_stored;
    /* ¿hemos detectado el boundary border?
     */
    bool *msg_boundary_border_detected;
    /* ¿hay que imprimir el caracter actual?
     */
    bool *print_curr_char;
    /* ¿es un mime a filtrar el valor actual?
     */
    bool *filter_curr_mime;
    /* ¿es un multipart el mime actual?
     */
    bool *multipart_curr_mime;
    /* ¿es un message/rfc822 el mime actual?
     */
    bool *message_curr_mime;
    /* content-type actual */
    char *content_type;
    /* stack de boundaries */
    char **boundaries;
    /* cantidad de boundaries */
    int boundaries_n;
    /* mensaje de reemplazo */
    char *filter_msg;
    /* mimes a filtrar */
    char **blacklist;
    /* cantidad de mimes a filtrar */
    int blacklist_n;
};

static bool T = true;
static bool F = false;

bool to_filter(struct ctx *ctx) {
    for(int i=0; i<ctx->blacklist_n; i++) {
        char *black = ctx->blacklist[i];
        bool eq = true;
        int slash_pos = -1;
        for(int j=0; j < strlen(black); j++) {
            if(j > 0) {
                if(black[j] == '/' && slash_pos < 0) {
                    slash_pos = j;
                } else if(black[j] == '*' && slash_pos == j-1 && j == strlen(black)-1) {
                    return true;
                }
            }
            if(tolower(ctx->content_type[j]) != tolower(black[j])) {
                eq = false;
                break;
            }
        }
        if(eq && strlen(black) == strlen(ctx->content_type)) {
            return true;
        }
    }
    return false;
}

bool is_multipart(char *content_type) {
    char multi[] = "multipart/";
    for(int i=0; i<strlen(multi); i++) {
        if(multi[i] != tolower(content_type[i])) {
            return false;
        }
    }

    return true;
}

bool is_message(char *content_type) {
    char message[] = "message/rfc822";
    if(strlen(content_type) != strlen(message)) {
        return false;
    } else {
        for(int j=0; j<strlen(message); j++) {
            if(message[j] != tolower(content_type[j])) {
                return false;
            }
        }
    }
    return true;
}

/* Detecta si un header-field-name equivale a Content-Type.
 * Deja el valor en `ctx->msg_content_type_field_detected'. Tres valores
 * posibles: NULL (no tenemos información suficiente todavia, por ejemplo
 * viene diciendo Conten), true si matchea, false si no matchea.
 */
static void
content_type_header(struct ctx *ctx, const uint8_t c) {
    const struct parser_event* e = parser_feed(ctx->ctype_header, c);
    do {
        debug("2.      typehr", parser_utils_strcmpi_event, e);
        switch(e->type) {
            case STRING_CMP_EQ:
                ctx->msg_content_type_field_detected = &T;
                break;
            case STRING_CMP_NEQ:
                ctx->msg_content_type_field_detected = &F;
                break;
        }
        e = e->next;
    } while (e != NULL);
}

/* Detecta si un header-field-name equivale a Content-Transfer.
 * Deja el valor en `ctx->msg_content_transfer_field_detected'. Tres valores
 * posibles: NULL (no tenemos información suficiente todavia, por ejemplo
 * viene diciendo Conten), true si matchea, false si no matchea.
 */
static void
content_transfer_header(struct ctx *ctx, const uint8_t c) {
    const struct parser_event* e = parser_feed(ctx->ctransfer_header, c);
    do {
        debug("2. ctransferhr", parser_utils_strcmpi_event, e);
        switch(e->type) {
            case STRING_CMP_EQ:
                ctx->msg_content_transfer_field_detected = &T;
                break;
            case STRING_CMP_NEQ:
                ctx->msg_content_transfer_field_detected = &F;
                break;
        }
        e = e->next;
    } while (e != NULL);
}

/**
 * Guarda el content-type de la data por venir en el mensaje.
 * El valor se guarda en ctx->content_type
 */
static void
content_type_value(struct ctx *ctx, const uint8_t c) {
    const struct parser_event* e = parser_feed(ctx->ctype_value, c);
    do {
        debug("2.    ctypeval", mime_value_event, e);
        switch(e->type) {
            case VALUE:
                nappend(ctx->content_type, c, MAX_STRING_LENGTH);
                ctx->msg_content_type_value_stored = NULL;
                break;
            case VALUE_END:
                fprintf(stderr, "ctype: %s\n",ctx->content_type);
                ctx->msg_content_type_value_stored = &T;
                if(to_filter(ctx)) {
                    printf(": text/plain");
                    ctx->filter_curr_mime = &T;
                } else {
                    printf(": %s", ctx->content_type);
                    ctx->filter_curr_mime = &F;
                }
                ctx->multipart_curr_mime = is_multipart(ctx->content_type)? &T : &F;
                ctx->message_curr_mime = is_message(ctx->content_type)? &T : &F;
                ctx->print_curr_char = &T;
                break;
            case WAIT:
                break;
            case UNEXPECTED:
                ctx->msg_content_type_value_stored = &F;
                break;
        }
        e = e->next;
    } while (e != NULL);
}

/* Detecta si un valor en content-type equivale a "boundary=".
 * Deja el valor en `ctx->msg_boundary_name_detected'. Tres valores
 * posibles: NULL (no tenemos información suficiente todavia), 
 * true si matchea, false si no matchea.
 */
static void
boundary_name(struct ctx *ctx, const uint8_t c) {
    const struct parser_event* e = parser_feed(ctx->boundary_name, c);
    do {
        debug("2.   boun_name", parser_utils_strcmpi_event, e);
        switch(e->type) {
            case STRING_CMP_EQ:
                ctx->msg_boundary_name_detected = &T;
                break;
            case STRING_CMP_NEQ:
                ctx->msg_boundary_name_detected = &F;
                break;
        }
        e = e->next;
    } while (e != NULL);
}

/**
 * Guarda un boundary key.
 * El valor se guarda en ctx->boundaries[ctx->boundaries_n]
 */
static void
boundary_key(struct ctx *ctx, const uint8_t c) {
    const struct parser_event* e = parser_feed(ctx->boundary_key, c);
    do {
        debug("2.boundary_key", mime_boundary_key_event, e);
        switch(e->type) {
            case BOUNDARY_KEY_VALUE:
                nappend(ctx->boundaries[ctx->boundaries_n], c, MAX_STRING_LENGTH);
                break;
            case BOUNDARY_KEY_VALUE_END:
                fprintf(stderr, "boundary: %s\n",ctx->boundaries[ctx->boundaries_n]);
                ctx->boundaries_n++;
                ctx->msg_boundary_key_stored = &T;
                break;
            case BOUNDARY_KEY_UNEXPECTED:
                break;
        }
        e = e->next;
    } while (e != NULL);
}

/* Detecta si la linea consiste de "--" seguido del ultimo boundary_key. Tres valores
 * posibles: NULL (no tenemos información suficiente todavia), 
 * true si matchea, false si no matchea.
 */
static void
boundary_border(struct ctx *ctx, const uint8_t c) {
    const struct parser_event* e = parser_feed(ctx->boundary_border, c);
    do {
        debug("3. boun_border", parser_utils_strcmpi_event, e);
        switch(e->type) {
            case STRING_CMP_EQ:
                ctx->msg_boundary_border_detected = &T;
                parser_reset(ctx->boundary_border);
                break;
            case STRING_CMP_NEQ:    
                ctx->msg_boundary_border_detected = &F;
                parser_reset(ctx->boundary_border);
                break;
        }
        e = e->next;
    } while (e != NULL);
}

/* Detecta si luego del boundary key hay un "--\r\n", "\r\n" u otra cosa.
 */
static void
boundary_border_end(struct ctx *ctx, const uint8_t c) {
    const struct parser_event* e = parser_feed(ctx->boundary_border_end, c);
    do {
        debug("3.  border_end", mime_boundary_border_end_event, e);
        switch(e->type) {
           case BOUNDARY_BORDER_END_VALUE:
                break;
            case BOUNDARY_BORDER_END_VALUE_END_HYPHENS:
                ctx->boundaries_n--;
                ctx->boundaries[ctx->boundaries_n] = 0;
                ctx->msg_boundary_border_detected  = NULL;
                ctx->print_curr_char               = &T;
                parser_reset(ctx->boundary_border_end);
                parser_reset(ctx->boundary_border);
                if(ctx->boundaries_n > 0) {
                    char *border = calloc(2 + MAX_STRING_LENGTH, sizeof(char));
                    strcat(border, "--");
                    strcat(border, ctx->boundaries[ctx->boundaries_n-1]);
                    boundary_parser_definition = parser_utils_strcmpi(border);
                    boundary_parser_init(ctx->boundary_border, &boundary_parser_definition);
                }
                break;
            case BOUNDARY_BORDER_END_VALUE_END_CRLF:
                ctx->msg_boundary_border_detected = NULL;
                if(!*ctx->multipart_curr_mime) {
                    ctx->print_curr_char = &T;
                }
                if(!*ctx->multipart_curr_mime || !*ctx->filter_curr_mime) {
                    parser_reset(ctx->msg);
                }
                parser_reset(ctx->boundary_border_end);
                break;
            case BOUNDARY_BORDER_END_WAIT:
                break;
            case BOUNDARY_BORDER_END_UNEXPECTED:
                ctx->msg_boundary_border_detected = &F;
                break;
            case BOUNDARY_BORDER_END_UNEXPECTED_CRLF:
                ctx->msg_boundary_border_detected = &F;
                parser_reset(ctx->boundary_border);
                parser_reset(ctx->boundary_border_end);
                break;
        }
        e = e->next;
    } while (e != NULL);
}

/* 
 * Procesa el body
 */
static void
body(struct ctx *ctx, const uint8_t c) {
    const struct parser_event* e = parser_feed(ctx->body, c);
    do {
        debug("2.        body", mime_body_event, e);
        switch(e->type) {
            case BODY_VALUE:
                if(ctx->boundaries_n > 0) {
                    if(ctx->msg_boundary_border_detected == NULL) {
                        for(int i = 0; i < e->n; i++) {
                            boundary_border(ctx, e->data[i]);
                        }
                    } else if(*ctx->msg_boundary_border_detected) {
                        for(int i = 0; i < e->n; i++) {
                            boundary_border_end(ctx, e->data[i]);
                        }
                    }
                    if(e->data[0] == '\n') {
                        ctx->msg_boundary_border_detected = NULL;
                        parser_reset(ctx->boundary_border);
                        parser_reset(ctx->boundary_border_end);
                    }
                }
                break;            
            case BODY_WAIT:
                break;
            case BODY_UNEXPECTED:
                break;
        }
        e = e->next;
    } while (e != NULL);
}

/**
 * Procesa un mensaje `tipo-rfc822'.
 * Si reconoce un al field-header-name Content-Type lo interpreta.
 *
 */
static void
mime_msg(struct ctx *ctx, const uint8_t c) {
    const struct parser_event* e = parser_feed(ctx->msg, c);

    do {
        debug("1.         msg", mime_msg_event, e);
        switch(e->type) {
            case MSG_NAME:
                if( ctx->msg_content_type_field_detected == NULL
                || *ctx->msg_content_type_field_detected) {
                    for(int i = 0; i < e->n; i++) {
                        content_type_header(ctx, e->data[i]);
                    }
                }
                if( *ctx->filter_curr_mime && (ctx->msg_content_transfer_field_detected == NULL
                || *ctx->msg_content_transfer_field_detected)) {
                    for(int i = 0; i < e->n; i++) {
                        content_transfer_header(ctx, e->data[i]);
                    }
                }
                if(c == '\n') {
                    parser_reset(ctx->ctype_header);
                    parser_reset(ctx->ctransfer_header);
                    ctx->msg_content_type_field_detected     = NULL;
                    ctx->msg_content_transfer_field_detected = NULL;
                }
                break;
            case MSG_NAME_END:
                // lo dejamos listo para el próximo header
                parser_reset(ctx->ctype_header);
                parser_reset(ctx->ctransfer_header);
                if(((ctx->msg_content_transfer_field_detected != NULL
                 && *ctx->msg_content_transfer_field_detected)
                 && *ctx->filter_curr_mime)
                 || (ctx->msg_content_type_field_detected != NULL
                 && *ctx->msg_content_type_field_detected)) {
                    ctx->print_curr_char = &F;
                }
                break;
            case MSG_VALUE:
                if( ctx->msg_content_type_field_detected != NULL
                && *ctx->msg_content_type_field_detected) {
                    if( ctx->msg_content_type_value_stored == NULL) {
                        for(int i = 0; i < e->n; i++) {
                            content_type_value(ctx, e->data[i]);
                        }
                    } else if(*ctx->multipart_curr_mime){
                        if(ctx->msg_boundary_name_detected == NULL) {
                            for(int i = 0; i < e->n; i++) {
                               boundary_name(ctx, e->data[i]);
                            }    
                        } else if(*ctx->msg_boundary_name_detected && 
                                   ctx->msg_boundary_key_stored == NULL){
                            for(int i = 0; i < e->n; i++) {
                                boundary_key(ctx, e->data[i]);
                            }
                        }
                    }
                }
                break;
            case MSG_VALUE_FOLD:
                break;
            case MSG_VALUE_END:
                if( ctx->msg_content_transfer_field_detected != NULL
                && *ctx->msg_content_transfer_field_detected) {
                    // si detecte el header de content-transfer-encoding es porque el valor
                    // se debia reemplazar (para que el mensaje de reemplazo sea visible)
                    printf(": 7bit\r\n");
                } else if(ctx->msg_content_type_value_stored == NULL) { // caso en que el value de content-type no terminaba en ';' 
                    if(strlen(ctx->content_type) > 0) {
                        fprintf(stderr, "ctype: %s\n",ctx->content_type);
                        if(to_filter(ctx)) {
                            printf(": text/plain\r\n");
                            ctx->filter_curr_mime = &T;
                        } else {
                            printf(": %s\r\n", ctx->content_type);
                            ctx->filter_curr_mime = &F;
                        }
                        ctx->multipart_curr_mime = is_multipart(ctx->content_type)? &T : &F;
                        ctx->message_curr_mime   = is_message(ctx->content_type)? &T : &F;
                    }
                }
                ctx->msg_content_type_field_detected     = NULL;
                ctx->msg_content_transfer_field_detected = NULL;
                ctx->msg_content_type_value_stored       = NULL;
                ctx->msg_boundary_name_detected          = NULL;
                ctx->msg_boundary_key_stored             = NULL;
                ctx->print_curr_char                     = &T;
                ctx->content_type[0]                     = 0;
                
                parser_reset(ctx->ctype_value);
                parser_reset(ctx->boundary_name);
                parser_reset(ctx->boundary_key);
                break;
            case MSG_BODY_START:
                if(*ctx->filter_curr_mime) {
                    printf("\r\n");
                    if(*ctx->multipart_curr_mime) {
                        printf("--%s\r\n", ctx->boundaries[ctx->boundaries_n-1]);
                    }
                    printf("%s\r\n", ctx->filter_msg);
                    if(!*ctx->multipart_curr_mime) {
                        printf("--%s", ctx->boundaries[ctx->boundaries_n-1]);                    
                    }
                    ctx->print_curr_char = &F;
                } else {
                    if(*ctx->message_curr_mime) {
                        parser_reset(ctx->msg);
                    }
                    ctx->print_curr_char = &T;
                }
                if(ctx->boundaries_n > 0) {
                    char *border = calloc(2 + MAX_STRING_LENGTH, sizeof(char));
                    strcat(border, "--");
                    strcat(border, ctx->boundaries[ctx->boundaries_n-1]);
                    boundary_parser_definition = parser_utils_strcmpi(border);
                    boundary_parser_init(ctx->boundary_border, &boundary_parser_definition);
                }
                break;
            case MSG_BODY:
                for(int i = 0; i < e->n; i++) {
                    body(ctx, e->data[i]);
                }
                break;
            case MSG_WAIT:
                break;
            case MSG_UNEXPECTED:
                break;
        }
        e = e->next;
    } while (e != NULL);
}

/* Delimita una respuesta multi-línea POP3. Se encarga del "byte-stuffing" */
static void
pop3_multi(struct ctx *ctx, const uint8_t c) {
    const struct parser_event* e = parser_feed(ctx->multi, c);
    do {
        debug("0.       multi", pop3_multi_event,  e);
        switch (e->type) {
            case POP3_MULTI_BYTE:
                for(int i = 0; i < e->n; i++) {
                    mime_msg(ctx, e->data[i]);
                }
                //printf("+++%s+++\n",ctx->content_type);
                if( ctx->print_curr_char != NULL
                && *ctx->print_curr_char) {
                    for(int i = 0; i < e->n; i++) {
                        printf("%c", e->data[i]);
                    }
                }
                break;
            case POP3_MULTI_WAIT:
                // nada para hacer mas que esperar
                break;
            case POP3_MULTI_FIN:
                // arrancamos de vuelta
                parser_reset(ctx->msg);
                break;
        }
        e = e->next;
    } while (e != NULL);
}

int
main(const int argc, const char **argv) {
    int fd = STDIN_FILENO;
    if(argc > 1) {
        fd = open(argv[1], 0);
        if(fd == -1) {
            perror("opening file");
            return 1;
        }
    }
    char *filter_msg    = getenv("FILTER_MSG");
    char *blacklist_raw = getenv("FILTER_MEDIAS");

    const unsigned int* no_class = parser_no_classes();
    struct parser_definition ctype_header_def =
            parser_utils_strcmpi("content-type");
    struct parser_definition ctransfer_header_def =
            parser_utils_strcmpi("content-transfer-encoding");
    struct parser_definition boundary_name_def =
            parser_utils_strcmpi_ignore_lwsp("boundary=\"");
    struct parser_definition boundary_border_def =
            parser_utils_strcmpi("--tempboundary");
    
    struct ctx ctx = {
        // Parsers
        .multi                  = parser_init(no_class,          pop3_multi_parser()),
        .msg                    = parser_init(init_char_class(), mime_message_parser()),
        .ctype_header           = parser_init(no_class,          &ctype_header_def),
        .ctransfer_header       = parser_init(init_char_class(), &ctransfer_header_def),
        .ctype_value            = parser_init(init_char_class(), mime_value_parser()),
        .boundary_name          = parser_init(init_char_class(), &boundary_name_def),
        .body                   = parser_init(init_char_class(), mime_body_parser()),
        .boundary_key           = parser_init(init_char_class(), mime_boundary_key_parser()),
        .boundary_border        = parser_init(init_char_class(), &boundary_border_def),
        .boundary_border_end    = parser_init(init_char_class(), mime_boundary_border_end_parser()),

        // Extra data
        .content_type           = calloc(MAX_STRING_LENGTH, sizeof(char)),
        .boundaries             = calloc(MAX_BOUNDARIES, sizeof(char *)),
        .boundaries_n           = 0,
        .msg_boundary_border_detected           = NULL,
        .filter_msg             = filter_msg,
        .blacklist              = calloc(MAX_STRING_LENGTH, sizeof(char *)),
        .blacklist_n            = 0,
        .print_curr_char        = &T,
        .filter_curr_mime       = &F,
        .multipart_curr_mime    = &F,
        .message_curr_mime      = &F,
    };

    for(int i=0; i<MAX_BOUNDARIES; i++) {
        ctx.boundaries[i] = calloc(MAX_STRING_LENGTH, sizeof(char));
        
    }    

    int j = 0;
    for(int i=0; i<MAX_BLACKLIST; i++) {
        ctx.blacklist[i] = calloc(MAX_STRING_LENGTH, sizeof(char));
        for(; j < strlen(blacklist_raw) && blacklist_raw[j] != ','; j++) {
            if(blacklist_raw[j] != ' ' && blacklist_raw[j] != '\t') {
                nappend(ctx.blacklist[i], blacklist_raw[j], 1024);
            }
        }
        j++;
        if(ctx.blacklist_n == 0 && j >= strlen(blacklist_raw)) {
            ctx.blacklist_n = i + 1;
            break;
        }
    }

    uint8_t data[4096];
    int n;
    do {
        n = read(fd, data, sizeof(data));
        for(ssize_t j = 0; j < n ; j++) {
            pop3_multi(&ctx, data[j]);
        }
    } while(n > 0);
    
    parser_destroy(ctx.multi);
    parser_destroy(ctx.msg);
    parser_destroy(ctx.ctype_header);
    parser_destroy(ctx.ctransfer_header);
    parser_destroy(ctx.ctype_value);
    parser_destroy(ctx.body);
    parser_destroy(ctx.boundary_name);
    parser_destroy(ctx.boundary_key);
    parser_destroy(ctx.boundary_border);
    parser_destroy(ctx.boundary_border_end);
    parser_utils_strcmpi_destroy(&ctype_header_def);
    parser_utils_strcmpi_destroy(&ctransfer_header_def);
    parser_utils_strcmpi_destroy(&boundary_name_def);
    parser_utils_strcmpi_destroy(&boundary_border_def);
    for(int i=0; i<MAX_BOUNDARIES; i++) {
        free(ctx.boundaries[i]);
    }
}
