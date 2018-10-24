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
#include "../utils/request.h"
#include "../utils/request_queue.h"
#include "../utils/response.h"
#include "../utils/metrics.h"

#define N(x) (sizeof(x)/sizeof((x)[0]))

extern arguments proxyArguments;
extern metrics  *proxy_metrics;

enum pop3_state {
    RESOLVE_ADDRESS,
    CONNECTING,
    HELLO,
    REQUEST,
    RESPONSE,
    TRANSFORM,
    APPEND_CAPA,
    DONE,
    ERROR,
};

struct hello_st {
    buffer              *wb;
};

struct request_st {
    buffer                  *wb, *rb;
    struct request_parser   parser;
    int                     *fd;
    fd_interest             duplex;
    struct request_st       *other;
    struct request_queue    *request_queue;

};

struct response_st {
    buffer                  *wb, *rb;
    struct response_parser  parser;
    int                     *fd;
    fd_interest             duplex;
    struct response_st      *other;
    struct request_queue    *request_queue;

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

    /** maquinas de estados */
    struct state_machine          stm;

    /** estados para el client_fd */
    //TODO: Revisar request response union
    union {
        struct hello_st hello;
        struct request_st request;
        struct response_st response;
    } client;
    /** estados para el origin_fd */
    union {
        struct request_st request;
        struct response_st response;
    } origin;

    /** buffers para ser usados read_buffer, write_buffer.*/
    //TODO: Aca se deberia modificar el tamaño del buffer en tiempo de ejecución creo
    uint8_t raw_buff_a[2048], raw_buff_b[2048];
    buffer read_buffer, write_buffer;

    struct request_queue *request_queue;

    /** cantidad de referencias a este objeto. si es uno se debe destruir */
    unsigned references;

    /** Identifica si el servidor de origen soporta pipelining o no*/
    bool pipeliner;

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

