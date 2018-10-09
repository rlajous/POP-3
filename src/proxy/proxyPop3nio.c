

#include <stdlib.h>
#include <bits/socket.h>
#include <sys/socket.h>
#include <stdint.h>
#include <string.h>
#include <netdb.h>

#include "../utils/buffer.h"
#include "../utils/stm.h"
#include "../utils/selector.h"

#define N(x) (sizeof(x)/sizeof((x)[0]))

enum pop3_state {
    /**
     * Recibe el hello del cliente y lo procesa
     *
     * Intereses:
     *      - OP_READ sobre el client_fd
     *
     * Transiciones:
     *      - HELLO_READ     mientras el mensaje no esté completo
     *      - HELLO_WRITE    cuando está completo
     *      - ERROR          abte cuyalquier error (IO/parseo)
     * */
    HELLO_READ,

    /**
     * envía la respuesta del `hello' al cliente.
     *
     * Intereses:
     *      - OP_WRITE sobre client_fd
     *
     * Transiciones:
     *      - HELLO_WRITE  mientras queden bytes por enviar
     *      - REQUEST_READ cuando se enviaron todos los bytes
     *      - ERROR        ante cualquier error (IO/parseo)
     */
    HELLO_WRITE,


    ERROR

};



struct hello_st {
    buffer              *rb, *wb;
    //struct hello_parser parser;
};

struct auth_st {
//    buffer                      *rb, *wb;
//
//    struct request              request;
//    struct authorization_parser parser;
//
//    enum pop_response_status    status;
//
//    struct sockaddr_storage   *origin_addr;
//    socklen_t                 *origin_addr_len;
//    int                       *origin_domain;
//
//    const int                 *client_fd;
//    int                       *origin_fd;
};

struct transaction_st {

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
//        struct auth_st auth;
//        struct transaction_st transaction;
//        struct update_st update;
    } client;
    /** estados para el origin_fd */
    union {
        struct hello_st hello;
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

/**
 *
 * */
static struct pop3 * pop3_new(int client_fd){
    struct pop3 * ret;

    if(pool == NULL){
        ret = malloc(sizeof(*ret));
    } else {
        ret = pool;
        pool = pool->next;
        ret->next = 0;
    }

    if(ret == NULL){
        goto finally;
    }
    memset(ret, 0x00, sizeof(*ret));

    ret->origin_fd          = -1;
    ret->client_fd          = client_fd;
    ret->client_addr_len    = sizeof(ret->client_addr);

    ret->stm    .initial    = HELLO_READ;
    ret->stm    .max_state  = ERROR;
    ret->stm    .states     = pop3_describe_states();
    stm_init(&ret->stm);

    buffer_init(&ret->read_buffer,N(ret->raw_buff_a), ret->raw_buff_a);
    buffer_init(&ret->write_buffer, N(ret->raw_buff_b), ret->raw_buff_b);


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
    if(p == NULL){
        //nada que hacer
    } else if (p->references == 1){
        if(p != NULL){
            if(pool_size < max_pool){
                p->next = pool;
                pool = p;
                pool_size++;
            } else {
                pop3_destroy_(p);
            }
        }
    } else {
        p->references = -1;
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



void
proxyPop3_passive_accept(struct selector_key *key){
    struct sockaddr_storage         client_addr;
    socklen_t                       client_addr_len = sizeof(client_addr);
    struct pop3                     *state          = NULL;

    const int client = accept(key->fd, (struct sockaddr*) &client_addr,
                              &client_addr_len);
}


static const struct state_definition *
pop3_describe_states(){
    return NULL;
}