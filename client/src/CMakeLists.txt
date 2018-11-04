cmake_minimum_required(VERSION 2.8)

add_executable(spcpClient client.c
        common.h
        handler.c
        handler.h)

target_link_libraries(spcpClient ${CMAKE_THREAD_LIBS_INIT} sctp)