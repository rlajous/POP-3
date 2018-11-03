#include <stdlib.h>
#include <string.h>
#include <check.h>

#include "request.h"
#include "tests.h"

START_TEST (test_request_invalid_method) {
    struct request request;
    struct request_parser parser{
      .request = &request;
    };

    #define FIXBUF(b, data) buffer_init(&(b), N(data), (data)); \
                            buffer_write_adv(&(b), N(data))

    request_parser_init(&parser);
    // "INV\CR\LF"
    uint_8 data[] = {
     0x49, 0x4E, 0x56, 0x0D, 0x0A
    }
    buffer b; FIXBUFF(b, data);
    bool errored = false;
    enum request_state st = request_consume(&b, &parser, &errored);

    ck_assert_uint_eq(true, errored);
    ck_assert_uint_eq(request_error, st);
}
END_TEST
