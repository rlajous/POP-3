#include <stdlib.h>
#include <sys/socket.h>
#include <stdint.h>
#include <string.h>
#include <netdb.h>
#include <pthread.h>
#include <stdio.h>
#include <errno.h>

#include "../utils/buffer.h"
#include "../utils/stm.h"
#include "../utils/selector.h"
#include "../utils/proxyArguments.h"
#include "../pop3Parsers/pop3request.h"
#include "../utils/request_queue.h"
#include "../pop3Parsers/pop3response.h"
#include "../utils/parser.h"
#include "../utils/parser_utils.h"
#include "../utils/metrics.h"
#include "../pop3Parsers/pop3responseDescaping.h"
#include "../pop3Parsers/pop3responseEscaping.h"

#define N(x) (sizeof(x)/sizeof((x)[0]))

extern arguments proxyArguments;
extern metrics  *proxy_metrics;

size_t BUFFER_SIZE = 2048;


enum pop3_state {
    RESOLVE_ADDRESS,
    CONNECTING,
    CAPA,
    HELLO,
    REQUEST,
    RESPONSE,
    TRANSFORM,
    APPEND_CAPA,
    DONE,
    HANDLEABLE_ERROR,
    ERROR,
};

typedef enum {
    CONNECTION_REFUSED = 0,
    DISCONNECTED,
    INTERNAL,
} handeleable_errors;

static char * error_messages[] = {
          "-ERR CONNECTION REFUSED\r\n",
          "-ERR DISCONNECTED\r\n",
          "-ERR PROXY ERROR\r\n"
        };

typedef enum {
    FORK_FAIL,
    EXEC_FAIL,
    PIPE_FAIL,
    SELECTOR_ERROR,
    OK,
} transformation_status;

struct hello_st {
    buffer                  *buffer;
    struct response_parser  response;
    bool                    done;
};

#define CAPA_CMD 6

struct capa_st {
    char    message[CAPA_CMD];
    size_t  remaining;
    char    *current;
    buffer  *wb;
    struct parser_definition pipelineDef;
    struct response_parser  response;
    struct parser *pipeline;
};

struct error_st {
    char    *message;
    size_t  remaining;
    buffer  *wb;
    handeleable_errors error;
};

struct request_st {
    buffer                  *buffer;
    int                     *fd;
    fd_interest             duplex;
    struct request_st       *other;
    struct request_queue    *request_queue;

};

struct response_st {
    buffer                  *wb, *rb;
    int                     *fd;
    fd_interest             duplex;
    struct response_st      *other;
    struct request_queue    *request_queue;
    bool                    should_parse;
};

struct transform_st {
    buffer                  *wb, *rb, *t_rb, *t_wb;
    struct response_parser  parser;
    bool                    transformation_done;
    struct request_queue    *request_queue;
    bool                    transform_needed;
    bool                    should_parse;

    bool                    descape_done;
    bool                    escape_done;
    bool                    transform_error;

    struct escape_response_parser   escape;
    struct descape_response_parser  descape;
    size_t                  termination_bytes;
};

struct append_capa_st {
    buffer                  *wb, *rb, *t_rb, *t_wb;
    struct response_parser  parser;
    bool                    capa_done;
    struct request_queue    *request_queue;
    bool                    append_needed;
    size_t                  sent_bytes;
    bool                    should_parse;
};

struct pop3 {
    /** información del cliente */
    struct sockaddr_storage       client_addr;
    socklen_t                     client_addr_len;
    int                           client_fd;

    /** resolución de la dirección del origin server */
    struct addrinfo              *origin_resolution;
    /** intento actual de la dirección del origin server */
    struct addrinfo              *origin_resolution_current;

    /** información del origin server */
    struct sockaddr_storage       origin_addr;
    socklen_t                     origin_addr_len;
    int                           origin_domain;
    int                           origin_fd;

    /** informacion de la transformacion*/
    int                           transformation_read;
    int                           transformation_write;
    struct transform_st           transform;

    struct append_capa_st         append_capa;

    /** maquinas de estados */
    struct state_machine          stm;

    /** estados para el client_fd */
    union {
        struct hello_st     hello;
        struct request_st   request;
        struct response_st  response;
        struct error_st     error;
    } client;

    struct response_parser  response_parser;
    struct request_parser   request_parser;

    /** estados para el origin_fd */
    union {
        struct capa_st     capa;
        struct request_st  request;
        struct response_st response;
    } origin;

    /** buffers para ser usados read_buffer, write_buffer.*/
    uint8_t *request_r;
    uint8_t *response_r, *response_w;
    uint8_t *transform_r, *transform_w;
    buffer request_buffer;
    buffer response_r_buffer, response_w_buffer;
    buffer transform_r_buffer, transform_w_buffer;

    struct request_queue *request_queue;

    /** cantidad de referencias a este objeto. si es uno se debe destruir */
    unsigned references;

    /** Identifica si el servidor de origen soporta pipelining o no*/
    bool pipeliner;

    /** Identifica el nombre de usuario del usuario logeado */
    char *username;

    /** true si el username fue validado con un pass, falso de lo contrario */
    bool verified_username;

    /** siguiente en el pool */
    struct pop3 *next;

};

/**
 * Pool de `struct pop3', para ser reusados.
 *
 * Como tenemos un unico hilo que emite eventos no necesitamos barreras de
 * contención.
 */

static const unsigned  max_pool  = 50; // tamaño máximo
static unsigned        pool_size = 0;  // tamaño actual
static struct pop3 * pool      = 0;  // pool propiamente dicho


static const struct state_definition *
pop3_describe_states(void);

static struct pop3 * pop3_new(int client_fd){
    struct pop3 * ret;

    if(pool == NULL){
        ret = malloc(sizeof(*ret));
    } else {
        ret       = pool;
        pool      = pool->next;
        ret->next = 0;
    }

    if(ret == NULL){
        goto finally;
    }
    memset(ret, 0x00, sizeof(*ret));

    ret->origin_fd          = -1;
    ret->client_fd          = client_fd;
    ret->client_addr_len    = sizeof(ret->client_addr);

    ret->stm    .initial    = RESOLVE_ADDRESS;
    ret->stm    .max_state  = ERROR;
    ret->stm    .states     = pop3_describe_states();
    stm_init(&ret->stm);

    ret->request_r      = malloc(BUFFER_SIZE);
    ret->response_r     = malloc(BUFFER_SIZE);
    ret->response_w     = malloc(BUFFER_SIZE);
    ret->transform_r    = malloc(BUFFER_SIZE);
    ret->transform_w    = malloc(BUFFER_SIZE);

    buffer_init(&ret->request_buffer  , BUFFER_SIZE, ret->request_r);
    buffer_init(&ret->response_r_buffer , BUFFER_SIZE, ret->response_r);
    buffer_init(&ret->response_w_buffer , BUFFER_SIZE, ret->response_w);
    buffer_init(&ret->transform_r_buffer , BUFFER_SIZE, ret->transform_r);
    buffer_init(&ret->transform_w_buffer , BUFFER_SIZE, ret->transform_w);

