cmake_minimum_required(VERSION 2.8)

add_executable(proxyPop main.c
        utils/selector.c
        utils/stm.c
        utils/buffer.c
        proxy/proxyPop3nio.c
        utils/request.c
        utils/proxyArguments.c
        utils/request_queue.c utils/response.c utils/response.h)
find_package(Threads)
target_link_libraries(proxyPop ${CMAKE_THREAD_LIBS_INIT})

if(CHECK_FOUND)
    add_executable(request_test
                   tests/requestTest.c utis/request.c utils/buffer.c)
    target_link_libraries(request_test check)
    add_test(NAME request_test COMMAND request_test)
endif()