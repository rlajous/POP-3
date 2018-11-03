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
#include "handler.h"

#define PROXY_SCTP_PORT 9090
#define DEFAULT_PORT 9090
#define DEFAULT_IP "127.0.0.1"

union ans {
  unsigned long long int lng;
  uint8_t arr[8];
};

typedef union ans *answer;
int validIpAddress(char *ipAddress);
void clean(uint8_t *buf);

int main(int argc, char *argv[])
{
  int connSock, in, i, ret, flags;
  int *position;
  struct sockaddr_in servaddr;
  struct sctp_status status;
  struct sctp_sndrcvinfo sndrcvinfo;
  struct sctp_event_subscribe events;
  struct sctp_initmsg initmsg;
  uint8_t buffer[MAX_BUFFER + 1];

  clean(buffer);
  position = 0;

  int port;
  char address[16];
  if (argc == 3)
  {
    int port_in = atoi(argv[2]);
    if (validIpAddress(argv[1]))
    {
      strcpy(address, argv[1]);
    }
    else
    {
      printf(" Bad IP argument, using default value.\n");
      strcpy(address, DEFAULT_IP);
    }
    if (port_in > 1024 && port_in < 60000)
    {
      port = port_in;
    }
    else
    {
      printf(" Bad PORT argument, using default value.\n");
      port = DEFAULT_PORT;
    }
  }
  else if (argc == 2)
  {
    int port_in = atoi(argv[1]);
    strcpy(address, DEFAULT_IP);
    if (port_in > 1024 && port_in < 60000)
    {
      port = port_in;
    }
    else
    {
      printf(" Bad PORT argument, using default value.\n");
      port = DEFAULT_PORT;
    }
  }
  else
  {
    if (argc == 1)
      printf(" No arguments, using default values.\n");
    else
      printf(" Bad arguments, using default values.\n");
    strcpy(address, DEFAULT_IP);
    port = DEFAULT_PORT;
  }
  printf(" Trying to connect to %s:%d\n", address, port);

  connSock = socket(AF_INET, SOCK_STREAM, IPPROTO_SCTP);

  if (connSock == -1)
  {
    exit(0);
  }

  bzero((void *)&servaddr, sizeof(servaddr));
  servaddr.sin_family = AF_INET;
  servaddr.sin_port = htons(port);
  servaddr.sin_addr.s_addr = inet_addr(address);

  ret = connect(connSock, (struct sockaddr *)&servaddr, sizeof(servaddr));

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
      int flag = 1;
      while (flag != 0)
      {
        if (fgets(buffer, sizeof(buffer), stdin) != NULL)
        {
          sscanf(buffer, "%s %s %s", first, second, third);
          flag = handleUser(first, connSock);
        }
        else
        {
          printf(" No characters read \n");
        }
      }
      printf(" Enter Password \n");
      flag = 1;
      while (flag != 0)
      {
        if (fgets(buffer, sizeof(buffer), stdin) != NULL)
        {
          sscanf(buffer, "%s %s %s", first, second, third);
          flag = handlePassword(first);
          if (first != 0)
          {
          }
        }
        else
        {
          printf(" No characters read \n");
        }
      }
      handleHelp();
      while (exit != 0)
      {
        if (fgets((char *)buffer, sizeof(buffer), stdin) == NULL)
        {
          printf(" No characters read, for more help enter number 0\n");
        }
        else
        {
          sscanf(buffer, "%s ", first);
          switch (first)
          {
          case '0':
            handleHelp(connSock);
            break;
          case '1':
            handleConcurrentConection(connSock);
            break;
          case '2':
            handleTransferedBytes(connSock);
            break;
          case '3':
            handleHistoricAccess(connSock);
            break;
          case '4':
            handleActiveTransformation(connSock);
            break;
          case '5':
            handleBufferSize(connSock);
            break;
          case '6':
            handleTransformationChange(connSock);
            break;
          case '7':
            handleTimeOut(connSock);
            break;
          case '8':
            exit = handleQuit(connSock);
            break;
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
  return;
}

int validIpAddress(char *ipAddress)
{
  struct sockaddr_in sa;
  int result = inet_pton(AF_INET, ipAddress, &(sa.sin_addr));
  return result != 0;
}