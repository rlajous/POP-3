//
// Created by francisco on 25/10/18.
//

#include <stdlib.h>
#include <sys/socket.h>
#include <stdint.h>
#include <string.h>
#include <netdb.h>
#include <pthread.h>
#include <stdio.h>
#include <errno.h>

#include "ServerSpcpNio.h"
#include "../utils/buffer.h"
#include "../utils/stm.h"
#include "../utils/selector.h"
#include "../utils/request_queue.h"
#include "../utils/metrics.h"
#include "../spcpParsers/spcpRequest.h"

#define N(x) (sizeof(x)/sizeof((x)[0]))

enum spcp_state {
    USER_READ,
    USER_WRITE,
    PASS_READ,
    PASS_WRITE,
    REQUEST_READ,
    REQUEST_WRITE,
    DONE,
    ERROR,
};

struct spcp {
    /** información del cliente */
    struct sockaddr_storage       client_addr;
    socklen_t                     client_addr_len;
    int                           client_fd;

    /** resolución de la dirección del origin server */
    struct addrinfo              *origin_resolution;
    /** maquinas de estados */
    struct state_machine          stm;

    /** Parser */
    struct request request;
    struct spcp_request_parser parser;
    /** El resumen de la respuesta a enviar */
    enum spcp_response_status status;

    /** buffers para ser usados read_buffer, write_buffer.*/
    uint8_t raw_buff_a[2048], raw_buff_b[2048];
    buffer read_buffer, write_buffer;

    /** cantidad de referencias a este objeto. si es uno se debe destruir */
    unsigned references;

    /** username del usuario que se esta tratando de logear*/
    char *username;

    /** siguiente en el pool */
    struct spcp *next;
};


static const unsigned  max_pool  = 50; // tamaño máximo
static unsigned        pool_size = 0;  // tamaño actual
static struct spcp * pool      = 0;  // pool propiamente dicho

static const struct state_definition *
spcp_describe_states(void);

/** crea un nuevo `struct spcp' */
static struct spcp *
spcp_new(int client_fd) {
    struct spcp *ret;

    if(pool == NULL) {
        ret = malloc(sizeof(*ret));
    } else {
        ret       = pool;
        pool      = pool->next;
        ret->next = 0;
    }
    if(ret == NULL) {
        goto finally;
    }
    memset(ret, 0x00, sizeof(*ret));

    ret->client_fd       = client_fd;
    ret->client_addr_len = sizeof(ret->client_addr);

    ret->stm    .initial   = USER_READ;
    ret->stm    .max_state = ERROR;
    ret->stm    .states    = spcp_describe_states();
    stm_init(&ret->stm);

    buffer_init(&ret->read_buffer,  N(ret->raw_buff_a), ret->raw_buff_a);
    buffer_init(&ret->write_buffer, N(ret->raw_buff_b), ret->raw_buff_b);

    ret->references = 1;
    finally:
    return ret;
}

/** realmente destruye */
static void
spcp_destroy_(struct spcp* s) {
    if(s->origin_resolution != NULL) {
        freeaddrinfo(s->origin_resolution);
        s->origin_resolution = 0;
    }
    free(s);
}

/**
 * destruye un  `struct spcp', tiene en cuenta las referencias
 * y el pool de objetos.
 */
static void
spcp_destroy(struct spcp *s) {
    if(s == NULL) {
        // nada para hacer
    } else if(s->references == 1) {
        if(s != NULL) {
            if(pool_size < max_pool) {
                s->next = pool;
                pool    = s;
                pool_size++;
            } else {
                spcp_destroy_(s);
            }
        }
    } else {
        s->references -= 1;
    }
}

void
spcp_pool_destroy(void) {
    struct spcp *next, *s;
    for(s = pool; s != NULL ; s = next) {
        next = s->next;
        free(s);
    }
}

/** obtiene el struct (spcp *) desde la llave de selección  */
#define ATTACHMENT(key) ( (struct spcp *)(key)->data)


/* declaración forward de los handlers de selección de una conexión
 * establecida entre un cliente y el proxy.
 */
static void spcp_read   (struct selector_key *key);
static void spcp_write  (struct selector_key *key);
static void spcp_block  (struct selector_key *key);
static void spcp_close  (struct selector_key *key);
static const struct fd_handler spcp_handler = {
        .handle_read   = spcp_read,
        .handle_write  = spcp_write,
        .handle_close  = spcp_close,
        .handle_block  = spcp_block,
};

/** Intenta aceptar la nueva conexión entrante*/
void
spcp_passive_accept(struct selector_key *key) {
    struct sockaddr_storage       client_addr;
    socklen_t                     client_addr_len = sizeof(client_addr);
    struct spcp                *state           = NULL;

    const int client = accept(key->fd, (struct sockaddr*) &client_addr,
                              &client_addr_len);
    if(client == -1) {
        goto fail;
    }
    if(selector_fd_set_nio(client) == -1) {
        goto fail;
    }
    state = spcp_new(client);
    if(state == NULL) {
        // sin un estado, nos es imposible manejaro.
        // tal vez deberiamos apagar accept() hasta que detectemos
        // que se liberó alguna conexión.
        goto fail;
    }
    memcpy(&state->client_addr, &client_addr, client_addr_len);
    state->client_addr_len = client_addr_len;

    if(SELECTOR_SUCCESS != selector_register(key->s, client, &spcp_handler,
                                             OP_READ, state)) {
        goto fail;
    }
    return ;
    fail:
    if(client != -1) {
        close(client);
    }
    spcp_destroy_(state);
}


/** definición de handlers para cada estado */
static const struct state_definition client_statbl[] = {
        {
                .state            = USER_READ,
        },{
                .state            = USER_WRITE,
        },{
                .state            = PASS_READ,
        },{
                .state            = PASS_WRITE,
        },{
                .state            = REQUEST_READ,
        },{
                .state            = REQUEST_WRITE,
        },{
                .state            = DONE,
        },{
                .state            = ERROR,
        }
};
static const struct state_definition *
spcp_describe_states(void) {
    return client_statbl;
}

///////////////////////////////////////////////////////////////////////////////
// Handlers top level de la conexión pasiva.
// son los que emiten los eventos a la maquina de estados.
static void
spcp_done(struct selector_key* key);

static void
spcp_read(struct selector_key *key) {
    struct state_machine *stm   = &ATTACHMENT(key)->stm;
    const enum  spcp_state st = stm_handler_read(stm, key);

    if(ERROR == st || DONE == st) {
        spcp_done(key);
    }
}

static void
spcp_write(struct selector_key *key) {
    struct state_machine *stm   = &ATTACHMENT(key)->stm;
    const enum spcp_state st = stm_handler_write(stm, key);

    if(ERROR == st || DONE == st) {
        spcp_done(key);
    }
}

static void
spcp_block(struct selector_key *key) {
    struct state_machine *stm   = &ATTACHMENT(key)->stm;
    const enum spcp_state st = stm_handler_block(stm, key);

    if(ERROR == st || DONE == st) {
        spcp_done(key);
    }
}

static void
spcp_close(struct selector_key *key) {
    spcp_destroy(ATTACHMENT(key));
}

static void
spcp_done(struct selector_key* key) {
    const int fd = ATTACHMENT(key)->client_fd;
    if(fd != -1) {
        if(SELECTOR_SUCCESS != selector_unregister_fd(key->s, fd)) {
            abort();
        }
        close(fd);
    }
}