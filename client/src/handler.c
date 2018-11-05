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
#include <stdbool.h>

void printError(uint8_t error_code) {
    switch (error_code) {
        case 0x01:
            printf("Authentication error \n");
            break;
        case 0x02:
            printf("Invalid command \n");
            break;
        case 0x03:
            printf("Invalid arguments \n");
            break;
        case 0x04:
        default:
            printf("Error \n");
    }
}


bool handleUser(char *user, int connSock) {
    uint8_t command = 0, nargs = 1;
    int ret;
    uint8_t datagram[MAX_DATAGRAM_SIZE];
    uint8_t res[MAX_DATAGRAM_SIZE];

    datagram[0] = command;
    datagram[1] = nargs;
    size_t length = strlen(user);
    datagram[2] = length;
    memcpy(datagram + 3, user, length);

    ret = sctp_sendmsg(connSock, (const void *) datagram, 3 + length,
                       NULL, 0, 0, 0, STREAM, 0, 0);

    ret = sctp_recvmsg(connSock, (void *) res, MAX_DATAGRAM_SIZE,
                       (struct sockaddr *) NULL, 0, 0, 0);
    if (res[0] != 0)  {
        printf("Invalid username \n");
        return false;
    }
    return true;
}

bool handlePassword(char *password, int connSock) {
    uint8_t command = 1, nargs = 1;
    int ret;
    uint8_t datagram[MAX_DATAGRAM_SIZE];
    uint8_t res[MAX_DATAGRAM_SIZE];

    datagram[0] = command;
    datagram[1] = nargs;
    size_t length = strlen(password);
    datagram[2] = length;
    memcpy(datagram + 3, password, length);
    ret = sctp_sendmsg(connSock, (const void *) datagram, 3 + length,
                       NULL, 0, 0, 0, STREAM, 0, 0);
    ret = sctp_recvmsg(connSock, (void *) res, MAX_DATAGRAM_SIZE,
                       (struct sockaddr *) NULL, 0, 0, 0);
    if (res[0] == 0) {
        printf("Login successful \n");
        return true;
    } else {
        printf("Invalid password, please try again. \n");
        return false;
    }
}

int getConcurrentConnections(int connSock) {
    uint8_t command = 0x02, nargs = 0;
    int ret;
    uint8_t datagram[MAX_DATAGRAM_SIZE];
    uint8_t res[MAX_DATAGRAM_SIZE];
    datagram[0] = command;
    datagram[1] = nargs;
    ret = sctp_sendmsg(connSock, (const void *) datagram, 2,
                       NULL, 0, 0, 0, STREAM, 0, 0);
    ret = sctp_recvmsg(connSock, (void *) res, MAX_DATAGRAM_SIZE,
                       (struct sockaddr *) NULL, 0, 0, 0);
    if (res[0] == 0) {
        char data[res[1] + 1];
        memcpy(data, res + 2, res[1]);
        data[res[1]] = '\0';
        printf("Concurrent connections: %s \n", data);
    } else {
        printError(res[0]);
    }
    return 1;
}

int handleTransferredBytes(int connSock) {
    uint8_t command = 3, nargs = 0;
    int ret;
    uint8_t datagram[MAX_DATAGRAM_SIZE];
    uint8_t res[MAX_DATAGRAM_SIZE];
    datagram[0] = command;
    datagram[1] = nargs;
    ret = sctp_sendmsg(connSock, (const void *) datagram, 2,
                       NULL, 0, 0, 0, STREAM, 0, 0);
    ret = sctp_recvmsg(connSock, (void *) res, MAX_DATAGRAM_SIZE,
                       (struct sockaddr *) NULL, 0, 0, 0);

    if (res[0] == 0) {
        char data[res[1] + 1];
        memcpy(data, res + 2, res[1]);
        data[res[1]] = '\0';
        printf("Bytes transfered: %s \n", data);
    } else {
        printError(res[0]);
    }
    return 1;
}

int handleHistoricAccess(int connSock) {
    uint8_t command = 4, nargs = 0;
    int ret;
    uint8_t datagram[MAX_DATAGRAM_SIZE];
    uint8_t res[MAX_DATAGRAM_SIZE];
    datagram[0] = command;
    datagram[1] = nargs;
    ret = sctp_sendmsg(connSock, (const void *) datagram, 2,
                       NULL, 0, 0, 0, STREAM, 0, 0);
    ret = sctp_recvmsg(connSock, (void *) res, MAX_DATAGRAM_SIZE,
                       (struct sockaddr *) NULL, 0, 0, 0);
    if (res[0] == 0) {
        char data[res[1] + 1];
        memcpy(data, res + 2, res[1]);
        data[res[1]] = '\0';
        printf("Historic accesses: %s \n", data);
    } else {
        printError(res[0]);
    }
    return 1;
}

