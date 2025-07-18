cmake_minimum_required(VERSION 3.10 FATAL_ERROR)

project(wsstest VERSION 0.1)

set(CMAKE_C_STANDARD 90)
add_executable(wsstest main.c)
target_compile_options(wsstest PRIVATE -Wall -Wextra -Wpedantic -Wformat=2 -Wshadow)
target_link_libraries(wsstest xcb)
install(TARGETS wsstest)
