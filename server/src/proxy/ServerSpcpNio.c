#include <stdlib.h>
#include <sys/socket.h>
#include <stdint.h>
#include <string.h>
#include <netdb.h>
#include <pthread.h>
#include <stdio.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/sctp.h>
#include <arpa/inet.h>


#include "ServerSpcpNio.h"
#include "../utils/buffer.h"
#include "../utils/stm.h"
#include "../utils/selector.h"
#include "../utils/request_queue.h"
#include "../utils/metrics.h"
#include "../spcpParsers/spcpRequest.h"
#include "spcpServerCredentials.h"
#include "../utils/proxyArguments.h"

#define N(x) (sizeof(x)/sizeof((x)[0]))

extern metrics  *proxy_metrics;
extern arguments proxyArguments;

struct sctp_sndrcvinfo sndrcvinfo;
int sctp_flags;

extern int BUFFER_SIZE;

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
    if(s->username != NULL) {
        free(s->username);
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


static fd_interest
spcp_compute_interests(fd_selector s, struct selector_key *key) {
    struct spcp *spcp = ATTACHMENT(key);

    fd_interest ret = OP_NOOP;
    if(buffer_can_write(&spcp->read_buffer)) {
        ret |= OP_READ;
    }
    if(buffer_can_read(&spcp->write_buffer)) {
        ret |= OP_WRITE;
    }
    if(SELECTOR_SUCCESS != selector_set_interest(s, spcp->client_fd, ret)) {
        abort();
    }
    return ret;
}

////////////////////////////////////////////////////////////////////////////////
///                                   USER                                   ///
////////////////////////////////////////////////////////////////////////////////


static void
user_read_init(const unsigned state, struct selector_key *key) {
    struct spcp *spcp = ATTACHMENT(key);
    spcp_request_parser_init(&spcp->parser);
}

static unsigned
user_process(struct selector_key *key) {
    struct spcp *spcp = ATTACHMENT(key);
    unsigned ret = USER_WRITE;

    struct spcp_request *request = &spcp->parser.request;
    if(spcp->username == NULL)
        spcp->username = malloc(spcp->parser.request.arg0_size + 1);
    else
        spcp->username = realloc(spcp->username, spcp->parser.request.arg0_size +1);
    if(spcp->username == NULL){
        spcp->status = spcp_err;
    }

    memcpy(spcp->username, spcp->parser.request.arg0, spcp->parser.request.arg0_size);
    spcp->username[spcp->parser.request.arg0_size] = '\0';

    if(user_present(spcp->username)) {
        spcp->status = spcp_success;
        if (-1 == spcp_no_data_request_marshall(&spcp->write_buffer, 0x00)) {
            ret = ERROR;
        }
    } else {
        spcp->status = spcp_auth_err;
        if (-1 == spcp_no_data_request_marshall(&spcp->write_buffer, 0x01)) {
            ret = ERROR;
        }
    }
    if(SELECTOR_SUCCESS != selector_set_interest_key(key, OP_WRITE)) {
        return ERROR;
    }
    return ret;
}

/** lee todos los bytes del mensaje de tipo `request' y inicia su proceso */
static unsigned
user_read(struct selector_key *key) {
    struct spcp *spcp = ATTACHMENT(key);

    buffer *b     = &spcp->read_buffer;
    unsigned  ret   = USER_READ;
    bool  error = false;
    uint8_t *ptr;
    size_t  count;
    ssize_t  n;

    ptr = buffer_write_ptr(b, &count);
    n = sctp_recvmsg(key->fd, ptr, count, (struct sockaddr *) NULL, 0, &sndrcvinfo, &sctp_flags);
    if(n > 0) {
        buffer_write_adv(b, n);
        int st = spcp_request_consume(b, &spcp->parser, &error);
        if(spcp_request_is_done(st, 0)) {
            ret = user_process(key);
        }
    } else {
        ret = ERROR;
    }

    return error ? ERROR : ret;
}

static unsigned
user_write(struct selector_key *key) {
    struct spcp *spcp = ATTACHMENT(key);

    unsigned  ret     = USER_WRITE;
    struct buffer *wb = &spcp->write_buffer;
    uint8_t *ptr;
    size_t  count;
    ssize_t  n;

    ptr = buffer_read_ptr(wb, &count);
    n = sctp_sendmsg(key->fd, ptr, count, 0, 0, 0, 0, 0, 0, 0 );
    if(n == -1) {
        ret = ERROR;
    } else {
        buffer_read_adv(wb, n);
        if(!buffer_can_read(wb)) {
            if(SELECTOR_SUCCESS == selector_set_interest_key(key, OP_READ)) {
                if(spcp->status == spcp_success){
                    ret = PASS_READ;
                }
                else if(spcp->status == spcp_auth_err){
                    ret = USER_READ;
                } else {
                    ret = ERROR;
                }
            } else {
                ret = ERROR;
            }
        }
    }

    return ret;
}
////////////////////////////////////////////////////////////////////////////////
///                                   PASS                                   ///
////////////////////////////////////////////////////////////////////////////////

static void
pass_read_init(const unsigned state, struct selector_key *key) {
    struct spcp *spcp = ATTACHMENT(key);
    spcp_request_parser_init(&spcp->parser);
}


static unsigned
pass_process(struct selector_key *key) {
    struct spcp *spcp = ATTACHMENT(key);
    unsigned ret = PASS_WRITE;

    char pass[spcp->parser.request.arg0_size + 1];
    memcpy(pass, spcp->parser.request.arg0, spcp->parser.request.arg0_size);
    pass[spcp->parser.request.arg0_size] = '\0';

    if(validate_user(spcp->username, pass)) {
        spcp->status = spcp_success;
        if (-1 == spcp_no_data_request_marshall(&spcp->write_buffer, 0x00)) {
            ret = ERROR;
        }
    } else {
        spcp->status = spcp_auth_err;
        if (-1 == spcp_no_data_request_marshall(&spcp->write_buffer, 0x01)) {
            ret = ERROR;
        }
    }
    if(SELECTOR_SUCCESS != selector_set_interest_key(key, OP_WRITE)) {
        return ERROR;
    }
    return ret;
}

static unsigned
pass_read(struct selector_key *key) {
    struct spcp *spcp = ATTACHMENT(key);

    buffer *b     = &spcp->read_buffer;
    unsigned  ret   = USER_READ;
    bool  error = false;
    uint8_t *ptr;
    size_t  count;
    ssize_t  n;

    ptr = buffer_write_ptr(b, &count);
    n = sctp_recvmsg(key->fd, ptr, count, (struct sockaddr *) NULL, 0, &sndrcvinfo, &sctp_flags);
    if(n > 0) {
        buffer_write_adv(b, n);
        int st = spcp_request_consume(b, &spcp->parser, &error);
        if(spcp_request_is_done(st, 0)) {
            ret = pass_process(key);
        }
    } else {
        ret = ERROR;
    }

    return error ? ERROR : ret;
}


static unsigned
pass_write(struct selector_key *key) {
    struct spcp *spcp = ATTACHMENT(key);

    unsigned  ret     = PASS_WRITE;
    struct buffer *wb = &spcp->write_buffer;
    uint8_t *ptr;
    size_t  count;
    ssize_t  n;

    ptr = buffer_read_ptr(wb, &count);
    n = sctp_sendmsg(key->fd, ptr, count, 0, 0, 0, 0, 0, 0, 0 );
    if(n == -1) {
        ret = ERROR;
    } else {
        buffer_read_adv(wb, n);
        if(!buffer_can_read(wb)) {
            if(SELECTOR_SUCCESS == selector_set_interest_key(key, OP_READ)) {
                if(spcp->status == spcp_success){
                    ret = REQUEST_READ;
                }
                else if(spcp->status == spcp_auth_err){
                    ret = USER_READ;
                } else {
                    ret = ERROR;
                }
            } else {
                ret = ERROR;
            }
        }
    }

    return ret;
}


////////////////////////////////////////////////////////////////////////////////
///                                 REQUEST                                  ///
////////////////////////////////////////////////////////////////////////////////

static void
spcp_request_init(const unsigned state, struct selector_key *key) {
    struct spcp *spcp = ATTACHMENT(key);
    spcp_request_parser_init(&spcp->parser);
}

static unsigned
get_concurrent_connections(struct buffer *b,  enum spcp_response_status *status) {
    unsigned data = proxy_metrics->concurrent_connections;

    int digits = 0;
    unsigned long long aux = data;
    while(aux != 0) {
        aux /= 10;
        ++digits;
    }
    /// Add one for the null termination
    char serialized_data[digits + 1];
    sprintf(serialized_data, "%d", data);

    *status = spcp_success;
    if( -1 == spcp_data_request_marshall(b, 0x00, serialized_data, strlen(serialized_data))) {
        return ERROR;
    }
        return REQUEST_WRITE;
}

static unsigned
get_transfered_bytes(struct buffer *b,  enum spcp_response_status *status) {
    unsigned long long data = proxy_metrics->bytes;

    int digits = 0;
    unsigned long long aux = data;
    while(aux != 0) {
        aux /= 10;
        ++digits;
    }
    /// Add one for the null termination
    char serialized_data[digits + 1];
    sprintf(serialized_data, "%llu", data);

    *status = spcp_success;
    if( -1 == spcp_data_request_marshall(b, 0x00, serialized_data, strlen(serialized_data))) {
        return ERROR;
    }
    return REQUEST_WRITE;
}

static unsigned
get_historical_accesses(struct buffer *b, enum spcp_response_status *status) {
    unsigned long data = proxy_metrics->historic_connections;

    int digits = 0;
    unsigned long long aux = data;
    while(aux != 0) {
        aux /= 10;
        ++digits;
    }
    /// Add one for the null termination
    char serialized_data[digits + 1];
    sprintf(serialized_data, "%lu", data);

    *status = spcp_success;
    if( -1 == spcp_data_request_marshall(b, 0x00, serialized_data, strlen(serialized_data))) {
        return ERROR;
    }
    return REQUEST_WRITE;
}

static unsigned
get_active_transformation(struct buffer *b, enum spcp_response_status *status) {

    if(proxyArguments->command != NULL) {
        if( -1 == spcp_data_request_marshall(b, 0x00, proxyArguments->command, strlen(proxyArguments->command))) {
            return ERROR;
        }
    } else {
        if( -1 == spcp_data_request_marshall(b, 0x00, "", 1)) {
            return ERROR;
        }
    }

    *status = spcp_success;
    return REQUEST_WRITE;
}

static unsigned
set_buffer_size(struct buffer *b, struct spcp_request *request, enum spcp_response_status *status) {

    if(request->arg0_size < 2){
        *status = spcp_invalid_arguments;
        if( -1 == spcp_no_data_request_marshall(b, spcp_invalid_arguments)) {
            return ERROR;
        }
    }
    char serialized_number[request->arg0_size + 1];
    memcpy(serialized_number, request->arg0, request->arg0_size);
    serialized_number[request->arg0_size] = '\0';
    long new_size = strtol(request->arg0, NULL, 10);

    BUFFER_SIZE = new_size;
    *status = spcp_success;

    if( -1 == spcp_no_data_request_marshall(b, spcp_success)) {
        return ERROR;
    }
    return REQUEST_WRITE;
}

static unsigned
set_transformation(struct buffer *b, struct spcp_request *request, enum spcp_response_status *status) {
    *status = spcp_success;

    char new_command[request->arg0_size + 1];
    memcpy(new_command, request->arg0, request->arg0_size);
    new_command[request->arg0_size] = '\0';

    if(strcmp(new_command, "") == 0){
        proxyArguments->command = NULL;
    } else {
        proxyArguments->command = modify_string(proxyArguments->command, new_command);
    }

    if( -1 == spcp_no_data_request_marshall(b, spcp_success)) {
        return ERROR;
    }
    return REQUEST_WRITE;
}

static unsigned
do_quit(struct buffer *b, enum spcp_response_status *status) {

    *status = spcp_success;
    if(-1 == spcp_no_data_request_marshall(b, spcp_success)) {
        return ERROR;
    }
    return DONE;
}

static unsigned
spcp_request_process(struct selector_key *key) {
    struct spcp *spcp = ATTACHMENT(key);
    struct spcp_request *request = &spcp->parser.request;
    unsigned ret;

    if(request->cmd <= spcp_pass){
        spcp->status = spcp_invalid_command;
        if(-1 == spcp_no_data_request_marshall(&spcp->write_buffer, spcp_invalid_command)) {
            return ERROR;
        }
    }

    switch(request->cmd){
        case spcp_concurrent_connections:
            ret = get_concurrent_connections(&spcp->write_buffer, &spcp->status);
            break;
        case spcp_transfered_bytes:
            ret = get_transfered_bytes(&spcp->write_buffer, &spcp->status);
            break;
        case spcp_historical_accesses:
            ret = get_historical_accesses(&spcp->write_buffer, &spcp->status);
            break;
        case spcp_active_transformation:
            ret = get_active_transformation(&spcp->write_buffer, &spcp->status);
            break;
        case spcp_set_buffer_size:
            ret = set_buffer_size(&spcp->write_buffer, request, &spcp->status);
            break;
        case spcp_change_transformation:
            ret = set_transformation(&spcp->write_buffer, request, &spcp->status);
            break;
        case spcp_quit:
            ret = DONE;
            break;
        default:
            spcp->status = spcp_invalid_command;
            ret = ERROR;
    }
    if(ret == ERROR){
        spcp->status = spcp_err;
    }
    if(SELECTOR_SUCCESS != selector_set_interest_key(key, OP_WRITE)) {
        return ERROR;
    }
    return ret;
}


static unsigned
spcp_request_read(struct selector_key *key) {
    struct spcp *spcp = ATTACHMENT(key);

    buffer *b     = &spcp->read_buffer;
    unsigned  ret   = REQUEST_READ;
    bool  error = false;
    uint8_t *ptr;
    size_t  count;
    ssize_t  n;

    ptr = buffer_write_ptr(b, &count);
    n = sctp_recvmsg(key->fd, ptr, count, (struct sockaddr *) NULL, 0, &sndrcvinfo, &sctp_flags);
    if(n > 0) {
        buffer_write_adv(b, n);
        int st = spcp_request_consume(b, &spcp->parser, &error);
        if(spcp_request_is_done(st, 0)) {
            ret = spcp_request_process(key);
        }
    } else {
        ret = ERROR;
    }
    return error ? ERROR : ret;
}


static unsigned
spcp_request_write(struct selector_key *key) {
    struct spcp *spcp = ATTACHMENT(key);

    unsigned  ret     = REQUEST_WRITE;
    struct buffer *wb = &spcp->write_buffer;
    uint8_t *ptr;
    size_t  count;
    ssize_t  n;

    ptr = buffer_read_ptr(wb, &count);
    n = sctp_sendmsg(key->fd, ptr, count, 0, 0, 0, 0, 0, 0, 0);
    if(n == -1) {
        ret = ERROR;
    } else {
        buffer_read_adv(wb, n);
        if(!buffer_can_read(wb)) {
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


static unsigned
done_write(struct selector_key *key) {
    struct spcp *spcp = ATTACHMENT(key);
    unsigned  ret     = DONE;
    struct buffer *wb = &spcp->write_buffer;
    uint8_t *ptr;
    size_t  count;
    ssize_t  n;

    ptr = buffer_read_ptr(wb, &count);
    n = sctp_sendmsg(key->fd, ptr, count, 0, 0, 0, 0, 0, 0, 0);
    if(n == -1) {
        ret = ERROR;
    } else {
        buffer_read_adv(wb, n);
        if(!buffer_can_read(wb)) {
            if(SELECTOR_SUCCESS == selector_set_interest_key(key, OP_READ)) {
                ret = DONE;
            } else {
                ret = ERROR;
            }
        }
    }
    return ret;

}

static void
parser_close(const unsigned state, struct selector_key *key) {
    struct spcp * spcp = ATTACHMENT(key);
    spcp_request_close(&spcp->parser);
}


/** definición de handlers para cada estado */
static const struct state_definition client_statbl[] = {
        {
                .state            = USER_READ,
                .on_arrival       = user_read_init,
                .on_read_ready    = user_read,
                .on_departure     = parser_close,
        },{
                .state            = USER_WRITE,
                .on_write_ready   = user_write,
                .on_departure     = parser_close,
        },{
                .state            = PASS_READ,
                .on_arrival       = pass_read_init,
                .on_read_ready    = pass_read,
                .on_departure     = parser_close,
        },{
                .state            = PASS_WRITE,
                .on_write_ready   = pass_write,
                .on_departure     = parser_close,
        },{
                .state            = REQUEST_READ,
                .on_arrival       = spcp_request_init,
                .on_read_ready    = spcp_request_read,
                .on_departure     = parser_close,

        },{
                .state            = REQUEST_WRITE,
                .on_write_ready   = spcp_request_write,
        },{
                .state            = DONE,
                .on_read_ready    = done_write,
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