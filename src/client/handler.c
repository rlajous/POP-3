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

int handleConcurrentConection();
int handleTransferedBytes();
int handleHistoricAcces();
int handleActiveTrasnformation();
int handleBufferSize();
int handleTransformationChange();
int handleTimeOut();

int handleHelp(){
  printf("\nThese are the following commands: \n");
  printf("0 -> HELP\n\n");
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

int handleExit(char * second, char * third, int connSock){
  char type = 3, command = 2, argsq = 0, code = 0, ret;
  uint8_t datagram[MAX_DATAGRAM];
 
  if(second[0]){
    printf(" Parameters not allowed. \n");
    return 0;
  }  

  datagram[0] = type;
  datagram[1] = command;
  datagram[2] = argsq;
  datagram[3] = code;
  ret = sctp_sendmsg( connSock, (const void *)datagram, 4,
                         NULL, 0, 0, 0, STREAM, 0, 0 );    
  //Close connection after recieving response
  return 1;
}

void printDatagram(uint8_t * datagram, int size){
  //Print Datagram for debbuging
  printf("Datagram: ");
  for (int i = 0; i < size; ++i)
    printf("[%d]%02x[%c]-",i, datagram[i], datagram[i]);
  printf("\n");
}