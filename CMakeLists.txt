# Copyright (C) 2016 Ben Smith
#
# This software may be modified and distributed under the terms
# of the MIT license.  See the LICENSE file for details.
cmake_minimum_required(VERSION 2.8)
project(binjgb)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR}/cmake)

option(WERROR "Build with warnings as errors" OFF)
option(WASM "Build for WebAssembly" OFF)
option(RGBDS_LIVE "Build for rgbds-live (Wasm only)" OFF)

if (MSVC)
  add_definitions(-W3 -D_CRT_SECURE_NO_WARNINGS)
  if (WERROR)
    add_definitions(-WX)
  endif ()
else ()
  add_definitions(
    -Wall -Wextra -Wpointer-arith -Wno-unused-parameter -g
    -Wno-unused-function -Wno-unused-variable
    -Wno-implicit-fallthrough
  )
  if (WERROR)
    add_definitions(-Werror)
  endif ()
endif ()

function (target_copy_to_bin name)
add_custom_target(${name}-copy-to-bin ALL
  COMMAND ${CMAKE_COMMAND} -E make_directory ${PROJECT_SOURCE_DIR}/bin
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${name}> ${PROJECT_SOURCE_DIR}/bin
  DEPENDS ${name}
)
endfunction ()

if (NOT EMSCRIPTEN)
  find_package(SDL2)
  find_package(OpenGL)

  if (SDL2_FOUND AND OPENGL_FOUND)
    add_executable(binjgb
      src/memory.c
      src/common.c
      src/options.c
      src/emulator.c
      src/host.c
      src/host-gl.c
      src/host-ui-simple.c
      src/joypad.c
      src/rewind.c
      src/binjgb.c
    )
    target_link_libraries(binjgb SDL2::SDL2 SDL2::SDL2main ${OPENGL_gl_LIBRARY})

    install(TARGETS binjgb DESTINATION bin)
    target_copy_to_bin(binjgb)
    if (SDL2_DYNAMIC)
      install(FILES ${SDL2_RUNTIME_LIBRARY} DESTINATION bin)
    endif ()

    add_executable(binjgb-debugger
      third_party/imgui/imgui.cpp
      third_party/imgui/imgui_draw.cpp
      third_party/imgui/imgui_tables.cpp
      third_party/imgui/imgui_widgets.cpp
      src/memory.c
      src/common.c
      src/options.c
      src/emulator-debug.c
      src/host.c
      src/host-gl.c
      src/host-ui-imgui.cc
      src/joypad.c
      src/rewind.c
      src/debugger/main.cc
      src/debugger/debugger.cc
      src/debugger/audio-window.cc
      src/debugger/emulator-window.cc
      src/debugger/disassembly-window.cc
      src/debugger/io-window.cc
      src/debugger/map-window.cc
      src/debugger/memory-window.cc
      src/debugger/obj-window.cc
      src/debugger/rewind-window.cc
      src/debugger/rom-window.cc
      src/debugger/tiledata-window.cc
    )
    set_property(TARGET binjgb-debugger PROPERTY CXX_STANDARD 11)
    set_property(TARGET binjgb-debugger PROPERTY CXX_STANDARD_REQUIRED ON)

    if (CMAKE_COMPILER_IS_GNUCXX)
      if (CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 8)
        set(IMGUI_EXTRA_FLAGS "-Wno-stringop-truncation -Wno-class-memaccess")
      endif ()
      set_source_files_properties(
        third_party/imgui/imgui.cpp
        third_party/imgui/imgui_draw.cpp
        third_party/imgui/imgui_tables.cpp
        third_party/imgui/imgui_widgets.cpp
        PROPERTIES
        COMPILE_FLAGS "${IMGUI_EXTRA_FLAGS} -Wno-extra -Wno-unused-but-set-variable  -Wno-maybe-uninitialized"
      )
      # Turn off warnings as errors, imgui has too many.
      add_definitions(-Wno-error)
    endif ()

    target_include_directories(binjgb-debugger PUBLIC
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/third_party/imgui
        ${PROJECT_SOURCE_DIR}/third_party/imgui_memory_editor)
    target_compile_definitions(binjgb-debugger PUBLIC BINJGB_HOST_IMGUI)
    target_link_libraries(binjgb-debugger SDL2::SDL2 SDL2::SDL2main ${OPENGL_gl_LIBRARY})
    install(TARGETS binjgb-debugger DESTINATION bin)
    target_copy_to_bin(binjgb-debugger)
  endif ()

  add_executable(binjgb-tester
    src/memory.c
    src/common.c
    src/options.c
    src/emulator.c
    src/joypad.c
    src/tester.c
  )
  install(TARGETS binjgb-tester DESTINATION bin)
  target_copy_to_bin(binjgb-tester)

  add_executable(binjgb-tester-debug
    src/memory.c
    src/common.c
    src/options.c
    src/emulator-debug.c
    src/joypad.c
    src/tester.c
  )
  target_compile_definitions(binjgb-tester-debug PUBLIC TESTER_DEBUGGER)
  install(TARGETS binjgb-tester-debug DESTINATION bin)
  target_copy_to_bin(binjgb-tester-debug)
else (EMSCRIPTEN)
  add_executable(binjgb
    src/memory.c
    src/emulator.c
    src/joypad.c
    src/rewind.c
    src/emscripten/wrapper.c)
  set(EXPORTED_JSON ${PROJECT_SOURCE_DIR}/src/emscripten/exported.json)
  target_include_directories(binjgb PUBLIC ${PROJECT_SOURCE_DIR}/src)

  if (RGBDS_LIVE)
    target_compile_definitions(binjgb PUBLIC RGBDS_LIVE)
  endif ()

  set(LINK_FLAGS
    --memory-init-file 0
    -s EXPORTED_FUNCTIONS=\"@${EXPORTED_JSON}\"
    -s MALLOC=emmalloc
    -s ASSERTIONS=0
    -s ENVIRONMENT=web
    -s FILESYSTEM=0
    -s EXIT_RUNTIME=0
    -s MODULARIZE=1
    -s EXPORT_NAME="Binjgb"
  )
  if (WASM)
    set(LINK_FLAGS ${LINK_FLAGS} -s WASM=1)
  else ()
    set(LINK_FLAGS ${LINK_FLAGS} -s WASM=0)
  endif ()

  string(REPLACE ";" " " LINK_FLAGS_STR "${LINK_FLAGS}")

  set_target_properties(binjgb
    PROPERTIES
    LINK_FLAGS "${LINK_FLAGS_STR}"
    LINK_DEPENDS "${EXPORTED_JSON}"
  )
endif ()
