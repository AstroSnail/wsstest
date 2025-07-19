cmake_minimum_required(VERSION 3.10 FATAL_ERROR)

project(wsstest VERSION 0.1)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_EXTENSIONS OFF)

add_executable(wsstest main.c)
# target_compile_features(wsstest PRIVATE c_std_99)
target_compile_options(wsstest PRIVATE -Wall -Wextra -Wpedantic -Wc90-c99-compat -Wformat=2 -Wformat-overflow=2 -Wformat-signedness -Wformat-truncation=2 -Wshadow)
target_link_libraries(wsstest xcb)
install(TARGETS wsstest)
