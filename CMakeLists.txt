cmake_minimum_required(VERSION 2.8)
find_package(SDL)
add_definitions(
  -Wall -Wextra -Werror -Wpointer-arith -Wno-unused-parameter -g
  -D_GNU_SOURCE # for qsort_r
)
include_directories(${SDL_INCLUDE_DIR})
add_executable(
  bgb
  main.c
)
target_link_libraries(bgb ${SDL_LIBRARY})
