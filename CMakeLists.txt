# Copyright (C) 2016 Ben Smith
#
# This software may be modified and distributed under the terms
# of the MIT license.  See the LICENSE file for details.
cmake_minimum_required(VERSION 2.8)
project(binjgb)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR}/cmake)

option(WASM "Build for WebAssembly" OFF)

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
    add_executable(binjgb
      src/common.c
      src/emulator.c
      src/host.c
      src/host-gl.c
      src/host-ui-simple.c
      src/binjgb.c
    )
    target_link_libraries(binjgb SDL2::SDL2 SDL2::SDL2main ${OPENGL_gl_LIBRARY})

    add_executable(binjgb-debugger
      third_party/imgui/imgui.cpp
      third_party/imgui/imgui_draw.cpp
      src/common.c
      src/options.c
      src/emulator-debug.c
      src/host.c
      src/host-gl.c
      src/host-ui-imgui.cc
      src/debugger.cc
    )
    set_property(TARGET binjgb-debugger PROPERTY CXX_STANDARD 11)
    set_property(TARGET binjgb-debugger PROPERTY CXX_STANDARD_REQUIRED ON)

    if (NOT MSVC)
      # Disable warnings in stb_truetype.h.
      set_source_files_properties(
        third_party/imgui/imgui_draw.cpp
        PROPERTIES
        COMPILE_FLAGS "-Wno-maybe-uninitialized"
      )
    endif ()

    target_include_directories(binjgb-debugger PUBLIC
        ${PROJECT_SOURCE_DIR}/third_party/imgui)
    target_compile_definitions(binjgb-debugger PUBLIC BINJGB_HOST_IMGUI)
    target_link_libraries(binjgb-debugger SDL2::SDL2 SDL2::SDL2main ${OPENGL_gl_LIBRARY})
    install(TARGETS binjgb-debugger DESTINATION bin)

    install(TARGETS binjgb DESTINATION bin)
    if (SDL2_DYNAMIC)
      install(FILES ${SDL2_RUNTIME_LIBRARY} DESTINATION bin)
    endif ()
  endif ()

  add_executable(binjgb-tester
    src/common.c
    src/options.c
    src/emulator.c
    src/tester.c
  )
  install(TARGETS binjgb-tester DESTINATION bin)
else (EMSCRIPTEN)
  add_executable(binjgb src/emulator.c src/emscripten/wrapper.c)
  set(EXPORTED_JSON ${PROJECT_SOURCE_DIR}/src/emscripten/exported.json)
  target_include_directories(binjgb PUBLIC ${PROJECT_SOURCE_DIR}/src)

  set(LINK_FLAGS
    --memory-init-file 0
    -s EXPORTED_FUNCTIONS=\"@${EXPORTED_JSON}\"
    -s NO_EXIT_RUNTIME=1
  )
  if (WASM)
    set(LINK_FLAGS ${LINK_FLAGS}
      -s WASM=1
      -s \"BINARYEN_TRAP_MODE='allow'\"
    )
  endif ()

  string(REPLACE ";" " " LINK_FLAGS_STR "${LINK_FLAGS}")

  set_target_properties(binjgb
    PROPERTIES
    LINK_FLAGS "${LINK_FLAGS_STR}"
    LINK_DEPENDS "${EXPORTED_JSON}"
  )
endif ()