    buffer_init(&ret->read_buffer,  N(ret->raw_buff_a), ret->raw_buff_a);
    buffer_init(&ret->write_buffer, N(ret->raw_buff_b), ret->raw_buff_b);
    ret->request_queue = malloc(sizeof(struct request_queue));
    queue_init(ret->request_queue);

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
    if(selector_fd_set_nio(client == -1)){
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
        //TODO: SEND ERROR
        return ERROR;
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
    //TODO: SEND CONNECTION ERROR
    return ERROR;
}

static unsigned
connecting(struct selector_key *key) {
    int error;
    socklen_t   len    = sizeof(error);
    struct pop3 *p     = ATTACHMENT(key);
    unsigned    ret;

    if (getsockopt(key->fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0) {
        //TODO: ERROR
        return ERROR;
    } else {
        if(error == 0) {
            p->origin_fd = key->fd;
        } else {
            selector_unregister_fd(key->s, key->fd);
            close(key->fd);
            if(next_origin_resolution(key) == true) {
                struct selector_key* k = malloc(sizeof(*key));
                if(k == NULL) {
                    ret = ERROR;
                } else { 
                    memcpy(k, key, sizeof(*k));
                    k->fd = p->client_fd;
                    ret   = resolve_connect(k);
                    free(k);
                }
                return ret;
            }
            //TODO: SEND ERROR MESSAGE
            return ERROR;
        }
    }

    selector_status s = SELECTOR_SUCCESS;

    s |= selector_set_interest    (key->s, p->client_fd, OP_NOOP);
    s |= selector_set_interest_key(key,                  OP_READ);
    return SELECTOR_SUCCESS == s ? HELLO : ERROR;
}

static fd_interest
compute_write_interests(fd_selector s, buffer * b, fd_interest duplex, int fd) {
    fd_interest ret = OP_NOOP;
    if ((duplex & OP_WRITE) && buffer_can_read (b)) {
        ret |= OP_WRITE;
    }
    if(SELECTOR_SUCCESS != selector_set_interest(s, fd, ret)) {
        abort();
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
        abort();
    }
    return ret;
}


////////////////////////////////////////////////////////////////////////////////
// HELLO
////////////////////////////////////////////////////////////////////////////////

static void
hello_init(const unsigned state, struct selector_key *key) {
    struct pop3     *p =  ATTACHMENT(key);
    struct hello_st *d = &p->client.hello;

    d->wb              = &(p->write_buffer);
}

static unsigned
hello_read(struct selector_key *key) {
    struct pop3 *p     =  ATTACHMENT(key);
    struct hello_st *d = &p->client.hello;
    unsigned  ret      = HELLO;
    uint8_t *ptr;
    size_t  count;
    ssize_t  n;

    ptr = buffer_write_ptr(d->wb, &count);
    n = recv(key->fd, ptr, count, 0);
    if(n > 0) {
        buffer_write_adv(d->wb, n);
        //TODO: We should parse response
        selector_status s = SELECTOR_SUCCESS;
        s |= selector_set_interest_key(key, OP_NOOP);
        s |= selector_set_interest    (key->s, p->client_fd, OP_WRITE);
        if(SELECTOR_SUCCESS != s) {
            ret = ERROR;
        }
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
    uint8_t *ptr;
    size_t  count;
    ssize_t  n;

    ptr = buffer_read_ptr(d->wb, &count);
    n = send(key->fd, ptr, count, MSG_NOSIGNAL);
    if(n == -1) {
        ret = ERROR;
    } else {
        buffer_read_adv(d->wb, n);
        if(!buffer_can_read(d->wb)) {
            if(SELECTOR_SUCCESS == selector_set_interest_key(key, OP_READ)) {
                ret = REQUEST;
            } else {
                ret = ERROR;
            }
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
    request_parser_init(&d->parser);

    d->fd        = &p->client_fd;
    d->rb        = &p->read_buffer;
    d->wb        = &p->write_buffer;
    d->duplex    = OP_READ | OP_WRITE;
    d->other     = &p->origin.request;
    d->request_queue = p->request_queue;

    d = &p->origin.request;
    d->fd       = &p->origin_fd;
    d->rb       = &p->read_buffer;
    d->wb       = &p->write_buffer;
    d->duplex   = OP_READ | OP_WRITE;
    d->other    = &p->client.request;
    d->request_queue = p->request_queue;

}

static bool
should_transform(struct request *request) {
    return false;
}

static bool
should_append_capa(struct request *request) {
    return false;
}

static enum pop3_state
determine_response_state(struct request_queue *q){
//    struct request *r = peek_request(q);
//
//    if(should_transform(r))
//        return TRANSFORM;
//    if(should_append_capa(r))
//        return APPEND_CAPA;

    return RESPONSE;
}

static unsigned
request_process(struct selector_key *key) {
    struct request_st * d     = &ATTACHMENT(key)->client.request;
    struct request_st *other  = d->other;

    buffer *rb      = d->rb;
    buffer *wb      = d->wb;
    unsigned  ret   = REQUEST;
    bool   error    = false;

    int st = request_consume(rb, wb, &d->parser, &error, d->request_queue);
    fd_interest client = compute_read_interests(key->s, d->rb, d->duplex, *d->fd);
    fd_interest origin = compute_write_interests(key->s, other->wb, other->duplex, *other->fd);

    if(client == OP_NOOP && origin == OP_NOOP) {
        compute_write_interests(key->s, d->rb, d->duplex, *d->fd);
        compute_read_interests(key->s, other->wb, other->duplex, *other->fd);
        ret = determine_response_state(d->request_queue);
    }

    return ret;
}

static unsigned
request_read(struct selector_key *key) {
    struct request_st * d     = &ATTACHMENT(key)->client.request;
    struct request_st *other  = d->other;

    buffer *rb      = d->rb;
    buffer *wb      = d->wb;
    unsigned  ret   = REQUEST;
    uint8_t *ptr;
    size_t  count;
    ssize_t  n;

    ptr = buffer_write_ptr(rb, &count);
    n = recv(key->fd, ptr, count, 0);
    if(n > 0) {
        buffer_write_adv(rb, n);
        ret = request_process(key);
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
    request_close(&d->parser);
}

static unsigned
request_write(struct selector_key *key) {
    struct request_st *d      = &ATTACHMENT(key)->origin.request;
    struct request_st *other  = d->other;

    unsigned ret = REQUEST;
    uint8_t *ptr;
    size_t  count;
    ssize_t  n;

    buffer *b = d->wb;
    ptr = buffer_read_ptr(b, &count);

    n = send(key->fd, ptr, count, MSG_NOSIGNAL);
    if(n == -1) {
        shutdown(*d->fd, SHUT_WR);
        d->duplex &= ~OP_WRITE;
        if(*d->other->fd != -1) {
            shutdown(*d->other->fd, SHUT_RD);
            d->other->duplex &= ~OP_READ;
        }
        //TODO: ESTO NO ES ERROR
        return ERROR;
    } else {
        buffer_read_adv(d->wb, n);
        if(buffer_can_read(d->rb)) {
            ret = request_process(key);
        }
        if(!buffer_can_read(d->rb) && !buffer_can_read(d->wb) && !queue_is_empty(d->request_queue)) {
            compute_read_interests(key->s, d->rb, d->duplex, *d->fd);
            compute_write_interests(key->s, other->wb, other->duplex, *other->fd);
            ret = determine_response_state(d->request_queue);
        }
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
    d->rb        = &p->read_buffer;
    d->wb        = &p->write_buffer;
    d->duplex    = OP_READ | OP_WRITE;
    d->other     = &p->origin.response;
    d->request_queue = p->request_queue;

    d = &p->origin.response;
    d->fd       = &p->origin_fd;
    d->rb       = &p->read_buffer;
    d->wb       = &p->write_buffer;
    d->duplex   = OP_READ | OP_WRITE;
    d->other    = &p->client.response;
    d->request_queue = p->request_queue;
    response_parser_init(&d->parser, pop_request(d->request_queue));

}

static unsigned
response_process(struct response_st *d, struct selector_key *key);

static unsigned
response_read(struct selector_key *key){
    struct response_st *d = &ATTACHMENT(key)->origin.response;
    struct response_st *other  = d->other;

    buffer *rb      = d->rb;
    buffer *wb      = d->wb;
    uint8_t *ptr;
    size_t  count;
    ssize_t  n;
    struct request_queue *q = d->request_queue;
    struct request *request;
    unsigned ret = RESPONSE;

    ptr = buffer_write_ptr(rb, &count);
    n = recv(key->fd, ptr, count, 0);
    if(n > 0) {
        buffer_write_adv(rb, n);
        while(buffer_can_read(rb)) {
            if(!buffer_can_write(wb)){
                break;
            }
            int st = response_consume(rb, wb, &d->parser, 0);
            if(response_is_done(st, 0)){
                response_close(&d->parser);
                if(!queue_is_empty(d->request_queue)) {
                    request = pop_request(d->request_queue);
                    response_parser_init(&d->parser, request);
                    ret = response_process(d, key);
                }
            } else {
                ret = RESPONSE;
            }
        }
        ret = response_process(d, key);
    //TODO: Checkear temas de shutdown
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

static unsigned
response_process(struct response_st *d, struct selector_key *key) {
    unsigned ret;
    struct response_st *other  = d->other;

    fd_interest origin = compute_read_interests(key->s, d->rb, d->duplex, *d->fd);
    fd_interest client = compute_write_interests(key->s, other->wb, other->duplex, *other->fd);

    if(client == OP_NOOP && origin == OP_NOOP) {
        compute_write_interests(key->s, d->wb, d->duplex, *d->fd);
        compute_read_interests(key->s, other->rb, other->duplex, *other->fd);
        ret = REQUEST;
    } else {
        ret = RESPONSE;
    }
    return ret;
}

static unsigned
response_write(struct selector_key *key){
    struct response_st *d      = &ATTACHMENT(key)->client.response;
    struct response_st *other  = d->other;


    unsigned ret    = REQUEST;
    uint8_t *ptr;
    size_t count;
    ssize_t n;

    buffer *b   = d->wb;
    ptr         = buffer_read_ptr(b, &count);

    n = send(key->fd, ptr, count, MSG_NOSIGNAL);
    if(n == -1) {
        ret = ERROR;
    } else {
        buffer_read_adv(d->wb, n);

        if(!buffer_can_read(d->wb)) {
            compute_read_interests(key->s, d->rb, d->duplex, *d->fd);
            compute_write_interests(key->s, other->wb, other->duplex, *other->fd);
            ret = REQUEST;
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
        },{
                .state            = APPEND_CAPA,
        },{
                .state            = DONE
        },{
                .state            = ERROR
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
    pop3_destroy(ATTACHMENT(key));
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