    ret->request_queue = malloc(sizeof(struct request_queue));
    queue_init(ret->request_queue);

    ret->pipeliner = false;
    ret->references = 1;
finally:
    return ret;

}

/** Realmente destruye */
static void
pop3_destroy_(struct pop3* p){
    if(p->origin_resolution != NULL){
        freeaddrinfo(p->origin_resolution);
        p->origin_resolution = 0;
    }
    if(p->request_queue != NULL){
        free(p->request_queue);
    }
    if(p->username != NULL){
        free(p->username);
    }
    free(p);
}

/** Destruye el struct pop3, tiene en cuenta las referencias y el pool de objetos */
static void
pop3_destroy(struct pop3* p){
    if(NULL != p) {
        if (p->references == 1) {
            if(pool_size < max_pool) {
                p->next = pool;
                pool = p;
                pool_size++;
            } else {
                pop3_destroy_(p);
            }
        } else {
            p->references -= 1;
        }
    }
}

void
pop3_pool_destroy(void){
    struct pop3 * next, *p;
    for(p = pool; p != NULL; p = next){
        next = p->next;
        free(p);
    }
}

/** obtiene el struct (pop3 *) desde la llave de selección  */
#define ATTACHMENT(key) ( (struct pop3 *)(key)->data)

static void pop3_read   (struct selector_key *key);
static void pop3_write  (struct selector_key *key);
static void pop3_block  (struct selector_key *key);
static void pop3_close  (struct selector_key *key);
static const struct fd_handler pop3_handler = {
        .handle_read   = pop3_read,
        .handle_write  = pop3_write,
        .handle_close  = pop3_close,
        .handle_block  = pop3_block,
};

/** Intenta aceptar la nueva conexión entrante */
void
proxyPop3_passive_accept(struct selector_key *key){
    struct sockaddr_storage         client_addr;
    socklen_t                       client_addr_len = sizeof(client_addr);
    struct pop3                     *state          = NULL;

    const int client = accept(key->fd, (struct sockaddr*) &client_addr,
                              &client_addr_len);
    if(client == -1){
        goto fail;
    }
    if(selector_fd_set_nio(client) == -1){
        goto fail;
    }
    state = pop3_new(client);
    if(state == NULL){
        //Posible mejora: Apagar el accept hasta que se libere
        // una conexion para ahorrar recursos.
        goto fail;
    }

    memcpy(&state->client_addr, &client_addr, (size_t)client_addr_len);
    state->client_addr_len = client_addr_len;

    if(SELECTOR_SUCCESS != selector_register(key->s, client, &pop3_handler, OP_WRITE, state)) {
        goto fail;
    }

    proxy_metrics->concurrent_connections++;
    proxy_metrics->historic_connections++;

    return;

fail:
    if(client != -1){
        close(client);
    }
    pop3_destroy(state);
}

/** RESOLVE_ADDRESS */

static void *
resolve_address_blocking(void *data);

static unsigned
resolve_connect(struct selector_key *key);

static unsigned
resolve_address_init(struct selector_key* key) {
    unsigned  ret;
    pthread_t tid;

    struct selector_key* k = malloc(sizeof(*key));
    if(NULL != k) {
        memcpy(k, key, sizeof(*k));
        if(-1 == pthread_create(&tid, 0, resolve_address_blocking, k)) {
            ret = ERROR;
        } else {
            ret = RESOLVE_ADDRESS;
            selector_set_interest_key(key, OP_NOOP);
        }
    } else {
        ret = ERROR;
    }

    return ret;
}

/**
 * Realiza la resolución de DNS bloqueante.
 *
 * Una vez resuelto notifica al selector para que el evento esté
 * disponible en la próxima iteración.
 */
static void *
resolve_address_blocking(void *data) {
    struct selector_key *key = (struct selector_key *) data;
    struct pop3         *p   = ATTACHMENT(key);

    pthread_detach(pthread_self());
    p->origin_resolution = 0;
    struct addrinfo hints = {
            .ai_family    = AF_UNSPEC,
            .ai_socktype  = SOCK_STREAM,
            .ai_flags     = AI_PASSIVE,
            .ai_protocol  = IPPROTO_TCP,
            .ai_canonname = NULL,
            .ai_addr      = NULL,
            .ai_next      = NULL,
    };

    char buff[7];
    snprintf(buff, sizeof(buff), "%hu",
             proxyArguments->origin_port);

    getaddrinfo(proxyArguments->origin_address, buff, &hints,
                &p->origin_resolution);

    selector_notify_block(key->s, key->fd);

    free(data);
    return 0;
}

/** procesa el resultado de la resolución de nombres */
static unsigned
resolve_address_done(struct selector_key *key) {
    struct pop3       *p =  ATTACHMENT(key);
    unsigned           ret;

    if(0 == p->origin_resolution) {
        p->client.error.error = CONNECTION_REFUSED;
        return HANDLEABLE_ERROR;
    } else {
        p->origin_domain   = p->origin_resolution->ai_family;
        p->origin_addr_len = p->origin_resolution->ai_addrlen;
        memcpy(&p->origin_addr,
               p->origin_resolution->ai_addr,
               (size_t) p->origin_resolution->ai_addrlen);
        p->origin_resolution_current = p->origin_resolution;
    }

    return resolve_connect(key);
}

static bool
next_origin_resolution(struct selector_key *key) {
    struct pop3 *p = ATTACHMENT(key);
    bool has_next  = false;

    if(0 != p->origin_resolution_current) {
        p->origin_resolution_current = p->origin_resolution_current->ai_next;
    }

    if(0 != p->origin_resolution_current) {
        p->origin_domain   = p->origin_resolution_current->ai_family;
        p->origin_addr_len = p->origin_resolution_current->ai_addrlen;
        memcpy(&p->origin_addr,
               p->origin_resolution_current->ai_addr,
               (size_t) p->origin_resolution_current->ai_addrlen);
        has_next = true;
    }

    return has_next;
}


/** CONNECTING */

static unsigned
resolve_connect(struct selector_key *key) {
    struct pop3 *p     = ATTACHMENT(key);
    bool        error  = false;

    int sock = socket(p->origin_domain, SOCK_STREAM, IPPROTO_TCP);
    if (sock == -1) {
        error = true;
        goto finally;
    }
    if (selector_fd_set_nio(sock) == -1) {
        error = true;
        goto finally;
    }
    if (-1 == connect(sock, (const struct sockaddr *)&p->origin_addr,
                      p->origin_addr_len)) {
        if(errno == EINPROGRESS) {
            // es esperable,  tenemos que esperar a la conexión
            // dejamos de de pollear el socket del cliente
            selector_status st = selector_set_interest_key(key, OP_NOOP);
            if(SELECTOR_SUCCESS != st) {
                error = true;
                goto finally;
            }

            // esperamos la conexion en el nuevo socket
            st = selector_register(key->s, sock, &pop3_handler,
                                   OP_WRITE, key->data);
            if(SELECTOR_SUCCESS != st) {
                error = true;

                goto finally;
            }
            p->references += 1;
        } else {
            error = true;
            selector_unregister_fd(key->s, sock);
            goto finally;
        }
    } else {
        // estamos conectados sin esperar... no parece posible
        abort();
    }

    return CONNECTING;

    finally:
    if (error) {
        if (sock != -1) {
            close(sock);
        }
    }
    p->client.error.error = CONNECTION_REFUSED;
    return HANDLEABLE_ERROR;
}

