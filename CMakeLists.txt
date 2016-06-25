# Copyright (C) 2016 Ben Smith
#
# This software may be modified and distributed under the terms
# of the MIT license.  See the LICENSE file for details.
cmake_minimum_required(VERSION 2.8)
find_package(SDL)
add_definitions(
  -Wall -Wextra -Werror -Wpointer-arith -Wno-unused-parameter -g
  -Wno-unused-function
  -Wno-unused-variable
)
add_executable(binjgb binjgb.c)
target_include_directories(binjgb PUBLIC ${SDL_INCLUDE_DIR})
target_link_libraries(binjgb ${SDL_LIBRARY})
add_executable(tester tester.c)
target_compile_definitions(tester PRIVATE NO_SDL)
