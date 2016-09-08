# Copyright (C) 2016 Ben Smith
#
# This software may be modified and distributed under the terms
# of the MIT license.  See the LICENSE file for details.
cmake_minimum_required(VERSION 2.8)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR}/cmake)

add_definitions(
  -Wall -Wextra -Werror -Wpointer-arith -Wno-unused-parameter -g
  -Wno-unused-function
  -Wno-unused-variable
)

if (NOT EMSCRIPTEN)
  find_package(SDL2)
  add_executable(binjgb src/binjgb.c)
  target_include_directories(binjgb PUBLIC ${SDL2_INCLUDE_DIR})
  target_link_libraries(binjgb ${SDL2_LIBRARY})
  add_executable(tester src/tester.c)
  target_compile_definitions(tester PRIVATE NO_SDL)
else (EMSCRIPTEN)
  add_executable(binjgb src/emscripten/main.c)
  set(PRE_JS ${PROJECT_SOURCE_DIR}/src/emscripten/pre.js)
  set(EXPORTED_JSON ${PROJECT_SOURCE_DIR}/src/emscripten/exported.json)
  target_include_directories(binjgb PUBLIC ${PROJECT_SOURCE_DIR}/src)

  set(LINK_FLAGS
    --pre-js ${PRE_JS}
    --memory-init-file 0
    -s EXPORTED_FUNCTIONS=\"@${EXPORTED_JSON}\"
    -s RESERVED_FUNCTION_POINTERS=10
    -s NO_EXIT_RUNTIME=1
  )
  string(REPLACE ";" " " LINK_FLAGS_STR "${LINK_FLAGS}")

  set_target_properties(binjgb
    PROPERTIES
    LINK_FLAGS "${LINK_FLAGS_STR}"
    LINK_DEPENDS "${PRE_JS};${EXPORTED_JSON}"
  )
endif ()
