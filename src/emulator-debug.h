/*
 * Copyright (C) 2017 Ben Smith
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#ifndef BINJGB_EMULATOR_DEBUG_H_
#define BINJGB_EMULATOR_DEBUG_H_

#include "common.h"
#include "emulator.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FOREACH_LOG_SYSTEM(V) \
  V(A, apu, APU)              \
  V(H, host, HOST)            \
  V(I, io, IO)                \
  V(N, interrupt, INTERRUPT)  \
  V(M, memory, MEMORY)        \
  V(P, ppu, PPU)

#define V(SHORT_NAME, name, NAME) \
  LOG_SYSTEM_##NAME, LOG_SYSTEM_##SHORT_NAME = LOG_SYSTEM_##NAME,
typedef enum LogSystem {
  FOREACH_LOG_SYSTEM(V)
  NUM_LOG_SYSTEMS,
} LogSystem;
#undef V

typedef enum LogLevel {
  LOG_LEVEL_QUIET = 0,
  LOG_LEVEL_INFO = 1,
  LOG_LEVEL_DEBUG = 2,
  LOG_LEVEL_VERBOSE = 3,
} LogLevel;

typedef enum PaletteType {
  PALETTE_TYPE_BGP,
  PALETTE_TYPE_OBP0,
  PALETTE_TYPE_OBP1,
} PaletteType;

struct Emulator;

#define TILE_DATA_TEXTURE_WIDTH 128
#define TILE_DATA_TEXTURE_HEIGHT 192
#define TILE_DATA_TEXTURE_SIZE \
  (TILE_DATA_TEXTURE_WIDTH * TILE_DATA_TEXTURE_HEIGHT)
typedef RGBA TileDataBuffer[TILE_DATA_TEXTURE_SIZE];

#if 0
typedef struct EmulatorHookContext {
  struct Emulator* e;
  void* user_data;
} EmulatorHookContext;

typedef struct EmulatorHooks {
  void* user_data;
  void (*message)(LogSystem, LogLevel, const char* message);
} EmulatorHooks;

void emulator_set_hooks(struct Emulator*, EmulatorHooks*);
#endif

void emulator_set_log_level(LogSystem, LogLevel);
void emulator_set_trace(Bool trace);
const char* emulator_get_log_system_name(LogSystem);
LogLevel emulator_get_log_level(LogSystem);

Palette emulator_get_palette(struct Emulator*, PaletteType);
void emulator_get_tile_data_buffer(struct Emulator*, Palette, TileDataBuffer);

#ifdef __cplusplus
}
#endif

#endif /* BINJGB_EMULATOR_DEBUG_H_ */
