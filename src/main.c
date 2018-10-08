
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <netinet/in.h>
#include <signal.h>

#include "selector.h"
#include "proxyPop3nio.h"

static bool done = false;

static void
sigterm_handler(const int signal){
    printf("signal %d, cleaning up and exiting\n", signal);
    done = true;
}

main(const int argc, const char **argv){
    unsigned port = 1080;

    if(argc == 1){

    } else if(argc == 2){
        char *end = 0;
        const long sl = strtol(argv[1], &end, 10);
        if (end == argv[1]|| '\0' != *end
            || ((LONG_MIN == sl || LONG_MAX == sl) && ERANGE == errno)
            || sl < 0 || sl > USHRT_MAX) {
            fprintf(stderr, "port should be an integer: %s\n", argv[1]);
            return 1;
        }
        port = sl;
    } else {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return 1;
    }

    close(0);

    const char *err_msg = NULL;
    selector_status ss = SELECTOR_SUCCESS;
    fd_selector selector = NULL;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(port);

    const int server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(server < 0){
        err_msg = "unable to create socket";
        goto finally;
    }
    fprintf(stdout, "Listening on TCP port %d\n", port);

    setsockopt(server, SOL_SOCKET,SO_REUSEADDR,&(int){1}, sizeof(int));

    if(bind(server, (struct sockaddr*)&addr, sizeof(addr)) < 0){
        err_msg = "Unable to bind socket";
        goto finally;
    }
    if(listen(server, 20) < 0){
        err_msg = "Unable to listen";
        goto finally;
    }

    signal(SIGTERM, sigterm_handler);
    signal(SIGINT, sigterm_handler);

    if(selector_fd_set_nio(server) == -1){
        err_msg = "getting server socket flags";
        goto finally;
    }

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
    const struct fd_handler proxyPop3 = {
            .handle_read    = proxyPop3_passive_accept,
            .handle_write   = NULL,
            .handle_close   = NULL,
    };
    ss = selector_register(selector, server, &proxyPop3, OP_READ, NULL);

    if(ss != SELECTOR_SUCCESS){
        err_msg = "registering fd";
        goto finally;
    }
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

    proxyPop3_pool_destoy();

    if(server >= 0) {
        close(server);
    }
    return ret;



}