#ifndef MAX_DATAGRAM
#define MAX_DATAGRAM 132
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "common.h"
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/sctp.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

void printError(uint8_t error_code) {
    switch (error_code) {
        case 0x01:
            printf("Authentication error");
            break;
        case 0x02:
            printf("Invalid command");
            break;
        case 0x03:
            printf("Invalid arguments");
            break;
        case 0x04:
        default:
            printf("Error");
    }
}


int handleUser(char *user, int connSock) {
    uint8_t command = 0, nargs = 1;
    int ret;
    uint8_t datagram[MAX_DATAGRAM];
    uint8_t res[MAX_DATAGRAM];

    datagram[0] = command;
    datagram[1] = nargs;
    size_t length = strlen(user) - 1;
    datagram[2] = length;
    memcpy(datagram + 3, user, length);

    ret = sctp_sendmsg(connSock, (const void *) datagram, 3 + length,
                       NULL, 0, 0, 0, STREAM, 0, 0);

    ret = sctp_recvmsg(connSock, (void *) res, MAX_DATAGRAM_SIZE,
                       (struct sockaddr *) NULL, 0, 0, 0);
    if (res[0] == 0) {
        printf("user set");
    } else {
        printError(res[0]);
    }
    return 1;
}

int handlePassword(char *password, int connSock) {
    uint8_t command = 1, nargs = 1;
    int ret;
    uint8_t datagram[MAX_DATAGRAM];
    uint8_t res[MAX_DATAGRAM];

    datagram[0] = command;
    datagram[1] = nargs;
    size_t length = strlen(password) - 1;
    datagram[2] = length;
    datagram[3] = password;
    ret = sctp_sendmsg(connSock, (const void *) datagram, 3 + length / 2,
                       NULL, 0, 0, 0, STREAM, 0, 0);
    ret = sctp_recvmsg(connSock, (void *) res, MAX_DATAGRAM_SIZE,
                       (struct sockaddr *) NULL, 0, 0, 0);
    if (res[0] == 0) {
        printf("password succes");
    } else {
        printError(res[0]);
    }
}

int handleConcurrentConection(int connSock) {
    uint8_t command = 2, nargs = 0;
    int ret;
    uint8_t datagram[MAX_DATAGRAM];
    uint8_t res[MAX_DATAGRAM];
    datagram[0] = command;
    datagram[1] = nargs;
    ret = sctp_sendmsg(connSock, (const void *) datagram, 2,
                       NULL, 0, 0, 0, STREAM, 0, 0);
    ret = sctp_recvmsg(connSock, (void *) res, MAX_DATAGRAM_SIZE,
                       (struct sockaddr *) NULL, 0, 0, 0);
    if (res[0] == 0) {
        printf("Concurrent connections set");
    } else {
        printError(res[0]);
    }
    return 1;
}

int handleTransferedBytes(int connSock) {
    uint8_t command = 3, nargs = 0;
    int ret;
    uint8_t datagram[MAX_DATAGRAM];
    uint8_t res[MAX_DATAGRAM];
    datagram[0] = command;
    datagram[1] = nargs;
    ret = sctp_sendmsg(connSock, (const void *) datagram, 2,
                       NULL, 0, 0, 0, STREAM, 0, 0);
    ret = sctp_recvmsg(connSock, (void *) res, MAX_DATAGRAM_SIZE,
                       (struct sockaddr *) NULL, 0, 0, 0);
    if (res[0] == 0) {
        printf("Bytes transferred");
    } else {
        printError(res[0]);
    }
    return 1;
}

int handleHistoricAccess(int connSock) {
    uint8_t command = 4, nargs = 0;
    int ret;
    uint8_t datagram[MAX_DATAGRAM];
    uint8_t res[MAX_DATAGRAM];
    datagram[0] = command;
    datagram[1] = nargs;
    ret = sctp_sendmsg(connSock, (const void *) datagram, 2,
                       NULL, 0, 0, 0, STREAM, 0, 0);
    ret = sctp_recvmsg(connSock, (void *) res, MAX_DATAGRAM_SIZE,
                       (struct sockaddr *) NULL, 0, 0, 0);
    if (res[0] == 0) {
        printf("Historic acces");
    } else {
        printError(res[0]);
    }
    return 1;
}

int handleActiveTransformation(int connSock) {
    uint8_t command = 0x05, nargs = 0;
    int ret;
    uint8_t datagram[MAX_DATAGRAM];
    uint8_t res[MAX_DATAGRAM];
    datagram[0] = command;
    datagram[1] = nargs;
    ret = sctp_sendmsg(connSock, (const void *) datagram, 2,
                       NULL, 0, 0, 0, STREAM, 0, 0);
    ret = sctp_recvmsg(connSock, (void *) res, MAX_DATAGRAM_SIZE,
                       (struct sockaddr *) NULL, 0, 0, 0);
    if (res[0] == 0) {
        printf("Transformation activated");
    } else {
        printError(res[0]);
    }
    return 1;
}

