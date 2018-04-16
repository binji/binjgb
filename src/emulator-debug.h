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

typedef enum SetLogLevelError {
  SET_LOG_LEVEL_ERROR_NONE = 0,
  SET_LOG_LEVEL_ERROR_INVALID_FORMAT = 1,
  SET_LOG_LEVEL_ERROR_UNKNOWN_LOG_SYSTEM = 2,
} SetLogLevelError;

typedef enum PaletteType {
  PALETTE_TYPE_BGP,
  PALETTE_TYPE_OBP0,
  PALETTE_TYPE_OBP1,
} PaletteType;

typedef enum CgbPaletteType {
  CGB_PALETTE_TYPE_BGCP,
  CGB_PALETTE_TYPE_OBCP,
} CgbPaletteType;

typedef enum LayerType {
  LAYER_TYPE_BG,
  LAYER_TYPE_WINDOW,
} LayerType;

typedef enum {
  ROM_USAGE_CODE = 1,
  ROM_USAGE_DATA = 2,
  ROM_USAGE_CODE_START = 4, /* Start of an opcode. */
} RomUsage;

struct Emulator;

#define TILE_DATA_TEXTURE_WIDTH 256
#define TILE_DATA_TEXTURE_HEIGHT 192
typedef u8 TileData[TILE_DATA_TEXTURE_WIDTH * TILE_DATA_TEXTURE_HEIGHT];

#define TILE_MAP_WIDTH 32
#define TILE_MAP_HEIGHT 32
#define TILE_MAP_SIZE (TILE_MAP_WIDTH * TILE_MAP_HEIGHT)
typedef u8 TileMap[TILE_MAP_SIZE];

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
SetLogLevelError emulator_set_log_level_from_string(const char*);
Bool emulator_get_trace();
void emulator_set_trace(Bool trace);
void emulator_push_trace(Bool trace);
void emulator_pop_trace();
const char* emulator_get_log_system_name(LogSystem);
LogLevel emulator_get_log_level(LogSystem);
void emulator_print_log_systems();

Bool emulator_is_cgb(struct Emulator*);

int emulator_get_rom_size(struct Emulator*);
u8* emulator_get_rom_usage(struct Emulator*);
void emulator_clear_rom_usage(struct Emulator* e);

int emulator_disassemble(struct Emulator*, Address, char* buffer, size_t size);
Registers emulator_get_registers(struct Emulator*);

int emulator_get_rom_bank(struct Emulator*, int region);

u8 emulator_read_u8_raw(struct Emulator*, Address);
void emulator_write_u8_raw(struct Emulator*, Address, u8);

TileDataSelect emulator_get_tile_data_select(struct Emulator*);
TileMapSelect emulator_get_tile_map_select(struct Emulator*, LayerType);
Palette emulator_get_palette(struct Emulator*, PaletteType);
PaletteRGBA emulator_get_palette_rgba(struct Emulator*, PaletteType);
PaletteRGBA emulator_get_cgb_palette_rgba(struct Emulator*, CgbPaletteType,
                                          int index);
void emulator_get_tile_data(struct Emulator*, TileData);
void emulator_get_tile_map(struct Emulator*, TileMapSelect, TileMap);
void emulator_get_bg_scroll(struct Emulator*, u8* x, u8* y);
void emulator_get_window_scroll(struct Emulator*, u8* x, u8* y);

Bool emulator_get_display(struct Emulator*);
Bool emulator_get_bg_display(struct Emulator*);
Bool emulator_get_window_display(struct Emulator*);
Bool emulator_get_obj_display(struct Emulator*);

ObjSize emulator_get_obj_size(struct Emulator*);
Obj emulator_get_obj(struct Emulator*, int index);
Bool obj_is_visible(const Obj* obj);

RGBA color_to_rgba(Color color);
PaletteRGBA palette_to_palette_rgba(Palette palette);

int opcode_bytes(u8 opcode);

#ifdef __cplusplus
}
#endif

#endif /* BINJGB_EMULATOR_DEBUG_H_ */
