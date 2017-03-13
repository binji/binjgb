/*
 * Copyright (C) 2017 Ben Smith
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#ifndef BINJGB_EMULATOR_H_
#define BINJGB_EMULATOR_H_

#include <stdint.h>

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SCREEN_WIDTH 160
#define SCREEN_HEIGHT 144
#define SCREEN_HEIGHT_WITH_VBLANK 154

#define CPU_CYCLES_PER_SECOND 4194304
#define APU_CYCLES_PER_SECOND 2097152
#define PPU_LINE_CYCLES 456
#define PPU_VBLANK_CYCLES (PPU_LINE_CYCLES * 10)
#define PPU_FRAME_CYCLES (PPU_LINE_CYCLES * SCREEN_HEIGHT_WITH_VBLANK)

#define MILLISECONDS_PER_SECOND 1000
#define VIDEO_FRAME_MS \
  ((f64)MILLISECONDS_PER_SECOND * PPU_FRAME_CYCLES / CPU_CYCLES_PER_SECOND)

#define SOUND_OUTPUT_COUNT 2

struct Emulator;

enum {
  CHANNEL1,
  CHANNEL2,
  CHANNEL3,
  CHANNEL4,
  CHANNEL_COUNT,
};

typedef struct JoypadButtons {
  Bool down, up, left, right;
  Bool start, select, B, A;
} JoypadButtons;

typedef void (*JoypadCallback)(struct JoypadButtons* joyp, void* user_data);

typedef RGBA FrameBuffer[SCREEN_WIDTH * SCREEN_HEIGHT];

typedef struct AudioBuffer {
  u32 frequency;    /* Sample frequency, as N samples per second */
  u32 freq_counter; /* Used for resampling; [0..APU_CYCLES_PER_SECOND). */
  u32 divisor;
  u8* data; /* Unsigned 8-bit 2-channel samples @ |frequency| */
  u8* end;
  u8* position;
} AudioBuffer;

typedef struct EmulatorInit {
  const char* rom_filename;
  int audio_frequency;
  int audio_frames;
} EmulatorInit;

typedef struct EmulatorConfig {
  Bool disable_sound[CHANNEL_COUNT];
  Bool disable_bg;
  Bool disable_window;
  Bool disable_obj;
  Bool allow_simulataneous_dpad_opposites;
} EmulatorConfig;

typedef u32 EmulatorEvent;
enum {
  EMULATOR_EVENT_NEW_FRAME = 0x1,
  EMULATOR_EVENT_AUDIO_BUFFER_FULL = 0x2,
};

const char* replace_extension(const char* filename, const char* extension);

struct Emulator* emulator_new(const EmulatorInit*);
void emulator_delete(struct Emulator*);

void emulator_set_joypad_buttons(struct Emulator*, JoypadButtons*);
void emulator_set_joypad_callback(struct Emulator*, JoypadCallback,
                                  void* user_data);
void emulator_set_config(struct Emulator*, const EmulatorConfig*);
EmulatorConfig emulator_get_config(struct Emulator*);
FrameBuffer* emulator_get_frame_buffer(struct Emulator*);
AudioBuffer* emulator_get_audio_buffer(struct Emulator*);
u32 emulator_get_cycles(struct Emulator*);
u32 emulator_get_ppu_frame(struct Emulator*);
u32 audio_buffer_get_frames(AudioBuffer*);

Result emulator_read_state_from_file(struct Emulator*, const char* filename);
Result emulator_read_ext_ram_from_file(struct Emulator*, const char* filename);
Result emulator_write_state_to_file(struct Emulator*, const char* filename);
Result emulator_write_ext_ram_to_file(struct Emulator*, const char* filename);

void emulator_step(struct Emulator*);
EmulatorEvent emulator_run(struct Emulator*, u32 max_audio_frames);

#ifdef __cplusplus
}
#endif

#endif /* BINJGB_EMULATOR_H_ */
