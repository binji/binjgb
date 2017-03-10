# Copyright (C) 2016 Ben Smith
#
# This software may be modified and distributed under the terms
# of the MIT license.  See the LICENSE file for details.
cmake_minimum_required(VERSION 2.8)
project(binjgb)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR}/cmake)

if (CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(CMAKE_INSTALL_PREFIX ${PROJECT_SOURCE_DIR} CACHE PATH "Install directory" FORCE)
endif ()

if (MSVC)
  add_definitions(-W3 -D_CRT_SECURE_NO_WARNINGS)
else ()
  add_definitions(
    -Wall -Wextra -Werror -Wpointer-arith -Wno-unused-parameter -g
    -Wno-unused-function
    -Wno-unused-variable
  )
endif ()

if (NOT EMSCRIPTEN)
  find_package(SDL2)
  find_package(OpenGL)

  if (SDL2_FOUND AND OPENGL_FOUND)
    add_executable(binjgb src/binjgb.c)
    target_link_libraries(binjgb SDL2::SDL2 SDL2::SDL2main ${OPENGL_gl_LIBRARY})

    add_executable(binjgb-debugger src/debugger.c src/options.c)
    target_link_libraries(binjgb-debugger SDL2::SDL2 SDL2::SDL2main ${OPENGL_gl_LIBRARY})
    install(TARGETS binjgb-debugger DESTINATION bin)

    install(TARGETS binjgb DESTINATION bin)
    if (SDL2_DYNAMIC)
      install(FILES ${SDL2_RUNTIME_LIBRARY} DESTINATION bin)
    endif ()
  endif ()

  add_executable(binjgb-tester src/tester.c src/options.c)
  target_compile_definitions(binjgb-tester PRIVATE NO_SDL)
  install(TARGETS binjgb-tester DESTINATION bin)
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
