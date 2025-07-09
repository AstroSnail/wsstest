cmake_minimum_required(VERSION 3.10 FATAL_ERROR)

project(wsstest VERSION 0.1)

add_executable(wsstest main.c)
target_link_libraries(wsstest xcb)
install(TARGETS wsstest)
