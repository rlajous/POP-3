CC=gcc
CFLAGS=-g
DEPS = mime_chars.h mime_msg.h mime_value.h mime_body.h mime_ctransfer_value.h mime_boundary_key.h mime_boundary_border_end.h pop3_multi.h parser_utils.h parser.h
#OBJ = mime_chars.o mime_msg.o parser_utils.o parser.o pop3_multi.o mime_value.o mime_body.o mime_ctransfer_value.o mime_boundary_key.o mime_boundary_border_end.o stripmime.o
# faltan en OBJ (porque les falta check.h para andar y no se donde esta): mime_msg_test.o parser_test.o parser_utils_test.o 
SOURCES = $(wildcard *.c)
OBJ= $(SOURCES:.c=.o)

%.o: %.c #$(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

stripmime: $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS)

.PHONY : clean
clean :
	-rm $(OBJ)