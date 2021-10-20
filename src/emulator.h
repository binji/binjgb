/*
 * Copyright (C) 2017 Ben Smith
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#ifndef BINJGB_EMULATOR_H_
#define BINJGB_EMULATOR_H_

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SCREEN_WIDTH 160
#define SCREEN_HEIGHT 144
#define SCREEN_HEIGHT_WITH_VBLANK 154
#define SGB_SCREEN_WIDTH 256
#define SGB_SCREEN_HEIGHT 224
#define SGB_SCREEN_LEFT ((SGB_SCREEN_WIDTH - SCREEN_WIDTH) / 2)
#define SGB_SCREEN_RIGHT ((SGB_SCREEN_WIDTH + SCREEN_WIDTH) / 2)
#define SGB_SCREEN_TOP ((SGB_SCREEN_HEIGHT - SCREEN_HEIGHT) / 2)
#define SGB_SCREEN_BOTTOM ((SGB_SCREEN_HEIGHT + SCREEN_HEIGHT) / 2)

#define CPU_TICKS_PER_SECOND 4194304
#define APU_TICKS_PER_SECOND 2097152
#define PPU_LINE_TICKS 456
#define PPU_VBLANK_TICKS (PPU_LINE_TICKS * 10)
#define PPU_FRAME_TICKS (PPU_LINE_TICKS * SCREEN_HEIGHT_WITH_VBLANK)

#define SOUND_OUTPUT_COUNT 2
#define PALETTE_COLOR_COUNT 4
#define OBJ_COUNT 40

#define OBJ_X_OFFSET 8
#define OBJ_Y_OFFSET 16

#define BUILTIN_PALETTE_COUNT 84

#define RGBA_WHITE 0xffffffffu
#define RGBA_LIGHT_GRAY 0xffaaaaaau
#define RGBA_DARK_GRAY 0xff555555u
#define RGBA_BLACK 0xff000000u

typedef struct Emulator Emulator;

enum {
  APU_CHANNEL1,
  APU_CHANNEL2,
  APU_CHANNEL3,
  APU_CHANNEL4,
  APU_CHANNEL_COUNT,
};

typedef void (*JoypadCallback)(struct JoypadButtons* joyp, void* user_data);

typedef struct JoypadCallbackInfo {
  JoypadCallback callback;
  void* user_data;
} JoypadCallbackInfo;

typedef RGBA FrameBuffer[SCREEN_WIDTH * SCREEN_HEIGHT];
typedef RGBA SgbFrameBuffer[SGB_SCREEN_WIDTH * SGB_SCREEN_HEIGHT];

typedef enum Color {
  COLOR_WHITE = 0,
  COLOR_LIGHT_GRAY = 1,
  COLOR_DARK_GRAY = 2,
  COLOR_BLACK = 3,
} Color;

typedef enum PaletteType {
  PALETTE_TYPE_BGP,
  PALETTE_TYPE_OBP0,
  PALETTE_TYPE_OBP1,
  PALETTE_TYPE_COUNT,
} PaletteType;

typedef enum {
  TILE_DATA_8800_97FF = 0,
  TILE_DATA_8000_8FFF = 1,
} TileDataSelect;

typedef enum TileMapSelect {
  TILE_MAP_9800_9BFF = 0,
  TILE_MAP_9C00_9FFF = 1,
} TileMapSelect;

typedef enum {
  OBJ_SIZE_8X8 = 0,
  OBJ_SIZE_8X16 = 1,
} ObjSize;

typedef enum ObjPriority {
  OBJ_PRIORITY_ABOVE_BG = 0,
  OBJ_PRIORITY_BEHIND_BG = 1,
} ObjPriority;

typedef enum {
  TIMER_CLOCK_4096_HZ = 0,
  TIMER_CLOCK_262144_HZ = 1,
  TIMER_CLOCK_65536_HZ = 2,
  TIMER_CLOCK_16384_HZ = 3,
} TimerClock;

/* TODO(binji): endianness */
#define REGISTER_PAIR(X, Y) \
  union {                   \
    struct { u8 Y; u8 X; }; \
    u16 X##Y;               \
  }

typedef struct {
  u8 A;
  REGISTER_PAIR(B, C);
  REGISTER_PAIR(D, E);
  REGISTER_PAIR(H, L);
  u16 SP;
  u16 PC;
  struct { Bool Z, N, H, C; } F;
} Registers;

