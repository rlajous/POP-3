#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "common.h"
#include <netinet/sctp.h>
#include <stdbool.h>
#include <ctype.h>
#include <netdb.h>
#include <limits.h>
#include <errno.h>
#include "handler.h"

#define DEFAULT_PORT 9090
#define DEFAULT_IP "127.0.0.1"

union ans {
  unsigned long long int lng;
  uint8_t arr[8];
};

typedef union ans *answer;
void clean(uint8_t *buf);
static uint16_t parse_port(const char * port);
bool resolve_address(char *address, uint16_t port, struct addrinfo ** addrinfo);

static void
print_usage() {
  printf("SCPC Client implementation \n");
  printf("Usage: spcpClient [POSIX STYLE OPTIONS]\n\n");
  printf("Options:\n");
  printf("%-30s", "\t-L config-address");
  printf("Specifies the address where the management service will be listening. By default it listens in loopback\n");
  printf("%-30s", "\t-o management-port");
  printf("SCTP port where the management server is listening. By default is 9090\n");
}

int main(int argc, char *argv[])
{
  int connSock, in, i, ret, flags;
  int *position;
  struct sockaddr_in servaddr;
  struct sctp_status status;
  struct sctp_sndrcvinfo sndrcvinfo;
  struct sctp_event_subscribe events;
  struct sctp_initmsg initmsg;
  char buffer[MAX_BUFFER + 1];

  clean(buffer);
  position = 0;

  uint16_t port = DEFAULT_PORT;
  char *address = DEFAULT_IP;
  int c;
  opterr = 0;
  size_t size;

  while ((c = getopt (argc, argv, "L:o:")) != -1) {
    switch (c) {
      case 'L':
        size = strlen(optarg);
        address = malloc(size);
        memcpy(address, optarg, size);
        break;
      case 'o':
        port = parse_port(optarg);
        break;
      case '?':
        if (optopt == 'L' || optopt == 'o') {
          fprintf(stderr, "Option -%c requires an argument.\n",
                  optopt);
        }
        else if (isprint(optopt)) {
          fprintf(stderr, "Unknown option `-%c'.\n", optopt);
        }
        else {
          fprintf(stderr,
                  "Unknown option character `\\x%x'.\n",
                  optopt);
        }
        exit(1);
      default:
        print_usage();
        exit(1);
    }
  }

  struct addrinfo *addr;
  if(!resolve_address(address, port, &addr)) {
    perror("Unable to resolve address");
    exit(1);
  }

  printf(" Trying to connect to %s:%d\n", address, port);

  connSock = socket(addr->ai_family, SOCK_STREAM, IPPROTO_SCTP);

  if (connSock == -1)
  {
    exit(0);
  }

  ret = connect(connSock, addr->ai_addr, addr->ai_addrlen);

  if (ret < 0)
  {
    printf(" Can't connect. Check if the server is working property.\n");
    exit(0);
  }

  printf("SPCP PROTOCOL CLIENT STARTED\n");
  printf("        Please login        \n");
  int exit = 1;
  while (exit)
  {
    printf(" Press 1 to login \n");
    if (fgets(buffer, sizeof(buffer), stdin) == NULL)
    {
      printf(" No characters read \n");
    }

    char first[MAX_BUFFER] = {0};
    char second[MAX_BUFFER] = {0};
    char third[MAX_BUFFER] = {0};
    sscanf(buffer, "%s %s %s", first, second, third);
    if (strcmp(first, "1") == 0)
    {
      int userLogged = false;
      int readUser = true, readPass = true, success;
      while(!userLogged) {
        printf("Enter username \n");
        while (readUser) {
          readPass = true;
          if (fgets(buffer, sizeof(buffer), stdin) != NULL) {
            sscanf(buffer, "%s %s %s", first, second, third);
            success = handleUser(first, connSock);
          } else {
            printf(" No characters read \n");
          }
          if (success) {
            readUser = false;
          }
        }
        printf("Enter Password \n");
        while (readPass) {
          if (fgets(buffer, sizeof(buffer), stdin) != NULL) {
            sscanf(buffer, "%s", first);
            success = handlePassword(first, connSock);
          } else {
            printf(" No characters read \n");
          }
          if (success) {
            userLogged = true;
            readPass = false;
          } else {
            readPass = false;
            readUser = true;
          }
        }
      }

      handleHelp();
      while (exit != 0)
      {
        if (fgets((char *)buffer, sizeof(buffer), stdin) == NULL)
        {
          printf(" No characters read, for more help enter number 0 \n");
        }
        else
        {
          uint64_t option;
          option = strtoul(buffer, NULL, 10);
          switch (option)
          {
          case 0:
            handleHelp();
            break;
          case 1:
            getConcurrentConnections(connSock);
            break;
          case 2:
            handleTransferredBytes(connSock);
            break;
          case 3:
            handleHistoricAccess(connSock);
            break;
          case 4:
            handleActiveTransformation(connSock);
            break;
          case 5:
            handleBufferSize(connSock);
            break;
          case 6:
            handleTransformationChange(connSock);
            break;
          case 7:
            exit = handleQuit(connSock);
            break;
          default:
            printf("Invalid option, for help enter 0 \n");
          }
        }
      }
    }
  }
  if (exit)
  {
    close(connSock);
    return 0;
  }
}

void clean(uint8_t *buf)
{
  for (int i = 0; i < MAX_BUFFER; i++)
  {
    buf[i] = 0;
  }
}

static uint16_t
parse_port(const char * port) {

  char *end     = 0;
  const long sl = strtol(port, &end, 10);

  if (end == port || '\0' != *end
      || ((LONG_MIN == sl || LONG_MAX == sl) && ERANGE == errno)
      || sl < 0 || sl > USHRT_MAX) {
    fprintf(stderr, "Error: Argument port %s should be an integer\n", port);
    exit(1);
  }

  return (uint16_t) sl;
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