int handleActiveTransformation(int connSock) {
    uint8_t command = 0x05, nargs = 0;
    int ret;
    uint8_t datagram[MAX_DATAGRAM_SIZE];
    uint8_t res[MAX_DATAGRAM_SIZE];
    datagram[0] = command;
    datagram[1] = nargs;
    ret = sctp_sendmsg(connSock, (const void *) datagram, 2,
                       NULL, 0, 0, 0, STREAM, 0, 0);
    ret = sctp_recvmsg(connSock, (void *) res, MAX_DATAGRAM_SIZE,
                       (struct sockaddr *) NULL, 0, 0, 0);
    if (res[0] == 0) {
        char data[res[1] + 1];
        memcpy(data, res + 2, res[1]);
        data[res[1]] = '\0';
        printf("Active transformation: %s \n", data);
    } else {
        printError(res[0]);
    }
    return 1;
}

int handleBufferSize(int connSock) {
    uint8_t arg_size = 0;
    int exit = 1;
    uint8_t command = 0x06, nargs = 1;
    int ret;
    uint8_t datagram[MAX_DATAGRAM_SIZE];
    uint8_t res[MAX_DATAGRAM_SIZE];
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
    ret = sctp_sendmsg(connSock, (const void *) datagram, arg_size + 3,
                       NULL, 0, 0, 0, STREAM, 0, 0);
    ret = sctp_recvmsg(connSock, (void *) res, MAX_DATAGRAM_SIZE,
                       (struct sockaddr *) NULL, 0, 0, 0);
    if (res[0] == 0x00) {
        printf("New Buffer size set \n");
    } else {
        printError(res[0]);
    }
    return 0;
}

int handleTransformationChange(int connSock) {
    uint8_t arg_size = 0;
    int exit = 1;
    uint8_t command = 0x07, nargs = 1;
    int ret;
    uint8_t datagram[MAX_DATAGRAM_SIZE];
    uint8_t res[MAX_DATAGRAM_SIZE];
    char buffer[256];
    datagram[0] = command;
    datagram[1] = nargs;

    while (exit) {
        printf(" Enter new transformation command \n");
        exit = fgets(buffer, 255, stdin) == NULL;
        arg_size = (uint8_t)(strlen(buffer) - 1);
        if (exit) {
            printf(" No characters read \n");
        } else {
            datagram[2] = arg_size;
            memcpy(datagram + 3, buffer, arg_size);
        }
    }
    ret = sctp_sendmsg(connSock, (const void *) datagram, arg_size + 3,
                       NULL, 0, 0, 0, STREAM, 0, 0);
    ret = sctp_recvmsg(connSock, (void *) res, MAX_DATAGRAM_SIZE,
                       (struct sockaddr *) NULL, 0, 0, 0);
    if (res[0] == 0x00) {
        printf("Transformation set \n");
    } else {
        printError(res[0]);
    }
    return 0;
}

int handleHelp() {
    printf("\nThese are the following commands: \n");
    printf("0 -> Help\n");
    printf("1 -> Concurrent Connections\n");
    printf("2 -> Transferred Bytes\n");
    printf("3 -> Historical Accesses\n");
    printf("4 -> Get Transformation\n");
    printf("5 -> Set buffer Size\n");
    printf("6 -> Set transformation\n");
    printf("7 -> Quit\n");
    return 0;
}

int handleQuit(int connSock) {
    uint8_t command = 4, nargs = 0;
    int ret;
    uint8_t datagram[MAX_DATAGRAM_SIZE];
    uint8_t res[MAX_DATAGRAM_SIZE];
    datagram[0] = command;
    datagram[1] = nargs;
    ret = sctp_sendmsg(connSock, (const void *) datagram, 2,
                       NULL, 0, 0, 0, STREAM, 0, 0);
    ret = sctp_recvmsg(connSock, (void *) res, MAX_DATAGRAM_SIZE,
                       (struct sockaddr *) NULL, 0, 0, 0);
    if (res[0] == 0) {
        printf("Goodbye!\n");
        return 0;
    } else {
        printError(res[0]);
    }
    return 1;
}

void printDatagram(uint8_t *datagram, int size) {
    printf("Datagram: ");
    for (int i = 0; i < size; ++i)
        printf("[%d]%02x[%c]-", i, datagram[i], datagram[i]);
    printf("\n");
}