typedef struct Obj {
  u8 y;
  u8 x;
  u8 tile;
  u8 byte3;
  ObjPriority priority;
  Bool yflip;
  Bool xflip;
  u8 palette;
  u8 bank;
  u8 cgb_palette;
} Obj;

typedef struct { Color color[PALETTE_COLOR_COUNT]; } Palette;

typedef struct PaletteRGBA {
  RGBA color[PALETTE_COLOR_COUNT];
} PaletteRGBA;

typedef struct AudioBuffer {
  u32 frequency;    /* Sample frequency, as N samples per second */
  u32 freq_counter; /* Used for resampling; [0..APU_TICKS_PER_SECOND). */
  u32 divisor;
  u32 frames; /* Number of frames to generate per call to emulator_run. */
  u8* data;   /* Unsigned 8-bit 2-channel samples @ |frequency| */
  u8* end;
  u8* position;
} AudioBuffer;

typedef enum CgbColorCurve {
  CGB_COLOR_CURVE_NONE,
  CGB_COLOR_CURVE_SAMEBOY_EMULATE_HARDWARE,
  CGB_COLOR_CURVE_GAMBATTE,
} CgbColorCurve;

typedef struct EmulatorInit {
  FileData rom;
  int audio_frequency;
  int audio_frames;
  u32 random_seed;
  u32 builtin_palette;
  Bool force_dmg;
  CgbColorCurve cgb_color_curve;
} EmulatorInit;

typedef struct EmulatorConfig {
  Bool disable_sound[APU_CHANNEL_COUNT];
  Bool disable_bg;
  Bool disable_window;
  Bool disable_obj;
  Bool allow_simulataneous_dpad_opposites;
} EmulatorConfig;

typedef u32 EmulatorEvent;
enum {
  EMULATOR_EVENT_NEW_FRAME = 0x1,
  EMULATOR_EVENT_AUDIO_BUFFER_FULL = 0x2,
  EMULATOR_EVENT_UNTIL_TICKS = 0x4,
  EMULATOR_EVENT_BREAKPOINT = 0x8,
  EMULATOR_EVENT_INVALID_OPCODE = 0x10,
};

extern const size_t s_emulator_state_size;

Emulator* emulator_new(const EmulatorInit*);
void emulator_delete(Emulator*);

void emulator_set_joypad_buttons(Emulator*, JoypadButtons*);
void emulator_set_joypad_callback(Emulator*, JoypadCallback, void* user_data);
JoypadCallbackInfo emulator_get_joypad_callback(Emulator*);
void emulator_set_config(Emulator*, const EmulatorConfig*);
EmulatorConfig emulator_get_config(Emulator*);
FrameBuffer* emulator_get_frame_buffer(Emulator*);
SgbFrameBuffer* emulator_get_sgb_frame_buffer(Emulator*);
AudioBuffer* emulator_get_audio_buffer(Emulator*);
Ticks emulator_get_ticks(Emulator*);
u32 emulator_get_ppu_frame(Emulator*);
u32 audio_buffer_get_frames(AudioBuffer*);
void emulator_set_builtin_palette(Emulator*, u32 index);
void emulator_set_bw_palette(Emulator*, PaletteType, const PaletteRGBA*);
void emulator_set_all_bw_palettes(Emulator*, const PaletteRGBA*);

void emulator_ticks_to_time(Ticks, u32* day, u32* hr, u32* min, u32* sec,
                            u32* ms);

Bool emulator_was_ext_ram_updated(Emulator*);

void emulator_init_state_file_data(FileData*);
void emulator_init_ext_ram_file_data(Emulator*, FileData*);
Result emulator_read_state(Emulator*, const FileData*);
Result emulator_write_state(Emulator*, FileData*);
Result emulator_read_ext_ram(Emulator*, const FileData*);
Result emulator_write_ext_ram(Emulator*, FileData*);

Result emulator_read_state_from_file(Emulator*, const char* filename);
Result emulator_write_state_to_file(Emulator*, const char* filename);
Result emulator_read_ext_ram_from_file(Emulator*, const char* filename);
Result emulator_write_ext_ram_to_file(Emulator*, const char* filename);

EmulatorEvent emulator_step(Emulator*);
EmulatorEvent emulator_run_until(Emulator*, Ticks until_ticks);

#ifdef __cplusplus
}
#endif

#endif /* BINJGB_EMULATOR_H_ */
