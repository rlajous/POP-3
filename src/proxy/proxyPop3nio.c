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

#define N(x) (sizeof(x)/sizeof((x)[0]))

extern arguments proxyArguments;

enum pop3_state {
    RESOLVE_ADDRESS,
    CONNECTING,
    HELLO,
    REQUEST_READ,
    REQUEST_WRITE,
    RESPONSE_READ,
    RESPONSE_WRITE,
    COMMIT,
    QUIT,
    DONE,
    ERROR,
};

struct hello_st {
    buffer              *wb;
};

struct request_st {
    buffer                  *origin_buffer, *client_buffer;

    struct request          request;
    struct request_parser   parser;

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
    union {
        struct hello_st hello;
        struct request_st request;
    } client;
    /** estados para el origin_fd */
    union {
        //struct hello_st hello;
    } orig;

    /** buffers para ser usados read_buffer, write_buffer.*/
    //TODO: Aca se deberia modificar el tamaño del buffer en tiempo de ejecución creo
    uint8_t raw_buff_a[2048], raw_buff_b[2048];
    buffer read_buffer, write_buffer;

    /** cantidad de referencias a este objeto. si es uno se debe destruir */
    unsigned references;

    /** siguiente en el pool */
    struct pop3 *next;

};

/**
 * Pool de `struct socks5', para ser reusados.
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
                ret = REQUEST_READ;
            } else {
                ret = ERROR;
            }
        }
    }

    return ret;
}

////////////////////////////////////////////////////////////////////////////////
// REQUEST
////////////////////////////////////////////////////////////////////////////////

/** inicializa las variables de los estados del REQUEST */
static void
request_init(const unsigned state, struct selector_key *key) {
    struct pop3     *p =  ATTACHMENT(key);
    struct request_st *d = &p->client.request;

    d->origin_buffer              = &(p->write_buffer);
    d->client_buffer              = &(p->read_buffer);
    d->parser.request  = &d->request;

}

static unsigned
request_process_error(struct selector_key *key, struct request_st *d);

static unsigned
request_read(struct selector_key *key) {
    struct request_st * d = &ATTACHMENT(key)->client.request;

    buffer *b     = d->client_buffer;
    unsigned  ret   = REQUEST_READ;
    bool  error = false;
    uint8_t *ptr;
    size_t  count;
    ssize_t  n;

    ptr = buffer_write_ptr(b, &count);
    n = recv(key->fd, ptr, count, 0);
    if(n > 0) {
        int st = request_consume(b, &d->parser, &error);
        if(request_is_done(st, 0)) {
            if(error){
                buffer_write_adv(b, n);
                ret = request_process_error(key, d);
            } else {
                ret = REQUEST_WRITE;
            }
        } else {
            ret = ERROR;
        }
        return error ? ERROR : ret;
    }

}
static unsigned
request_process_error(struct selector_key *key, struct request_st *d) {

    enum request_state st = d->parser.state;
    //Escribir en el buffer que usa el fd de Response write una response apropiada al error.
    return RESPONSE_WRITE;
}

static void
request_read_close(const unsigned state, struct selector_key *key) {
    struct request_st * d = &ATTACHMENT(key)->client.request;

    request_close(&d->parser);
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
                .state            = REQUEST_READ,
                .on_arrival       = request_init,
                .on_departure     = request_read_close,
                .on_read_ready    = request_read,
        },{
                .state            = REQUEST_WRITE
        },{
                .state            = RESPONSE_READ
        },{
                .state            = RESPONSE_WRITE
        },{
                .state            = COMMIT
        },{
                .state            = QUIT
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
    for(unsigned i = 0; i < N(fds); i++) {
        if(fds[i] != -1) {
            if(SELECTOR_SUCCESS != selector_unregister_fd(key->s, fds[i])) {
                abort();
            }
            close(fds[i]);
        }
    }
}
