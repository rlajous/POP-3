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
#include "handlers.h"


#define PROXY_SCTP_PORT     9090
#define DEFAULT_PORT        9090
#define DEFAULT_IP          "127.0.0.1"

union ans{
    unsigned long long int lng;
    uint8_t                arr[8];
};

typedef union ans * answer;
int validIpAddress(char * ipAddress);
void clean(uint8_t * buf);

int main(int argc, char * argv[]) {
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
  if(argc == 3){    //Custom port and ip
    int port_in = atoi( argv[2] );
    if(validIpAddress(argv[1])){
      strcpy(address, argv[1]);
    }else{
      printf(" Bad IP argument, using default value.\n");
      strcpy(address, DEFAULT_IP);  //Localhost
    }
    if(port_in > 1024 && port_in < 60000){  //Valid ports
      port = port_in;
    }else{
      printf(" Bad PORT argument, using default value.\n");
      port = DEFAULT_PORT;
    }
  }else if(argc == 2){  //Custom port
    int port_in = atoi( argv[1] );
    strcpy(address, DEFAULT_IP);
    if(port_in > 1024 && port_in < 60000){  //Valid ports
      port = port_in;
    }else{
      printf(" Bad PORT argument, using default value.\n");
      port = DEFAULT_PORT;
    }
  }else{  //Default values
    if(argc == 1)
      printf(" No arguments, using default values.\n");
    else
      printf(" Bad arguments, using default values.\n");
    strcpy(address, DEFAULT_IP);
    port = DEFAULT_PORT;
  }
  printf(" Trying to connect to %s:%d\n", address, port);


  /* Create an SCTP TCP-Style Socket */
  connSock = socket(AF_INET, SOCK_STREAM, IPPROTO_SCTP);

  if (connSock==-1){
    exit();    
  }

  /* Specify that a maximum of 5 streams will be available per socket */
  memset(&initmsg, 0, sizeof(initmsg));
  initmsg.sinit_num_ostreams = 5;
  initmsg.sinit_max_instreams = 5;
  initmsg.sinit_max_attempts = 4;
  ret = setsockopt(connSock, IPPROTO_SCTP, SCTP_INITMSG,
                   &initmsg, sizeof(initmsg));

  /* Specify the peer endpoint to which we'll connect */
  bzero((void *) &servaddr, sizeof(servaddr));
  servaddr.sin_family = AF_INET;
  servaddr.sin_port = htons(port);
  servaddr.sin_addr.s_addr = inet_addr(address);

  /* Connect to the server */
  ret = connect(connSock, (struct sockaddr *) &servaddr, sizeof(servaddr));

  if (ret < 0) {
    printf(" Can't connect. Check if the server is working property.\n");
    exit(0);
  }
  /* Enable receipt of SCTP Snd/Rcv Data via sctp_recvmsg */
  memset((void *) &events, 0, sizeof(events));
  events.sctp_data_io_event = 1;
  ret = setsockopt(connSock, SOL_SCTP, SCTP_EVENTS,
                   (const void *) &events, sizeof(events));

  /* Read and emit the status of the Socket (optional step) */
  in = sizeof(status);
  ret = getsockopt(connSock, SOL_SCTP, SCTP_STATUS,
                   (void *) &status, (socklen_t * ) & in);

  printf(" WELCOME TO J2M2 PROTOCOL CLIENT \n");
  printf("        Please login        \n");
  // Start J2M2 Logic

 while(1){
  if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
    printf(" No characters read \n");
  }

  char first[MAX_BUFFER] = {0};
  char second[MAX_BUFFER] = {0};
  char third[MAX_BUFFER] = {0};
  sscanf(buffer, "%s %s %s", first, second, third);

  if (strcmp(first, "login") == 0) {
    if (handleLogin(second, third, connSock)) {
      while (1) {
        first[0]  = 0;
        second[0] = 0;
        third[0]  = 0;

        if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
          printf(" No characters read \n");
          break;
        }
        sscanf(buffer, "%s %s %[\001-\377]", first, second, third);
        if (strcmp(first, "metric") == 0) {
          if (!handleMetric(second, third, connSock)) {
            continue;
          }
        } else if (strcmp(first, "config") == 0) {
          if (!handleConfig(second, third, connSock)) {
            continue;
          }
        } else if (strcmp(first, "exit") == 0) {
          if (!handleExit(second, third, connSock)) {
            continue;
          }
        } else if (strcmp(first, "help") == 0) {
          handleHelp();
        } else {
          printf(" Bad Request \n");
          continue;
        }
        // Recieve response
        flags = 0;
        uint8_t res[MAX_BUFFER];
        clean(res);
        answer aux = calloc(1, sizeof(union ans));

        ret = sctp_recvmsg(connSock, (void *) res, MAX_DATAGRAM_SIZE,
                           (struct sockaddr *) NULL, 0, &sndrcvinfo, &flags);

        //Response statuts OK accepted. Dependes on data sent.
        if (res[TYPE] == 3 && res[COMMAND] == 2) {
            printf("[  -----   Bye!  -----   ]\n");
            close(connSock);
            exit(0);
        }

        if (res[CODE] == 1) {
          printf("---Start Message---\n");
          //if metric trabytes -> uint64_t sent. Else uint8_t.
          if(res[TYPE] == 1 && res[COMMAND] == 2 && res[ARGSQ] == 1){
            for(int i = 0; i < ONE_BYTE; i++){
              aux->arr[i] = res[HEADERS + i];     //Sending 8 bytes to union. 
            }                                     //After that we will be ready to read the correct number.
            printf("%llu\n", aux->lng);
          } else if(res[TYPE] == 2 && res[ARGSQ] == 0){
              if(res[START_BYTES] == 1){
                printf("Transformation Activated.\n");
              } else {
                printf("Transformation Desactivated.\n");
              }
          }else {
            printf("%u\n", res[START_BYTES]);
          }
          printf("---End Message---\n");
        } else {
          printf(" Error code %i\n \n", res[CODE]);
        }
      }
    } else {
      printf(" Invalid login. \n");
    }
  } else {
    printf(" Please login property. \n");
  }
  }
    close(connSock);
    return 0;
}

void clean(uint8_t * buf){
  for(int i = 0; i < MAX_BUFFER; i++){
    buf[i] = 0;
  }
  return;
}

int validIpAddress(char * ipAddress)
{
  struct sockaddr_in sa;
  int result = inet_pton(AF_INET, ipAddress, &(sa.sin_addr));
  return result != 0;
}