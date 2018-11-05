cmake_minimum_required(VERSION 2.8)

add_executable(proxyPop main.c
        utils/selector.c
        utils/stm.c
        utils/buffer.c
        proxy/proxyPop3nio.c
        pop3Parsers/pop3request.c
        utils/proxyArguments.c
        utils/request_queue.c
        utils/parser.c
        utils/parser_utils.c
        pop3Parsers/pop3response.c
        pop3Parsers/pop3response.h
        proxy/ServerSpcpNio.c proxy/ServerSpcpNio.h
        spcpParsers/spcpRequest.c
        spcpParsers/spcpRequest.h
        proxy/spcpServerCredentials.c
        proxy/spcpServerCredentials.h
        pop3Parsers/pop3responseDescaping.c pop3Parsers/pop3responseDescaping.h
        pop3Parsers/pop3responseEscaping.c pop3Parsers/pop3responseEscaping.h)
find_package(Threads)
target_link_libraries(proxyPop ${CMAKE_THREAD_LIBS_INIT} sctp)

if(CHECK_FOUND)
    add_executable(request_test
                   tests/requestTest.c utis/request.c utils/buffer.c)
    target_link_libraries(request_test check)
    add_test(NAME request_test COMMAND request_test)
endif()