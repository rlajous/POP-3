#ifndef MAX_DATAGRAM
#define MAX_DATAGRAM 132
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "common.h"
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/sctp.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

int handleUser(char *user)
{
  char command = 0, nargs = 1, ret;
  uint8_t datagram[MAX_DATAGRAM];
  datagram[0] = command;
  datagram[1] = nargs;
  int lenght = strlen(user);
  datagram[2] = lenght;
  datagram[3] = user;
  ret = sctp_sendmsg(connSock, (const void *)datagram, 3 + length / 2,
                     NULL, 0, 0, 0, STREAM, 0, 0);
  //Close connection after recieving response
  ret = sctp_recvmsg(connSock, (void *)res, MAX_DATAGRAM_SIZE,
                     (struct sockaddr *)NULL, 0, 0, 0);
  if (res[0] == 0)
  {
    printf("user seted");
  }
  else
  {
    printf("Error code = %s", res[0]);
  }
  return 1;
}

int handlePassword(char *password)
{
  char command = 1, nargs = 1, ret;
  uint8_t datagram[MAX_DATAGRAM];
  datagram[0] = command;
  datagram[1] = nargs;
  int lenght = strlen(password);
  datagram[2] = lenght;
  datagram[3] = password;
  ret = sctp_sendmsg(connSock, (const void *)datagram, 3 + length / 2,
                     NULL, 0, 0, 0, STREAM, 0, 0);
  //Close connection after recieving response
  ret = sctp_recvmsg(connSock, (void *)res, MAX_DATAGRAM_SIZE,
                     (struct sockaddr *)NULL, 0, 0, 0);
  if (res[0] == 0)
  {
    printf("password succes");
  }
  else
  {
    printf("Error code = %s", res[0]);
  }
}

int handleConcurrentConection()
{
  char command = 2, nargs = 0, ret;
  uint8_t datagram[MAX_DATAGRAM];
  datagram[0] = command;
  datagram[1] = nargs;
  ret = sctp_sendmsg(connSock, (const void *)datagram, 2,
                     NULL, 0, 0, 0, STREAM, 0, 0);
  //Close connection after recieving response
  ret = sctp_recvmsg(connSock, (void *)res, MAX_DATAGRAM_SIZE,
                     (struct sockaddr *)NULL, 0, 0, 0);
  if (res[0] == 0)
  {
    printf("Concurrent conections seted");
  }
  else
  {
    printf("Error code = %s", res[0]);
  }
  return 1;
}

int handleTransferedBytes()
{
  char command = 3, nargs = 0, ret;
  uint8_t datagram[MAX_DATAGRAM];
  datagram[0] = command;
  datagram[1] = nargs;
  ret = sctp_sendmsg(connSock, (const void *)datagram, 2,
                     NULL, 0, 0, 0, STREAM, 0, 0);
  //Close connection after recieving response
  ret = sctp_recvmsg(connSock, (void *)res, MAX_DATAGRAM_SIZE,
                     (struct sockaddr *)NULL, 0, 0, 0);
  if (res[0] == 0)
  {
    printf("Bytes transfered");
  }
  else
  {
    printf("Error code = %s", res[0]);
  }
  return 1;
}

int handleHistoricAcces()
{
  char command = 4, nargs = 0, ret;
  uint8_t datagram[MAX_DATAGRAM];
  datagram[0] = command;
  datagram[1] = nargs;
  ret = sctp_sendmsg(connSock, (const void *)datagram, 2,
                     NULL, 0, 0, 0, STREAM, 0, 0);
  //Close connection after recieving response
  ret = sctp_recvmsg(connSock, (void *)res, MAX_DATAGRAM_SIZE,
                     (struct sockaddr *)NULL, 0, 0, 0);
  if (res[0] == 0)
  {
    printf("Historic acces");
  }
  else
  {
    printf("Error code = %s", res[0]);
  }
  return 1;
}

int handleActiveTrasnformation()
{
  char command = 5, nargs = 0, ret;
  uint8_t datagram[MAX_DATAGRAM];
  datagram[0] = command;
  datagram[1] = nargs;
  ret = sctp_sendmsg(connSock, (const void *)datagram, 2,
                     NULL, 0, 0, 0, STREAM, 0, 0);
  //Close connection after recieving response
  ret = sctp_recvmsg(connSock, (void *)res, MAX_DATAGRAM_SIZE,
                     (struct sockaddr *)NULL, 0, 0, 0);
  if (res[0] == 0)
  {
    printf("Transformation activated");
  }
  else
  {
    printf("Error code = %s", res[0]);
  }
  return 1;
}