int handleBufferSize(int connSock) {
    uint8_t arg_size;
    int exit = 1;
    uint8_t command = 0x06, nargs = 1;
    int ret;
    uint8_t datagram[MAX_DATAGRAM];
    uint8_t res[MAX_DATAGRAM];
    char buffer[256];
    datagram[0] = command;
    datagram[1] = nargs;

    while (exit) {
        printf(" Enter new buffer size (in decimal)\n");
        exit = fgets(buffer, 255, stdin) == NULL;
        arg_size = (uint8_t)(strlen(buffer) - 1);
        if (exit) {
            printf(" No characters read \n");
        } else {
            datagram[2] = arg_size;
            memcpy(datagram + 3, buffer, arg_size);
        }
    }
    ret = sctp_sendmsg(connSock, (const void *) datagram, 2,
                       NULL, 0, 0, 0, STREAM, 0, 0);
    ret = sctp_recvmsg(connSock, (void *) res, MAX_DATAGRAM_SIZE,
                       (struct sockaddr *) NULL, 0, 0, 0);
    if (res[0] == 0x00) {
        printf("Buffer size set");
    } else {
        printError(res[0]);
    }
    return 0;
}

int handleTransformationChange(int connSock) {
    int exit = 1;
    uint8_t command = 0x0, nargs = 1;
    int ret;
    uint8_t datagram[MAX_DATAGRAM];
    uint8_t res[MAX_DATAGRAM];
    datagram[0] = command;
    datagram[1] = nargs;
    char buffer[2];

    while (exit) {
        printf(" Enter new transformation \n");
        exit = fgets(buffer, 255, stdin) == NULL;
        if (exit) {
            printf(" No characters read \n");
        } else {
            //to do format data
            sscanf(buffer, "%s %s", datagram[2]);
        }
    }
    ret = sctp_sendmsg(connSock, (const void *) datagram, 2,
                       NULL, 0, 0, 0, STREAM, 0, 0);
    ret = sctp_recvmsg(connSock, (void *) res, MAX_DATAGRAM_SIZE,
                       (struct sockaddr *) NULL, 0, 0, 0);
    if (res[0] == 0) {
        printf("Transformation done");
    } else {
        printf("Error code = %s", res[0]);
    }
    return 0;
}

int handleTimeOut(int connSock) {
    int exit = 1;
    char command = 10, nargs = 2, ret;
    uint8_t datagram[MAX_DATAGRAM];
    uint8_t res[MAX_DATAGRAM];
    char buffer[2];
    datagram[0] = command;
    datagram[1] = nargs;
    while (exit) {
        printf(" Enter buffer size \n");
        exit = fgets(buffer, sizeof(buffer), stdin) == NULL;
        if (exit) {
            printf(" No characters read \n");
        } else {
            sscanf(buffer, "%s %s", datagram[2], datagram[3]);
            datagram[2] = atoi(datagram[2]);
            datagram[3] = atoi(datagram[3]);
        }
    }
    ret = sctp_sendmsg(connSock, (const void *) datagram, 2,
                       NULL, 0, 0, 0, STREAM, 0, 0);
    ret = sctp_recvmsg(connSock, (void *) res, MAX_DATAGRAM_SIZE,
                       (struct sockaddr *) NULL, 0, 0, 0);
    if (res[0] == 0) {
        printf("Timeout seted");
    } else {
        printf("Error code = %s", res[0]);
    }
    return 0;
}

int handleHelp() {
    printf("\nThese are the following commands: \n");
    printf("0 -> Help\n\n");
    printf("1 -> Concurrent Conection\n\n");
    printf("2 -> Transfered Byte\n\n");
    printf("3 -> History Acces\n\n");
    printf("4 -> Get Transformation\n");
    printf("5 -> Set buffer Size\n");
    printf("6 -> Set transformation\n");
    printf("7 -> Time Out\n");
    printf("8 -> Quit\n");
    return 0;
}

int handleQuit(int connSock) {
    char command = 6, nargs = 0, ret;
    uint8_t datagram[MAX_DATAGRAM];
    uint8_t res[MAX_DATAGRAM];
    datagram[0] = command;
    datagram[1] = nargs;
    ret = sctp_sendmsg(connSock, (const void *) datagram, 2,
                       NULL, 0, 0, 0, STREAM, 0, 0);
    ret = sctp_recvmsg(connSock, (void *) res, MAX_DATAGRAM_SIZE,
                       (struct sockaddr *) NULL, 0, 0, 0);
    if (res[0] == 0) {
        printf("BYE");
    } else {
        printf("Error code = %s", res[0]);
    }
    return 1;
}

void printDatagram(uint8_t *datagram, int size) {
    printf("Datagram: ");
    for (int i = 0; i < size; ++i)
        printf("[%d]%02x[%c]-", i, datagram[i], datagram[i]);
    printf("\n");
}