cmake_minimum_required(VERSION 2.8)

add_executable(proxyPop main.c
                        selector.c
                        selector.h
                        proxyPop3nio.c proxyPop3nio.h)