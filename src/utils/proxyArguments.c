#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <errno.h>

#include "proxyArguments.h"

#define DEFAULT_ORIGIN_PORT 110
#define DEFAULT_PROXY_PORT 1110
#define DEFAULT_CONFIG_PORT 9090
#define LOOPBACK "127.0.0.1"
#define DEFAULT_REPLACEMENT_MESSAGE "Parte Reemplazada."
#define DEFAULT_ERROR_FILE "/dev/null"
#define VERSION "0.0.1"

static void
print_usage() {
  printf("Proxy POP3 implementation \n");
  printf("Usage: pop3filter [POSIX STYLE OPTIONS] <origin-server>\n\n");
  printf("Options:\n");
  printf("%-30s","\t-e filter-error-file");
  printf("Specifies the file where stderr is redirected to when executing filters. "
         "By default the file is /dev/null\n");
  printf("%-30s", "\t-h");
  printf("Prints out help and terminates\n");
  printf("%-30s", "\t-l pop3-addresss");
  printf("Specifies the address where the proxy will be listening. By default it listens in every interface\n");
  printf("%-30s", "\t-L config-address");
  printf("Specifies the address where the management service will be listening. By default it listens in loopback\n");
  printf("%-30s", "\t-m message");
  printf("Message left by the filter when replacing text parts\n");
  printf("%-30s", "\t-M censored-media-types");
  printf("List of censored media types\n");
  printf("%-30s", "\t-o management-port");
  printf("SCTP port where the management server is listening. By default is 9090\n");
  printf("%-30s", "\t-p local-port");
  printf("TCP port where the POP3 proxy will listen. By default is 1110\n");
  printf("%-30s", "\t-P origin-port");
  printf("TCP port where the POP3 origin server is listening. By default is 110\n");
  printf("%-30s", "\t-t cmd");
  printf("Specifies the command used when applying transformations\n");
  printf("%-30s", "\t-v");
  printf("Prints out version and terminates\n");
}

static void
print_version(arguments arguments) {
  printf("Proxy Pop3 version %s\n", arguments->version);
}

static void
add_message(const int messages, const char *optarg, arguments args) {

  char  *message;
  size_t length = strlen(optarg);

  if(0 != messages) {
    size_t old_length = strlen(args->message);
    message           = realloc(args->message, old_length + length + 1);
    if(NULL == message) {
      fprintf(stderr, "Error: No memory available\n");
      exit(1);
    }
    message = strcat(message, "\n");
    message = strncat(message, optarg, length + 1);
  } else {
    message = malloc(length + 1);
    if(NULL == message) {
      fprintf(stderr, "Error: No memory available\n");
      exit(1);
    }
    strncpy(message, optarg, length + 1);
  }
  args->message = message;
}

static void
add_command(arguments arguments, const char * command) {

  size_t length       = strlen(command) + 1;
  char  *new_command  = malloc(length);

  if (NULL == new_command) {
    fprintf(stderr, "Error: No memory available\n");
    exit(1);
  }

  strncpy(new_command, command, length + 1);
  arguments->command = new_command;
}

/** Extracted from Juan F. Codagnone's code*/
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

/** Default values*/
static arguments
init_arguments() {
  arguments ret           = malloc(sizeof(args));

  ret->origin_address     = NULL;
  ret->origin_port        = DEFAULT_ORIGIN_PORT;

  ret->pop3_address       = NULL;
  ret->pop3_port          = DEFAULT_PROXY_PORT;

  ret->config_address     = LOOPBACK;
  ret->config_port        = DEFAULT_CONFIG_PORT;

  ret->command            = NULL;
  ret->message            = DEFAULT_REPLACEMENT_MESSAGE;
  ret->filter_error_file  = DEFAULT_ERROR_FILE;
  //Todo: MediaTypes

  ret->version = VERSION;

  return ret;
}

arguments
parse_arguments(const int argc, char * const* argv) {
  arguments ret = init_arguments();
  int       option    = 0;
  int       messages  = 0;
            opterr    = 0;

  while ((option = getopt(argc, argv, "e:hl:L:m:M:o:p:P:t:v")) != -1) {
    switch (option) {
      case 'e':
        ret->filter_error_file = optarg;
        break;
      case 'h':
        print_usage();
        exit(0);
      case 'l':
        ret->pop3_address = optarg;
        break;
      case 'L':
        ret->config_address = optarg;
        break;
      case 'm':
        add_message(messages, optarg, ret);
        messages++;
        break;
      case 'M':
        //TODO
        break;
      case 'o':
        ret->config_port = parse_port(optarg);
        break;
      case 'p':
        ret->pop3_port = parse_port(optarg);
        break;
      case 'P':
        ret->origin_port = parse_port(optarg);
        break;
      case 't':
        add_command(ret, optarg);
        break;
      case 'v':
        print_version(ret);
        exit(0);
      case '?':
        if (optopt == 'e' || optopt == 'l' || optopt == 'L'
            || optopt == 'm' || optopt == 'M' || optopt == 'o'
            || optopt == 'p' || optopt == 'P' || optopt == 't') {
          fprintf(stderr, "Option -%c requires an argument.\n",
                  optopt);
        } else if (isprint(optopt)) {
          fprintf(stderr, "Unknown option '-%c'.\n", optopt);
        } else {
          fprintf(stderr, "Unknown option character '\\x%x'.\n",
                  optopt);
        }
        exit(1);
      default:
        print_usage();
        exit(1);
    }
  }

  if (1 == argc - optind) {
    size_t length   = strlen(argv[optind]);
    char  *address  = malloc(length + 1);
    if (NULL == address) {
      fprintf(stderr, "Error: No memory available\n");
      exit(1);
    }

    strncpy(address, argv[optind], length + 1);
    ret->origin_address = address;
  } else {
    print_usage();
    exit(1);
  }
  return ret;
}
