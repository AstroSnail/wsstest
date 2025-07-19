cmake_minimum_required(VERSION 3.21 FATAL_ERROR)

project(wsstest VERSION 0.1)

set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(wsstest main.c)
# require n3054 § 6.7.3/10 const array type semantics
# (n3054 is just the c23 draft i have at hand)
# would really really really like a way to disable only that warning
# (it only comes bundled in -Wpedantic [<=c11] and -Wc11-c23-compat [>=c23])
# project is otherwise pmuch c90 compatible with posix.1-2008 libraries
# (string.h strsignal() in particular)
target_compile_features(wsstest PRIVATE c_std_23)
target_compile_options(wsstest PRIVATE -Wall -Wextra -Wpedantic -Wc++-compat -Wc90-c99-compat -Wc99-c11-compat -Wc11-c23-compat)
target_link_libraries(wsstest xcb)
install(TARGETS wsstest)

install(FILES build/compile_commands.json DESTINATION .)
