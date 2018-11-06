
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <netinet/in.h>
#include <signal.h>
#include <netdb.h>

#include "utils/selector.h"
#include "proxy/proxyPop3nio.h"
#include "utils/proxyArguments.h"
#include "utils/metrics.h"
#include "proxy/ServerSpcpNio.h"

static bool done = false;
arguments proxyArguments;
metrics  *proxy_metrics;

static void
sigterm_handler(const int signal){
    printf("signal %d, cleaning up and exiting\n", signal);
    done = true;
}

bool
resolve_address(char *address, uint16_t port, struct addrinfo ** addrinfo) {

  struct addrinfo addr = {
          .ai_family    = AF_UNSPEC,    /* Allow IPv4 or IPv6 */
          .ai_socktype  = SOCK_STREAM,
          .ai_flags     = AI_PASSIVE,   /* For wildcard IP address */
          .ai_protocol  = 0,            /* Any protocol */
          .ai_canonname = NULL,
          .ai_addr      = NULL,
          .ai_next      = NULL,
  };

  char buff[7];
  snprintf(buff, sizeof(buff), "%hu", port);
  if (0 != getaddrinfo(address, buff, &addr,
                       addrinfo)){
    return false;
  }
  return true;
}

int
main(const int argc, char * const *argv){
    proxyArguments = parse_arguments(argc, argv);
    proxy_metrics  = malloc(sizeof(metrics));
    memset(proxy_metrics, 0, sizeof(metrics));

    close(0);

    const char *err_msg = NULL;
    selector_status ss = SELECTOR_SUCCESS;
    fd_selector selector = NULL;

    signal(SIGTERM, sigterm_handler);
    signal(SIGINT, sigterm_handler);

    const struct selector_init conf = {
            .signal  = SIGALRM,
            .select_timeout = {
                    .tv_nsec = 0,
                    .tv_sec = 10,
            },
    };
    if(selector_init(&conf) != 0){
        err_msg = "initilizing selector";
        goto finally;
    }
    selector = selector_new(1024);
    if(selector == NULL) {
        err_msg = "unable to create selector";
        goto finally;
    }

    struct addrinfo *pop3_addr;
    if(resolve_address(proxyArguments->pop3_address,
                       proxyArguments->pop3_port, &pop3_addr) == false) {
      err_msg = "unable to resolve address";
      goto finally;
    }

    struct addrinfo *pop3_curr = pop3_addr;

    const struct fd_handler proxyPop3 = {
            .handle_read    = proxyPop3_passive_accept,
            .handle_write   = NULL,
            .handle_close   = NULL,
    };

    int pop3_server;

    do {
        pop3_server = socket(pop3_curr->ai_family, SOCK_STREAM, IPPROTO_TCP);
        if(pop3_server < 0){
            err_msg = "unable to create pop3 proxy socket";
            goto finally;
        }

        setsockopt(pop3_server, SOL_SOCKET,SO_REUSEADDR, &(int){1}, sizeof(int));

        if(bind(pop3_server, pop3_curr->ai_addr, pop3_curr->ai_addrlen) < 0){
            err_msg = "Unable to bind pop3 socket";
            goto finally;
        }

        if(listen(pop3_server, 20) < 0){
            err_msg = "Unable to listen at pop3 socket";
            goto finally;
        }

        if(selector_fd_set_nio(pop3_server) == -1){
            err_msg = "getting server socket flags";
            goto finally;
        }

        ss = selector_register(selector, pop3_server, &proxyPop3, OP_READ, NULL);

        if(ss != SELECTOR_SUCCESS){
            err_msg = "registering pop3 fd";
            goto finally;
        }

        pop3_curr = pop3_curr->ai_next;
    } while(pop3_curr != NULL);

    fprintf(stdout, "Pop3 proxy listening on TCP port %d\n", proxyArguments->pop3_port);
    freeaddrinfo(pop3_addr);


    /// SPCP


    struct addrinfo *spcp_addr;
    if(resolve_address(proxyArguments->spcp_address,
                       proxyArguments->spcp_port, &spcp_addr) == false) {
        err_msg = "unable to resolve spcp_server address";
        goto finally;
    }

    const struct fd_handler spcpServer = {
            .handle_read    = spcp_passive_accept,
            .handle_write   = NULL,
            .handle_close   = NULL,
    };

    struct addrinfo *spcp_curr = spcp_addr;
    int spcp_server;
    do {
        spcp_server = socket(spcp_curr->ai_family, SOCK_STREAM, IPPROTO_SCTP);
        if (spcp_server < 0) {
            err_msg = "unable to create spcp_server socket";
            goto finally;
        }

        setsockopt(spcp_server, SOL_SOCKET, SO_REUSEADDR, &(int) {1}, sizeof(int));

        if (bind(spcp_server, spcp_curr->ai_addr, spcp_curr->ai_addrlen) < 0) {
            err_msg = "Unable to bind scpcp socket";
            goto finally;
        }

        if (listen(spcp_server, 20) < 0) {
            err_msg = "Unable to listen at spcp socket";
            goto finally;
        }


        ss = selector_register(selector, spcp_server, &spcpServer, OP_READ, NULL);

        if (ss != SELECTOR_SUCCESS) {
            err_msg = "registering spcp fd";
            goto finally;
        }
        spcp_curr = spcp_curr->ai_next;
    } while(spcp_curr != NULL);

    fprintf(stdout, "spcp server listening on SCTP port %d\n", proxyArguments->spcp_port);
    freeaddrinfo(spcp_addr);

    for(;!done;){
        err_msg = NULL;
        ss = selector_select(selector);
        if(ss != SELECTOR_SUCCESS){
            err_msg = "serving";
            goto finally;
        }
    }
    if(err_msg == NULL){
        err_msg = "closing";
    }

    int ret = 0;

finally:
    if(ss != SELECTOR_SUCCESS) {
        fprintf(stderr, "%s: %s\n", (err_msg == NULL) ? "": err_msg,
                ss == SELECTOR_IO
                ? strerror(errno)
                : selector_error(ss));
        ret = 2;
    } else if(err_msg) {
        perror(err_msg);
        ret = 1;
    }
    if(selector != NULL) {
        selector_destroy(selector);
    }
    selector_close();

    pop3_pool_destroy();

    destroy_arguments(proxyArguments);

    free(proxy_metrics);

    if(pop3_server >= 0) {
        close(pop3_server);
    }
    return ret;



}