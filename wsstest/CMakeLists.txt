# SPDX-FileCopyrightText: 2025 AstroSnail <astrosnail@protonmail.com>
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.10 FATAL_ERROR)
project(wsstest VERSION 0.1)
include(GNUInstallDirs)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
install(FILES build/compile_commands.json TYPE DATA)

add_executable(wsstest main.c)
# doesn't add -std=c99
# target_compile_features(wsstest PRIVATE c_std_99)
target_compile_options(wsstest PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(
    wsstest
    wayland-client
    wayland-protocols-client
    xcb
    xcb-util)
install(TARGETS wsstest)