int handleBufferSize()
{
  int exit = 1;
  char command = 10, nargs = 1, ret;
  uint8_t datagram[MAX_DATAGRAM];
  datagram[0] = command;
  datagram[1] = nargs;
  while (exit)
  {
    printf(" Enter buffer size \n");
    exit = fgets(buffer, sizeof(buffer), stdin) == NULL;
    if (exit)
    {
      printf(" No characters read \n");
    }
    else
    {
      //to do format data
      sscanf(buffer, "%s ", datagram[2]);
      datagram[2] = atoi(datagram[2]);
    }
  }
  ret = sctp_sendmsg(connSock, (const void *)datagram, 2,
                     NULL, 0, 0, 0, STREAM, 0, 0);
  ret = sctp_recvmsg(connSock, (void *)res, MAX_DATAGRAM_SIZE,
                     (struct sockaddr *)NULL, 0, 0, 0);
  if (res[0] == 0)
  {
    printf("Buffer size seted");
  }
  else
  {
    printf("Error code = %s", res[0]);
  }
  return 0;
}

int handleTransformationChange()
{
  int exit = 1;
  char command = 10, nargs = 1, ret;
  uint8_t datagram[MAX_DATAGRAM];
  datagram[0] = command;
  datagram[1] = nargs;
  while (exit)
  {
    printf(" Enter buffer size \n");
    exit = fgets(buffer, sizeof(buffer), stdin) == NULL;
    if (exit)
    {
      printf(" No characters read \n");
    }
    else
    {
      //to do format data
      sscanf(buffer, "%s %s", datagram[2]);
    }
  }
  ret = sctp_sendmsg(connSock, (const void *)datagram, 2,
                     NULL, 0, 0, 0, STREAM, 0, 0);
  ret = sctp_recvmsg(connSock, (void *)res, MAX_DATAGRAM_SIZE,
                     (struct sockaddr *)NULL, 0, 0, 0);
  if (res[0] == 0)
  {
    printf("Transformation done");
  }
  else
  {
    printf("Error code = %s", res[0]);
  }
  return 0;
}

int handleTimeOut()
{
  int exit = 1;
  char command = 10, nargs = 2, ret;
  uint8_t datagram[MAX_DATAGRAM];
  datagram[0] = command;
  datagram[1] = nargs;
  while (exit)
  {
    printf(" Enter buffer size \n");
    exit = fgets(buffer, sizeof(buffer), stdin) == NULL;
    if (exit)
    {
      printf(" No characters read \n");
    }
    else
    {
      //to do format data
      sscanf(buffer, "%s %s", datagram[2], datagram[3]);
      datagram[2] = atoi(datagram[2]);
      datagram[3] = atoi(datagram[3]);
    }
  }
  ret = sctp_sendmsg(connSock, (const void *)datagram, 2,
                     NULL, 0, 0, 0, STREAM, 0, 0);

  ret = sctp_recvmsg(connSock, (void *)res, MAX_DATAGRAM_SIZE,
                     (struct sockaddr *)NULL, 0, 0, 0);
  if (res[0] == 0)
  {
    printf("Timeout seted");
  }
  else
  {
    printf("Error code = %s", res[0]);
  }
  return 0;
}

int handleHelp()
{
  printf("\nThese are the following commands: \n");
  printf("0 -> Help\n\n");
  printf("1 -> Concurrent Conection\n\n");
  printf("2 -> Transfered Byte\n\n");
  printf("3 -> History Acces\n\n");
  printf("4 -> Active Transformation\n");
  printf("5 -> Buffer Size\n");
  printf("6 -> Transformation Change\n");
  printf("7 -> Time Out\n");
  printf("8 -> Quit\n");
  return 0;
}

int handleQuit()
{
  char command = 6, nargs = 0, ret;
  uint8_t datagram[MAX_DATAGRAM];
  datagram[0] = command;
  datagram[1] = nargs;
  ret = sctp_sendmsg(connSock, (const void *)datagram, 2,
                     NULL, 0, 0, 0, STREAM, 0, 0);
  //Close connection after recieving response
  ret = sctp_recvmsg(connSock, (void *)res, MAX_DATAGRAM_SIZE,
                     (struct sockaddr *)NULL, 0, 0, 0);
  if (res[0] == 0)
  {
    printf("BYE");
  }
  else
  {
    printf("Error code = %s", res[0]);
  }
  return 1;
}

void printDatagram(uint8_t *datagram, int size)
{
  //Print Datagram for debbuging
  printf("Datagram: ");
  for (int i = 0; i < size; ++i)
    printf("[%d]%02x[%c]-", i, datagram[i], datagram[i]);
  printf("\n");
}