# SPDX-FileCopyrightText: 2025 AstroSnail <astrosnail@protonmail.com>
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.10 FATAL_ERROR)

project(wsstest VERSION 0.1)

set(CMAKE_C_STANDARD 90)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(wsstest main.c)
# doesn't add -std=c90
# target_compile_features(wsstest PRIVATE c_std_90)
# -Wc++-compat doesn't seem to warn about all differences between c and c++
# e.g. assigning a string literal to a non-const char *, or missing struct field
# initializers implicitly initializing to 0
target_compile_options(wsstest PRIVATE -Wall -Wextra -Wpedantic -Wc++-compat)
# if (CMAKE_C_COMPILER_ID STREQUAL "GNU")
# 	target_compile_options(wsstest PRIVATE ...)
# elseif (CMAKE_C_COMPILER_ID STREQUAL "Clang")
# 	target_compile_options(wsstest PRIVATE ...)
# endif()
target_link_libraries(wsstest wayland-client xcb xcb-util)
install(TARGETS wsstest)

install(FILES build/compile_commands.json DESTINATION .)