static unsigned
connecting(struct selector_key *key) {
    int error;
    socklen_t len = sizeof(error);
    struct pop3 *p = ATTACHMENT(key);
    unsigned ret;

    if (getsockopt(key->fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0) {
        p->client.error.error = CONNECTION_REFUSED;
        return HANDLEABLE_ERROR;
    } else {
        if (error == 0) {
            p->origin_fd = key->fd;
        } else {
            selector_unregister_fd(key->s, key->fd);
            close(key->fd);
            if (next_origin_resolution(key) == true) {
                struct selector_key *k = malloc(sizeof(*key));
                if (k == NULL) {
                    ret = ERROR;
                } else {
                    memcpy(k, key, sizeof(*k));
                    k->fd = p->client_fd;
                    ret = resolve_connect(k);
                    free(k);
                }
                return ret;
            }
            p->client.error.error = CONNECTION_REFUSED;
            return HANDLEABLE_ERROR;
        }
    }

    selector_status s = SELECTOR_SUCCESS;

    s |= selector_set_interest(key->s, p->client_fd, OP_NOOP);
    s |= selector_set_interest_key(key, OP_READ);
    return SELECTOR_SUCCESS == s ? HELLO : ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// CAPA
////////////////////////////////////////////////////////////////////////////////

static unsigned
capa_read_process(struct selector_key *key, uint8_t read) {
    struct pop3     *p =  ATTACHMENT(key);
    struct capa_st  *d = &p->origin.capa;
    unsigned  ret      = CAPA;
    enum response_state st;
    const struct parser_event * event;

    st   = response_parser_feed(&d->response, read);

    if(p->pipeliner != true && d->response.pop3_response_success == true) {
        event = parser_feed(d->pipeline, read);
        if(event->type == STRING_CMP_EQ) {
            p->pipeliner = true;
        }

        if(st == response_new_line) {
            parser_reset(d->pipeline);
        }
    }

    if(response_is_done(st, 0)) {
        response_close(&d->response);
        parser_destroy(d->pipeline);

        selector_status s = SELECTOR_SUCCESS;
        s |= selector_set_interest_key(key, OP_NOOP);
        s |= selector_set_interest    (key->s, p->client_fd, OP_READ);
        ret = SELECTOR_SUCCESS == s ? REQUEST : ERROR;
        if(ret == REQUEST) {
            request_parser_init(&p->request_parser);
        }
    }

    return ret;
}

static void
capa_init(const unsigned state, struct selector_key *key) {
    struct pop3     *p =  ATTACHMENT(key);
    struct capa_st  *d = &p->origin.capa;
    struct parser_definition def = parser_utils_strcmpi("pipelining");
    memcpy(d->message, "CAPA\r\n", CAPA_CMD);
    d->remaining   = CAPA_CMD;
    d->current     = d->message;
    d->wb          = &(p->request_buffer);
    memcpy(&d->pipelineDef, &def, sizeof(def));
    d->pipeline    = parser_init(parser_no_classes(), &d->pipelineDef);

    struct request *capa_req = malloc(sizeof(struct request));
    capa_req->cmd   = unknown;
    capa_req->multi = true;
    capa_req->nargs = 0;

    response_parser_init(&d->response, capa_req);
}

static unsigned
capa_read(struct selector_key *key) {
    struct pop3     *p =  ATTACHMENT(key);
    struct capa_st  *d = &p->origin.capa;
    unsigned  ret      = CAPA;
    uint8_t *ptr;
    uint8_t read;
    size_t  count;
    ssize_t  n;

    ptr = buffer_write_ptr(d->wb, &count);
    n   = recv(key->fd, ptr, count, 0);
    if(n > 0) {
        buffer_write_adv(d->wb, n);
        while(buffer_can_read(d->wb)) {
            read = buffer_read(d->wb);
            ret = capa_read_process(key, read);
        }
    } else {
        ret = ERROR;
    }

    return ret;
}

static unsigned
capa_write(struct selector_key *key) {
    struct pop3     *p =  ATTACHMENT(key);
    struct capa_st  *d = &p->origin.capa;

    unsigned  ret      = CAPA;
    uint8_t *ptr;
    size_t  count;
    ssize_t  n;

    if(d->remaining > 0) {
        ptr = buffer_write_ptr(d->wb, &count);
        count = count >= d->remaining ? d->remaining : count;
        memcpy(ptr, d->current, count);
        buffer_write_adv(d->wb, count);
        d->remaining -= count;
        d->current   += count;
    }

    ptr = buffer_read_ptr(d->wb, &count);
    n   = send(key->fd, ptr, count, MSG_NOSIGNAL);
    if(n > 0) {
        buffer_read_adv(d->wb, n);
        if(!buffer_can_read(d->wb) && d->remaining == 0) {
            if( SELECTOR_SUCCESS != selector_set_interest_key(key, OP_READ)) {
                p->client.error.error = INTERNAL;
                return HANDLEABLE_ERROR;
            }
        }
    } else {
        ret = ERROR;
    }

    return ret;
}


static fd_interest
compute_write_interests(fd_selector s, buffer * b, fd_interest duplex, int fd) {
    fd_interest ret = OP_NOOP;
    if ((duplex & OP_WRITE) && buffer_can_read (b)) {
        ret |= OP_WRITE;
    }
    if(SELECTOR_SUCCESS != selector_set_interest(s, fd, ret)) {
        ret = OP_NOOP;
    }
    return ret;
}

static fd_interest
compute_read_interests(fd_selector s, buffer * b, fd_interest duplex, int fd) {
    fd_interest ret = OP_NOOP;
    if ((duplex & OP_READ)  && buffer_can_write(b)) {
        ret |= OP_READ;
    }
    if(SELECTOR_SUCCESS != selector_set_interest(s, fd, ret)) {
        ret = OP_NOOP;
    }
    return ret;
}


////////////////////////////////////////////////////////////////////////////////
// HELLO
////////////////////////////////////////////////////////////////////////////////

static unsigned
hello_interests(struct selector_key *key) {
  unsigned ret = HELLO;
  struct pop3     *p =  ATTACHMENT(key);
  struct hello_st *d = &p->client.hello;

  compute_read_interests(key->s, d->buffer, OP_READ, p->origin_fd);
  compute_write_interests(key->s, d->buffer, OP_WRITE, p->client_fd);

  return ret;
}

static void
hello_init(const unsigned state, struct selector_key *key) {
    struct pop3     *p =  ATTACHMENT(key);
    struct hello_st *d = &p->client.hello;

    d->buffer          = &(p->request_buffer);
    d->done            = false;

  struct request *hello = malloc(sizeof(struct request));
  hello->cmd   = unknown;
  hello->multi = false;
  hello->nargs = 0;

  response_parser_init(&d->response, hello);
}

static unsigned
hello_read(struct selector_key *key) {
    struct pop3 *p     =  ATTACHMENT(key);
    struct hello_st *d = &p->client.hello;
    unsigned  ret      = HELLO;
    buffer *buffer     = d->buffer;
    uint8_t *ptr;
    size_t  count;
    ssize_t  n;

    ptr = buffer_write_ptr(buffer, &count);
    n = recv(key->fd, ptr, count, 0);
    if(n > 0) {
        buffer_write_adv(buffer, n);
        ret = hello_interests(key);
    } else {
        ret = ERROR;
    }

    return ret;
}

static unsigned
hello_write(struct selector_key *key) {
    struct pop3 *p     =  ATTACHMENT(key);
    struct hello_st *d = &p->client.hello;

    unsigned  ret     = HELLO;
    buffer  *buffer   = d->buffer;
    uint8_t *ptr;
    size_t  count;
    ssize_t  n;

    enum response_state st;
    while(buffer_can_parse(d->buffer) && !d->done) {
        st = response_consume(buffer, &d->response);
        if(response_is_done(st, 0)){
            response_close(&d->response);
            d->done = true;
        }
    }

    ptr = buffer_parse_ptr(d->buffer, &count);
    n = send(key->fd, ptr, count, MSG_NOSIGNAL);
    proxy_metrics->bytes += n;
    if(n == -1) {
        ret = ERROR;
    } else {
        buffer_read_adv(d->buffer, n);
        if(!buffer_can_read_parsed(buffer) && d->done) {
            selector_status s = SELECTOR_SUCCESS;
            s |= selector_set_interest_key(key, OP_NOOP);
            s |= selector_set_interest    (key->s, p->origin_fd, OP_WRITE);

            ret = SELECTOR_SUCCESS == s ? CAPA : ERROR;
        } else {
            ret = hello_interests(key);
        }
    }

    return ret;
}

////////////////////////////////////////////////////////////////////////////////
/// REQUEST
////////////////////////////////////////////////////////////////////////////////
/** inicializa las variables de los estados del REQUEST */
static void
request_init(const unsigned state, struct selector_key *key) {
    struct pop3     *p =  ATTACHMENT(key);
    struct request_st *d = &p->client.request;

    d->fd        = &p->client_fd;
    d->buffer    = &p->request_buffer;
    d->duplex    = OP_READ | OP_WRITE;
    d->other     = &p->origin.request;
    d->request_queue = p->request_queue;

    d = &p->origin.request;
    d->fd       = &p->origin_fd;
    d->buffer   = &p->request_buffer;
    d->duplex   = OP_READ | OP_WRITE;
    d->other    = &p->client.request;
    d->request_queue = p->request_queue;

}

static bool
should_transform(struct request *request) {
    return request->cmd == retr && proxyArguments->command != NULL;
}

static bool
should_append_capa(struct request *request) {
    return request->cmd == capa;
}

static enum pop3_state
determine_response_state(struct request_queue *q, struct selector_key *key){
    struct request *r = peek_request(q);
    struct pop3    *p = ATTACHMENT(key);

    if(should_transform(r))
        return TRANSFORM;
    if(should_append_capa(r) && !p->pipeliner)
        return APPEND_CAPA;

    return RESPONSE;
}

static unsigned
request_interests(struct selector_key *key) {
    struct pop3 *p            = ATTACHMENT(key);
    struct request_st * d     = &ATTACHMENT(key)->client.request;
    struct request_st *other  = d->other;

    unsigned ret = REQUEST;

    fd_interest client = compute_read_interests(key->s, d->buffer, d->duplex, p->client_fd);
    fd_interest origin = compute_write_interests(key->s, other->buffer, other->duplex, p->origin_fd);

    if(client == OP_NOOP && origin == OP_NOOP) {
        compute_write_interests(key->s, d->buffer, d->duplex, p->client_fd);
        compute_read_interests(key->s, other->buffer, other->duplex, p->origin_fd);
        ret = RESPONSE;
    }

    return ret;
}

bool
request_can_read(struct selector_key *key) {
    struct pop3 *p = ATTACHMENT(key);
    buffer * buffer = p->client.request.buffer;
    uint8_t c;
    ssize_t n;

    if(!buffer_can_write(buffer)) {
        return false;
    }
    if((recv(p->client_fd, &c, 1, 0)) > 0) {
        buffer_write(buffer, c);
        return true;
    }
    return false;
}

static unsigned
request_read(struct selector_key *key) {
    struct request_st * d     = &ATTACHMENT(key)->client.request;
    struct request_st *other  = d->other;

    buffer *buffer      = d->buffer;
    unsigned  ret   = REQUEST;
    uint8_t *ptr;
    size_t  count;
    ssize_t  n;

    ptr = buffer_write_ptr(buffer, &count);
    n = recv(key->fd, ptr, count, 0);
    if(n > 0) {
        buffer_write_adv(buffer, n);
        ret = request_interests(key);
    } else {
        shutdown(*d->fd, SHUT_RD);
        d->duplex &= ~OP_READ;
        if(*d->other->fd != -1) {
            shutdown(*d->other->fd, SHUT_WR);
            d->other->duplex &= ~OP_WRITE;
        }
    }

    return ret;
}

static void
request_read_close(const unsigned state, struct selector_key *key) {
    struct request_st * d = &ATTACHMENT(key)->client.request;
    request_close(&ATTACHMENT(key)->request_parser);
}

static unsigned
request_write(struct selector_key *key) {
    struct pop3       *p          =  ATTACHMENT(key);
    struct request_st *d          = &p->origin.request;
    struct request_st *other      = d->other;
    struct request    *request    = NULL;
    struct request_parser *parser = &p->request_parser;

    unsigned ret = REQUEST;
    uint8_t *ptr;
    size_t  count;
    ssize_t  n;
    bool    complete_request = true;
    bool    error            = false;

    buffer *b = d->buffer;

    enum request_state st;
    while(buffer_can_parse(d->buffer)) {
        st = request_consume(b, parser, &error, d->request_queue);
        if(request_is_done(st, &error)){
            request_close(parser);
            request_parser_init(parser);
        }
    }

    request = peek_next_unsent(d->request_queue);
    if(request == NULL) {
        complete_request = false;
        request = &parser->request;
    }

    ptr = buffer_parse_ptr(d->buffer, &count);

    if(!p->pipeliner) {
        count = count > request->length ? request->length : count;
    }

    n = send(key->fd, ptr, count, MSG_NOSIGNAL);
    if(n == -1) {
        shutdown(*d->fd, SHUT_WR);
        d->duplex &= ~OP_WRITE;
        if(*d->other->fd != -1) {
            shutdown(*d->other->fd, SHUT_RD);
            d->other->duplex &= ~OP_READ;
        }
        return ERROR;
    } else {
        buffer_read_adv(b, n);
        request->length -= n;
        proxy_metrics->bytes += n;

        if(!p->pipeliner) {
            if(complete_request) {
                request->length = -1;
                compute_read_interests(key->s, b, d->duplex, *d->fd);
                compute_write_interests(key->s, b, other->duplex, *other->fd);
                return RESPONSE;
            }
        } else {
            if(!buffer_can_read_parsed(d->buffer) && !queue_is_empty(d->request_queue) && !request_can_read(key)) {
                compute_read_interests(key->s, b, d->duplex, *d->fd);
                compute_write_interests(key->s, b, other->duplex, *other->fd);
                return RESPONSE;
            }
        }
        ret = request_interests(key);
    }

    return ret;
}

////////////////////////////////////////////////////////////////////////////////
/// RESPONSE
////////////////////////////////////////////////////////////////////////////////

static void
response_init(const unsigned state, struct selector_key *key) {
    struct pop3     *p =  ATTACHMENT(key);
    struct response_st *d = &p->client.response;

    d->fd        = &p->client_fd;
    d->rb        = &p->response_r_buffer;
    d->wb        = &p->response_w_buffer;
    d->duplex    = OP_READ | OP_WRITE;
    d->other     = &p->origin.response;
    d->request_queue = p->request_queue;
    d->should_parse = true;

    d = &p->origin.response;
    d->fd       = &p->origin_fd;
    d->rb       = &p->response_r_buffer;
    d->wb       = &p->response_w_buffer;
    d->duplex   = OP_READ | OP_WRITE;
    d->other    = &p->client.response;
    d->request_queue = p->request_queue;
    d->should_parse = true;
    response_parser_init(&p->response_parser, pop_request(d->request_queue));
}

static void
write_user(struct selector_key *key, char *username, size_t length){
    struct pop3 *pop3 = ATTACHMENT(key);
    if(pop3->username == NULL) {
        pop3->username = malloc(length+1);
    } else {
        pop3->username = realloc(pop3->username, length+1);
    }
    memcpy(pop3->username, username, length);
    pop3->username[length] = '\0';
    printf("Attempting to validate user %s with origin server\n", pop3->username);
}

static void
lock_user(struct selector_key *key){
    struct pop3 *pop3 = ATTACHMENT(key);
    pop3->verified_username = true;
    printf("Origin server successfully validated user %s\n", pop3->username);
}

static unsigned
response_interests(struct selector_key *key);

static unsigned
response_read(struct selector_key *key) {
    struct pop3        *pop = ATTACHMENT(key);
    struct response_st *d   = &pop->origin.response;

    buffer *rb      = d->rb;

    uint8_t *ptr;
    size_t  count;
    ssize_t  n;
    unsigned ret = RESPONSE;


    ptr = buffer_write_ptr(rb, &count);
    n = recv(key->fd, ptr, count, 0);
    if(n > 0) {
        buffer_write_adv(rb, n);
        ret = response_interests(key);
    } else {
        shutdown(*d->fd, SHUT_RD);
        d->duplex &= ~OP_READ;
        if(*d->other->fd != -1) {
            close(*d->other->fd);
            d->other->duplex &= ~OP_WRITE;
        }

    }
    return ret;
}

static unsigned
response_interests(struct selector_key *key) {
    unsigned ret;
    struct pop3        *pop    = ATTACHMENT(key);
    struct response_st *d   = &pop->origin.response;
    struct response_st *other  = d->other;

    fd_interest origin = compute_read_interests(key->s, d->rb, d->duplex, pop->origin_fd);
    fd_interest client = compute_write_interests(key->s, other->rb, other->duplex, pop->client_fd);

    if(client == OP_NOOP && origin == OP_NOOP) {
        compute_write_interests(key->s, d->wb, d->duplex, pop->origin_fd);
        compute_read_interests(key->s, other->rb, other->duplex, pop->client_fd);
        ret = REQUEST;
    } else {
        ret = RESPONSE;
    }
    return ret;
}

static unsigned
response_write(struct selector_key *key){
    struct pop3        *p      = ATTACHMENT(key);
    struct response_st *d      = &ATTACHMENT(key)->client.response;
    struct response_st *other  = d->other;

    unsigned ret    = RESPONSE;
    uint8_t *ptr;
    size_t count;
    ssize_t n;
    buffer  *buffer = d->rb;
    bool locked_usr = ATTACHMENT(key)->verified_username;

    struct response_parser *parser = &p->response_parser;
    struct request_queue   *queue  = d->request_queue;
    struct request         *request;

    if(should_transform(parser->request)) {
        selector_set_interest(key->s, *other->fd, OP_READ);
        selector_set_interest(key->s, *d->fd, OP_WRITE);
        return TRANSFORM;
    }

  if(should_append_capa(parser->request) && !p->pipeliner) {
    selector_set_interest(key->s, *other->fd, OP_READ);
    selector_set_interest(key->s, *d->fd, OP_WRITE);
    return APPEND_CAPA;
  }

  enum response_state st;
  while(buffer_can_parse(buffer) && d->should_parse) {
    st = response_consume(buffer, parser);
    if(response_is_done(st, 0)) {
      if(parser->request->cmd == user && parser->pop3_response_success == true && !locked_usr) {
        write_user(key, parser->request->arg[0], parser->request->argsize[0]);
      }
      else if(parser->request->cmd == pass && parser->pop3_response_success == true){
        lock_user(key);
      } else {
        printf("Received response for %s command for user %s\n",
               POP3_CMDS_INFO[parser->request->cmd].string_representation,
               (p->username == NULL ? "unknown" : p->username));
      }
        printf("sending response for %s command for user %s\n",
               POP3_CMDS_INFO[parser->request->cmd].string_representation,
                       (p->username == NULL ? "unknown" : p->username));
      response_close(parser);
      if(!queue_is_empty(queue) &&
         p->pipeliner &&
         determine_response_state(queue, key) == RESPONSE) {
         request = pop_request(queue);
         response_parser_init(parser, request);
      } else {
          d->should_parse = false;
          break;
      }
    }
  }

  ptr = buffer_parse_ptr(buffer, &count);
  n = send(key->fd, ptr, count, MSG_NOSIGNAL);
  proxy_metrics->bytes += n;

  if(n == -1) {
    ret = ERROR;
  } else {
    buffer_read_adv(buffer, n);

    if(!buffer_can_read_parsed(buffer) &&
       ((queue_is_empty(d->request_queue) && p->pipeliner) || (!p->pipeliner))
       && response_is_done(p->response_parser.response_state, 0)) {
      compute_read_interests(key->s, &p->request_buffer, OP_READ, *d->fd);
      compute_write_interests(key->s, &p->request_buffer, OP_WRITE, *other->fd);
      ret = REQUEST;
    } else if(!buffer_can_read_parsed(buffer) &&
              response_is_done(p->response_parser.response_state, 0) &&
              (ret = determine_response_state(d->request_queue, key)) != RESPONSE) {
      request = pop_request(d->request_queue);
      response_parser_init(&p->response_parser, request);
      selector_set_interest(key->s, *other->fd, OP_READ);
      selector_set_interest(key->s, *d->fd, OP_WRITE);
      return ret;
    }
  }
  if(ret == RESPONSE){
    ret = response_interests(key);
  }
  return ret;
}

////////////////////////////////////////////////////////////////////////////////
/// TRANSFORMATION
////////////////////////////////////////////////////////////////////////////////

transformation_status
open_transformation(struct selector_key * key);

static void
transform_init(const unsigned state, struct selector_key *key) {
    struct pop3         *p =  ATTACHMENT(key);
    struct transform_st *t = &p->transform;

    t->rb                  = &p->response_r_buffer;
    t->wb                  = &p->response_w_buffer;
    t->t_rb                = &p->transform_r_buffer;
    t->t_wb                = &p->transform_w_buffer;
    t->request_queue       = p->request_queue;
    t->transformation_done = false;
    t->transform_needed    = false;
    t->parser              = p->response_parser;
    t->should_parse        = true;
    t->descape_done        = false;
    t->escape_done         = false;
    t->transform_error     = false;
    t->termination_bytes   = 0;

    printf("Transforming response for %s request for user %s\n",
           POP3_CMDS_INFO[t->parser.request->cmd].string_representation,
           (p->username == NULL ? "unknown" : p->username));
}

static unsigned
transform_interests(struct transform_st *t, struct selector_key *key) {
    struct pop3 *pop = ATTACHMENT(key);
    buffer  *rb   = t->rb;
    buffer  *wb   = t->wb;
    buffer  *t_wb = t->t_wb;

    fd_interest origin = compute_read_interests(key->s, rb, OP_READ, pop->origin_fd);
    fd_interest client = compute_write_interests(key->s, t_wb, OP_WRITE, pop->client_fd);
    if(client == OP_NOOP) {
        compute_write_interests(key->s, rb, OP_WRITE, pop->client_fd);
    }

    if(t->transform_needed) {
        compute_read_interests(key->s, t_wb, OP_READ, pop->transformation_read);
        compute_write_interests(key->s, rb, OP_WRITE, pop->transformation_write);
    }

    return TRANSFORM;
}

static unsigned
transform_read(struct selector_key *key) {
    struct pop3         *pop = ATTACHMENT(key);
    struct transform_st *t   = &pop->transform;

    buffer  *rb  = t->rb;
    uint8_t *ptr;
    size_t  count;
    ssize_t  n;

    unsigned ret;

    ptr = buffer_write_ptr(rb, &count);
    n = recv(key->fd, ptr, count, 0);
    if(n > 0) {
        buffer_write_adv(rb, n);
        ret = transform_interests(t, key);
    } else {
        ret = ERROR;
    }
    return ret;
}

static unsigned
transform_write(struct selector_key *key) {
    struct pop3         *pop = ATTACHMENT(key);
    struct transform_st *t   = &pop->transform;
    struct escape_response_parser *e_parser = &t->escape;

    buffer  *rb  = t->rb;
    buffer  *wb  = t->wb;
    buffer  *tb  = t->t_wb;
    uint8_t *ptr;
    size_t  count;
    ssize_t  n;
    struct response_parser *parser = &t->parser;
    struct request_queue   *queue  = t->request_queue;
    struct request         *request;
    char                   *termination  =  ".\r\n";

    unsigned ret = TRANSFORM;
    enum response_state st;

    if(t->transform_needed && t->should_parse && !t->transform_error) {
        enum response_state escape_st = response_byte;
        buffer_write_ptr(wb, &count);
        while(buffer_can_read(tb) && count >= 2) {
            escape_response_consume(tb, wb, e_parser);
            buffer_write_ptr(wb, &count);
        }

      ptr = buffer_read_ptr(wb, &count);
      n = send(key->fd, ptr, count, MSG_NOSIGNAL);
      proxy_metrics->bytes += n;

      if(n == -1) {
        ret = ERROR;
      } else {
        buffer_read_adv(wb, n);

        if(t->transformation_done && !buffer_can_read(wb) && !buffer_can_read(tb)) {
            if(!t->escape_done) {
                while(buffer_can_write(wb)) {
                    buffer_write(wb,termination[t->termination_bytes]);
                    t->termination_bytes++;
                    if(t->termination_bytes == strlen(termination)) {
                        t->escape_done = true;
                        break;
                    }
                }
                return transform_interests(t, key);
            }

            printf("Finished transformation \n");
          if(queue_is_empty(queue)) {
            selector_set_interest(key->s, pop->client_fd, OP_READ);
            selector_set_interest(key->s, pop->origin_fd, OP_WRITE);
            ret = REQUEST;
          } else {
            selector_set_interest(key->s, pop->origin_fd, OP_READ);
            selector_set_interest(key->s, pop->client_fd, OP_WRITE);
            ret = RESPONSE;
          }
        } else {
          ret = transform_interests(t, key);
        }
      }
      return ret;
    }

    while(buffer_can_parse(rb) && t->should_parse) {
        st = response_consume(rb, parser);
        if(t->transform_needed == false && st == response_new_line && !t->transform_error) {
            t->transform_needed = parser->pop3_response_success ? true : false;
            t->transform_error = open_transformation(key) != OK;
            if(!t->transform_error && t->transform_needed) {
                response_close(parser);
                descape_response_parser_init(&t->descape);
                escape_response_parser_init(&t->escape);
                t->should_parse = false;
            } else {
                t->should_parse = true;
            }
            break;
        }
        if(response_is_done(st, 0)) {
            response_close(parser);
            t->should_parse = false;
            break;
        }
    }

    ptr = buffer_parse_ptr(rb, &count);
    n = send(key->fd, ptr, count, MSG_NOSIGNAL);

    if(n == -1) {
        ret = ERROR;
    } else {
        buffer_read_adv(rb, n);

        if(t->transform_needed && !buffer_can_read_parsed(rb)) {
            t->should_parse = true;
        }

        if(response_is_done(t->parser.response_state, 0) && !buffer_can_read_parsed(rb)) {
          if(queue_is_empty(queue)) {
            selector_set_interest(key->s, pop->client_fd, OP_READ);
            selector_set_interest(key->s, pop->origin_fd, OP_WRITE);
            ret = REQUEST;
          } else {
            selector_set_interest(key->s, pop->origin_fd, OP_READ);
            selector_set_interest(key->s, pop->client_fd, OP_WRITE);
            ret = RESPONSE;
          }
        } else {
            ret = transform_interests(t, key);
        }
    }

    return ret;
}

#define READ_END  0
#define WRITE_END 1

static void
set_environment(struct pop3 * p);

static void
transf_read(struct selector_key *key);

static void
transf_write(struct selector_key *key);

static void
transf_close(struct selector_key *key);

static const struct fd_handler transformation_handler = {
        .handle_read   = transf_read,
        .handle_write  = transf_write,
        .handle_close  = transf_close,
        .handle_block  = NULL,
};

transformation_status
open_transformation(struct selector_key * key) {
    struct pop3 *p = ATTACHMENT(key);

    pid_t pid;
    char * args[4];
    args[0] = "bash";
    args[1] = "-c";
    args[2] = proxyArguments->command;
    args[3] = NULL;

    int proxy_transform[2];
    int transform_proxy[2];

    if (pipe(proxy_transform) < 0) {
        return PIPE_FAIL;
    }

    if(pipe(transform_proxy) < 0) {
        close(proxy_transform[0]);
        close(proxy_transform[1]);
        return PIPE_FAIL;
    }

    pid = fork();

    if (pid == -1) {
        close(proxy_transform[0]);
        close(proxy_transform[1]);
        close(transform_proxy[0]);
        close(transform_proxy[1]);
        return FORK_FAIL;
    }
    else if (pid == 0) {
        dup2(transform_proxy[WRITE_END], STDOUT_FILENO);
        close(transform_proxy[WRITE_END]);
        close(transform_proxy[READ_END]);

        dup2(proxy_transform[READ_END], STDIN_FILENO);
        close(proxy_transform[READ_END]);
        close(proxy_transform[WRITE_END]);


        FILE * f = freopen(proxyArguments->filter_error_file, "a+", stderr);
        if (f == NULL) {
            freopen("/dev/null", "w", stderr);
        }

        set_environment(p);

        int exec = execv("/bin/bash", args);
        if (exec == -1) {
            exit(EXEC_FAIL);
        }

    } else {
        close(proxy_transform[READ_END]);
        close(transform_proxy[WRITE_END]);

        if(selector_fd_set_nio(transform_proxy[READ_END]) != -1 &&
        selector_register(key->s, transform_proxy[READ_END], &transformation_handler, OP_READ, p) == SELECTOR_SUCCESS) {
            p->transformation_read = transform_proxy[READ_END];
        } else {
            close(proxy_transform[WRITE_END]);
            close(transform_proxy[READ_END]);
            return SELECTOR_ERROR;
        }

        if(selector_fd_set_nio(proxy_transform[WRITE_END]) != -1 &&
           selector_register(key->s, proxy_transform[WRITE_END], &transformation_handler, OP_WRITE, p) == SELECTOR_SUCCESS) {
            p->transformation_write = proxy_transform[WRITE_END];
        } else {
            selector_unregister_fd(key->s, transform_proxy[READ_END]);
            close(proxy_transform[READ_END]);
            close(transform_proxy[WRITE_END]);
            return SELECTOR_ERROR;
        }
    }

    return OK;
}

static void
set_environment(struct pop3 * p) {
    if(proxyArguments->media_types != NULL) {
        setenv("FILTER_MEDIAS", proxyArguments->media_types, 1);
    }
    if(proxyArguments->message != NULL) {
        setenv("FILTER_MSG", proxyArguments->message, 1);
    }
    if(proxyArguments->version != NULL) {
        setenv("POP3FILTER_VERSION", proxyArguments->version, 1);
    }
    if(p->username != NULL) {
        setenv("POP3_USERNAME", p->username, 1);
    }
    if(proxyArguments->origin_address != NULL) {
        setenv("POP3_SERVER",  proxyArguments->origin_address, 1);
    }
}


////////////////////////////////////////////////////////////////////////////////
/// TRANSFORMATION HANDLERS
////////////////////////////////////////////////////////////////////////////////


static void
transf_read(struct selector_key *key) {
    struct pop3         *p = ATTACHMENT(key);
    struct transform_st *t = &p->transform;

    buffer  *tb = t->t_wb;
    uint8_t *ptr;
    size_t count;
    ssize_t n;

    ptr = buffer_write_ptr(tb, &count);
    n = read(p->transformation_read, ptr, count);
    if (n <= 0) {
        t->transformation_done = true;
        selector_set_interest(key->s, p->client_fd, OP_WRITE);
        selector_set_interest(key->s, p->transformation_read, OP_NOOP);
    } else {
        buffer_write_adv(tb, n);
        selector_set_interest(key->s, p->client_fd, OP_WRITE);
    }
}

static void
transf_write(struct selector_key *key) {
    struct pop3         *p = ATTACHMENT(key);
    struct transform_st *t = &p->transform;
    struct descape_response_parser *parser = &t->descape;

    buffer  *b  = t->rb;
    buffer  *tb = t->t_rb;
    uint8_t *ptr;
    size_t count;
    ssize_t n;

    enum response_state st = response_byte;
    buffer_write_ptr(tb, &count);

    while(buffer_can_read(b) && count >= 2 && !t->descape_done) {
        buffer_write_ptr(tb, &count);
        st = descape_response_consume(b, tb, parser);
        if(descape_response_is_done(parser)) {
            descape_response_close(parser);
            t->descape_done = true;
            break;
        }
    }

    ptr = buffer_read_ptr(tb, &count);
    n = write(key->fd, ptr, count);

    if (n > 0) {

        buffer_read_adv(tb, n);
        transform_interests(t, key);
    } else if (n == -1 || n == 0) {
        selector_set_interest_key(key, OP_NOOP);
        selector_set_interest(key->s, p->origin_fd, OP_READ);
        selector_set_interest(key->s, p->client_fd, OP_WRITE);
    }

    if(!buffer_can_read(tb) && !buffer_can_read(b) && t->descape_done) {
        selector_unregister_fd(key->s, p->transformation_write);
        close(p->transformation_write);
    }
}

static void
transf_close(struct selector_key *key) {
    struct pop3         *p = ATTACHMENT(key);
    struct transform_st *t = &p->transform;

    if(key->fd == p->transformation_read) {
        t->transformation_done = true;
    }
    close(key->fd);
}


////////////////////////////////////////////////////////////////////////////////
/// HANDLEABLE_ERROR
////////////////////////////////////////////////////////////////////////////////

static void
error_init(const unsigned state, struct selector_key *key) {
  struct pop3      *p =  ATTACHMENT(key);
  struct error_st  *d = &p->client.error;

  d->message     = error_messages[d->error];
  d->remaining   = strlen(d->message);
  d->wb          = &(p->request_buffer);
  selector_set_interest(key->s, p->client_fd, OP_WRITE);
}

static unsigned
error_write(struct selector_key *key) {
  struct pop3     *p  =  ATTACHMENT(key);
  struct error_st *d  = &p->client.error;
  unsigned        ret = HANDLEABLE_ERROR;

  uint8_t *ptr;
  size_t  count;
  ssize_t  n;

  if(d->remaining > 0) {
    ptr = buffer_write_ptr(d->wb, &count);
    count = count >= d->remaining ? d->remaining : count;
    memcpy(ptr, d->message, count);
    buffer_write_adv(d->wb, count);
    d->remaining -= count;
    d->message   += count;
  }

  ptr = buffer_read_ptr(d->wb, &count);
  n   = send(key->fd, ptr, count, MSG_NOSIGNAL);
  if(n > 0) {
    buffer_read_adv(d->wb, n);
    if (!buffer_can_read(d->wb) && d->remaining == 0) {
      ret = ERROR;
    }
  }

  return ret;
}

////////////////////////////////////////////////////////////////////////////////
/// APPEND_CAPA
////////////////////////////////////////////////////////////////////////////////

static void
append_capa_init(const unsigned state, struct selector_key *key) {
    struct pop3         *p =  ATTACHMENT(key);
    struct append_capa_st *a = &p->append_capa;

    a->rb                  = &p->response_r_buffer;
    a->wb                  = &p->response_w_buffer;
    a->request_queue       = p->request_queue;
    a->capa_done           = false;
    a->append_needed       = false;
    a->parser              = p->response_parser;
    a->sent_bytes          = 0;
    a->should_parse        = true;
}


static unsigned
append_capa_read(struct selector_key *key) {
    struct pop3         *pop = ATTACHMENT(key);
    struct append_capa_st *a   = &pop->append_capa;

    buffer  *rb  = a->rb;
    buffer  *wb  = a->wb;
    uint8_t *ptr;
    size_t  count;
    ssize_t  n;

    ptr = buffer_write_ptr(rb, &count);
    n = recv(key->fd, ptr, count, 0);
    if(n > 0) {
        buffer_write_adv(rb, n);
        compute_read_interests(key->s, rb, OP_READ, pop->origin_fd);
        compute_write_interests(key->s, rb, OP_WRITE, pop->client_fd);
    } else {
        return ERROR;
    }
    return APPEND_CAPA;
}

static unsigned
append_capa_write(struct selector_key *key) {
    struct pop3         *pop = ATTACHMENT(key);
    struct append_capa_st *a   = &pop->append_capa;

    buffer  *rb  = a->rb;
    buffer  *wb  = a->wb;
    uint8_t *ptr;
    char *append = "PIPELINING\r\n";
    size_t  count;
    ssize_t  n;
    struct response_parser *parser = &a->parser;
    struct request_queue   *queue  = a->request_queue;
    struct request         *request;

    unsigned ret = APPEND_CAPA;
    enum response_state st;

    if(a->append_needed && !a->capa_done) {
        count = strlen(append) - a->sent_bytes;
        n = send(key->fd, append + a->sent_bytes, count, MSG_NOSIGNAL);

        if(n == -1) {
            ret = ERROR;
        } else {
            a->sent_bytes += n;

            if(a->sent_bytes >= strlen(append)) {
              a->capa_done = true;
              compute_read_interests(key->s, rb, OP_READ, pop->origin_fd);
              compute_write_interests(key->s, rb, OP_WRITE, pop->client_fd);
              ret = APPEND_CAPA;
            }
        }
        return ret;
    }

    while(buffer_can_parse(rb) && a->should_parse) {
        st = response_consume(rb, parser);
        if(a->append_needed == false && st == response_new_line && !a->capa_done) {
            a->append_needed = parser->pop3_response_success ? true : false;
            break;
        }
        if(response_is_done(st, 0)) {
            response_close(parser);
            a->should_parse = false;
            break;
        }
    }

    ptr = buffer_parse_ptr(rb, &count);
    n = send(key->fd, ptr, count, MSG_NOSIGNAL);

    if(n == -1) {
        ret = ERROR;
    } else {
        buffer_read_adv(rb, n);

        if(response_is_done(a->parser.response_state, 0) && !buffer_can_read_parsed(rb)) {
            if(queue_is_empty(queue)) {
                selector_set_interest(key->s, pop->client_fd, OP_READ);
                selector_set_interest(key->s, pop->origin_fd, OP_WRITE);
                ret = REQUEST;
            } else {
                selector_set_interest(key->s, pop->origin_fd, OP_READ);
                selector_set_interest(key->s, pop->client_fd, OP_WRITE);
                ret = RESPONSE;
            }
        } else {
            compute_read_interests(key->s, rb, OP_READ, pop->origin_fd);
            compute_write_interests(key->s, rb, OP_WRITE, pop->client_fd);
            ret = APPEND_CAPA;
        }
    }

    return ret;
}

////////////////////////////////////////////////////////////////////////////////

/** definición de handlers para cada estado */
static const struct state_definition proxy_states[] = {
        {
                .state            = RESOLVE_ADDRESS,
                .on_write_ready   = resolve_address_init,
                .on_block_ready   = resolve_address_done,
        },{
                .state            = CONNECTING,
                .on_write_ready   = connecting
        },{
                .state            = CAPA,
                .on_arrival       = capa_init,
                .on_read_ready    = capa_read,
                .on_write_ready   = capa_write,
        },{
                .state            = HELLO,
                .on_arrival       = hello_init,
                .on_read_ready    = hello_read,
                .on_write_ready   = hello_write,
        },{
                .state            = REQUEST,
                .on_arrival       = request_init,
                .on_departure     = request_read_close,
                .on_read_ready    = request_read,
                .on_write_ready   = request_write,
        },{
                .state            = RESPONSE,
                .on_arrival       = response_init,
                .on_write_ready   = response_write,
                .on_read_ready    = response_read,
        },{
                .state            = TRANSFORM,
                .on_arrival       = transform_init,
                .on_read_ready    = transform_read,
                .on_write_ready   = transform_write,
        },{
                .state            = APPEND_CAPA,
                .on_arrival       = append_capa_init,
                .on_read_ready    = append_capa_read,
                .on_write_ready   = append_capa_write,
        },{
                .state            = DONE,
        },{
                .state            = HANDLEABLE_ERROR,
                .on_arrival       = error_init,
                .on_write_ready   = error_write,
        },{
                .state            = ERROR,
        }
};

static const struct state_definition *
pop3_describe_states(){
    return proxy_states;
}

///////////////////////////////////////////////////////////////////////////////
// Handlers top level de la conexión pasiva.
// son los que emiten los eventos a la maquina de estados.
static void
pop3_done(struct selector_key* key);

static void
pop3_read(struct selector_key *key) {
    struct state_machine *stm   = &ATTACHMENT(key)->stm;
    const enum pop3_state st = (enum pop3_state) stm_handler_read(stm, key);

    if(ERROR == st || DONE == st) {
        pop3_done(key);
    }
}

static void
pop3_write(struct selector_key *key) {
    struct state_machine *stm   = &ATTACHMENT(key)->stm;
    const enum pop3_state st = (enum pop3_state) stm_handler_write(stm, key);

    if(ERROR == st || DONE == st) {
        pop3_done(key);
    }
}

static void
pop3_block(struct selector_key *key) {
    struct state_machine *stm   = &ATTACHMENT(key)->stm;
    const enum pop3_state st = (enum pop3_state) stm_handler_block(stm, key);

    if(ERROR == st || DONE == st) {
        pop3_done(key);
    }
}

static void
pop3_close(struct selector_key *key) {
    struct pop3 *pop3 = ATTACHMENT(key);
    pop3_destroy(pop3);
    if(pop3->client_fd == key->fd) {
      proxy_metrics->concurrent_connections--;
    }
}

static void
pop3_done(struct selector_key* key) {
    const int fds[] = {
            ATTACHMENT(key)->client_fd,
            ATTACHMENT(key)->origin_fd,
    };

    proxy_metrics->concurrent_connections--;

    for(unsigned i = 0; i < N(fds); i++) {
        if(fds[i] != -1) {
            if(SELECTOR_SUCCESS != selector_unregister_fd(key->s, fds[i])) {
                abort();
            }
            close(fds[i]);
        }
    }
}
