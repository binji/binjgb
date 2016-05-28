cmake_minimum_required(VERSION 2.8)
add_definitions(
  -Wall -Wextra -Werror -Wpointer-arith -Wno-unused-parameter -g
)
add_executable(
  bgb
  main.c
)
