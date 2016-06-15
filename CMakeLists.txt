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
  -D_GNU_SOURCE # for qsort_r
)
include_directories(${SDL_INCLUDE_DIR})
add_executable(
  binjgb
  main.c
)
target_link_libraries(binjgb ${SDL_LIBRARY})
