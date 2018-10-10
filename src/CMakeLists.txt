cmake_minimum_required(VERSION 2.8)

add_executable(proxyPop main.c
        utils/selector.c
        utils/stm.c
        utils/buffer.c
        proxy/proxyPop3nio.c
        utils/request.c)