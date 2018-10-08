

#include <stdlib.h>
#include <bits/socket.h>
#include <sys/socket.h>
#include <stdint.h>

#include "buffer.h"


struct hello_st {
    buffer              *rb, *wb;
    //struct hello_parser parser;
};

struct auth_st {
    buffer                      *rb, *wb;

    struct request              request;
    struct authorization_parser parser;

    enum pop_response_status    status;

    struct sockaddr_storage   *origin_addr;
    socklen_t                 *origin_addr_len;
    int                       *origin_domain;

    const int                 *client_fd;
    int                       *origin_fd;
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
        struct auth_st auth;
        struct transaction_st transaction;
        struct update_st update;
    } client;
    /** estados para el origin_fd */
    union {
        struct hello_st hello;
        struct auth_st auth;
        struct transaction_st transaction;
        struct update_st update;
    } orig;

    /** buffers para ser usados read_buffer, write_buffer.*/
    uint8_t raw_buff_a[2048], raw_buff_b[2048];
    buffer read_buffer, write_buffer;

    /** cantidad de referencias a este objeto. si es uno se debe destruir */
    unsigned references;

    /** siguiente en el pool */
    struct pop3 *next;

};

void
proxyPop3_passive_accept(struct selector_key *key){
    struct sockaddr_storage         client_addr;
    socklen_t                       client_addr_len = sizeof(client_addr);
    struct pop3                     *state          = NULL;

    const int client = accept(key->fd, (struct sockaddr*) &client_addr,
                              &client_addr_len);
}
