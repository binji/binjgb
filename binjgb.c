/*
 * Copyright (C) 2016 Ben Smith
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include <assert.h>
#include <inttypes.h>
#include <sched.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

#define SUCCESS(x) ((x) == OK)
#define FAIL(x) ((x) != OK)

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define ZERO_MEMORY(x) memset(&(x), 0, sizeof(x))
#define MIN(x, y) ((x) < (y) ? (x) : (y))
#define MAX(x, y) ((x) > (y) ? (x) : (y))

#ifndef NDEBUG
#define LOG(...) fprintf(stdout, __VA_ARGS__)
#define LOG_IF(cond, ...) do if (cond) LOG(__VA_ARGS__); while(0)
#else
#define LOG(...)
#define LOG_IF(cond, ...)
#endif

#define LOG_LEVEL_IS(system, value) (s_log_level_##system >= (value))
#define INFO(system, ...) LOG_IF(LOG_LEVEL_IS(system, 1), __VA_ARGS__)
#define DEBUG(system, ...) LOG_IF(LOG_LEVEL_IS(system, 2), __VA_ARGS__)
#define VERBOSE(system, ...) LOG_IF(LOG_LEVEL_IS(system, 3), __VA_ARGS__)

#define PRINT_ERROR(...) fprintf(stderr, __VA_ARGS__)
#define CHECK_MSG(x, ...)                       \
  if (!(x)) {                                   \
    PRINT_ERROR("%s:%d: ", __FILE__, __LINE__); \
    PRINT_ERROR(__VA_ARGS__);                   \
    goto error;                                 \
  }
#define CHECK(x) \
  if (!(x)) {    \
    goto error;  \
  }

#define UNREACHABLE(...)      \
  do {                        \
    PRINT_ERROR(__VA_ARGS__); \
    exit(1);                  \
  } while (0)

#define VALUE_WRAPPED(X, MAX) ((X) >= (MAX) ? ((X) -= (MAX), TRUE) : FALSE)

typedef uint16_t Address;
typedef uint16_t MaskedAddress;
typedef uint32_t RGBA;

/* Configurable constants */
#define RGBA_WHITE 0xffffffffu
#define RGBA_LIGHT_GRAY 0xffaaaaaau
#define RGBA_DARK_GRAY 0xff555555u
#define RGBA_BLACK 0xff000000u
/* Run this many instructions before checking for timeout. */
#define EMULATOR_INSTRUCTION_QUANTA 2000

/* ROM header stuff */
#define ROM_U8(type, addr) ((type) * (rom_data->data + addr))
#define ROM_U16_BE(addr) \
  ((uint16_t)((ROM_U8(uint16_t, addr) << 8) | ROM_U8(uint16_t, addr + 1)))

#define ENTRY_POINT_START_ADDR 0x100
#define ENTRY_POINT_END_ADDR 0x103
#define LOGO_START_ADDR 0x104
#define LOGO_END_ADDR 0x133
#define TITLE_START_ADDR 0x134
#define TITLE_END_ADDR 0x143
#define CGB_FLAG_ADDR 0x143
#define SGB_FLAG_ADDR 0x146
#define CARTRIDGE_TYPE_ADDR 0x147
#define ROM_SIZE_ADDR 0x148
#define EXT_RAM_SIZE_ADDR 0x149
#define HEADER_CHECKSUM_ADDR 0x14d
#define GLOBAL_CHECKSUM_START_ADDR 0x14e
#define GLOBAL_CHECKSUM_END_ADDR 0x14f

#define HEADER_CHECKSUM_RANGE_START 0x134
#define HEADER_CHECKSUM_RANGE_END 0x14c

/* Sizes */
#define MINIMUM_ROM_SIZE 32768
#define VIDEO_RAM_SIZE 8192
#define WORK_RAM_MAX_SIZE 32768
#define EXT_RAM_MAX_SIZE 32768
#define WAVE_RAM_SIZE 16
#define HIGH_RAM_SIZE 127
#define ROM_BANK_SHIFT 14
#define EXT_RAM_BANK_SHIFT 13

/* Cycle counts */
#define MILLISECONDS_PER_SECOND 1000
#define CPU_CYCLES_PER_SECOND 4194304
#define CPU_MCYCLE 4
#define APU_CYCLES_PER_SECOND (CPU_CYCLES_PER_SECOND / APU_CYCLES)
#define APU_CYCLES 2 /* APU runs at 2MHz */
#define PPU_MODE2_CYCLES 80
#define PPU_MODE3_CYCLES 172  /* TODO: This and HBLANK are not */
#define PPU_HBLANK_CYCLES 204 /* correct, they need to vary based on */
#define PPU_LINE_CYCLES 456   /* SCX, window, sprites, etc. */
#define PPU_VBLANK_CYCLES (PPU_LINE_CYCLES * 10)
#define PPU_FRAME_CYCLES (PPU_LINE_CYCLES * SCREEN_HEIGHT_WITH_VBLANK)
#define PPU_ENABLE_DISPLAY_DELAY_FRAMES 4
#define DMA_CYCLES 648
#define DMA_DELAY_CYCLES 8
#define SERIAL_CYCLES (CPU_CYCLES_PER_SECOND / 8192)

/* Memory map */
#define ADDR_MASK_1K 0x03ff
#define ADDR_MASK_4K 0x0fff
#define ADDR_MASK_8K 0x1fff
#define ADDR_MASK_16K 0x3fff
#define ADDR_MASK_32K 0x7fff

#define MBC_RAM_ENABLED_MASK 0xf
#define MBC_RAM_ENABLED_VALUE 0xa
#define MBC1_ROM_BANK_LO_SELECT_MASK 0x1f
#define MBC1_BANK_HI_SELECT_MASK 0x3
#define MBC1_BANK_HI_SHIFT 5
/* MBC2 has built-in RAM, 512 4-bit values. It's not external, but it maps to
 * the same address space. */
#define MBC2_RAM_SIZE 0x200
#define MBC2_RAM_ADDR_MASK 0x1ff
#define MBC2_RAM_VALUE_MASK 0xf
#define MBC2_ADDR_SELECT_BIT_MASK 0x100
#define MBC2_ROM_BANK_SELECT_MASK 0xf
#define MBC3_ROM_BANK_SELECT_MASK 0x7f
#define MBC3_RAM_BANK_SELECT_MASK 0x7

#define OAM_START_ADDR 0xfe00
#define OAM_END_ADDR 0xfe9f
#define UNUSED_START_ADDR 0xfea0
#define UNUSED_END_ADDR 0xfeff
#define IO_START_ADDR 0xff00
#define APU_START_ADDR 0xff10
#define APU_END_ADDR 0xff2f
#define WAVE_RAM_START_ADDR 0xff30
#define WAVE_RAM_END_ADDR 0xff3f
#define IO_END_ADDR 0xff7f
#define HIGH_RAM_START_ADDR 0xff80
#define HIGH_RAM_END_ADDR 0xfffe

#define OAM_TRANSFER_SIZE (OAM_END_ADDR - OAM_START_ADDR + 1)

/* Video */
#define SCREEN_WIDTH 160
#define SCREEN_HEIGHT 144
#define SCREEN_HEIGHT_WITH_VBLANK 154
#define TILE_COUNT (256 + 256) /* Actually 256+128, but we mirror the middle. */
#define TILE_WIDTH 8
#define TILE_HEIGHT 8
#define TILE_MAP_COUNT 2
#define TILE_MAP_WIDTH 32
#define TILE_MAP_HEIGHT 32

#define WINDOW_MAX_X 166
#define WINDOW_MAX_Y 143
#define WINDOW_X_OFFSET 7

#define OBJ_COUNT 40
#define OBJ_PER_LINE_COUNT 10
#define OBJ_PALETTE_COUNT 2
#define OBJ_Y_OFFSET 16
#define OBJ_X_OFFSET 8

#define PALETTE_COLOR_COUNT 4

/* Audio */
#define NRX1_MAX_LENGTH 64
#define NR31_MAX_LENGTH 256
#define SWEEP_MAX_PERIOD 8
#define SOUND_MAX_FREQUENCY 2047
#define WAVE_SAMPLE_COUNT 32
#define NOISE_MAX_CLOCK_SHIFT 13
#define ENVELOPE_MAX_PERIOD 8
#define ENVELOPE_MAX_VOLUME 15
#define DUTY_CYCLE_COUNT 8
#define SOUND_OUTPUT_COUNT 2
#define SO1_MAX_VOLUME 7
#define SO2_MAX_VOLUME 7
/* Additional samples so the AudioBuffer doesn't overflow. This could happen
 * because the audio buffer is updated at the granularity of an instruction, so
 * the most extra samples that could be added is equal to the APU cycle count
 * of the slowest instruction. */
#define AUDIO_BUFFER_EXTRA_CHANNEL_SAMPLES 256

/* TODO hack to make dmg_sound-2 tests pass. */
#define WAVE_SAMPLE_TRIGGER_OFFSET_CYCLES 2
#define WAVE_SAMPLE_READ_OFFSET_CYCLES 0
#define WAVE_SAMPLE_WRITE_OFFSET_CYCLES 0

#define FRAME_SEQUENCER_COUNT 8
#define FRAME_SEQUENCER_CYCLES 8192 /* 512Hz */
#define FRAME_SEQUENCER_UPDATE_ENVELOPE_FRAME 7

/* Addresses are relative to IO_START_ADDR (0xff00). */
#define FOREACH_IO_REG(V)                     \
  V(JOYP, 0x00) /* Joypad */                  \
  V(SB, 0x01)   /* Serial transfer data */    \
  V(SC, 0x02)   /* Serial transfer control */ \
  V(DIV, 0x04)  /* Divider */                 \
  V(TIMA, 0x05) /* Timer counter */           \
  V(TMA, 0x06)  /* Timer modulo */            \
  V(TAC, 0x07)  /* Timer control */           \
  V(IF, 0x0f)   /* Interrupt request */       \
  V(LCDC, 0x40) /* LCD control */             \
  V(STAT, 0x41) /* LCD status */              \
  V(SCY, 0x42)  /* Screen Y */                \
  V(SCX, 0x43)  /* Screen X */                \
  V(LY, 0x44)   /* Y Line */                  \
  V(LYC, 0x45)  /* Y Line compare */          \
  V(DMA, 0x46)  /* DMA transfer to OAM */     \
  V(BGP, 0x47)  /* BG palette */              \
  V(OBP0, 0x48) /* OBJ palette 0 */           \
  V(OBP1, 0x49) /* OBJ palette 1 */           \
  V(WY, 0x4a)   /* Window Y */                \
  V(WX, 0x4b)   /* Window X */                \
  V(IE, 0xff)   /* Interrupt enable */

/* Addresses are relative to APU_START_ADDR (0xff10). */
#define FOREACH_APU_REG(V)                                   \
  V(NR10, 0x0)  /* Channel 1 sweep */                        \
  V(NR11, 0x1)  /* Channel 1 sound length/wave pattern */    \
  V(NR12, 0x2)  /* Channel 1 volume envelope */              \
  V(NR13, 0x3)  /* Channel 1 frequency lo */                 \
  V(NR14, 0x4)  /* Channel 1 frequency hi */                 \
  V(NR21, 0x6)  /* Channel 2 sound length/wave pattern */    \
  V(NR22, 0x7)  /* Channel 2 volume envelope */              \
  V(NR23, 0x8)  /* Channel 2 frequency lo */                 \
  V(NR24, 0x9)  /* Channel 2 frequency hi */                 \
  V(NR30, 0xa)  /* Channel 3 DAC enabled */                  \
  V(NR31, 0xb)  /* Channel 3 sound length */                 \
  V(NR32, 0xc)  /* Channel 3 select output level */          \
  V(NR33, 0xd)  /* Channel 3 frequency lo */                 \
  V(NR34, 0xe)  /* Channel 3 frequency hi */                 \
  V(NR41, 0x10) /* Channel 4 sound length */                 \
  V(NR42, 0x11) /* Channel 4 volume envelope */              \
  V(NR43, 0x12) /* Channel 4 polynomial counter */           \
  V(NR44, 0x13) /* Channel 4 counter/consecutive; trigger */ \
  V(NR50, 0x14) /* Sound volume */                           \
  V(NR51, 0x15) /* Sound output select */                    \
  V(NR52, 0x16) /* Sound enabled */

#define INVALID_READ_BYTE 0xff

#define WRITE_REG(X, MACRO) MACRO(X, DECODE)
#define READ_REG(X, MACRO) MACRO(X, ENCODE)
#define BITS_MASK(HI, LO) ((1 << ((HI) - (LO) + 1)) - 1)
#define ENCODE(X, HI, LO) (((X) & BITS_MASK(HI, LO)) << (LO))
#define DECODE(X, HI, LO) (((X) >> (LO)) & BITS_MASK(HI, LO))
#define BITS(X, OP, HI, LO) OP(X, HI, LO)
#define BIT(X, OP, B) OP(X, B, B)

#define CPU_FLAG_Z(X, OP) BIT(X, OP, 7)
#define CPU_FLAG_N(X, OP) BIT(X, OP, 6)
#define CPU_FLAG_H(X, OP) BIT(X, OP, 5)
#define CPU_FLAG_C(X, OP) BIT(X, OP, 4)

#define JOYP_UNUSED 0xc0
#define JOYP_RESULT_MASK 0x0f
#define JOYP_JOYPAD_SELECT(X, OP) BITS(X, OP, 5, 4)
#define JOYP_DPAD_DOWN(X, OP) BIT(X, OP, 3)
#define JOYP_DPAD_UP(X, OP) BIT(X, OP, 2)
#define JOYP_DPAD_LEFT(X, OP) BIT(X, OP, 1)
#define JOYP_DPAD_RIGHT(X, OP) BIT(X, OP, 0)
#define JOYP_BUTTON_START(X, OP) BIT(X, OP, 3)
#define JOYP_BUTTON_SELECT(X, OP) BIT(X, OP, 2)
#define JOYP_BUTTON_B(X, OP) BIT(X, OP, 1)
#define JOYP_BUTTON_A(X, OP) BIT(X, OP, 0)

#define SC_UNUSED 0x7e
#define SC_TRANSFER_START(X, OP) BIT(X, OP, 7)
#define SC_SHIFT_CLOCK(X, OP) BIT(X, OP, 0)

#define TAC_UNUSED 0xf8
#define TAC_TIMER_ON(X, OP) BIT(X, OP, 2)
#define TAC_CLOCK_SELECT(X, OP) BITS(X, OP, 1, 0)

#define INTERRUPT_VBLANK_MASK 0x01
#define INTERRUPT_LCD_STAT_MASK 0x02
#define INTERRUPT_TIMER_MASK 0x04
#define INTERRUPT_SERIAL_MASK 0x08
#define INTERRUPT_JOYPAD_MASK 0x10
#define INTERRUPT_UNUSED 0xe0

#define LCDC_DISPLAY(X, OP) BIT(X, OP, 7)
#define LCDC_WINDOW_TILE_MAP_SELECT(X, OP) BIT(X, OP, 6)
#define LCDC_WINDOW_DISPLAY(X, OP) BIT(X, OP, 5)
#define LCDC_BG_TILE_DATA_SELECT(X, OP) BIT(X, OP, 4)
#define LCDC_BG_TILE_MAP_SELECT(X, OP) BIT(X, OP, 3)
#define LCDC_OBJ_SIZE(X, OP) BIT(X, OP, 2)
#define LCDC_OBJ_DISPLAY(X, OP) BIT(X, OP, 1)
#define LCDC_BG_DISPLAY(X, OP) BIT(X, OP, 0)

#define STAT_UNUSED 0x80
#define STAT_YCOMPARE_INTR(X, OP) BIT(X, OP, 6)
#define STAT_MODE2_INTR(X, OP) BIT(X, OP, 5)
#define STAT_VBLANK_INTR(X, OP) BIT(X, OP, 4)
#define STAT_HBLANK_INTR(X, OP) BIT(X, OP, 3)
#define STAT_YCOMPARE(X, OP) BIT(X, OP, 2)
#define STAT_MODE(X, OP) BITS(X, OP, 1, 0)

#define PALETTE_COLOR3(X, OP) BITS(X, OP, 7, 6)
#define PALETTE_COLOR2(X, OP) BITS(X, OP, 5, 4)
#define PALETTE_COLOR1(X, OP) BITS(X, OP, 3, 2)
#define PALETTE_COLOR0(X, OP) BITS(X, OP, 1, 0)

#define NR10_SWEEP_PERIOD(X, OP) BITS(X, OP, 6, 4)
#define NR10_SWEEP_DIRECTION(X, OP) BIT(X, OP, 3)
#define NR10_SWEEP_SHIFT(X, OP) BITS(X, OP, 2, 0)

#define NRX1_WAVE_DUTY(X, OP) BITS(X, OP, 7, 6)
#define NRX1_LENGTH(X, OP) BITS(X, OP, 5, 0)

#define NRX2_INITIAL_VOLUME(X, OP) BITS(X, OP, 7, 4)
#define NRX2_DAC_ENABLED(X, OP) BITS(X, OP, 7, 3)
#define NRX2_ENVELOPE_DIRECTION(X, OP) BIT(X, OP, 3)
#define NRX2_ENVELOPE_PERIOD(X, OP) BITS(X, OP, 2, 0)

#define NRX4_INITIAL(X, OP) BIT(X, OP, 7)
#define NRX4_LENGTH_ENABLED(X, OP) BIT(X, OP, 6)
#define NRX4_FREQUENCY_HI(X, OP) BITS(X, OP, 2, 0)

#define NR30_DAC_ENABLED(X, OP) BIT(X, OP, 7)

#define NR32_SELECT_WAVE_VOLUME(X, OP) BITS(X, OP, 6, 5)

#define NR43_CLOCK_SHIFT(X, OP) BITS(X, OP, 7, 4)
#define NR43_LFSR_WIDTH(X, OP) BIT(X, OP, 3)
#define NR43_DIVISOR(X, OP) BITS(X, OP, 2, 0)

#define OBJ_PRIORITY(X, OP) BIT(X, OP, 7)
#define OBJ_YFLIP(X, OP) BIT(X, OP, 6)
#define OBJ_XFLIP(X, OP) BIT(X, OP, 5)
#define OBJ_PALETTE(X, OP) BIT(X, OP, 4)

#define NR50_VIN_SO2(X, OP) BIT(X, OP, 7)
#define NR50_SO2_VOLUME(X, OP) BITS(X, OP, 6, 4)
#define NR50_VIN_SO1(X, OP) BIT(X, OP, 3)
#define NR50_SO1_VOLUME(X, OP) BITS(X, OP, 2, 0)

#define NR51_SOUND4_SO2(X, OP) BIT(X, OP, 7)
#define NR51_SOUND3_SO2(X, OP) BIT(X, OP, 6)
#define NR51_SOUND2_SO2(X, OP) BIT(X, OP, 5)
#define NR51_SOUND1_SO2(X, OP) BIT(X, OP, 4)
#define NR51_SOUND4_SO1(X, OP) BIT(X, OP, 3)
#define NR51_SOUND3_SO1(X, OP) BIT(X, OP, 2)
#define NR51_SOUND2_SO1(X, OP) BIT(X, OP, 1)
#define NR51_SOUND1_SO1(X, OP) BIT(X, OP, 0)

#define NR52_ALL_SOUND_ENABLED(X, OP) BIT(X, OP, 7)
#define NR52_SOUND4_ON(X, OP) BIT(X, OP, 3)
#define NR52_SOUND3_ON(X, OP) BIT(X, OP, 2)
#define NR52_SOUND2_ON(X, OP) BIT(X, OP, 1)
#define NR52_SOUND1_ON(X, OP) BIT(X, OP, 0)

#define FOREACH_RESULT(V) \
  V(OK, 0)                \
  V(ERROR, 1)

#define FOREACH_BOOL(V) \
  V(FALSE, 0)           \
  V(TRUE, 1)

#define FOREACH_CGB_FLAG(V)   \
  V(CGB_FLAG_NONE, 0)         \
  V(CGB_FLAG_SUPPORTED, 0x80) \
  V(CGB_FLAG_REQUIRED, 0xC0)

#define FOREACH_SGB_FLAG(V) \
  V(SGB_FLAG_NONE, 0)       \
  V(SGB_FLAG_SUPPORTED, 3)

#define FOREACH_CARTRIDGE_TYPE(V)                                              \
  V(CARTRIDGE_TYPE_ROM_ONLY, 0x0, NO_MBC, NO_RAM, NO_BATTERY)                  \
  V(CARTRIDGE_TYPE_MBC1, 0x1, MBC1, NO_RAM, NO_BATTERY)                        \
  V(CARTRIDGE_TYPE_MBC1_RAM, 0x2, MBC1, WITH_RAM, NO_BATTERY)                  \
  V(CARTRIDGE_TYPE_MBC1_RAM_BATTERY, 0x3, MBC1, WITH_RAM, WITH_BATTERY)        \
  V(CARTRIDGE_TYPE_MBC2, 0x5, MBC2, NO_RAM, NO_BATTERY)                        \
  V(CARTRIDGE_TYPE_MBC2_BATTERY, 0x6, MBC2, NO_RAM, WITH_BATTERY)              \
  V(CARTRIDGE_TYPE_ROM_RAM, 0x8, NO_MBC, WITH_RAM, NO_BATTERY)                 \
  V(CARTRIDGE_TYPE_ROM_RAM_BATTERY, 0x9, NO_MBC, WITH_RAM, WITH_BATTERY)       \
  V(CARTRIDGE_TYPE_MMM01, 0xb, MMM01, NO_RAM, NO_BATTERY)                      \
  V(CARTRIDGE_TYPE_MMM01_RAM, 0xc, MMM01, WITH_RAM, NO_BATTERY)                \
  V(CARTRIDGE_TYPE_MMM01_RAM_BATTERY, 0xd, MMM01, WITH_RAM, WITH_BATTERY)      \
  V(CARTRIDGE_TYPE_MBC3_TIMER_BATTERY, 0xf, MBC3, NO_RAM, WITH_BATTERY)        \
  V(CARTRIDGE_TYPE_MBC3_TIMER_RAM_BATTERY, 0x10, MBC3, WITH_RAM, WITH_BATTERY) \
  V(CARTRIDGE_TYPE_MBC3, 0x11, MBC3, NO_RAM, NO_BATTERY)                       \
  V(CARTRIDGE_TYPE_MBC3_RAM, 0x12, MBC3, WITH_RAM, NO_BATTERY)                 \
  V(CARTRIDGE_TYPE_MBC3_RAM_BATTERY, 0x13, MBC3, WITH_RAM, WITH_BATTERY)       \
  V(CARTRIDGE_TYPE_MBC4, 0x15, MBC4, NO_RAM, NO_BATTERY)                       \
  V(CARTRIDGE_TYPE_MBC4_RAM, 0x16, MBC4, WITH_RAM, NO_BATTERY)                 \
  V(CARTRIDGE_TYPE_MBC4_RAM_BATTERY, 0x17, MBC4, WITH_RAM, WITH_BATTERY)       \
  V(CARTRIDGE_TYPE_MBC5, 0x19, MBC5, NO_RAM, NO_BATTERY)                       \
  V(CARTRIDGE_TYPE_MBC5_RAM, 0x1a, MBC5, WITH_RAM, NO_BATTERY)                 \
  V(CARTRIDGE_TYPE_MBC5_RAM_BATTERY, 0x1b, MBC5, WITH_RAM, WITH_BATTERY)       \
  V(CARTRIDGE_TYPE_MBC5_RUMBLE, 0x1c, MBC5, NO_RAM, NO_BATTERY)                \
  V(CARTRIDGE_TYPE_MBC5_RUMBLE_RAM, 0x1d, MBC5, WITH_RAM, NO_BATTERY)          \
  V(CARTRIDGE_TYPE_MBC5_RUMBLE_RAM_BATTERY, 0x1e, MBC5, WITH_RAM,              \
    WITH_BATTERY)                                                              \
  V(CARTRIDGE_TYPE_POCKET_CAMERA, 0xfc, NO_MBC, NO_RAM, NO_BATTERY)            \
  V(CARTRIDGE_TYPE_BANDAI_TAMA5, 0xfd, TAMA5, NO_RAM, NO_BATTERY)              \
  V(CARTRIDGE_TYPE_HUC3, 0xfe, HUC3, NO_RAM, NO_BATTERY)                       \
  V(CARTRIDGE_TYPE_HUC1_RAM_BATTERY, 0xff, HUC1, WITH_RAM, NO_BATTERY)

/* TODO: Correct masks for 1.1M, 1.2M, 1.5M sizes? */
#define FOREACH_ROM_SIZE(V)        \
  V(ROM_SIZE_32K, 0, 2, 0x1)       \
  V(ROM_SIZE_64K, 1, 4, 0x3)       \
  V(ROM_SIZE_128K, 2, 8, 0x7)      \
  V(ROM_SIZE_256K, 3, 16, 0xf)     \
  V(ROM_SIZE_512K, 4, 32, 0x1f)    \
  V(ROM_SIZE_1M, 5, 64, 0x3f)      \
  V(ROM_SIZE_2M, 6, 128, 0x7f)     \
  V(ROM_SIZE_4M, 7, 256, 0xff)     \
  V(ROM_SIZE_1_1M, 0x52, 72, 0x7f) \
  V(ROM_SIZE_1_2M, 0x53, 80, 0x7f) \
  V(ROM_SIZE_1_5M, 0x54, 96, 0x7f)

#define FOREACH_EXT_RAM_SIZE(V)  \
  V(EXT_RAM_SIZE_NONE, 0, 0, 0)  \
  V(EXT_RAM_SIZE_2K, 1, 2048, 0x7ff) \
  V(EXT_RAM_SIZE_8K, 2, 8192, 0x1fff) \
  V(EXT_RAM_SIZE_32K, 3, 32768, 0x7fff)

#define DEFINE_ENUM(name, code, ...) name = code,
#define DEFINE_STRING(name, code, ...) [code] = #name,

static const char* get_enum_string(const char** strings,
                                   size_t string_count,
                                   size_t value) {
  const char* result = value < string_count ? strings[value] : "unknown";
  return result ? result : "unknown";
}

#define DEFINE_NAMED_ENUM(NAME, Name, name, foreach)                           \
  typedef enum { foreach (DEFINE_ENUM) NAME##_COUNT } Name;                    \
  static Result is_##name##_valid(Name value) { return value < NAME##_COUNT; } \
  static const char* get_##name##_string(Name value) {                         \
    static const char* s_strings[] = {foreach (DEFINE_STRING)};                \
    return get_enum_string(s_strings, ARRAY_SIZE(s_strings), value);           \
  }

DEFINE_NAMED_ENUM(RESULT, Result, result, FOREACH_RESULT)
DEFINE_NAMED_ENUM(BOOL, Bool, bool, FOREACH_BOOL)
DEFINE_NAMED_ENUM(CGB_FLAG, CgbFlag, cgb_flag, FOREACH_CGB_FLAG)
DEFINE_NAMED_ENUM(SGB_FLAG, SgbFlag, sgb_flag, FOREACH_SGB_FLAG)
DEFINE_NAMED_ENUM(CARTRIDGE_TYPE,
                  CartridgeType,
                  cartridge_type,
                  FOREACH_CARTRIDGE_TYPE)
DEFINE_NAMED_ENUM(ROM_SIZE, RomSize, rom_size, FOREACH_ROM_SIZE)
DEFINE_NAMED_ENUM(EXT_RAM_SIZE, ExtRamSize, ext_ram_size, FOREACH_EXT_RAM_SIZE)

#define DEFINE_IO_REG_ENUM(name, code, ...) IO_##name##_ADDR = code,
#define DEFINE_APU_REG_ENUM(name, code, ...) APU_##name##_ADDR = code,

#define DEFINE_NAMED_REG(NAME, Name, name, foreach, enum_def)        \
  typedef enum { foreach (enum_def) NAME##_REG_COUNT } Name;         \
  static Result is_##name##_valid(Name value) {                      \
    return value < NAME##_REG_COUNT;                                 \
  }                                                                  \
  static const char* get_##name##_string(Name value) {               \
    static const char* s_strings[] = {foreach (DEFINE_STRING)};      \
    return get_enum_string(s_strings, ARRAY_SIZE(s_strings), value); \
  }

DEFINE_NAMED_REG(IO, IOReg, io_reg, FOREACH_IO_REG, DEFINE_IO_REG_ENUM)
DEFINE_NAMED_REG(APU, APUReg, apu_reg, FOREACH_APU_REG, DEFINE_APU_REG_ENUM)

static uint32_t s_rom_bank_count[] = {
#define V(name, code, bank_count, bank_mask) [code] = bank_count,
    FOREACH_ROM_SIZE(V)
#undef V
};

static uint32_t s_rom_bank_mask[] = {
#define V(name, code, bank_count, bank_mask) [code] = bank_mask,
    FOREACH_ROM_SIZE(V)
#undef V
};

static uint32_t s_ext_ram_byte_size[] = {
#define V(name, code, byte_size, addr_mask) [code] = byte_size,
    FOREACH_EXT_RAM_SIZE(V)
#undef V
};

static uint32_t s_ext_ram_addr_mask[] = {
#define V(name, code, byte_size, addr_mask) [code] = addr_mask,
    FOREACH_EXT_RAM_SIZE(V)
#undef V
};


typedef enum {
  MBC_TYPE_NO_MBC,
  MBC_TYPE_MBC1,
  MBC_TYPE_MBC2,
  MBC_TYPE_MBC3,
  MBC_TYPE_MBC4,
  MBC_TYPE_MBC5,
  MBC_TYPE_MMM01,
  MBC_TYPE_TAMA5,
  MBC_TYPE_HUC3,
  MBC_TYPE_HUC1,
} MBCType;
static MBCType s_mbc_type[] = {
#define V(name, code, mbc, ram, battery) [code] = MBC_TYPE_##mbc,
    FOREACH_CARTRIDGE_TYPE(V)
#undef V
};

typedef enum {
  EXT_RAM_TYPE_NO_RAM,
  EXT_RAM_TYPE_WITH_RAM,
} ExtRamType;
static ExtRamType s_ext_ram_type[] = {
#define V(name, code, mbc, ram, battery) [code] = EXT_RAM_TYPE_##ram,
    FOREACH_CARTRIDGE_TYPE(V)
#undef V
};

typedef enum {
  BATTERY_TYPE_NO_BATTERY,
  BATTERY_TYPE_WITH_BATTERY,
} BatteryType;
static BatteryType s_battery_type[] = {
#define V(name, code, mbc, ram, battery) [code] = BATTERY_TYPE_##battery,
    FOREACH_CARTRIDGE_TYPE(V)
#undef V
};

typedef enum {
  MEMORY_MAP_ROM,
  MEMORY_MAP_ROM_BANK_SWITCH,
  MEMORY_MAP_VRAM,
  MEMORY_MAP_EXT_RAM,
  MEMORY_MAP_WORK_RAM,
  MEMORY_MAP_WORK_RAM_BANK_SWITCH,
  MEMORY_MAP_OAM,
  MEMORY_MAP_UNUSED,
  MEMORY_MAP_IO,
  MEMORY_MAP_APU,
  MEMORY_MAP_WAVE_RAM,
  MEMORY_MAP_HIGH_RAM,
} MemoryMapType;

typedef enum {
  BANK_MODE_ROM = 0,
  BANK_MODE_RAM = 1,
} BankMode;

typedef enum {
  JOYPAD_SELECT_BOTH = 0,
  JOYPAD_SELECT_BUTTONS = 1,
  JOYPAD_SELECT_DPAD = 2,
  JOYPAD_SELECT_NONE = 3,
} JoypadSelect;

typedef enum {
  TIMER_CLOCK_4096_HZ = 0,
  TIMER_CLOCK_262144_HZ = 1,
  TIMER_CLOCK_65536_HZ = 2,
  TIMER_CLOCK_16384_HZ = 3,
} TimerClock;
/* TIMA is incremented when the given bit of div_counter changes from 1 to 0. */
static const uint16_t s_tima_mask[] = {1 << 9, 1 << 3, 1 << 5, 1 << 7};

typedef enum {
  SERIAL_CLOCK_EXTERNAL = 0,
  SERIAL_CLOCK_INTERNAL = 1,
} SerialClock;

enum {
  CHANNEL1,
  CHANNEL2,
  CHANNEL3,
  CHANNEL4,
  CHANNEL_COUNT,
};

enum {
  SOUND1,
  SOUND2,
  SOUND3,
  SOUND4,
  VIN,
  SOUND_COUNT,
};

typedef enum {
  SWEEP_DIRECTION_ADDITION = 0,
  SWEEP_DIRECTION_SUBTRACTION = 1,
} SweepDirection;

typedef enum {
  ENVELOPE_ATTENUATE = 0,
  ENVELOPE_AMPLIFY = 1,
} EnvelopeDirection;

typedef enum {
  WAVE_DUTY_12_5 = 0,
  WAVE_DUTY_25 = 1,
  WAVE_DUTY_50 = 2,
  WAVE_DUTY_75 = 3,
  WAVE_DUTY_COUNT,
} WaveDuty;

typedef enum {
  WAVE_VOLUME_MUTE = 0,
  WAVE_VOLUME_100 = 1,
  WAVE_VOLUME_50 = 2,
  WAVE_VOLUME_25 = 3,
  WAVE_VOLUME_COUNT,
} WaveVolume;

typedef enum {
  LFSR_WIDTH_15 = 0, /* 15-bit LFSR */
  LFSR_WIDTH_7 = 1,  /* 7-bit LFSR */
} LFSRWidth;

typedef enum {
  NOISE_DIVISOR_8 = 0,
  NOISE_DIVISOR_16 = 1,
  NOISE_DIVISOR_32 = 2,
  NOISE_DIVISOR_48 = 3,
  NOISE_DIVISOR_64 = 4,
  NOISE_DIVISOR_80 = 5,
  NOISE_DIVISOR_96 = 6,
  NOISE_DIVISOR_112 = 7,
  NOISE_DIVISOR_COUNT,
} NoiseDivisor;

typedef enum {
  TILE_MAP_9800_9BFF = 0,
  TILE_MAP_9C00_9FFF = 1,
} TileMapSelect;

typedef enum {
  TILE_DATA_8800_97FF = 0,
  TILE_DATA_8000_8FFF = 1,
} TileDataSelect;

typedef enum {
  OBJ_SIZE_8X8 = 0,
  OBJ_SIZE_8X16 = 1,
} ObjSize;
static uint8_t s_obj_size_to_height[] = {
  [OBJ_SIZE_8X8] = 8,
  [OBJ_SIZE_8X16] = 16,
};

typedef enum {
  PPU_MODE_HBLANK = 0, /* PPU mode 0 */
  PPU_MODE_VBLANK = 1, /* PPU mode 1 */
  PPU_MODE2 = 2,       /* PPU mode 2 */
  PPU_MODE3 = 3,
} PPUMode;

typedef enum {
  COLOR_WHITE = 0,
  COLOR_LIGHT_GRAY = 1,
  COLOR_DARK_GRAY = 2,
  COLOR_BLACK = 3,
} Color;
static RGBA s_color_to_rgba[] = {
  [COLOR_WHITE] = RGBA_WHITE,
  [COLOR_LIGHT_GRAY] = RGBA_LIGHT_GRAY,
  [COLOR_DARK_GRAY] = RGBA_DARK_GRAY,
  [COLOR_BLACK] = RGBA_BLACK,
};
static uint8_t s_color_to_obj_mask[] = {
  [COLOR_WHITE] = 0xff,
  [COLOR_LIGHT_GRAY] = 0,
  [COLOR_DARK_GRAY] = 0,
  [COLOR_BLACK] = 0,
};

typedef enum {
  OBJ_PRIORITY_ABOVE_BG = 0,
  OBJ_PRIORITY_BEHIND_BG = 1,
} ObjPriority;

typedef enum {
  DMA_INACTIVE = 0,
  DMA_TRIGGERED = 1,
  DMA_ACTIVE = 2,
} DMAState;

typedef struct {
  uint8_t* data;
  size_t size;
} RomData;

typedef struct {
  uint8_t data[EXT_RAM_MAX_SIZE];
  size_t size;
  BatteryType battery_type;
} ExtRam;

typedef struct {
  uint8_t data[WORK_RAM_MAX_SIZE];
  size_t size; /* normally 8k, 32k in CGB mode */
} WorkRam;

typedef struct {
  const char* start;
  size_t length;
} StringSlice;

typedef struct {
  StringSlice title;
  CgbFlag cgb_flag;
  SgbFlag sgb_flag;
  CartridgeType cartridge_type;
  RomSize rom_size;
  ExtRamSize ext_ram_size;
  uint8_t header_checksum;
  uint16_t global_checksum;
  Result header_checksum_valid;
  Result global_checksum_valid;
} RomInfo;

struct Emulator;

typedef struct {
  uint8_t byte_2000_3fff;
  uint8_t byte_4000_5fff;
  BankMode bank_mode;
} MBC1;

typedef struct {
  uint8_t rom_bank_mask;
  uint8_t rom_bank;
  uint16_t ext_ram_addr_mask;
  uint8_t ext_ram_bank;
  Bool ext_ram_enabled;
  union {
    MBC1 mbc1;
  };
  uint8_t (*read_work_ram_bank_switch)(struct Emulator*, MaskedAddress);
  uint8_t (*read_ext_ram)(struct Emulator*, MaskedAddress);
  void (*write_rom)(struct Emulator*, MaskedAddress, uint8_t);
  void (*write_work_ram_bank_switch)(struct Emulator*, MaskedAddress, uint8_t);
  void (*write_ext_ram)(struct Emulator*, MaskedAddress, uint8_t);
} MemoryMap;

typedef struct {
  MemoryMapType type;
  MaskedAddress addr;
} MemoryTypeAddressPair;

/* TODO(binji): endianness */
#define REGISTER_PAIR(X, Y) \
  union {                   \
    struct {                \
      uint8_t Y;            \
      uint8_t X;            \
    };                      \
    uint16_t X##Y;          \
  }

typedef struct {
  uint8_t A;
  REGISTER_PAIR(B, C);
  REGISTER_PAIR(D, E);
  REGISTER_PAIR(H, L);
  uint16_t SP;
  uint16_t PC;
  struct {
    Bool Z;
    Bool N;
    Bool H;
    Bool C;
  } F;
} Registers;

typedef uint8_t Tile[TILE_WIDTH * TILE_HEIGHT];
typedef uint8_t TileMap[TILE_MAP_WIDTH * TILE_MAP_HEIGHT];

typedef struct {
  Tile tile[TILE_COUNT];
  TileMap map[TILE_MAP_COUNT];
  uint8_t data[VIDEO_RAM_SIZE];
} VideoRam;

typedef struct {
  Color color[PALETTE_COLOR_COUNT];
} Palette;

typedef struct {
  uint8_t y;
  uint8_t x;
  uint8_t tile;
  uint8_t byte3;
  ObjPriority priority;
  Bool yflip;
  Bool xflip;
  uint8_t palette;
} Obj;

typedef struct {
  Obj objs[OBJ_COUNT];
  Palette obp[OBJ_PALETTE_COUNT];
} OAM;

typedef struct {
  Bool down, up, left, right;
  Bool start, select, B, A;
  JoypadSelect joypad_select;
} Joypad;

typedef struct {
  Bool IME;   /* Interrupt Master Enable */
  uint8_t IE; /* Interrupt Enable */
  uint8_t IF; /* Interrupt Request */
  /* Internal state */
  uint8_t new_IF;   /* TODO: document */
  Bool enable;      /* Set after EI instruction. This delays updating IME. */
  Bool halt;        /* Halted, waiting for an interrupt. */
  Bool halt_DI;     /* Halted w/ disabled interrupts. */
} Interrupts;

typedef struct {
  uint8_t TIMA;            /* Incremented at rate defined by clock_select */
  uint8_t TMA;             /* When TIMA overflows, it is set to this value */
  TimerClock clock_select; /* Select the rate of TIMA */
  /* Internal state */
  uint16_t div_counter; /* Interal clock counter, upper 8 bits are DIV. */
  Bool tima_overflow;   /* Used to implement TIMA overflow delay. */
  Bool on;
} Timer;

typedef struct {
  Bool transferring;
  SerialClock clock;
  uint8_t SB; /* Serial transfer data. */
  /* Internal state */
  uint8_t transferred_bits;
  uint32_t cycles;
} Serial;

typedef struct {
  uint8_t period;
  SweepDirection direction;
  uint8_t shift;
  /* Internal state */
  uint16_t frequency;
  uint8_t timer; /* 0..period */
  Bool enabled;
  Bool calculated_subtract;
} Sweep;

typedef struct {
  uint8_t initial_volume;
  EnvelopeDirection direction;
  uint8_t period;
  /* Internal state */
  uint8_t volume; /* 0..15 */
  uint32_t timer; /* 0..period */
  Bool automatic; /* TRUE when MAX/MIN has not yet been reached. */
} Envelope;

typedef struct {
  uint32_t time;    /* Time (in cycles) the sample was read. */
  uint8_t position; /* Position in Wave RAM when read. */
  uint8_t byte;     /* Byte read from the Wave RAM. */
  uint8_t data;     /* Just the 4-bits of the sample. */
} WaveSample;

/* Channel 1 and 2 */
typedef struct {
  WaveDuty duty;
  /* Internal state */
  uint8_t sample;   /* Last sample generated, 0..1 */
  uint32_t period;  /* Calculated from the frequency. */
  uint8_t position; /* Position in the duty cycle, 0..7 */
  uint32_t cycles;  /* 0..period */
} SquareWave;

/* Channel 3 */
typedef struct {
  WaveVolume volume;
  uint8_t ram[WAVE_RAM_SIZE];
  /* Internal state */
  WaveSample sample[2]; /* The two most recent samples read. */
  uint32_t period;      /* Calculated from the frequency. */
  uint8_t position;     /* 0..31 */
  uint32_t cycles;      /* 0..period */
  Bool playing; /* TRUE if the channel has been triggered but the DAC not
                        disabled. */
} Wave;

/* Channel 4 */
typedef struct {
  uint8_t clock_shift;
  LFSRWidth lfsr_width;
  NoiseDivisor divisor;
  /* Internal state */
  uint8_t sample;  /* Last sample generated, 0..1 */
  uint16_t lfsr;   /* Linear feedback shift register, 15- or 7-bit. */
  uint32_t period; /* Calculated from the clock_shift and divisor. */
  uint32_t cycles; /* 0..period */
} Noise;

typedef struct {
  SquareWave square_wave; /* Channel 1, 2 */
  Envelope envelope;      /* Channel 1, 2, 4 */
  uint16_t frequency;     /* Channel 1, 2, 3 */
  uint16_t length;        /* All channels */
  Bool length_enabled;    /* All channels */
  /* Internal state */
  Bool dac_enabled;
  Bool status; /* Status bit for NR52 */
} Channel;

typedef struct {
  uint16_t* data; /* Unsigned 16-bit 2-channel samples @ 2MHz */
  uint16_t* end;
  uint16_t* position;
} AudioBuffer;

typedef struct {
  uint8_t so2_volume;
  uint8_t so1_volume;
  Bool so2_output[SOUND_COUNT];
  Bool so1_output[SOUND_COUNT];
  Bool enabled;
  Sweep sweep;
  Wave wave;
  Noise noise;
  Channel channel[CHANNEL_COUNT];
  /* Internal state */
  uint8_t frame;         /* 0..FRAME_SEQUENCER_COUNT */
  uint32_t frame_cycles; /* 0..FRAME_SEQUENCER_CYCLES */
  uint32_t cycles;       /* Raw cycle counter */
  AudioBuffer* buffer;
} APU;

typedef struct {
  Bool display;
  TileMapSelect window_tile_map_select;
  Bool window_display;
  TileDataSelect bg_tile_data_select;
  TileMapSelect bg_tile_map_select;
  ObjSize obj_size;
  Bool obj_display;
  Bool bg_display;
} LCDControl;

typedef struct {
  Bool irq;
  /* Internal state */
  Bool trigger;
  uint8_t delay;
  int32_t cycles;
} LCDStatusInterrupt;

typedef struct {
  LCDStatusInterrupt y_compare;
  LCDStatusInterrupt mode2;
  LCDStatusInterrupt vblank;
  LCDStatusInterrupt hblank;
  PPUMode mode;
  /* Internal state */
  Bool IF; /* Internal interrupt flag. */
  PPUMode next_mode;
  PPUMode trigger_mode;
  uint32_t mode_cycles;
  Bool ly_eq_lyc;
  Bool new_ly_eq_lyc;
} LCDStatus;

typedef struct {
  LCDControl lcdc; /* LCD control */
  LCDStatus stat;  /* LCD status */
  uint8_t SCY;     /* Screen Y */
  uint8_t SCX;     /* Screen X */
  uint8_t LY;      /* Line Y */
  uint8_t LYC;     /* Line Y Compare */
  uint8_t WY;      /* Window Y */
  uint8_t WX;      /* Window X */
  Palette bgp;     /* BG Palette */
  /* Internal state */
  uint32_t LY_cycles;
  uint32_t line_cycles;
  uint32_t frame;
  uint8_t line_y; /* The currently rendering line. Can be different than LY. */
  uint8_t win_y;  /* The window Y is only incremented when rendered. */
  uint8_t frame_WY; /* WY is cached per frame. */
  Bool new_frame_edge;
  uint8_t display_delay_frames; /* Wait this many frames before displaying. */
} PPU;

typedef struct {
  DMAState state;
  MemoryTypeAddressPair source;
  uint32_t cycles;
} DMA;

typedef RGBA FrameBuffer[SCREEN_WIDTH * SCREEN_HEIGHT];

typedef struct {
  Bool disable_sound[CHANNEL_COUNT];
  Bool disable_bg;
  Bool disable_window;
  Bool disable_obj;
  Bool no_sync;
  Bool paused;
  Bool step;
} EmulatorConfig;

typedef struct Emulator {
  EmulatorConfig config;
  RomInfo rom_info;
  RomData rom_data;
  MemoryMap memory_map;
  Registers reg;
  VideoRam vram;
  ExtRam ext_ram;
  WorkRam ram;
  Interrupts interrupts;
  OAM oam;
  Joypad joypad;
  Serial serial;
  Timer timer;
  APU apu;
  PPU ppu;
  DMA dma;
  uint8_t hram[HIGH_RAM_SIZE];
  FrameBuffer frame_buffer;
  uint32_t cycles;
} Emulator;

static Bool s_never_trace = 0;
static Bool s_trace = 0;
static uint32_t s_trace_counter = 0;
static int s_log_level_memory = 1;
static int s_log_level_ppu = 2;
static int s_log_level_apu = 1;
static int s_log_level_io = 2;
static int s_log_level_interrupt = 2;

static void print_emulator_info(Emulator*);
static void write_apu(Emulator*, Address, uint8_t);
static void write_io(Emulator*, Address, uint8_t);

static Result read_rom_data_from_file(const char* filename,
                                      RomData* out_rom_data) {
  FILE* f = fopen(filename, "rb");
  CHECK_MSG(f, "unable to open file \"%s\".\n", filename);
  CHECK_MSG(fseek(f, 0, SEEK_END) >= 0, "fseek to end failed.\n");
  long size = ftell(f);
  CHECK_MSG(size >= 0, "ftell failed.");
  CHECK_MSG(fseek(f, 0, SEEK_SET) >= 0, "fseek to beginning failed.\n");
  CHECK_MSG(size >= MINIMUM_ROM_SIZE, "size < minimum rom size (%u).\n",
            MINIMUM_ROM_SIZE);
  uint8_t* data = malloc(size);
  CHECK_MSG(data, "allocation failed.\n");
  CHECK_MSG(fread(data, size, 1, f) == 1, "fread failed.\n");
  fclose(f);
  out_rom_data->data = data;
  out_rom_data->size = size;
  return OK;
error:
  if (f) {
    fclose(f);
  }
  return ERROR;
}

static void get_rom_title(RomData* rom_data, StringSlice* out_title) {
  const char* start = (char*)rom_data->data + TITLE_START_ADDR;
  const char* end = start + TITLE_END_ADDR;
  const char* p = start;
  size_t length = 0;
  while (p <= end && *p != 0 && (*p & 0x80) == 0) {
    length++;
    p++;
  }
  out_title->start = start;
  out_title->length = length;
}

static Result validate_header_checksum(RomData* rom_data) {
  uint8_t expected_checksum = ROM_U8(uint8_t, HEADER_CHECKSUM_ADDR);
  uint8_t checksum = 0;
  size_t i = 0;
  for (i = HEADER_CHECKSUM_RANGE_START; i <= HEADER_CHECKSUM_RANGE_END; ++i) {
    checksum = checksum - rom_data->data[i] - 1;
  }
  return checksum == expected_checksum ? OK : ERROR;
}

static Result validate_global_checksum(RomData* rom_data) {
  uint16_t expected_checksum = ROM_U16_BE(GLOBAL_CHECKSUM_START_ADDR);
  uint16_t checksum = 0;
  size_t i = 0;
  for (i = 0; i < rom_data->size; ++i) {
    if (i == GLOBAL_CHECKSUM_START_ADDR || i == GLOBAL_CHECKSUM_END_ADDR)
      continue;
    checksum += rom_data->data[i];
  }
  return checksum == expected_checksum ? OK : ERROR;
}

static uint32_t get_rom_byte_size(RomSize rom_size) {
  assert(is_rom_size_valid(rom_size));
  uint32_t rom_bank_count = s_rom_bank_count[rom_size];
  return rom_bank_count << ROM_BANK_SHIFT;
}

static Result get_rom_info(RomData* rom_data, RomInfo* out_rom_info) {
  RomInfo rom_info;
  ZERO_MEMORY(rom_info);

  rom_info.rom_size = ROM_U8(RomSize, ROM_SIZE_ADDR);
  uint32_t rom_byte_size = get_rom_byte_size(rom_info.rom_size);
  CHECK_MSG(rom_data->size == rom_byte_size,
            "Invalid ROM size: expected %u, got %zu.\n", rom_byte_size,
            rom_data->size);

  get_rom_title(rom_data, &rom_info.title);
  rom_info.cgb_flag = ROM_U8(CgbFlag, CGB_FLAG_ADDR);
  rom_info.sgb_flag = ROM_U8(SgbFlag, SGB_FLAG_ADDR);
  rom_info.cartridge_type = ROM_U8(CartridgeType, CARTRIDGE_TYPE_ADDR);
  CHECK_MSG(is_cartridge_type_valid(rom_info.cartridge_type),
            "Invalid cartridge type: %u\n", rom_info.cartridge_type);
  rom_info.ext_ram_size = ROM_U8(ExtRamSize, EXT_RAM_SIZE_ADDR);
  CHECK_MSG(is_ext_ram_size_valid(rom_info.ext_ram_size),
            "Invalid ext ram size: %u\n", rom_info.ext_ram_size);
  rom_info.header_checksum = ROM_U8(uint8_t, HEADER_CHECKSUM_ADDR);
  rom_info.header_checksum_valid = validate_header_checksum(rom_data);
  rom_info.global_checksum = ROM_U16_BE(GLOBAL_CHECKSUM_START_ADDR);
  rom_info.global_checksum_valid = validate_global_checksum(rom_data);

  *out_rom_info = rom_info;
  return OK;
error:
  return ERROR;
}

static void print_rom_info(RomInfo* rom_info) {
  printf("title: \"%.*s\"\n", (int)rom_info->title.length,
         rom_info->title.start);
  printf("cgb flag: %s\n", get_cgb_flag_string(rom_info->cgb_flag));
  printf("sgb flag: %s\n", get_sgb_flag_string(rom_info->sgb_flag));
  printf("cartridge type: %s\n",
         get_cartridge_type_string(rom_info->cartridge_type));
  printf("rom size: %s\n", get_rom_size_string(rom_info->rom_size));
  printf("ext ram size: %s\n", get_ext_ram_size_string(rom_info->ext_ram_size));

  printf("header checksum: 0x%02x [%s]\n", rom_info->header_checksum,
         get_result_string(rom_info->header_checksum_valid));
  printf("global checksum: 0x%04x [%s]\n", rom_info->global_checksum,
         get_result_string(rom_info->global_checksum_valid));
}

static void rom_only_write_rom(Emulator* e, MaskedAddress addr, uint8_t value) {
}

static uint8_t gb_read_work_ram_bank_switch(Emulator* e, MaskedAddress addr) {
  assert(addr <= ADDR_MASK_4K);
  return e->ram.data[0x1000 + addr];
}

static void gb_write_work_ram_bank_switch(Emulator* e,
                                          MaskedAddress addr,
                                          uint8_t value) {
  assert(addr <= ADDR_MASK_4K);
  e->ram.data[0x1000 + addr] = value;
}

static void mbc1_write_rom(Emulator* e, MaskedAddress addr, uint8_t value) {
  MemoryMap* memory_map = &e->memory_map;
  MBC1* mbc1 = &memory_map->mbc1;
  switch (addr >> 13) {
    case 0: /* 0000-1fff */
      e->memory_map.ext_ram_enabled =
          (value & MBC_RAM_ENABLED_MASK) == MBC_RAM_ENABLED_VALUE;
      break;
    case 1: /* 2000-3fff */
      mbc1->byte_2000_3fff = value;
      break;
    case 2: /* 4000-5fff */
      mbc1->byte_4000_5fff = value;
      break;
    case 3: /* 6000-7fff */
      mbc1->bank_mode = (BankMode)(value & 1);
      break;
    default:
      UNREACHABLE("invalid addr: 0x%04x\n", addr);
      break;
  }

  memory_map->rom_bank = mbc1->byte_2000_3fff & MBC1_ROM_BANK_LO_SELECT_MASK;
  if (memory_map->rom_bank == 0) {
    memory_map->rom_bank++;
  }

  if (mbc1->bank_mode == BANK_MODE_ROM) {
    memory_map->rom_bank |= (mbc1->byte_4000_5fff & MBC1_BANK_HI_SELECT_MASK)
                            << MBC1_BANK_HI_SHIFT;
    memory_map->ext_ram_bank = 0;
  } else {
    memory_map->ext_ram_bank = mbc1->byte_4000_5fff & MBC1_BANK_HI_SELECT_MASK;
  }

  memory_map->rom_bank &= memory_map->rom_bank_mask;

  VERBOSE(memory,
          "mbc1_write_rom(0x%04x, 0x%02x): rom bank = 0x%02x (0x%06x)\n", addr,
          value, memory_map->rom_bank, memory_map->rom_bank << ROM_BANK_SHIFT);
}

static uint8_t dummy_read_ext_ram(Emulator* e, MaskedAddress addr) {
  return 0;
}

static void dummy_write_ext_ram(Emulator* e,
                                MaskedAddress addr,
                                uint8_t value) {}

static uint32_t get_ext_ram_address(Emulator* e, MaskedAddress addr) {
  assert(addr <= ADDR_MASK_8K);
  uint8_t ram_bank = e->memory_map.ext_ram_bank;
  uint32_t ram_addr =
      ((ram_bank << EXT_RAM_BANK_SHIFT) & e->memory_map.ext_ram_addr_mask) |
      addr;
  assert(ram_addr < e->ext_ram.size);
  return ram_addr;
}

#define INFO_READ_RAM_DISABLED \
  INFO(memory, "%s(0x%04x) ignored, ram disabled.\n", __func__, addr)
#define INFO_WRITE_RAM_DISABLED                                               \
  INFO(memory, "%s(0x%04x, 0x%02x) ignored, ram disabled.\n", __func__, addr, \
       value);

static uint8_t gb_read_ext_ram(Emulator* e, MaskedAddress addr) {
  if (e->memory_map.ext_ram_enabled) {
    return e->ext_ram.data[get_ext_ram_address(e, addr)];
  } else {
    INFO_READ_RAM_DISABLED;
    return INVALID_READ_BYTE;
  }
}

static void gb_write_ext_ram(Emulator* e,
                                  MaskedAddress addr,
                                  uint8_t value) {
  if (e->memory_map.ext_ram_enabled) {
    e->ext_ram.data[get_ext_ram_address(e, addr)] = value;
  } else {
    INFO_WRITE_RAM_DISABLED;
  }
}

static void mbc2_write_rom(Emulator* e, MaskedAddress addr, uint8_t value) {
  MemoryMap* memory_map = &e->memory_map;
  switch (addr >> 13) {
    case 0: /* 0000-1fff */
      if ((addr & MBC2_ADDR_SELECT_BIT_MASK) == 0) {
        e->memory_map.ext_ram_enabled =
            (value & MBC_RAM_ENABLED_MASK) == MBC_RAM_ENABLED_VALUE;
      }
      VERBOSE(memory, "%s(0x%04x, 0x%02x): enabled = %u\n", __func__, addr,
              value, e->memory_map.ext_ram_enabled);
      break;
    case 1: /* 2000-3fff */
      if ((addr & MBC2_ADDR_SELECT_BIT_MASK) != 0) {
        memory_map->rom_bank =
            value & MBC2_ROM_BANK_SELECT_MASK & e->memory_map.rom_bank_mask;
        VERBOSE(memory, "%s(0x%04x, 0x%02x): rom bank = 0x%02x (0x%06x)\n",
                __func__, addr, value, memory_map->rom_bank,
                memory_map->rom_bank << ROM_BANK_SHIFT);
      }
      break;
    default:
      break;
  }
}

static uint8_t mbc2_read_ram(Emulator* e, MaskedAddress addr) {
  if (e->memory_map.ext_ram_enabled) {
    return e->ext_ram.data[addr & MBC2_RAM_ADDR_MASK];
  } else {
    INFO_READ_RAM_DISABLED;
    return INVALID_READ_BYTE;
  }
}

static void mbc2_write_ram(Emulator* e, MaskedAddress addr, uint8_t value) {
  if (e->memory_map.ext_ram_enabled) {
    e->ext_ram.data[addr & MBC2_RAM_ADDR_MASK] = value & MBC2_RAM_VALUE_MASK;
  } else {
    INFO_WRITE_RAM_DISABLED;
  }
}

static void mbc3_write_rom(Emulator* e, MaskedAddress addr, uint8_t value) {
  MemoryMap* memory_map = &e->memory_map;
  switch (addr >> 13) {
    case 0: /* 0000-1fff */
      e->memory_map.ext_ram_enabled =
          (value & MBC_RAM_ENABLED_MASK) == MBC_RAM_ENABLED_VALUE;
      break;
    case 1: /* 2000-3fff */
      memory_map->rom_bank =
          value & MBC3_ROM_BANK_SELECT_MASK & e->memory_map.rom_bank_mask;
      VERBOSE(memory, "%s(0x%04x, 0x%02x): rom bank = 0x%02x (0x%06x)\n",
              __func__, addr, value, memory_map->rom_bank,
              memory_map->rom_bank << ROM_BANK_SHIFT);
      break;
    case 2: /* 4000-5fff */
      memory_map->ext_ram_bank = value & MBC3_RAM_BANK_SELECT_MASK;
      break;
    default:
      break;
  }
}

static Result init_memory_map(Emulator* e) {
  MemoryMap* memory_map = &e->memory_map;
  ZERO_MEMORY(*memory_map);
  memory_map->rom_bank = 1;
  memory_map->rom_bank_mask = s_rom_bank_mask[e->rom_info.rom_size];
  memory_map->ext_ram_addr_mask = s_ext_ram_addr_mask[e->rom_info.ext_ram_size];
  memory_map->read_work_ram_bank_switch = gb_read_work_ram_bank_switch;
  memory_map->write_work_ram_bank_switch = gb_write_work_ram_bank_switch;

  switch (s_ext_ram_type[e->rom_info.cartridge_type]) {
    case EXT_RAM_TYPE_WITH_RAM:
      assert(is_ext_ram_size_valid(e->rom_info.ext_ram_size));
      memory_map->read_ext_ram = gb_read_ext_ram;
      memory_map->write_ext_ram = gb_write_ext_ram;
      e->ext_ram.size = s_ext_ram_byte_size[e->rom_info.ext_ram_size];
      break;
    default:
    case EXT_RAM_TYPE_NO_RAM:
      memory_map->read_ext_ram = dummy_read_ext_ram;
      memory_map->write_ext_ram = dummy_write_ext_ram;
      e->ext_ram.size = 0;
      break;
  }

  switch (s_mbc_type[e->rom_info.cartridge_type]) {
    case MBC_TYPE_NO_MBC:
      memory_map->write_rom = rom_only_write_rom;
      break;
    case MBC_TYPE_MBC1:
      memory_map->write_rom = mbc1_write_rom;
      break;
    case MBC_TYPE_MBC2:
      memory_map->write_rom = mbc2_write_rom;
      memory_map->read_ext_ram = mbc2_read_ram;
      memory_map->write_ext_ram = mbc2_write_ram;
      e->ext_ram.size = MBC2_RAM_SIZE;
      break;
    case MBC_TYPE_MBC3:
      memory_map->write_rom = mbc3_write_rom;
      /* TODO handle MBC3 RTC */
      break;
    default:
      PRINT_ERROR("memory map for %s not implemented.\n",
                  get_cartridge_type_string(e->rom_info.cartridge_type));
      return ERROR;
  }

  e->ext_ram.battery_type = s_battery_type[e->rom_info.cartridge_type];
  return OK;
}

static uint8_t get_f_reg(Registers* reg) {
  return READ_REG(reg->F.Z, CPU_FLAG_Z) |
         READ_REG(reg->F.N, CPU_FLAG_N) |
         READ_REG(reg->F.H, CPU_FLAG_H) |
         READ_REG(reg->F.C, CPU_FLAG_C);
}

static uint16_t get_af_reg(Registers* reg) {
  return (reg->A << 8) | get_f_reg(reg);
}

static void set_af_reg(Registers* reg, uint16_t af) {
  reg->A = af >> 8;
  reg->F.Z = WRITE_REG(af, CPU_FLAG_Z);
  reg->F.N = WRITE_REG(af, CPU_FLAG_N);
  reg->F.H = WRITE_REG(af, CPU_FLAG_H);
  reg->F.C = WRITE_REG(af, CPU_FLAG_C);
}

static Result init_emulator(Emulator* e,
                            RomData* rom_data,
                            AudioBuffer* audio_buffer) {
  ZERO_MEMORY(*e);
  e->rom_data = *rom_data;
  e->apu.buffer = audio_buffer;
  CHECK(SUCCESS(get_rom_info(rom_data, &e->rom_info)));
  print_rom_info(&e->rom_info);
  CHECK(SUCCESS(init_memory_map(e)));
  set_af_reg(&e->reg, 0x01b0);
  e->reg.BC = 0x0013;
  e->reg.DE = 0x00d8;
  e->reg.HL = 0x014d;
  e->reg.SP = 0xfffe;
  e->reg.PC = 0x0100;
  e->interrupts.IME = FALSE;
  /* Enable apu first, so subsequent writes succeed. */
  write_apu(e, APU_NR52_ADDR, 0xf1);
  write_apu(e, APU_NR11_ADDR, 0x80);
  write_apu(e, APU_NR12_ADDR, 0xf3);
  write_apu(e, APU_NR14_ADDR, 0x80);
  write_apu(e, APU_NR50_ADDR, 0x77);
  write_apu(e, APU_NR51_ADDR, 0xf3);
  /* Turn down the volume on channel1, it is playing by default (because of the
   * GB startup sound), but we don't want to hear it when starting the
   * emulator. */
  e->apu.channel[CHANNEL1].envelope.volume = 0;
  write_io(e, IO_LCDC_ADDR, 0x91);
  write_io(e, IO_SCY_ADDR, 0x00);
  write_io(e, IO_SCX_ADDR, 0x00);
  write_io(e, IO_LYC_ADDR, 0x00);
  write_io(e, IO_BGP_ADDR, 0xfc);
  write_io(e, IO_OBP0_ADDR, 0xff);
  write_io(e, IO_OBP1_ADDR, 0xff);
  write_io(e, IO_IF_ADDR, 0x1);
  write_io(e, IO_IE_ADDR, 0x0);
  return OK;
error:
  return ERROR;
}

static MemoryTypeAddressPair map_address(Address addr) {
  MemoryTypeAddressPair result;
  switch (addr >> 12) {
    case 0x0:
    case 0x1:
    case 0x2:
    case 0x3:
      result.type = MEMORY_MAP_ROM;
      result.addr = addr & ADDR_MASK_16K;
      break;

    case 0x4:
    case 0x5:
    case 0x6:
    case 0x7:
      result.type = MEMORY_MAP_ROM_BANK_SWITCH;
      result.addr = addr & ADDR_MASK_16K;
      break;

    case 0x8:
    case 0x9:
      result.type = MEMORY_MAP_VRAM;
      result.addr = addr & ADDR_MASK_8K;
      break;

    case 0xA:
    case 0xB:
      result.type = MEMORY_MAP_EXT_RAM;
      result.addr = addr & ADDR_MASK_8K;
      break;

    case 0xC:
    case 0xE: /* mirror of 0xc000..0xcfff */
      result.type = MEMORY_MAP_WORK_RAM;
      result.addr = addr & ADDR_MASK_4K;
      break;

    case 0xD:
      result.type = MEMORY_MAP_WORK_RAM_BANK_SWITCH;
      result.addr = addr & ADDR_MASK_4K;
      break;

    case 0xF:
      if (addr < OAM_START_ADDR) {
        /* 0xf000 - 0xfdff: mirror of 0xd000-0xddff */
        result.type = MEMORY_MAP_WORK_RAM_BANK_SWITCH;
        result.addr = addr & ADDR_MASK_4K;
      } else if (addr <= OAM_END_ADDR) {
        /* 0xfe00 - 0xfe9f */
        result.type = MEMORY_MAP_OAM;
        result.addr = addr - OAM_START_ADDR;
      } else if (addr <= UNUSED_END_ADDR) {
        /* 0xfea0 - 0xfeff */
        result.type = MEMORY_MAP_UNUSED;
        result.addr = addr;
      } else if (addr < APU_START_ADDR) {
        /* 0xff00 - 0xff0f */
        result.type = MEMORY_MAP_IO;
        result.addr = addr - IO_START_ADDR;
      } else if (addr < WAVE_RAM_START_ADDR) {
        /* 0xff10 - 0xff2f */
        result.type = MEMORY_MAP_APU;
        result.addr = addr - APU_START_ADDR;
      } else if (addr <= WAVE_RAM_END_ADDR) {
        /* 0xff30 - 0xff3f */
        result.type = MEMORY_MAP_WAVE_RAM;
        result.addr = addr - WAVE_RAM_START_ADDR;
      } else if (addr <= IO_END_ADDR) {
        /* 0xff40 - 0xff7f */
        result.type = MEMORY_MAP_IO;
        result.addr = addr - IO_START_ADDR;
      } else if (addr <= HIGH_RAM_END_ADDR) {
        /* 0xff80 - 0xfffe */
        result.type = MEMORY_MAP_HIGH_RAM;
        result.addr = addr - HIGH_RAM_START_ADDR;
      } else {
        /* 0xffff: IE register */
        result.type = MEMORY_MAP_IO;
        result.addr = addr - IO_START_ADDR;
      }
      break;
  }
  return result;
}

static uint8_t read_vram(Emulator* e, MaskedAddress addr) {
  if (e->ppu.stat.mode == PPU_MODE3) {
    DEBUG(ppu, "read_vram(0x%04x): returning 0xff because in use.\n", addr);
    return INVALID_READ_BYTE;
  } else {
    assert(addr <= ADDR_MASK_8K);
    return e->vram.data[addr];
  }
}

static Bool is_using_oam(Emulator* e) {
  return e->ppu.stat.mode == PPU_MODE2 || e->ppu.stat.mode == PPU_MODE3;
}

static uint8_t read_oam(Emulator* e, MaskedAddress addr) {
  if (is_using_oam(e)) {
    DEBUG(ppu, "read_oam(0x%04x): returning 0xff because in use.\n", addr);
    return INVALID_READ_BYTE;
  }

  uint8_t obj_index = addr >> 2;
  Obj* obj = &e->oam.objs[obj_index];
  switch (addr & 3) {
    case 0: return obj->y + OBJ_Y_OFFSET;
    case 1: return obj->x + OBJ_X_OFFSET;
    case 2: return obj->tile;
    case 3: return obj->byte3;
  }
  UNREACHABLE("invalid OAM address: 0x%04x\n", addr);
}

static uint8_t read_io(Emulator* e, MaskedAddress addr) {
  switch (addr) {
    case IO_JOYP_ADDR: {
      uint8_t result = 0;
      if (e->joypad.joypad_select == JOYPAD_SELECT_BUTTONS ||
          e->joypad.joypad_select == JOYPAD_SELECT_BOTH) {
        result |= READ_REG(e->joypad.start, JOYP_BUTTON_START) |
                  READ_REG(e->joypad.select, JOYP_BUTTON_SELECT) |
                  READ_REG(e->joypad.B, JOYP_BUTTON_B) |
                  READ_REG(e->joypad.A, JOYP_BUTTON_A);
      }

      if (e->joypad.joypad_select == JOYPAD_SELECT_DPAD ||
          e->joypad.joypad_select == JOYPAD_SELECT_BOTH) {
        result |= READ_REG(e->joypad.down, JOYP_DPAD_DOWN) |
                  READ_REG(e->joypad.up, JOYP_DPAD_UP) |
                  READ_REG(e->joypad.left, JOYP_DPAD_LEFT) |
                  READ_REG(e->joypad.right, JOYP_DPAD_RIGHT);
      }

      /* The bits are low when the buttons are pressed. */
      return JOYP_UNUSED |
             READ_REG(e->joypad.joypad_select, JOYP_JOYPAD_SELECT) |
             (~result & JOYP_RESULT_MASK);
    }
    case IO_SB_ADDR:
      return e->serial.SB;
    case IO_SC_ADDR:
      return SC_UNUSED | READ_REG(e->serial.transferring, SC_TRANSFER_START) |
             READ_REG(e->serial.clock, SC_SHIFT_CLOCK);
    case IO_DIV_ADDR:
      INFO(io, "%s(0x%04x [%s]) = 0x%02x [cy: %u].\n", __func__, addr,
           get_io_reg_string(addr), e->timer.div_counter >> 8, e->cycles);
      return e->timer.div_counter >> 8;
    case IO_TIMA_ADDR:
      return e->timer.TIMA;
    case IO_TMA_ADDR:
      return e->timer.TMA;
    case IO_TAC_ADDR:
      return TAC_UNUSED | READ_REG(e->timer.on, TAC_TIMER_ON) |
             READ_REG(e->timer.clock_select, TAC_CLOCK_SELECT);
    case IO_IF_ADDR: {
      uint8_t result = INTERRUPT_UNUSED | e->interrupts.IF;
      INFO(io, "%s(0x%04x [%s]) = 0x%02x [cy: %u].\n", __func__, addr,
           get_io_reg_string(addr), result, e->cycles);
      return result;
    }
    case IO_LCDC_ADDR:
      return READ_REG(e->ppu.lcdc.display, LCDC_DISPLAY) |
             READ_REG(e->ppu.lcdc.window_tile_map_select,
                      LCDC_WINDOW_TILE_MAP_SELECT) |
             READ_REG(e->ppu.lcdc.window_display, LCDC_WINDOW_DISPLAY) |
             READ_REG(e->ppu.lcdc.bg_tile_data_select,
                      LCDC_BG_TILE_DATA_SELECT) |
             READ_REG(e->ppu.lcdc.bg_tile_map_select, LCDC_BG_TILE_MAP_SELECT) |
             READ_REG(e->ppu.lcdc.obj_size, LCDC_OBJ_SIZE) |
             READ_REG(e->ppu.lcdc.obj_display, LCDC_OBJ_DISPLAY) |
             READ_REG(e->ppu.lcdc.bg_display, LCDC_BG_DISPLAY);
    case IO_STAT_ADDR: {
      uint8_t result = STAT_UNUSED |
                       READ_REG(e->ppu.stat.y_compare.irq, STAT_YCOMPARE_INTR) |
                       READ_REG(e->ppu.stat.mode2.irq, STAT_MODE2_INTR) |
                       READ_REG(e->ppu.stat.vblank.irq, STAT_VBLANK_INTR) |
                       READ_REG(e->ppu.stat.hblank.irq, STAT_HBLANK_INTR) |
                       READ_REG(e->ppu.stat.ly_eq_lyc, STAT_YCOMPARE) |
                       READ_REG(e->ppu.stat.mode, STAT_MODE);
      INFO(io, "%s(0x%04x [%s]) = 0x%02x [cy: %u].\n", __func__, addr,
           get_io_reg_string(addr), result, e->cycles);
      return result;
    }
    case IO_SCY_ADDR:
      return e->ppu.SCY;
    case IO_SCX_ADDR:
      return e->ppu.SCX;
    case IO_LY_ADDR:
      INFO(io, "%s(0x%04x [%s]) = %u [cy: %u].\n", __func__, addr,
           get_io_reg_string(addr), e->ppu.LY, e->cycles);
      return e->ppu.LY;
    case IO_LYC_ADDR:
      return e->ppu.LYC;
    case IO_DMA_ADDR:
      return INVALID_READ_BYTE; /* Write only. */
    case IO_BGP_ADDR:
      return READ_REG(e->ppu.bgp.color[3], PALETTE_COLOR3) |
             READ_REG(e->ppu.bgp.color[2], PALETTE_COLOR2) |
             READ_REG(e->ppu.bgp.color[1], PALETTE_COLOR1) |
             READ_REG(e->ppu.bgp.color[0], PALETTE_COLOR0);
    case IO_OBP0_ADDR:
      return READ_REG(e->oam.obp[0].color[3], PALETTE_COLOR3) |
             READ_REG(e->oam.obp[0].color[2], PALETTE_COLOR2) |
             READ_REG(e->oam.obp[0].color[1], PALETTE_COLOR1) |
             READ_REG(e->oam.obp[0].color[0], PALETTE_COLOR0);
    case IO_OBP1_ADDR:
      return READ_REG(e->oam.obp[1].color[3], PALETTE_COLOR3) |
             READ_REG(e->oam.obp[1].color[2], PALETTE_COLOR2) |
             READ_REG(e->oam.obp[1].color[1], PALETTE_COLOR1) |
             READ_REG(e->oam.obp[1].color[0], PALETTE_COLOR0);
    case IO_WY_ADDR:
      return e->ppu.WY;
    case IO_WX_ADDR:
      return e->ppu.WX;
    case IO_IE_ADDR:
      return e->interrupts.IE;
    default:
      INFO(io, "%s(0x%04x [%s]) ignored.\n", __func__, addr,
           get_io_reg_string(addr));
      return INVALID_READ_BYTE;
  }
}

static uint8_t read_nrx1_reg(Channel* channel) {
  return READ_REG(channel->square_wave.duty, NRX1_WAVE_DUTY);
}

static uint8_t read_nrx2_reg(Channel* channel) {
  return READ_REG(channel->envelope.initial_volume, NRX2_INITIAL_VOLUME) |
         READ_REG(channel->envelope.direction, NRX2_ENVELOPE_DIRECTION) |
         READ_REG(channel->envelope.period, NRX2_ENVELOPE_PERIOD);
}

static uint8_t read_nrx4_reg(Channel* channel) {
  return READ_REG(channel->length_enabled, NRX4_LENGTH_ENABLED);
}

static uint8_t read_apu(Emulator* e, MaskedAddress addr) {
  /* APU returns 1 for invalid bits. */
  static uint8_t mask[] = {
      0x80, 0x3f, 0x00, 0xff, 0xbf,                        /* NR10-NR14 */
      0xff, 0x3f, 0x00, 0xff, 0xbf,                        /* NR20-NR24 */
      0x7f, 0xff, 0x9f, 0xff, 0xbf,                        /* NR30-NR34 */
      0xff, 0xff, 0x00, 0x00, 0xbf,                        /* NR40-NR44 */
      0x00, 0x00, 0x70,                                    /* NR50-NR52 */
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff /* Unused. */
  };

  /* addr is relative to APU_START_ADDR. */
  assert(addr < ARRAY_SIZE(mask));
  uint8_t result = mask[addr];

  APU* apu = &e->apu;
  Channel* channel1 = &apu->channel[CHANNEL1];
  Channel* channel2 = &apu->channel[CHANNEL2];
  Channel* channel3 = &apu->channel[CHANNEL3];
  Channel* channel4 = &apu->channel[CHANNEL4];
  Sweep* sweep = &apu->sweep;
  Wave* wave = &apu->wave;
  Noise* noise = &apu->noise;

  switch (addr) {
    case APU_NR10_ADDR: {
      result |= READ_REG(sweep->period, NR10_SWEEP_PERIOD) |
                READ_REG(sweep->direction, NR10_SWEEP_DIRECTION) |
                READ_REG(sweep->shift, NR10_SWEEP_SHIFT);
      break;
    }
    case APU_NR11_ADDR:
      result |= read_nrx1_reg(channel1);
      break;
    case APU_NR12_ADDR:
      result |= read_nrx2_reg(channel1);
      break;
    case APU_NR13_ADDR:
      result |= INVALID_READ_BYTE;
      break;
    case APU_NR14_ADDR:
      result |= read_nrx4_reg(channel1);
      break;
    case APU_NR21_ADDR:
      result |= read_nrx1_reg(channel2);
      break;
    case APU_NR22_ADDR:
      result |= read_nrx2_reg(channel2);
      break;
    case APU_NR23_ADDR:
      result |= INVALID_READ_BYTE;
      break;
    case APU_NR24_ADDR:
      result |= read_nrx4_reg(channel2);
      break;
    case APU_NR30_ADDR:
      result |=
          READ_REG(channel3->dac_enabled, NR30_DAC_ENABLED);
      break;
    case APU_NR31_ADDR:
      result |= INVALID_READ_BYTE;
      break;
    case APU_NR32_ADDR:
      result |= READ_REG(wave->volume, NR32_SELECT_WAVE_VOLUME);
      break;
    case APU_NR33_ADDR:
      result |= INVALID_READ_BYTE;
      break;
    case APU_NR34_ADDR:
      result |= read_nrx4_reg(channel3);
      break;
    case APU_NR41_ADDR:
      result |= INVALID_READ_BYTE;
      break;
    case APU_NR42_ADDR:
      result |= read_nrx2_reg(channel4);
      break;
    case APU_NR43_ADDR:
      result |=
          READ_REG(noise->clock_shift, NR43_CLOCK_SHIFT) |
          READ_REG(noise->lfsr_width, NR43_LFSR_WIDTH) |
          READ_REG(noise->divisor, NR43_DIVISOR);
      break;
    case APU_NR44_ADDR:
      result |= read_nrx4_reg(channel4);
      break;
    case APU_NR50_ADDR:
      result |= READ_REG(apu->so2_output[VIN], NR50_VIN_SO2) |
                READ_REG(apu->so2_volume, NR50_SO2_VOLUME) |
                READ_REG(apu->so1_output[VIN], NR50_VIN_SO1) |
                READ_REG(apu->so1_volume, NR50_SO1_VOLUME);
      break;
    case APU_NR51_ADDR:
      result |= READ_REG(apu->so2_output[SOUND4], NR51_SOUND4_SO2) |
                READ_REG(apu->so2_output[SOUND3], NR51_SOUND3_SO2) |
                READ_REG(apu->so2_output[SOUND2], NR51_SOUND2_SO2) |
                READ_REG(apu->so2_output[SOUND1], NR51_SOUND1_SO2) |
                READ_REG(apu->so1_output[SOUND4], NR51_SOUND4_SO1) |
                READ_REG(apu->so1_output[SOUND3], NR51_SOUND3_SO1) |
                READ_REG(apu->so1_output[SOUND2], NR51_SOUND2_SO1) |
                READ_REG(apu->so1_output[SOUND1], NR51_SOUND1_SO1);
      break;
    case APU_NR52_ADDR:
      result |= READ_REG(apu->enabled, NR52_ALL_SOUND_ENABLED) |
                READ_REG(channel4->status, NR52_SOUND4_ON) |
                READ_REG(channel3->status, NR52_SOUND3_ON) |
                READ_REG(channel2->status, NR52_SOUND2_ON) |
                READ_REG(channel1->status, NR52_SOUND1_ON);
      VERBOSE(apu, "read nr52: 0x%02x de=0x%04x\n", result, e->reg.DE);
      break;
    default:
      break;
  }

  return result;
}

static WaveSample* is_concurrent_wave_ram_access(Emulator* e,
                                                 uint8_t offset_cycles) {
  Wave* wave = &e->apu.wave;
  size_t i;
  for (i = 0; i < ARRAY_SIZE(wave->sample); ++i) {
    if (wave->sample[i].time == e->cycles + offset_cycles) {
      return &wave->sample[i];
    }
  }
  return NULL;
}

static uint8_t read_wave_ram(Emulator* e, MaskedAddress addr) {
  Wave* wave = &e->apu.wave;
  if (e->apu.channel[CHANNEL3].status) {
    /* If the wave channel is playing, the byte is read from the sample
     * position. On DMG, this is only allowed if the read occurs exactly when
     * it is being accessed by the Wave channel.  */
    uint8_t result;
    WaveSample* sample = is_concurrent_wave_ram_access(e, 0);
    if (sample) {
      result = sample->byte;
      DEBUG(apu, "%s(0x%02x) while playing => 0x%02x (cycle: %u)\n", __func__,
            addr, result, e->cycles);
    } else {
      result = INVALID_READ_BYTE;
      DEBUG(apu, "%s(0x%02x) while playing, invalid (0xff) (cycle: %u).\n",
            __func__, addr, e->cycles);
    }
    return result;
  } else {
    return wave->ram[addr];
  }
}

static Bool is_dma_access_ok(Emulator* e, MemoryTypeAddressPair pair) {
  /* TODO: need to figure out bus conflicts during DMA for non-OAM accesses. */
  return e->dma.state != DMA_ACTIVE || pair.type != MEMORY_MAP_OAM;
}

static uint8_t read_u8_no_dma_check(Emulator* e, MemoryTypeAddressPair pair) {
  switch (pair.type) {
    case MEMORY_MAP_ROM:
      return e->rom_data.data[pair.addr];
    case MEMORY_MAP_ROM_BANK_SWITCH: {
      uint8_t rom_bank = e->memory_map.rom_bank;
      uint32_t rom_addr = (rom_bank << ROM_BANK_SHIFT) | pair.addr;
      assert(rom_addr < e->rom_data.size);
      return e->rom_data.data[rom_addr];
    }
    case MEMORY_MAP_VRAM:
      return read_vram(e, pair.addr);
    case MEMORY_MAP_EXT_RAM:
      return e->memory_map.read_ext_ram(e, pair.addr);
    case MEMORY_MAP_WORK_RAM:
      return e->ram.data[pair.addr];
    case MEMORY_MAP_WORK_RAM_BANK_SWITCH:
      return e->memory_map.read_work_ram_bank_switch(e, pair.addr);
    case MEMORY_MAP_OAM:
      return read_oam(e, pair.addr);
    case MEMORY_MAP_UNUSED:
      return 0;
    case MEMORY_MAP_IO: {
      uint8_t value = read_io(e, pair.addr);
      VERBOSE(io, "read_io(0x%04x [%s]) = 0x%02x\n", pair.addr,
              get_io_reg_string(pair.addr), value);
      return value;
    }
    case MEMORY_MAP_APU:
      return read_apu(e, pair.addr);
    case MEMORY_MAP_WAVE_RAM:
      return read_wave_ram(e, pair.addr);
    case MEMORY_MAP_HIGH_RAM:
      return e->hram[pair.addr];
    default:
      UNREACHABLE("invalid address: %u 0x%04x.\n", pair.type, pair.addr);
  }
}

static uint8_t read_u8(Emulator* e, Address addr) {
  MemoryTypeAddressPair pair = map_address(addr);
  if (!is_dma_access_ok(e, pair)) {
    INFO(memory, "%s(0x%04x) during DMA.\n", __func__, addr);
    return INVALID_READ_BYTE;
  }

  return read_u8_no_dma_check(e, pair);
}

static void write_vram_tile_data(Emulator* e,
                                 uint32_t index,
                                 uint32_t plane,
                                 uint32_t y,
                                 uint8_t value) {
  VERBOSE(ppu, "write_vram_tile_data: [%u] (%u, %u) = %u\n", index, plane, y,
          value);
  assert(index < TILE_COUNT);
  Tile* tile = &e->vram.tile[index];
  uint32_t data_index = y * TILE_WIDTH;
  assert(data_index < TILE_WIDTH * TILE_HEIGHT);
  uint8_t* data = &(*tile)[data_index];
  uint8_t i;
  uint8_t mask = 1 << plane;
  uint8_t not_mask = ~mask;
  for (i = 0; i < 8; ++i) {
    data[i] = (data[i] & not_mask) | (((value >> (7 - i)) << plane) & mask);
  }
}

static void write_vram(Emulator* e, MaskedAddress addr, uint8_t value) {
  if (e->ppu.stat.mode == PPU_MODE3) {
    DEBUG(ppu, "%s(0x%04x, 0x%02x) ignored, using vram.\n", __func__, addr,
          value);
    return;
  }

  assert(addr <= ADDR_MASK_8K);
  /* Store the raw data so it doesn't have to be re-packed when reading. */
  e->vram.data[addr] = value;

  if (addr < 0x1800) {
    /* 0x8000-0x97ff: Tile data */
    uint32_t tile_index = addr >> 4;     /* 16 bytes per tile. */
    uint32_t tile_y = (addr >> 1) & 0x7; /* 2 bytes per row. */
    uint32_t plane = addr & 1;
    write_vram_tile_data(e, tile_index, plane, tile_y, value);
    if (tile_index >= 128 && tile_index < 256) {
      /* Mirror tile data from 0x8800-0x8fff as if it were at 0x9800-0x9fff; it
       * isn't actually, but when using TILE_DATA_8800_97FF, the tile indexes
       * work as though it was. */
      write_vram_tile_data(e, tile_index + 256, plane, tile_y, value);
    }
  } else {
    /* 0x9800-0x9fff: Tile map data */
    addr -= 0x1800; /* Adjust to range 0x000-0x7ff. */
    uint32_t map_index = addr >> 10;
    assert(map_index < TILE_MAP_COUNT);
    e->vram.map[map_index][addr & ADDR_MASK_1K] = value;
  }
}

static void write_oam_no_mode_check(Emulator* e,
                                    MaskedAddress addr,
                                    uint8_t value) {
  Obj* obj = &e->oam.objs[addr >> 2];
  switch (addr & 3) {
    case 0:
      obj->y = value - OBJ_Y_OFFSET;
      break;
    case 1:
      obj->x = value - OBJ_X_OFFSET;
      break;
    case 2:
      obj->tile = value;
      break;
    case 3:
      obj->byte3 = value;
      obj->priority = WRITE_REG(value, OBJ_PRIORITY);
      obj->yflip = WRITE_REG(value, OBJ_YFLIP);
      obj->xflip = WRITE_REG(value, OBJ_XFLIP);
      obj->palette = WRITE_REG(value, OBJ_PALETTE);
      break;
  }
}

static void write_oam(Emulator* e, MaskedAddress addr, uint8_t value) {
  if (is_using_oam(e)) {
    INFO(ppu, "%s(0x%04x, 0x%02x): ignored because in use.\n", __func__, addr,
         value);
    return;
  }

  write_oam_no_mode_check(e, addr, value);
}

static void increment_tima(Emulator* e) {
  if (++e->timer.TIMA == 0) {
    INFO(interrupt, ">> trigger TIMER [cy: %u]\n", e->cycles + CPU_MCYCLE);
    e->timer.tima_overflow = TRUE;
    e->interrupts.new_IF |= INTERRUPT_TIMER_MASK;
  }
}

static void write_div_counter(Emulator* e, uint16_t div_counter) {
  if (e->timer.on) {
    uint16_t falling_edge =
        ((e->timer.div_counter ^ div_counter) & ~div_counter);
    if ((falling_edge & s_tima_mask[e->timer.clock_select]) != 0) {
      increment_tima(e);
    }
  }
  e->timer.div_counter = div_counter;
}

static void write_stat(Emulator* e, uint8_t value) {
  if (e->ppu.lcdc.display) {
    Bool hblank =
        e->ppu.stat.trigger_mode == PPU_MODE_HBLANK && !e->ppu.stat.hblank.irq;
    Bool vblank =
        e->ppu.stat.trigger_mode == PPU_MODE_VBLANK && !e->ppu.stat.vblank.irq;
    if (!e->ppu.stat.IF && (hblank || vblank)) {
      INFO(ppu, ">> trigger STAT from write [%c%c] [LY: %u] [cy: %u]\n",
           vblank ? 'V' : '.', hblank ? 'H' : '.', e->ppu.LY,
           e->cycles + CPU_MCYCLE);
      e->interrupts.new_IF |= INTERRUPT_LCD_STAT_MASK;
      e->interrupts.IF |= INTERRUPT_LCD_STAT_MASK;
      e->ppu.stat.IF = TRUE;
    }
  }
  e->ppu.stat.y_compare.irq = WRITE_REG(value, STAT_YCOMPARE_INTR);
  e->ppu.stat.mode2.irq = WRITE_REG(value, STAT_MODE2_INTR);
  e->ppu.stat.vblank.irq = WRITE_REG(value, STAT_VBLANK_INTR);
  e->ppu.stat.hblank.irq = WRITE_REG(value, STAT_HBLANK_INTR);
}

static void check_stat(Emulator* e) {
  if (e->ppu.lcdc.display) {
    Bool hblank =
        e->ppu.stat.trigger_mode == PPU_MODE_HBLANK && e->ppu.stat.hblank.irq;
    Bool vblank =
        e->ppu.stat.trigger_mode == PPU_MODE_VBLANK && e->ppu.stat.vblank.irq;
    Bool mode2 = e->ppu.stat.trigger_mode == PPU_MODE2 && e->ppu.stat.mode2.irq;
    Bool mode2_edge = e->ppu.stat.mode2.trigger && e->ppu.stat.mode2.irq;
    Bool y_compare = e->ppu.stat.new_ly_eq_lyc && e->ppu.stat.y_compare.irq;
    Bool y_compare_edge =
        e->ppu.stat.y_compare.trigger && e->ppu.stat.y_compare.irq;
    if (!e->ppu.stat.IF && (hblank || vblank || mode2_edge || y_compare_edge)) {
      INFO(ppu, ">> trigger STAT [%c%c%c%c] [LY: %u] [cy: %u]\n",
           y_compare_edge ? 'Y' : '.', mode2_edge ? 'O' : '.',
           vblank ? 'V' : '.', hblank ? 'H' : '.', e->ppu.LY,
           e->cycles + CPU_MCYCLE);
      e->interrupts.new_IF |= INTERRUPT_LCD_STAT_MASK;
      if (hblank || mode2_edge) {
        e->interrupts.IF |= INTERRUPT_LCD_STAT_MASK;
      }
    } else if (!(hblank || vblank || mode2 || y_compare)) {
      if (e->ppu.stat.IF) {
        INFO(ppu,
             ">> clear internal STAT IF tmode:%u [%c%c%c%c%c%c] [LY: %u] [cy: "
             "%u]\n",
             e->ppu.stat.trigger_mode,
             e->ppu.stat.y_compare.trigger ? 'y' : '.',
             e->ppu.stat.y_compare.irq ? 'Y' : '.',
             e->ppu.stat.mode2.trigger ? 'o' : '.',
             e->ppu.stat.mode2.irq ? 'O' : '.',
             e->ppu.stat.vblank.irq ? 'V' : '.',
             e->ppu.stat.hblank.irq ? 'H' : '.', e->ppu.LY,
             e->cycles + CPU_MCYCLE);
      }
      e->ppu.stat.IF = FALSE;
    }
  }
}

static void check_ly_eq_lyc(Emulator* e) {
  if (e->ppu.lcdc.display) {
    if (e->ppu.LY == e->ppu.LYC) {
      INFO(ppu, ">> trigger Y compare [LY: %u] [cy: %u]\n", e->ppu.LY,
           e->cycles + CPU_MCYCLE);
      e->ppu.stat.y_compare.trigger = TRUE;
      e->ppu.stat.new_ly_eq_lyc = TRUE;
    } else {
      e->ppu.stat.y_compare.trigger = FALSE;
      e->ppu.stat.ly_eq_lyc = e->ppu.stat.new_ly_eq_lyc = FALSE;
    }
  }
}

static void write_io(Emulator* e, MaskedAddress addr, uint8_t value) {
  DEBUG(io, "%s(0x%04x [%s], 0x%02x) [cy: %u]\n", __func__, addr,
        get_io_reg_string(addr), value, e->cycles);
  switch (addr) {
    case IO_JOYP_ADDR:
      e->joypad.joypad_select = WRITE_REG(value, JOYP_JOYPAD_SELECT);
      break;
    case IO_SB_ADDR:
      e->serial.SB = value;
      break;
    case IO_SC_ADDR:
      e->serial.transferring = WRITE_REG(value, SC_TRANSFER_START);
      e->serial.clock = WRITE_REG(value, SC_SHIFT_CLOCK);
      if (e->serial.transferring) {
        e->serial.cycles = 0;
        e->serial.transferred_bits = 0;
      }
      break;
    case IO_DIV_ADDR:
      write_div_counter(e, 0);
      break;
    case IO_TIMA_ADDR:
      e->timer.TIMA = value;
      break;
    case IO_TMA_ADDR:
      e->timer.TMA = value;
      break;
    case IO_TAC_ADDR: {
      Bool old_timer_on = e->timer.on;
      uint16_t old_tima_mask = s_tima_mask[e->timer.clock_select];
      e->timer.clock_select = WRITE_REG(value, TAC_CLOCK_SELECT);
      e->timer.on = WRITE_REG(value, TAC_TIMER_ON);
      /* TIMA is incremented when a specific bit of div_counter transitions
       * from 1 to 0. This can happen as a result of writing to DIV, or in this
       * case modifying which bit we're looking at. */
      Bool tima_tick = FALSE;
      if (!old_timer_on) {
        uint16_t tima_mask = s_tima_mask[e->timer.clock_select];
        if (e->timer.on) {
          tima_tick = (e->timer.div_counter & old_tima_mask) != 0;
        } else {
          tima_tick = (e->timer.div_counter & old_tima_mask) != 0 &&
                      (e->timer.div_counter & tima_mask) == 0;
        }
        if (tima_tick) {
          increment_tima(e);
        }
      }
      break;
    }
    case IO_IF_ADDR:
      e->interrupts.new_IF = e->interrupts.IF = value;
      break;
    case IO_LCDC_ADDR: {
      LCDControl* lcdc = &e->ppu.lcdc;
      Bool was_enabled = lcdc->display;
      lcdc->display = WRITE_REG(value, LCDC_DISPLAY);
      lcdc->window_tile_map_select =
          WRITE_REG(value, LCDC_WINDOW_TILE_MAP_SELECT);
      lcdc->window_display = WRITE_REG(value, LCDC_WINDOW_DISPLAY);
      lcdc->bg_tile_data_select = WRITE_REG(value, LCDC_BG_TILE_DATA_SELECT);
      lcdc->bg_tile_map_select = WRITE_REG(value, LCDC_BG_TILE_MAP_SELECT);
      lcdc->obj_size = WRITE_REG(value, LCDC_OBJ_SIZE);
      lcdc->obj_display = WRITE_REG(value, LCDC_OBJ_DISPLAY);
      lcdc->bg_display = WRITE_REG(value, LCDC_BG_DISPLAY);
      if (was_enabled ^ lcdc->display) {
        if (lcdc->display) {
          DEBUG(ppu, "Enabling display. [cy: %u]\n", e->cycles);
          e->ppu.display_delay_frames = PPU_ENABLE_DISPLAY_DELAY_FRAMES;
          e->ppu.stat.mode = PPU_MODE_HBLANK;
          e->ppu.stat.next_mode = PPU_MODE3;
          e->ppu.stat.trigger_mode = PPU_MODE2;
          e->ppu.stat.hblank.delay = CPU_MCYCLE;
          e->ppu.stat.mode_cycles = PPU_MODE2_CYCLES;
          e->ppu.LY_cycles = PPU_LINE_CYCLES - CPU_MCYCLE;
          e->ppu.line_cycles = PPU_LINE_CYCLES - CPU_MCYCLE;
          e->ppu.LY = e->ppu.line_y = 0;
          check_ly_eq_lyc(e);
        } else {
          DEBUG(ppu, "Disabling display. [cy: %u]\n", e->cycles);
          e->ppu.stat.mode = PPU_MODE_HBLANK;
          e->ppu.LY = e->ppu.line_y = 0;
          /* Clear the framebuffer. */
          size_t i;
          for (i = 0; i < ARRAY_SIZE(e->frame_buffer); ++i) {
            e->frame_buffer[i] = RGBA_WHITE;
          }
          e->ppu.new_frame_edge = TRUE;
        }
      }
      break;
    }
    case IO_STAT_ADDR:
      write_stat(e, value);
      break;
    case IO_SCY_ADDR:
      e->ppu.SCY = value;
      break;
    case IO_SCX_ADDR:
      e->ppu.SCX = value;
      break;
    case IO_LY_ADDR:
      break;
    case IO_LYC_ADDR:
      e->ppu.LYC = value;
      check_ly_eq_lyc(e);
      break;
    case IO_DMA_ADDR:
      /* DMA can be restarted. */
      e->dma.state =
          (e->dma.state != DMA_INACTIVE ? e->dma.state : DMA_TRIGGERED);
      e->dma.source = map_address(value << 8);
      e->dma.cycles = 0;
      break;
    case IO_BGP_ADDR:
      e->ppu.bgp.color[3] = WRITE_REG(value, PALETTE_COLOR3);
      e->ppu.bgp.color[2] = WRITE_REG(value, PALETTE_COLOR2);
      e->ppu.bgp.color[1] = WRITE_REG(value, PALETTE_COLOR1);
      e->ppu.bgp.color[0] = WRITE_REG(value, PALETTE_COLOR0);
      break;
    case IO_OBP0_ADDR:
      e->oam.obp[0].color[3] = WRITE_REG(value, PALETTE_COLOR3);
      e->oam.obp[0].color[2] = WRITE_REG(value, PALETTE_COLOR2);
      e->oam.obp[0].color[1] = WRITE_REG(value, PALETTE_COLOR1);
      e->oam.obp[0].color[0] = WRITE_REG(value, PALETTE_COLOR0);
      break;
    case IO_OBP1_ADDR:
      e->oam.obp[1].color[3] = WRITE_REG(value, PALETTE_COLOR3);
      e->oam.obp[1].color[2] = WRITE_REG(value, PALETTE_COLOR2);
      e->oam.obp[1].color[1] = WRITE_REG(value, PALETTE_COLOR1);
      e->oam.obp[1].color[0] = WRITE_REG(value, PALETTE_COLOR0);
      break;
    case IO_WY_ADDR:
      e->ppu.WY = value;
      break;
    case IO_WX_ADDR:
      e->ppu.WX = value;
      break;
    case IO_IE_ADDR:
      e->interrupts.IE = value;
      break;
    default:
      INFO(memory, "%s(0x%04x, 0x%02x) ignored.\n", __func__, addr, value);
      break;
  }
}

#define CHANNEL_INDEX(c) ((c) - e->apu.channel)

static void write_nrx1_reg(Emulator* e, Channel* channel, uint8_t value) {
  if (e->apu.enabled) {
    channel->square_wave.duty = WRITE_REG(value, NRX1_WAVE_DUTY);
  }
  channel->length = NRX1_MAX_LENGTH - WRITE_REG(value, NRX1_LENGTH);
  VERBOSE(apu, "write_nrx1_reg(%zu, 0x%02x) length=%u\n",
          CHANNEL_INDEX(channel), value, channel->length);
}

static void write_nrx2_reg(Emulator* e, Channel* channel, uint8_t value) {
  channel->envelope.initial_volume = WRITE_REG(value, NRX2_INITIAL_VOLUME);
  channel->dac_enabled = WRITE_REG(value, NRX2_DAC_ENABLED) != 0;
  if (!channel->dac_enabled) {
    channel->status = FALSE;
    VERBOSE(apu, "write_nrx2_reg(%zu, 0x%02x) dac_enabled = false\n",
            CHANNEL_INDEX(channel), value);
  }
  if (channel->status) {
    VERBOSE(apu, "write_nrx2_reg(%zu, 0x%02x) zombie mode?\n",
            CHANNEL_INDEX(channel), value);
  }
  channel->envelope.direction = WRITE_REG(value, NRX2_ENVELOPE_DIRECTION);
  channel->envelope.period = WRITE_REG(value, NRX2_ENVELOPE_PERIOD);
  VERBOSE(apu, "write_nrx2_reg(%zu, 0x%02x) initial_volume=%u\n",
          CHANNEL_INDEX(channel), value, channel->envelope.initial_volume);
}

static void write_nrx3_reg(Emulator* e, Channel* channel, uint8_t value) {
  channel->frequency &= ~0xff;
  channel->frequency |= value;
}

/* Returns TRUE if this channel was triggered. */
static Bool write_nrx4_reg(Emulator* e,
                           Channel* channel,
                           uint8_t value,
                           uint16_t max_length) {
  Bool trigger = WRITE_REG(value, NRX4_INITIAL);
  Bool was_length_enabled = channel->length_enabled;
  channel->length_enabled = WRITE_REG(value, NRX4_LENGTH_ENABLED);
  channel->frequency &= 0xff;
  channel->frequency |= WRITE_REG(value, NRX4_FREQUENCY_HI) << 8;

  /* Extra length clocking occurs on NRX4 writes if the next APU frame isn't a
   * length counter frame. This only occurs on transition from disabled to
   * enabled. */
  Bool next_frame_is_length = (e->apu.frame & 1) == 1;
  if (!was_length_enabled && channel->length_enabled && !next_frame_is_length &&
      channel->length > 0) {
    channel->length--;
    DEBUG(apu, "%s(%zu, 0x%02x) extra length clock = %u\n", __func__,
          CHANNEL_INDEX(channel), value, channel->length);
    if (!trigger && channel->length == 0) {
      DEBUG(apu, "%s(%zu, 0x%02x) disabling channel.\n", __func__,
            CHANNEL_INDEX(channel), value);
      channel->status = FALSE;
    }
  }

  if (trigger) {
    if (channel->length == 0) {
      channel->length = max_length;
      if (channel->length_enabled && !next_frame_is_length) {
        channel->length--;
      }
      DEBUG(apu, "%s(%zu, 0x%02x) trigger, new length = %u\n", __func__,
            CHANNEL_INDEX(channel), value, channel->length);
    }
    if (channel->dac_enabled) {
      channel->status = TRUE;
    }
  }

  VERBOSE(apu, "write_nrx4_reg(%zu, 0x%02x) trigger=%u length_enabled=%u\n",
          CHANNEL_INDEX(channel), value, trigger, channel->length_enabled);
  return trigger;
}

static void trigger_nrx4_envelope(Emulator* e, Envelope* envelope) {
  envelope->volume = envelope->initial_volume;
  envelope->timer = envelope->period ? envelope->period : ENVELOPE_MAX_PERIOD;
  envelope->automatic = envelope->period != 0;
  /* If the next APU frame will update the envelope, increment the timer. */
  if (e->apu.frame + 1 == FRAME_SEQUENCER_UPDATE_ENVELOPE_FRAME) {
    envelope->timer++;
  }
  DEBUG(apu, "%s: volume=%u, timer=%u\n", __func__, envelope->volume,
        envelope->timer);
}

static uint16_t calculate_sweep_frequency(Sweep* sweep) {
  uint16_t f = sweep->frequency;
  if (sweep->direction == SWEEP_DIRECTION_ADDITION) {
    return f + (f >> sweep->shift);
  } else {
    sweep->calculated_subtract = TRUE;
    return f - (f >> sweep->shift);
  }
}

static void trigger_nr14_reg(Emulator* e, Channel* channel, Sweep* sweep) {
  sweep->enabled = sweep->period || sweep->shift;
  sweep->frequency = channel->frequency;
  sweep->timer = sweep->period ? sweep->period : SWEEP_MAX_PERIOD;
  sweep->calculated_subtract = FALSE;
  if (sweep->shift && calculate_sweep_frequency(sweep) > SOUND_MAX_FREQUENCY) {
    channel->status = FALSE;
    DEBUG(apu, "%s: disabling, sweep overflow.\n", __func__);
  } else {
    DEBUG(apu, "%s: sweep frequency=%u\n", __func__, sweep->frequency);
  }
}

static void trigger_nr34_reg(Emulator* e, Channel* channel, Wave* wave) {
  wave->position = 0;
  wave->cycles = wave->period;
  /* Triggering the wave channel while it is already playing will corrupt the
   * wave RAM. */
  if (wave->playing) {
    WaveSample* sample =
        is_concurrent_wave_ram_access(e, WAVE_SAMPLE_TRIGGER_OFFSET_CYCLES);
    if (sample) {
      assert(sample->position < 32);
      switch (sample->position >> 3) {
        case 0:
          wave->ram[0] = sample->byte;
          break;
        case 1:
        case 2:
        case 3:
          memcpy(&wave->ram[0], &wave->ram[(sample->position >> 1) & 12], 4);
          break;
      }
      DEBUG(apu, "%s: corrupting wave ram. (cy: %u)\n", __func__, e->cycles);
    } else {
      DEBUG(apu, "%s: ignoring write (cy: %u)\n", __func__, e->cycles);
    }
  }
  wave->playing = TRUE;
}

static void trigger_nr44_reg(Emulator* e, Channel* channel, Noise* noise) {
  noise->lfsr = 0x7fff;
}

static void write_wave_period(Emulator* e, Channel* channel, Wave* wave) {
  wave->period = ((SOUND_MAX_FREQUENCY + 1) - channel->frequency) * 2;
  DEBUG(apu, "%s: freq: %u cycle: %u period: %u\n", __func__,
        channel->frequency, wave->cycles, wave->period);
}

static void write_square_wave_period(Channel* channel, SquareWave* wave) {
  wave->period = ((SOUND_MAX_FREQUENCY + 1) - channel->frequency) * 4;
  DEBUG(apu, "%s: freq: %u cycle: %u period: %u\n", __func__,
        channel->frequency, wave->cycles, wave->period);
}

static void write_noise_period(Channel* channel, Noise* noise) {
  static const uint8_t s_divisors[NOISE_DIVISOR_COUNT] = {8,  16, 32, 48,
                                                          64, 80, 96, 112};
  uint8_t divisor = s_divisors[noise->divisor];
  assert(noise->divisor < NOISE_DIVISOR_COUNT);
  noise->period = divisor << noise->clock_shift;
  DEBUG(apu, "%s: divisor: %u clock shift: %u period: %u\n", __func__,
        divisor, noise->clock_shift, noise->period);
}

static void write_apu(Emulator* e, MaskedAddress addr, uint8_t value) {
  if (!e->apu.enabled) {
    if (addr == APU_NR11_ADDR || addr == APU_NR21_ADDR ||
        addr == APU_NR31_ADDR || addr == APU_NR41_ADDR) {
      /* DMG allows writes to the length counters when power is disabled. */
    } else if (addr == APU_NR52_ADDR) {
      /* Always can write to NR52; it's necessary to re-enable power to APU. */
    } else {
      /* Ignore all other writes. */
      DEBUG(apu, "%s(0x%04x [%s], 0x%02x) ignored.\n", __func__, addr,
            get_apu_reg_string(addr), value);
      return;
    }
  }

  APU* apu = &e->apu;
  Channel* channel1 = &apu->channel[CHANNEL1];
  Channel* channel2 = &apu->channel[CHANNEL2];
  Channel* channel3 = &apu->channel[CHANNEL3];
  Channel* channel4 = &apu->channel[CHANNEL4];
  Sweep* sweep = &apu->sweep;
  Wave* wave = &apu->wave;
  Noise* noise = &apu->noise;

  DEBUG(apu, "%s(0x%04x [%s], 0x%02x)\n", __func__, addr,
        get_apu_reg_string(addr), value);
  switch (addr) {
    case APU_NR10_ADDR: {
      SweepDirection old_direction = sweep->direction;
      sweep->period = WRITE_REG(value, NR10_SWEEP_PERIOD);
      sweep->direction = WRITE_REG(value, NR10_SWEEP_DIRECTION);
      sweep->shift = WRITE_REG(value, NR10_SWEEP_SHIFT);
      if (old_direction == SWEEP_DIRECTION_SUBTRACTION &&
          sweep->direction == SWEEP_DIRECTION_ADDITION &&
          sweep->calculated_subtract) {
        channel1->status = FALSE;
      }
      break;
    }
    case APU_NR11_ADDR:
      write_nrx1_reg(e, channel1, value);
      break;
    case APU_NR12_ADDR:
      write_nrx2_reg(e, channel1, value);
      break;
    case APU_NR13_ADDR:
      write_nrx3_reg(e, channel1, value);
      write_square_wave_period(channel1, &channel1->square_wave);
      break;
    case APU_NR14_ADDR: {
      Bool trigger = write_nrx4_reg(e, channel1, value, NRX1_MAX_LENGTH);
      write_square_wave_period(channel1, &channel1->square_wave);
      if (trigger) {
        trigger_nrx4_envelope(e, &channel1->envelope);
        trigger_nr14_reg(e, channel1, sweep);
      }
      break;
    }
    case APU_NR21_ADDR:
      write_nrx1_reg(e, channel2, value);
      break;
    case APU_NR22_ADDR:
      write_nrx2_reg(e, channel2, value);
      break;
    case APU_NR23_ADDR:
      write_nrx3_reg(e, channel2, value);
      write_square_wave_period(channel2, &channel2->square_wave);
      break;
    case APU_NR24_ADDR: {
      Bool trigger = write_nrx4_reg(e, channel2, value, NRX1_MAX_LENGTH);
      write_square_wave_period(channel2, &channel2->square_wave);
      if (trigger) {
        trigger_nrx4_envelope(e, &channel2->envelope);
      }
      break;
    }
    case APU_NR30_ADDR:
      channel3->dac_enabled = WRITE_REG(value, NR30_DAC_ENABLED);
      if (!channel3->dac_enabled) {
        channel3->status = FALSE;
        wave->playing = FALSE;
      }
      break;
    case APU_NR31_ADDR:
      channel3->length = NR31_MAX_LENGTH - value;
      break;
    case APU_NR32_ADDR:
      wave->volume = WRITE_REG(value, NR32_SELECT_WAVE_VOLUME);
      break;
    case APU_NR33_ADDR:
      write_nrx3_reg(e, channel3, value);
      write_wave_period(e, channel3, wave);
      break;
    case APU_NR34_ADDR: {
      Bool trigger = write_nrx4_reg(e, channel3, value, NR31_MAX_LENGTH);
      write_wave_period(e, channel3, wave);
      if (trigger) {
        trigger_nr34_reg(e, channel3, wave);
      }
      break;
    }
    case APU_NR41_ADDR:
      write_nrx1_reg(e, channel4, value);
      break;
    case APU_NR42_ADDR:
      write_nrx2_reg(e, channel4, value);
      break;
    case APU_NR43_ADDR: {
      noise->clock_shift = WRITE_REG(value, NR43_CLOCK_SHIFT);
      noise->lfsr_width = WRITE_REG(value, NR43_LFSR_WIDTH);
      noise->divisor = WRITE_REG(value, NR43_DIVISOR);
      write_noise_period(channel4, noise);
      break;
    }
    case APU_NR44_ADDR: {
      Bool trigger = write_nrx4_reg(e, channel4, value, NRX1_MAX_LENGTH);
      if (trigger) {
        write_noise_period(channel4, noise);
        trigger_nrx4_envelope(e, &channel4->envelope);
        trigger_nr44_reg(e, channel4, noise);
      }
      break;
    }
    case APU_NR50_ADDR:
      apu->so2_output[VIN] = WRITE_REG(value, NR50_VIN_SO2);
      apu->so2_volume = WRITE_REG(value, NR50_SO2_VOLUME);
      apu->so1_output[VIN] = WRITE_REG(value, NR50_VIN_SO1);
      apu->so1_volume = WRITE_REG(value, NR50_SO1_VOLUME);
      break;
    case APU_NR51_ADDR:
      apu->so2_output[SOUND4] = WRITE_REG(value, NR51_SOUND4_SO2);
      apu->so2_output[SOUND3] = WRITE_REG(value, NR51_SOUND3_SO2);
      apu->so2_output[SOUND2] = WRITE_REG(value, NR51_SOUND2_SO2);
      apu->so2_output[SOUND1] = WRITE_REG(value, NR51_SOUND1_SO2);
      apu->so1_output[SOUND4] = WRITE_REG(value, NR51_SOUND4_SO1);
      apu->so1_output[SOUND3] = WRITE_REG(value, NR51_SOUND3_SO1);
      apu->so1_output[SOUND2] = WRITE_REG(value, NR51_SOUND2_SO1);
      apu->so1_output[SOUND1] = WRITE_REG(value, NR51_SOUND1_SO1);
      break;
    case APU_NR52_ADDR: {
      Bool was_enabled = apu->enabled;
      Bool is_enabled = WRITE_REG(value, NR52_ALL_SOUND_ENABLED);
      if (was_enabled && !is_enabled) {
        DEBUG(apu, "Powered down APU. Clearing registers.\n");
        int i;
        for (i = 0; i < APU_REG_COUNT; ++i) {
          switch (i) {
            case APU_NR52_ADDR:
              /* Don't clear. */
              break;
            default:
              write_apu(e, i, 0);
          }
        }
      } else if (!was_enabled && is_enabled) {
        DEBUG(apu, "Powered up APU. Resetting frame and sweep timers.\n");
        apu->frame = 7;
      }
      apu->enabled = is_enabled;
      break;
    }
  }
}

static void write_wave_ram(Emulator* e, MaskedAddress addr, uint8_t value) {
  Wave* wave = &e->apu.wave;
  if (e->apu.channel[CHANNEL3].status) {
    /* If the wave channel is playing, the byte is written to the sample
     * position. On DMG, this is only allowed if the write occurs exactly when
     * it is being accessed by the Wave channel. */
    WaveSample* sample = is_concurrent_wave_ram_access(e, 0);
    if (sample) {
      wave->ram[sample->position >> 1] = value;
      DEBUG(apu, "%s(0x%02x, 0x%02x) while playing.\n", __func__, addr, value);
    }
  } else {
    e->apu.wave.ram[addr] = value;
    DEBUG(apu, "%s(0x%02x, 0x%02x)\n", __func__, addr, value);
  }
}

static void write_u8(Emulator* e, Address addr, uint8_t value) {
  MemoryTypeAddressPair pair = map_address(addr);
  if (!is_dma_access_ok(e, pair)) {
    INFO(memory, "%s(0x%04x, 0x%02x) during DMA.\n", __func__, addr, value);
    return;
  }

  switch (pair.type) {
    case MEMORY_MAP_ROM:
      e->memory_map.write_rom(e, pair.addr, value);
      break;
    case MEMORY_MAP_ROM_BANK_SWITCH:
      /* TODO(binji): cleanup */
      e->memory_map.write_rom(e, pair.addr + 0x4000, value);
      break;
    case MEMORY_MAP_VRAM:
      return write_vram(e, pair.addr, value);
    case MEMORY_MAP_EXT_RAM:
      e->memory_map.write_ext_ram(e, pair.addr, value);
      break;
    case MEMORY_MAP_WORK_RAM:
      e->ram.data[pair.addr] = value;
      break;
    case MEMORY_MAP_WORK_RAM_BANK_SWITCH:
      e->memory_map.write_work_ram_bank_switch(e, pair.addr, value);
      break;
    case MEMORY_MAP_OAM:
      write_oam(e, pair.addr, value);
      break;
    case MEMORY_MAP_UNUSED:
      break;
    case MEMORY_MAP_IO:
      write_io(e, pair.addr, value);
      break;
    case MEMORY_MAP_APU:
      write_apu(e, pair.addr, value);
      break;
    case MEMORY_MAP_WAVE_RAM:
      write_wave_ram(e, pair.addr, value);
      break;
    case MEMORY_MAP_HIGH_RAM:
      VERBOSE(memory, "write_hram(0x%04x, 0x%02x)\n", addr, value);
      e->hram[pair.addr] = value;
      break;
  }
}

static TileMap* get_tile_map(Emulator* e, TileMapSelect select) {
  switch (select) {
    case TILE_MAP_9800_9BFF: return &e->vram.map[0];
    case TILE_MAP_9C00_9FFF: return &e->vram.map[1];
    default: return NULL;
  }
}

static Tile* get_tile_data(Emulator* e, TileDataSelect select) {
  switch (select) {
    case TILE_DATA_8000_8FFF: return &e->vram.tile[0];
    case TILE_DATA_8800_97FF: return &e->vram.tile[256];
    default: return NULL;
  }
}

static uint8_t get_tile_map_palette_index(TileMap* map,
                                          Tile* tiles,
                                          uint8_t x,
                                          uint8_t y) {
  uint8_t tile_index = (*map)[((y >> 3) * TILE_MAP_WIDTH) | (x >> 3)];
  Tile* tile = &tiles[tile_index];
  return (*tile)[(y & 7) * TILE_WIDTH | (x & 7)];
}

static RGBA get_palette_index_rgba(uint8_t palette_index, Palette* palette) {
  return s_color_to_rgba[palette->color[palette_index]];
}

static void render_line(Emulator* e, uint8_t line_y) {
  if (line_y >= SCREEN_HEIGHT) {
    return;
  }

  RGBA* line_data = &e->frame_buffer[line_y * SCREEN_WIDTH];

  uint8_t bg_obj_mask[SCREEN_WIDTH];

  uint8_t sx;
  for (sx = 0; sx < SCREEN_WIDTH; ++sx) {
    bg_obj_mask[sx] = s_color_to_obj_mask[COLOR_WHITE];
    line_data[sx] = RGBA_WHITE;
  }

  if (e->ppu.lcdc.bg_display && !e->config.disable_bg) {
    TileMap* map = get_tile_map(e, e->ppu.lcdc.bg_tile_map_select);
    Tile* tiles = get_tile_data(e, e->ppu.lcdc.bg_tile_data_select);
    Palette* palette = &e->ppu.bgp;
    uint8_t bg_y = line_y + e->ppu.SCY;
    uint8_t bg_x = e->ppu.SCX;
    int sx;
    for (sx = 0; sx < SCREEN_WIDTH; ++sx, ++bg_x) {
      uint8_t palette_index =
          get_tile_map_palette_index(map, tiles, bg_x, bg_y);
      bg_obj_mask[sx] = s_color_to_obj_mask[palette_index];
      line_data[sx] = get_palette_index_rgba(palette_index, palette);
    }
  }

  if (e->ppu.lcdc.window_display && e->ppu.WX <= WINDOW_MAX_X &&
      line_y >= e->ppu.frame_WY && !e->config.disable_window) {
    TileMap* map = get_tile_map(e, e->ppu.lcdc.window_tile_map_select);
    Tile* tiles = get_tile_data(e, e->ppu.lcdc.bg_tile_data_select);
    Palette* palette = &e->ppu.bgp;
    uint8_t win_x = 0;
    int sx = 0;
    if (e->ppu.WX < WINDOW_X_OFFSET) {
      /* Start at the leftmost screen X, but skip N pixels of the window. */
      win_x = WINDOW_X_OFFSET - e->ppu.WX;
    } else {
      /* Start N pixels right of the left of the screen. */
      sx += e->ppu.WX - WINDOW_X_OFFSET;
    }
    for (; sx < SCREEN_WIDTH; ++sx, ++win_x) {
      uint8_t palette_index =
          get_tile_map_palette_index(map, tiles, win_x, e->ppu.win_y);
      bg_obj_mask[sx] = s_color_to_obj_mask[palette_index];
      line_data[sx] = get_palette_index_rgba(palette_index, palette);
    }
    e->ppu.win_y++;
  }

  if (e->ppu.lcdc.obj_display && !e->config.disable_obj) {
    uint8_t obj_height = s_obj_size_to_height[e->ppu.lcdc.obj_size];
    Obj line_objs[OBJ_PER_LINE_COUNT];
    int n;
    int dst = 0;
    /* Put the visible sprites into line_objs, but insert them so sprites with
     * smaller X-coordinates are earlier. Also, store the Y-coordinate relative
     * to the line being drawn, range [0..obj_height). */
    for (n = 0; n < OBJ_COUNT && dst < OBJ_PER_LINE_COUNT; ++n) {
      Obj *src = &e->oam.objs[n];
      uint8_t rel_y = line_y - src->y;
      if (rel_y < obj_height) {
        int j = dst;
        while (j > 0 && src->x < line_objs[j - 1].x) {
          line_objs[j] = line_objs[j - 1];
          j--;
        }
        line_objs[j] = *src;
        line_objs[j].y = rel_y;
        dst++;
      }
    }

    /* Draw in reverse so sprites with higher priority are rendered on top. */
    for (n = dst - 1; n >= 0; --n) {
      Obj* o = &line_objs[n];
      uint8_t oy = o->y;
      assert(oy < obj_height);

      uint8_t* tile_data;
      if (o->yflip) {
        oy = obj_height - 1 - oy;
      }
      if (obj_height == 8) {
        tile_data = &e->vram.tile[o->tile][oy * TILE_HEIGHT];
      } else if (oy < 8) {
        /* Top tile of 8x16 sprite. */
        tile_data = &e->vram.tile[o->tile & 0xfe][oy * TILE_HEIGHT];
      } else {
        /* Bottom tile of 8x16 sprite. */
        tile_data = &e->vram.tile[o->tile | 0x01][(oy - 8) * TILE_HEIGHT];
      }

      Palette* palette = &e->oam.obp[o->palette];
      int d = 1;
      uint8_t sx = o->x;
      if (o->xflip) {
        d = -1;
        tile_data += 7;
      }
      int n;
      for (n = 0; n < 8; ++n, ++sx, tile_data += d) {
        if (sx >= SCREEN_WIDTH) {
          continue;
        }
        if (o->priority == OBJ_PRIORITY_BEHIND_BG && bg_obj_mask[sx] == 0) {
          continue;
        }
        uint8_t palette_index = *tile_data;
        if (palette_index != 0) {
          line_data[sx] = get_palette_index_rgba(palette_index, palette);
        }
      }
    }
  }
}

static void dma_mcycle(Emulator* e) {
  if (e->dma.state == DMA_INACTIVE) {
    return;
  }
  if (e->dma.cycles < DMA_DELAY_CYCLES) {
    e->dma.cycles += CPU_MCYCLE;
    if (e->dma.cycles >= DMA_DELAY_CYCLES) {
      e->dma.cycles = DMA_DELAY_CYCLES;
      e->dma.state = DMA_ACTIVE;
    }
    return;
  }

  uint8_t addr_offset = (e->dma.cycles - DMA_DELAY_CYCLES) >> 2;
  assert(addr_offset < OAM_TRANSFER_SIZE);
  MemoryTypeAddressPair pair = e->dma.source;
  pair.addr += addr_offset;
  uint8_t value = read_u8_no_dma_check(e, pair);
  write_oam_no_mode_check(e, addr_offset, value);
  e->dma.cycles += CPU_MCYCLE;
  if (VALUE_WRAPPED(e->dma.cycles, DMA_CYCLES)) {
    e->dma.state = DMA_INACTIVE;
  }
}

static void trigger_vblank(Emulator* e) {
  e->interrupts.new_IF |= INTERRUPT_VBLANK_MASK;
  if (e->ppu.display_delay_frames == 0) {
    e->ppu.new_frame_edge = TRUE;
  } else {
    e->ppu.display_delay_frames--;
  }
  e->ppu.frame++;
}

static void ppu_mcycle(Emulator* e) {
  PPU* ppu = &e->ppu;
  if (!ppu->lcdc.display) {
    return;
  }

  uint32_t cycle = e->cycles + CPU_MCYCLE;
  PPUMode last_trigger_mode = ppu->stat.trigger_mode;
  Bool last_mode2_trigger = ppu->stat.mode2.trigger;
  Bool last_y_compare_trigger = ppu->stat.y_compare.trigger;

  /* hblank interrupt */
  if (ppu->stat.next_mode == PPU_MODE_HBLANK) {
    ppu->stat.hblank.cycles -= CPU_MCYCLE;
    if (ppu->stat.hblank.cycles <= 0) {
      if (ppu->stat.hblank.delay > 0) {
        ppu->stat.hblank.delay -= CPU_MCYCLE;
      } else {
        INFO(ppu, ">> trigger mode 0 [LY: %u] [cy: %u]\n", ppu->LY, cycle);
        ppu->stat.trigger_mode = PPU_MODE_HBLANK;
        /* Add an aribtrary value so it doesn't retrigger; this value will be
         * reset before the next hblank should occur. */
        ppu->stat.hblank.cycles += PPU_FRAME_CYCLES;
      }
    }
  }

  /* STAT mode */
  ppu->stat.mode_cycles -= CPU_MCYCLE;
  if (ppu->stat.mode_cycles == 0) {
    INFO(ppu, ">> mode %u => %u [cy: %u]\n", ppu->stat.mode,
         ppu->stat.next_mode, cycle);
    PPUMode last_mode = ppu->stat.mode;
    ppu->stat.mode = ppu->stat.next_mode;
    switch (ppu->stat.mode) {
      case PPU_MODE_HBLANK:
        /* Normal Hblank will run until it the line changes, so we don't need
         * to track the mode cycles; it's fine to use an arbitrary value. */
        ppu->stat.mode_cycles =
            (last_mode == PPU_MODE_VBLANK) ? CPU_MCYCLE : PPU_FRAME_CYCLES;
        ppu->stat.next_mode = PPU_MODE2;
        break;
      case PPU_MODE_VBLANK:
        ppu->stat.mode_cycles = PPU_VBLANK_CYCLES - CPU_MCYCLE;
        ppu->stat.next_mode = PPU_MODE_HBLANK;
        break;
      case PPU_MODE2:
        ppu->stat.mode_cycles = PPU_MODE2_CYCLES;
        ppu->stat.next_mode = PPU_MODE3;
        break;
      case PPU_MODE3:
        ppu->stat.trigger_mode = PPU_MODE3;
        ppu->stat.mode_cycles = PPU_MODE3_CYCLES;
        ppu->stat.next_mode = PPU_MODE_HBLANK;
        ppu->stat.hblank.cycles = PPU_MODE3_CYCLES - CPU_MCYCLE;
        render_line(e, ppu->line_y);
        break;
    }
  }

  /* LYC */
  e->ppu.stat.ly_eq_lyc = e->ppu.stat.new_ly_eq_lyc;
  e->ppu.stat.y_compare.trigger = FALSE;
  if (ppu->stat.y_compare.delay > 0) {
    ppu->stat.y_compare.delay -= CPU_MCYCLE;
    if (ppu->stat.y_compare.delay == 0) {
      check_ly_eq_lyc(e);
    }
  }

  /* line_y */
  ppu->stat.mode2.trigger = FALSE;
  ppu->line_cycles -= CPU_MCYCLE;
  if (ppu->line_cycles == 0) {
    ppu->line_cycles = PPU_LINE_CYCLES;
    ppu->line_y++;
    if (ppu->LY < SCREEN_HEIGHT) {
      INFO(ppu, ">> trigger mode 2 [LY: %u] [cy: %u]\n", ppu->LY, cycle);
      ppu->stat.mode2.trigger = TRUE;
      ppu->stat.trigger_mode = PPU_MODE2;
    }
    if (VALUE_WRAPPED(ppu->line_y, SCREEN_HEIGHT_WITH_VBLANK)) {
      ppu->frame_WY = ppu->WY;
      ppu->win_y = 0;
    }
  }

  /* LY */
  ppu->LY_cycles -= CPU_MCYCLE;
  if (ppu->LY_cycles == 0) {
    ++ppu->LY;
    if (ppu->LY < SCREEN_HEIGHT) {
      ppu->stat.next_mode = PPU_MODE2;
      ppu->stat.mode_cycles = CPU_MCYCLE;
    }
    if (ppu->LY == SCREEN_HEIGHT) {
      INFO(ppu, ">> trigger mode 1 [cy: %u]\n", cycle);
      ppu->stat.next_mode = PPU_MODE_VBLANK;
      ppu->stat.mode_cycles = CPU_MCYCLE;
      ppu->stat.trigger_mode = PPU_MODE_VBLANK;
      trigger_vblank(e);
    }
    if (ppu->LY == SCREEN_HEIGHT_WITH_VBLANK - 1) {
      ppu->LY_cycles = CPU_MCYCLE;
    } else if (ppu->LY == SCREEN_HEIGHT_WITH_VBLANK) {
      ppu->LY = 0;
      ppu->LY_cycles = PPU_LINE_CYCLES + PPU_LINE_CYCLES - CPU_MCYCLE;
    } else {
      ppu->LY_cycles = PPU_LINE_CYCLES;
    }
    if (ppu->LY == 0) {
      ppu->stat.y_compare.delay = CPU_MCYCLE;
    } else {
      check_ly_eq_lyc(e);
    }
  }

  if (ppu->stat.trigger_mode != last_trigger_mode ||
      ppu->stat.mode2.trigger != last_mode2_trigger ||
      ppu->stat.y_compare.trigger != last_y_compare_trigger) {
    check_stat(e);
  }
}

static void timer_mcycle(Emulator* e) {
  if (e->timer.on && e->timer.tima_overflow) {
    e->timer.tima_overflow = FALSE;
    e->timer.TIMA = e->timer.TMA;
  }
  write_div_counter(e, e->timer.div_counter + CPU_MCYCLE);
}

static void update_channel_sweep(Channel* channel, Sweep* sweep) {
  if (!sweep->enabled) {
    return;
  }

  uint8_t period = sweep->period;
  if (--sweep->timer == 0) {
    if (period) {
      sweep->timer = period;
      uint16_t new_frequency = calculate_sweep_frequency(sweep);
      if (new_frequency > SOUND_MAX_FREQUENCY) {
        DEBUG(apu, "%s: disabling from sweep overflow\n", __func__);
        channel->status = FALSE;
      } else {
        if (sweep->shift) {
          DEBUG(apu, "%s: updated frequency=%u\n", __func__, new_frequency);
          sweep->frequency = channel->frequency = new_frequency;
          write_square_wave_period(channel, &channel->square_wave);
        }

        /* Perform another overflow check */
        if (calculate_sweep_frequency(sweep) > SOUND_MAX_FREQUENCY) {
          DEBUG(apu, "%s: disabling from 2nd sweep overflow\n", __func__);
          channel->status = FALSE;
        }
      }
    } else {
      sweep->timer = SWEEP_MAX_PERIOD;
    }
  }
}

static uint8_t update_square_wave(Channel* channel, SquareWave* wave) {
  static uint8_t duty[WAVE_DUTY_COUNT][DUTY_CYCLE_COUNT] = {
    {0, 0, 0, 0, 0, 0, 0, 1}, /* WAVE_DUTY_12_5 */
    {1, 0, 0, 0, 0, 0, 0, 1}, /* WAVE_DUTY_25 */
    {1, 0, 0, 0, 0, 1, 1, 1}, /* WAVE_DUTY_50 */
    {0, 1, 1, 1, 1, 1, 1, 0}, /* WAVE_DUTY_75 */
  };

  if (wave->cycles <= APU_CYCLES) {
    wave->cycles += wave->period;
    wave->position++;
    VALUE_WRAPPED(wave->position, DUTY_CYCLE_COUNT);
    wave->sample = duty[wave->duty][wave->position];
  }
  wave->cycles -= APU_CYCLES;
  return wave->sample;
}

static void update_channel_length(Channel* channel) {
  if (channel->length_enabled && channel->length > 0) {
    if (--channel->length == 0) {
      channel->status = FALSE;
    }
  }
}

static void update_channel_envelope(Emulator* e, Channel* channel) {
  Envelope* envelope = &channel->envelope;
  if (envelope->period) {
    if (envelope->automatic && --envelope->timer == 0) {
      envelope->timer = envelope->period;
      if (envelope->direction == ENVELOPE_ATTENUATE) {
        if (envelope->volume > 0) {
          envelope->volume--;
        } else {
          envelope->automatic = FALSE;
        }
      } else {
        if (envelope->volume < ENVELOPE_MAX_VOLUME) {
          envelope->volume++;
        } else {
          envelope->automatic = FALSE;
        }
      }
    }
  } else {
    envelope->timer = ENVELOPE_MAX_PERIOD;
  }
}

static uint8_t update_wave(APU* apu, Wave* wave) {
  if (wave->cycles <= APU_CYCLES) {
    wave->cycles += wave->period;
    wave->position++;
    VALUE_WRAPPED(wave->position, WAVE_SAMPLE_COUNT);
    WaveSample sample;
    sample.time = apu->cycles + wave->cycles;
    sample.position = wave->position;
    sample.byte = wave->ram[wave->position >> 1];
    if ((wave->position & 1) == 0) {
      sample.data = sample.byte >> 4; /* High nybble. */
    } else {
      sample.data = sample.byte & 0x0f; /* Low nybble. */
    }
    wave->sample[1] = wave->sample[0];
    wave->sample[0] = sample;
    VERBOSE(apu, "update_wave: position: %u => %u (cy: %u)\n", wave->position,
            sample.data, sample.time);
  }
  wave->cycles -= APU_CYCLES;
  return wave->sample[0].data;
}

static uint8_t update_noise(APU* apu, Noise* noise) {
  if (noise->clock_shift <= NOISE_MAX_CLOCK_SHIFT &&
      noise->cycles <= APU_CYCLES) {
    noise->cycles += noise->period;
    uint16_t bit = (noise->lfsr ^ (noise->lfsr >> 1)) & 1;
    if (noise->lfsr_width == LFSR_WIDTH_7) {
      noise->lfsr = ((noise->lfsr >> 1) & ~0x40) | (bit << 6);
    } else {
      noise->lfsr = ((noise->lfsr >> 1) & ~0x4000) | (bit << 14);
    }
    noise->sample = ~noise->lfsr & 1;
  }
  noise->cycles -= APU_CYCLES;
  return noise->sample;
}

static uint16_t channelx_sample(Channel* channel, uint8_t sample) {
  assert(channel->status);
  assert(sample < 2);
  assert(channel->envelope.volume < 16);
  /* Convert from a 1-bit sample (with 4-bit volume) to a 16-bit sample. */
  return (sample * channel->envelope.volume) << 12;
}

static uint16_t channel3_sample(Channel* channel, Wave* wave, uint8_t sample) {
  assert(channel->status);
  assert(sample < 16);
  assert(wave->volume < WAVE_VOLUME_COUNT);
  static uint8_t shift[WAVE_VOLUME_COUNT] = {4, 0, 1, 2};
  /* Convert from a 4-bit sample to a 16-bit sample. */
  return (sample >> shift[wave->volume]) << 12;
}

static void write_sample(APU* apu, uint16_t so1, uint16_t so2) {
  AudioBuffer* buffer = apu->buffer;
  assert(buffer->position + 2 <= buffer->end);
  *buffer->position++ = so1;
  *buffer->position++ = so2;
}

static void apu_mcycle(Emulator* e) {
  APU* apu = &e->apu;
  uint8_t i;
  if (!apu->enabled) {
    for (i = 0; i < CPU_MCYCLE; i += APU_CYCLES) {
      write_sample(apu, 0, 0);
    }
    return;
  }

  Channel* channel1 = &apu->channel[CHANNEL1];
  Channel* channel2 = &apu->channel[CHANNEL2];
  Channel* channel3 = &apu->channel[CHANNEL3];
  Channel* channel4 = &apu->channel[CHANNEL4];
  Sweep* sweep = &apu->sweep;
  Wave* wave = &apu->wave;
  Noise* noise = &apu->noise;

  /* Synchronize with CPU cycle counter. */
  apu->cycles = e->cycles;

  for (i = 0; i < CPU_MCYCLE; i += APU_CYCLES) {
    Bool do_length = FALSE;
    Bool do_envelope = FALSE;
    Bool do_sweep = FALSE;
    apu->cycles += APU_CYCLES;
    apu->frame_cycles += APU_CYCLES;
    if (VALUE_WRAPPED(apu->frame_cycles, FRAME_SEQUENCER_CYCLES)) {
      apu->frame++;
      VALUE_WRAPPED(apu->frame, FRAME_SEQUENCER_COUNT);

      switch (apu->frame) {
        case 0: do_length = TRUE; break;
        case 1: break;
        case 2: do_length = do_sweep = TRUE; break;
        case 3: break;
        case 4: do_length = TRUE; break;
        case 5: break;
        case 6: do_length = do_sweep = TRUE; break;
        case 7: do_envelope = TRUE; break;
      }

      VERBOSE(apu, "%s: %c%c%c frame: %u cy: %u\n", __func__,
              do_length ? 'L' : '.', do_envelope ? 'E' : '.',
              do_sweep ? 'S' : '.', apu->frame, e->cycles + i);
    }

    uint16_t sample = 0;
    uint32_t so1_mixed_sample = 0;
    uint32_t so2_mixed_sample = 0;

    /* Channel 1 */
    if (channel1->status) {
      if (do_sweep) update_channel_sweep(channel1, sweep);
      sample = update_square_wave(channel1, &channel1->square_wave);
    }
    if (do_length) update_channel_length(channel1);
    if (channel1->status) {
      if (do_envelope) update_channel_envelope(e, channel1);
      if (!e->config.disable_sound[CHANNEL1]) {
        sample = channelx_sample(channel1, sample);
        if (apu->so1_output[CHANNEL1]) {
          so1_mixed_sample += sample;
        }
        if (apu->so2_output[CHANNEL1]) {
          so2_mixed_sample += sample;
        }
      }
    }

    /* Channel 2 */
    if (channel2->status) {
      sample = update_square_wave(channel2, &channel2->square_wave);
    }
    if (do_length) update_channel_length(channel2);
    if (channel2->status) {
      if (do_envelope) update_channel_envelope(e, channel2);
      sample = channelx_sample(channel2, sample);
      if (!e->config.disable_sound[CHANNEL2]) {
        if (apu->so1_output[CHANNEL2]) {
          so1_mixed_sample += sample;
        }
        if (apu->so2_output[CHANNEL2]) {
          so2_mixed_sample += sample;
        }
      }
    }

    /* Channel 3 */
    if (channel3->status) {
      sample = update_wave(apu, wave);
    }
    if (do_length) update_channel_length(channel3);
    if (channel3->status) {
      sample = channel3_sample(channel3, wave, sample);
      if (!e->config.disable_sound[CHANNEL3]) {
        if (apu->so1_output[CHANNEL3]) {
          so1_mixed_sample += sample;
        }
        if (apu->so2_output[CHANNEL3]) {
          so2_mixed_sample += sample;
        }
      }
    }

    /* Channel 4 */
    if (do_length) update_channel_length(channel4);
    if (channel4->status) {
      sample = update_noise(apu, noise);
      if (do_envelope) update_channel_envelope(e, channel4);
      sample = channelx_sample(channel4, sample);
      if (!e->config.disable_sound[CHANNEL4]) {
        if (apu->so1_output[CHANNEL4]) {
          so1_mixed_sample += sample;
        }
        if (apu->so2_output[CHANNEL4]) {
          so2_mixed_sample += sample;
        }
      }
    }

    so1_mixed_sample *= (apu->so1_volume + 1);
    so1_mixed_sample /= ((SO1_MAX_VOLUME + 1) * CHANNEL_COUNT);
    so2_mixed_sample *= (apu->so2_volume + 1);
    so2_mixed_sample /= ((SO2_MAX_VOLUME + 1) * CHANNEL_COUNT);
    write_sample(apu, so1_mixed_sample, so2_mixed_sample);
  }
}

static void serial_mcycle(Emulator* e) {
  if (!e->serial.transferring) {
    return;
  }
  if (e->serial.clock == SERIAL_CLOCK_INTERNAL) {
    e->serial.cycles += CPU_MCYCLE;
    if (VALUE_WRAPPED(e->serial.cycles, SERIAL_CYCLES)) {
      /* Since we're never connected to another device, always shift in 0xff. */
      e->serial.SB = (e->serial.SB << 1) | 1;
      e->serial.transferred_bits++;
      if (VALUE_WRAPPED(e->serial.transferred_bits, 8)) {
        e->serial.transferring = 0;
        e->interrupts.new_IF |= INTERRUPT_SERIAL_MASK;
      }
    }
  }
}

static void mcycle(Emulator* e) {
  e->interrupts.IF = e->interrupts.new_IF;
  dma_mcycle(e);
  ppu_mcycle(e);
  timer_mcycle(e);
  apu_mcycle(e);
  serial_mcycle(e);
  e->cycles += CPU_MCYCLE;
}

static uint8_t read_u8_cy(Emulator* e, Address addr) {
  mcycle(e);
  uint8_t result = read_u8(e, addr);
  return result;
}

static uint16_t read_u16_cy(Emulator* e, Address addr) {
  uint8_t lo = read_u8_cy(e, addr);
  uint8_t hi = read_u8_cy(e, addr + 1);
  return (hi << 8) | lo;
}

static void write_u8_cy(Emulator* e, Address addr, uint8_t value) {
  mcycle(e);
  write_u8(e, addr, value);
}

static void write_u16_cy(Emulator* e, Address addr, uint16_t value) {
  write_u8_cy(e, addr + 1, value >> 8);
  write_u8_cy(e, addr, value);
}

static uint8_t s_opcode_bytes[] = {
    /*       0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f */
    /* 00 */ 1, 3, 1, 1, 1, 1, 2, 1, 3, 1, 1, 1, 1, 1, 2, 1,
    /* 10 */ 1, 3, 1, 1, 1, 1, 2, 1, 2, 1, 1, 1, 1, 1, 2, 1,
    /* 20 */ 2, 3, 1, 1, 1, 1, 2, 1, 2, 1, 1, 1, 1, 1, 2, 1,
    /* 30 */ 2, 3, 1, 1, 1, 1, 2, 1, 2, 1, 1, 1, 1, 1, 2, 1,
    /* 40 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    /* 50 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    /* 60 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    /* 70 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    /* 80 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    /* 90 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    /* a0 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    /* b0 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    /* c0 */ 1, 1, 3, 3, 3, 1, 2, 1, 1, 1, 3, 2, 3, 3, 2, 1,
    /* d0 */ 1, 1, 3, 0, 3, 1, 2, 1, 1, 1, 3, 0, 3, 0, 2, 1,
    /* e0 */ 2, 1, 1, 0, 0, 1, 2, 1, 2, 1, 3, 0, 0, 0, 2, 1,
    /* f0 */ 2, 1, 1, 1, 0, 1, 2, 1, 2, 1, 3, 1, 0, 0, 2, 1,
};

static const char* s_opcode_mnemonic[256] = {
    "NOP", "LD BC,%hu", "LD (BC),A", "INC BC", "INC B", "DEC B", "LD B,%hhu",
    "RLCA", "LD (%04hXH),SP", "ADD HL,BC", "LD A,(BC)", "DEC BC", "INC C",
    "DEC C", "LD C,%hhu", "RRCA", "STOP", "LD DE,%hu", "LD (DE),A", "INC DE",
    "INC D", "DEC D", "LD D,%hhu", "RLA", "JR %+hhd", "ADD HL,DE", "LD A,(DE)",
    "DEC DE", "INC E", "DEC E", "LD E,%hhu", "RRA", "JR NZ,%+hhd", "LD HL,%hu",
    "LDI (HL),A", "INC HL", "INC H", "DEC H", "LD H,%hhu", "DAA", "JR Z,%+hhd",
    "ADD HL,HL", "LDI A,(HL)", "DEC HL", "INC L", "DEC L", "LD L,%hhu", "CPL",
    "JR NC,%+hhd", "LD SP,%hu", "LDD (HL),A", "INC SP", "INC (HL)", "DEC (HL)",
    "LD (HL),%hhu", "SCF", "JR C,%+hhd", "ADD HL,SP", "LDD A,(HL)", "DEC SP",
    "INC A", "DEC A", "LD A,%hhu", "CCF", "LD B,B", "LD B,C", "LD B,D",
    "LD B,E", "LD B,H", "LD B,L", "LD B,(HL)", "LD B,A", "LD C,B", "LD C,C",
    "LD C,D", "LD C,E", "LD C,H", "LD C,L", "LD C,(HL)", "LD C,A", "LD D,B",
    "LD D,C", "LD D,D", "LD D,E", "LD D,H", "LD D,L", "LD D,(HL)", "LD D,A",
    "LD E,B", "LD E,C", "LD E,D", "LD E,E", "LD E,H", "LD E,L", "LD E,(HL)",
    "LD E,A", "LD H,B", "LD H,C", "LD H,D", "LD H,E", "LD H,H", "LD H,L",
    "LD H,(HL)", "LD H,A", "LD L,B", "LD L,C", "LD L,D", "LD L,E", "LD L,H",
    "LD L,L", "LD L,(HL)", "LD L,A", "LD (HL),B", "LD (HL),C", "LD (HL),D",
    "LD (HL),E", "LD (HL),H", "LD (HL),L", "HALT", "LD (HL),A", "LD A,B",
    "LD A,C", "LD A,D", "LD A,E", "LD A,H", "LD A,L", "LD A,(HL)", "LD A,A",
    "ADD A,B", "ADD A,C", "ADD A,D", "ADD A,E", "ADD A,H", "ADD A,L",
    "ADD A,(HL)", "ADD A,A", "ADC A,B", "ADC A,C", "ADC A,D", "ADC A,E",
    "ADC A,H", "ADC A,L", "ADC A,(HL)", "ADC A,A", "SUB B", "SUB C", "SUB D",
    "SUB E", "SUB H", "SUB L", "SUB (HL)", "SUB A", "SBC B", "SBC C", "SBC D",
    "SBC E", "SBC H", "SBC L", "SBC (HL)", "SBC A", "AND B", "AND C", "AND D",
    "AND E", "AND H", "AND L", "AND (HL)", "AND A", "XOR B", "XOR C", "XOR D",
    "XOR E", "XOR H", "XOR L", "XOR (HL)", "XOR A", "OR B", "OR C", "OR D",
    "OR E", "OR H", "OR L", "OR (HL)", "OR A", "CP B", "CP C", "CP D", "CP E",
    "CP H", "CP L", "CP (HL)", "CP A", "RET NZ", "POP BC", "JP NZ,%04hXH",
    "JP %04hXH", "CALL NZ,%04hXH", "PUSH BC", "ADD A,%hhu", "RST 0", "RET Z",
    "RET", "JP Z,%04hXH", NULL, "CALL Z,%04hXH", "CALL %04hXH", "ADC A,%hhu",
    "RST 8H", "RET NC", "POP DE", "JP NC,%04hXH", NULL, "CALL NC,%04hXH",
    "PUSH DE", "SUB %hhu", "RST 10H", "RET C", "RETI", "JP C,%04hXH", NULL,
    "CALL C,%04hXH", NULL, "SBC A,%hhu", "RST 18H", "LD (FF%02hhXH),A",
    "POP HL", "LD (FF00H+C),A", NULL, NULL, "PUSH HL", "AND %hhu", "RST 20H",
    "ADD SP,%hhd", "JP HL", "LD (%04hXH),A", NULL, NULL, NULL, "XOR %hhu",
    "RST 28H", "LD A,(FF%02hhXH)", "POP AF", "LD A,(FF00H+C)", "DI", NULL,
    "PUSH AF", "OR %hhu", "RST 30H", "LD HL,SP%+hhd", "LD SP,HL",
    "LD A,(%04hXH)", "EI", NULL, NULL, "CP %hhu", "RST 38H",
};

static const char* s_cb_opcode_mnemonic[256] = {
    "RLC B",      "RLC C",   "RLC D",      "RLC E",   "RLC H",      "RLC L",
    "RLC (HL)",   "RLC A",   "RRC B",      "RRC C",   "RRC D",      "RRC E",
    "RRC H",      "RRC L",   "RRC (HL)",   "RRC A",   "RL B",       "RL C",
    "RL D",       "RL E",    "RL H",       "RL L",    "RL (HL)",    "RL A",
    "RR B",       "RR C",    "RR D",       "RR E",    "RR H",       "RR L",
    "RR (HL)",    "RR A",    "SLA B",      "SLA C",   "SLA D",      "SLA E",
    "SLA H",      "SLA L",   "SLA (HL)",   "SLA A",   "SRA B",      "SRA C",
    "SRA D",      "SRA E",   "SRA H",      "SRA L",   "SRA (HL)",   "SRA A",
    "SWAP B",     "SWAP C",  "SWAP D",     "SWAP E",  "SWAP H",     "SWAP L",
    "SWAP (HL)",  "SWAP A",  "SRL B",      "SRL C",   "SRL D",      "SRL E",
    "SRL H",      "SRL L",   "SRL (HL)",   "SRL A",   "BIT 0,B",    "BIT 0,C",
    "BIT 0,D",    "BIT 0,E", "BIT 0,H",    "BIT 0,L", "BIT 0,(HL)", "BIT 0,A",
    "BIT 1,B",    "BIT 1,C", "BIT 1,D",    "BIT 1,E", "BIT 1,H",    "BIT 1,L",
    "BIT 1,(HL)", "BIT 1,A", "BIT 2,B",    "BIT 2,C", "BIT 2,D",    "BIT 2,E",
    "BIT 2,H",    "BIT 2,L", "BIT 2,(HL)", "BIT 2,A", "BIT 3,B",    "BIT 3,C",
    "BIT 3,D",    "BIT 3,E", "BIT 3,H",    "BIT 3,L", "BIT 3,(HL)", "BIT 3,A",
    "BIT 4,B",    "BIT 4,C", "BIT 4,D",    "BIT 4,E", "BIT 4,H",    "BIT 4,L",
    "BIT 4,(HL)", "BIT 4,A", "BIT 5,B",    "BIT 5,C", "BIT 5,D",    "BIT 5,E",
    "BIT 5,H",    "BIT 5,L", "BIT 5,(HL)", "BIT 5,A", "BIT 6,B",    "BIT 6,C",
    "BIT 6,D",    "BIT 6,E", "BIT 6,H",    "BIT 6,L", "BIT 6,(HL)", "BIT 6,A",
    "BIT 7,B",    "BIT 7,C", "BIT 7,D",    "BIT 7,E", "BIT 7,H",    "BIT 7,L",
    "BIT 7,(HL)", "BIT 7,A", "RES 0,B",    "RES 0,C", "RES 0,D",    "RES 0,E",
    "RES 0,H",    "RES 0,L", "RES 0,(HL)", "RES 0,A", "RES 1,B",    "RES 1,C",
    "RES 1,D",    "RES 1,E", "RES 1,H",    "RES 1,L", "RES 1,(HL)", "RES 1,A",
    "RES 2,B",    "RES 2,C", "RES 2,D",    "RES 2,E", "RES 2,H",    "RES 2,L",
    "RES 2,(HL)", "RES 2,A", "RES 3,B",    "RES 3,C", "RES 3,D",    "RES 3,E",
    "RES 3,H",    "RES 3,L", "RES 3,(HL)", "RES 3,A", "RES 4,B",    "RES 4,C",
    "RES 4,D",    "RES 4,E", "RES 4,H",    "RES 4,L", "RES 4,(HL)", "RES 4,A",
    "RES 5,B",    "RES 5,C", "RES 5,D",    "RES 5,E", "RES 5,H",    "RES 5,L",
    "RES 5,(HL)", "RES 5,A", "RES 6,B",    "RES 6,C", "RES 6,D",    "RES 6,E",
    "RES 6,H",    "RES 6,L", "RES 6,(HL)", "RES 6,A", "RES 7,B",    "RES 7,C",
    "RES 7,D",    "RES 7,E", "RES 7,H",    "RES 7,L", "RES 7,(HL)", "RES 7,A",
    "SET 0,B",    "SET 0,C", "SET 0,D",    "SET 0,E", "SET 0,H",    "SET 0,L",
    "SET 0,(HL)", "SET 0,A", "SET 1,B",    "SET 1,C", "SET 1,D",    "SET 1,E",
    "SET 1,H",    "SET 1,L", "SET 1,(HL)", "SET 1,A", "SET 2,B",    "SET 2,C",
    "SET 2,D",    "SET 2,E", "SET 2,H",    "SET 2,L", "SET 2,(HL)", "SET 2,A",
    "SET 3,B",    "SET 3,C", "SET 3,D",    "SET 3,E", "SET 3,H",    "SET 3,L",
    "SET 3,(HL)", "SET 3,A", "SET 4,B",    "SET 4,C", "SET 4,D",    "SET 4,E",
    "SET 4,H",    "SET 4,L", "SET 4,(HL)", "SET 4,A", "SET 5,B",    "SET 5,C",
    "SET 5,D",    "SET 5,E", "SET 5,H",    "SET 5,L", "SET 5,(HL)", "SET 5,A",
    "SET 6,B",    "SET 6,C", "SET 6,D",    "SET 6,E", "SET 6,H",    "SET 6,L",
    "SET 6,(HL)", "SET 6,A", "SET 7,B",    "SET 7,C", "SET 7,D",    "SET 7,E",
    "SET 7,H",    "SET 7,L", "SET 7,(HL)", "SET 7,A",
};

static void print_instruction(Emulator* e, Address addr) {
  uint8_t opcode = read_u8(e, addr);
  if (opcode == 0xcb) {
    uint8_t cb_opcode = read_u8(e, addr + 1);
    const char* mnemonic = s_cb_opcode_mnemonic[cb_opcode];
    printf("0x%04x: cb %02x     %-15s", addr, cb_opcode, mnemonic);
  } else {
    char buffer[64];
    const char* mnemonic = s_opcode_mnemonic[opcode];
    uint8_t bytes = s_opcode_bytes[opcode];
    switch (bytes) {
      case 0:
        printf("0x%04x: %02x        %-15s", addr, opcode, "*INVALID*");
        break;
      case 1:
        printf("0x%04x: %02x        %-15s", addr, opcode, mnemonic);
        break;
      case 2: {
        uint8_t byte = read_u8(e, addr + 1);
        snprintf(buffer, sizeof(buffer), mnemonic, byte);
        printf("0x%04x: %02x %02x     %-15s", addr, opcode, byte, buffer);
        break;
      }
      case 3: {
        uint8_t byte1 = read_u8(e, addr + 1);
        uint8_t byte2 = read_u8(e, addr + 2);
        uint16_t word = (byte2 << 8) | byte1;
        snprintf(buffer, sizeof(buffer), mnemonic, word);
        printf("0x%04x: %02x %02x %02x  %-15s", addr, opcode, byte1, byte2,
               buffer);
        break;
      }
      default:
        UNREACHABLE("invalid opcode byte length.\n");
        break;
    }
  }
}

static void print_emulator_info(Emulator* e) {
  if (!s_never_trace && s_trace && !e->interrupts.halt) {
    printf("A:%02X F:%c%c%c%c BC:%04X DE:%04x HL:%04x SP:%04x PC:%04x",
           e->reg.A, e->reg.F.Z ? 'Z' : '-', e->reg.F.N ? 'N' : '-',
           e->reg.F.H ? 'H' : '-', e->reg.F.C ? 'C' : '-', e->reg.BC, e->reg.DE,
           e->reg.HL, e->reg.SP, e->reg.PC);
    printf(" (cy: %u)", e->cycles);
    if (s_log_level_ppu >= 1) {
      printf(" ppu:%c%u", e->ppu.lcdc.display ? '+' : '-', e->ppu.stat.mode);
    }
    if (s_log_level_ppu >= 2) {
      printf(" LY:%u", e->ppu.LY);
    }
    printf(" |");
    print_instruction(e, e->reg.PC);
    printf("\n");
    if (s_trace_counter > 0) {
      if (--s_trace_counter == 0) {
        s_trace = FALSE;
      }
    }
  }
}

#define NI UNREACHABLE("opcode not implemented!\n")
#define INVALID UNREACHABLE("invalid opcode 0x%02x!\n", opcode);

#define REG(R) e->reg.R
#define FLAG(x) e->reg.F.x
#define INTR(m) e->interrupts.m
#define CY mcycle(e)

#define RA REG(A)
#define RHL REG(HL)
#define RSP REG(SP)
#define RPC REG(PC)
#define FZ FLAG(Z)
#define FC FLAG(C)
#define FH FLAG(H)
#define FN FLAG(N)

#define COND_NZ (!FZ)
#define COND_Z FZ
#define COND_NC (!FC)
#define COND_C FC
#define FZ_EQ0(X) FZ = (uint8_t)(X) == 0
#define FC_ADD(X, Y) FC = ((X) + (Y) > 0xff)
#define FC_ADD16(X, Y) FC = ((X) + (Y) > 0xffff)
#define FC_ADC(X, Y, C) FC = ((X) + (Y) + (C) > 0xff)
#define FC_SUB(X, Y) FC = ((int)(X) - (int)(Y) < 0)
#define FC_SBC(X, Y, C) FC = ((int)(X) - (int)(Y) - (int)(C) < 0)
#define MASK8(X) ((X) & 0xf)
#define MASK16(X) ((X) & 0xfff)
#define FH_ADD(X, Y) FH = (MASK8(X) + MASK8(Y) > 0xf)
#define FH_ADD16(X, Y) FH = (MASK16(X) + MASK16(Y) > 0xfff)
#define FH_ADC(X, Y, C) FH = (MASK8(X) + MASK8(Y) + C > 0xf)
#define FH_SUB(X, Y) FH = ((int)MASK8(X) - (int)MASK8(Y) < 0)
#define FH_SBC(X, Y, C) FH = ((int)MASK8(X) - (int)MASK8(Y) - (int)C < 0)
#define FCH_ADD(X, Y) FC_ADD(X, Y); FH_ADD(X, Y)
#define FCH_ADD16(X, Y) FC_ADD16(X, Y); FH_ADD16(X, Y)
#define FCH_ADC(X, Y, C) FC_ADC(X, Y, C); FH_ADC(X, Y, C)
#define FCH_SUB(X, Y) FC_SUB(X, Y); FH_SUB(X, Y)
#define FCH_SBC(X, Y, C) FC_SBC(X, Y, C); FH_SBC(X, Y, C)

#define READ8(X) read_u8_cy(e, X)
#define READ16(X) read_u16_cy(e, X)
#define WRITE8(X, V) write_u8_cy(e, X, V)
#define WRITE16(X, V) write_u16_cy(e, X, V)
#define READ_N READ8(RPC + 1)
#define READ_NN READ16(RPC + 1)
#define READMR(MR) READ8(REG(MR))
#define WRITEMR(MR, V) WRITE8(REG(MR), V)

#define BASIC_OP_R(R, OP) u = REG(R); OP; REG(R) = u
#define BASIC_OP_MR(MR, OP) u = READMR(MR); OP; WRITEMR(MR, u)

#define ADD_FLAGS(X, Y) FZ_EQ0((X) + (Y)); FN = 0; FCH_ADD(X, Y)
#define ADD_FLAGS16(X, Y) FN = 0; FCH_ADD16(X, Y)
#define ADD_SP_FLAGS(Y) FZ = FN = 0; FCH_ADD((uint8_t)RSP, (uint8_t)Y)
#define ADD_R(R) ADD_FLAGS(RA, REG(R)); RA += REG(R)
#define ADD_MR(MR) u = READMR(MR); ADD_FLAGS(RA, u); RA += u
#define ADD_N u = READ_N; ADD_FLAGS(RA, u); RA += u
#define ADD_HL_RR(RR) CY; ADD_FLAGS16(RHL, REG(RR)); RHL += REG(RR)
#define ADD_SP_N s = (int8_t)READ_N; ADD_SP_FLAGS(s); RSP += s; CY; CY

#define ADC_FLAGS(X, Y, C) FZ_EQ0((X) + (Y) + (C)); FN = 0; FCH_ADC(X, Y, C)
#define ADC_R(R) u = REG(R); c = FC; ADC_FLAGS(RA, u, c); RA += u + c
#define ADC_MR(MR) u = READMR(MR); c = FC; ADC_FLAGS(RA, u, c); RA += u + c
#define ADC_N u = READ_N; c = FC; ADC_FLAGS(RA, u, c); RA += u + c

#define AND_FLAGS FZ_EQ0(RA); FH = 1; FN = FC = 0
#define AND_R(R) RA &= REG(R); AND_FLAGS
#define AND_MR(MR) RA &= READMR(MR); AND_FLAGS
#define AND_N RA &= READ_N; AND_FLAGS

#define BIT_FLAGS(BIT, X) FZ_EQ0(X & (1 << BIT)); FN = 0; FH = 1
#define BIT_R(BIT, R) u = REG(R); BIT_FLAGS(BIT, u)
#define BIT_MR(BIT, MR) u = READMR(MR); BIT_FLAGS(BIT, u)

#define CALL(X) CY; RSP -= 2; WRITE16(RSP, new_pc); new_pc = X
#define CALL_NN u16 = READ_NN; CALL(u16)
#define CALL_F_NN(COND) u16 = READ_NN; if (COND) { CALL(u16); }

#define CCF FC ^= 1; FN = FH = 0

#define CP_FLAGS(X, Y) FZ_EQ0(X - Y); FN = 1; FCH_SUB(X, Y)
#define CP_R(R) CP_FLAGS(RA, REG(R))
#define CP_N u = READ_N; CP_FLAGS(RA, u)
#define CP_MR(MR) u = READMR(MR); CP_FLAGS(RA, u)

#define CPL RA = ~RA; FN = FH = 1

#define DAA                              \
  do {                                   \
    u = 0;                               \
    if (FH || (!FN && (RA & 0xf) > 9)) { \
      u = 6;                             \
    }                                    \
    if (FC || (!FN && RA > 0x99)) {      \
      u |= 0x60;                         \
      FC = 1;                            \
    }                                    \
    RA += FN ? -u : u;                   \
    FZ_EQ0(RA);                          \
    FH = 0;                              \
  } while (0)

#define DEC u--
#define DEC_FLAGS FZ_EQ0(u); FN = 1; FH = MASK8(u) == 0xf
#define DEC_R(R) BASIC_OP_R(R, DEC); DEC_FLAGS
#define DEC_RR(RR) REG(RR)--; CY
#define DEC_MR(MR) BASIC_OP_MR(MR, DEC); DEC_FLAGS

#define DI INTR(IME) = INTR(enable) = FALSE;
#define EI INTR(enable) = TRUE;

#define HALT                        \
  do {                              \
    INTR(halt) = TRUE;              \
    INTR(halt_DI) = INTR(IME) == 0; \
  } while (0)

#define INC u++
#define INC_FLAGS FZ_EQ0(u); FN = 0; FH = MASK8(u) == 0
#define INC_R(R) BASIC_OP_R(R, INC); INC_FLAGS
#define INC_RR(RR) REG(RR)++; CY
#define INC_MR(MR) BASIC_OP_MR(MR, INC); INC_FLAGS

#define JP new_pc = u16
#define JP_F_NN(COND) u16 = READ_NN; if (COND) { JP; CY; }
#define JP_RR(RR) u16 = REG(RR); JP
#define JP_NN u16 = READ_NN; JP; CY

#define JR new_pc += s; CY
#define JR_F_N(COND) s = READ_N; if (COND) { JR; }
#define JR_N s = READ_N; JR

#define LD_R_R(RD, RS) REG(RD) = REG(RS)
#define LD_R_N(R) REG(R) = READ_N
#define LD_RR_RR(RRD, RRS) REG(RRD) = REG(RRS); CY
#define LD_RR_NN(RR) REG(RR) = READ_NN
#define LD_R_MR(R, MR) REG(R) = READMR(MR)
#define LD_R_MN(R) REG(R) = READ8(READ_NN)
#define LD_MR_R(MR, R) WRITEMR(MR, REG(R))
#define LD_MR_N(MR) WRITEMR(MR, READ_N)
#define LD_MN_R(R) WRITE8(READ_NN, REG(R))
#define LD_MFF00_N_R(R) WRITE8(0xFF00 + READ_N, RA)
#define LD_MFF00_R_R(R1, R2) WRITE8(0xFF00 + REG(R1), REG(R2))
#define LD_R_MFF00_N(R) REG(R) = READ8(0xFF00 + READ_N)
#define LD_R_MFF00_R(R1, R2) REG(R1) = READ8(0xFF00 + REG(R2))
#define LD_MNN_SP u16 = READ_NN; WRITE16(u16, RSP)
#define LD_HL_SP_N s = (int8_t)READ_N; ADD_SP_FLAGS(s); RHL = RSP + s; CY

#define OR_FLAGS FZ_EQ0(RA); FN = FH = FC = 0
#define OR_R(R) RA |= REG(R); OR_FLAGS
#define OR_MR(MR) RA |= READMR(MR); OR_FLAGS
#define OR_N RA |= READ_N; OR_FLAGS

#define POP_RR(RR) REG(RR) = READ16(RSP); RSP += 2
#define POP_AF set_af_reg(&e->reg, READ16(RSP)); RSP += 2

#define PUSH_RR(RR) CY; RSP -= 2; WRITE16(RSP, REG(RR))
#define PUSH_AF CY; RSP -= 2; WRITE16(RSP, get_af_reg(&e->reg))

#define RES(BIT) u &= ~(1 << (BIT))
#define RES_R(BIT, R) BASIC_OP_R(R, RES(BIT))
#define RES_MR(BIT, MR) BASIC_OP_MR(MR, RES(BIT))

#define RET new_pc = READ16(RSP); RSP += 2; CY
#define RET_F(COND) CY; if (COND) { RET; }
#define RETI INTR(enable) = FALSE; INTR(IME) = TRUE; RET

#define RL c = (u >> 7) & 1; u = (u << 1) | FC; FC = c
#define RL_FLAGS FZ_EQ0(u); FN = FH = 0
#define RLA BASIC_OP_R(A, RL); FZ = FN = FH = 0
#define RL_R(R) BASIC_OP_R(R, RL); RL_FLAGS
#define RL_MR(MR) BASIC_OP_MR(MR, RL); RL_FLAGS

#define RLC c = (u >> 7) & 1; u = (u << 1) | c; FC = c
#define RLC_FLAGS FZ_EQ0(u); FN = FH = 0
#define RLCA BASIC_OP_R(A, RLC); FZ = FN = FH = 0
#define RLC_R(R) BASIC_OP_R(R, RLC); RLC_FLAGS
#define RLC_MR(MR) BASIC_OP_MR(MR, RLC); RLC_FLAGS

#define RR c = u & 1; u = (FC << 7) | (u >> 1); FC = c
#define RR_FLAGS FZ_EQ0(u); FN = FH = 0
#define RRA BASIC_OP_R(A, RR); FZ = FN = FH = 0
#define RR_R(R) BASIC_OP_R(R, RR); RR_FLAGS
#define RR_MR(MR) BASIC_OP_MR(MR, RR); RR_FLAGS

#define RRC c = u & 1; u = (c << 7) | (u >> 1); FC = c
#define RRC_FLAGS FZ_EQ0(u); FN = FH = 0
#define RRCA BASIC_OP_R(A, RRC); FZ = FN = FH = 0
#define RRC_R(R) BASIC_OP_R(R, RRC); RRC_FLAGS
#define RRC_MR(MR) BASIC_OP_MR(MR, RRC); RRC_FLAGS

#define SCF FC = 1; FN = FH = 0

#define SET(BIT) u |= (1 << BIT)
#define SET_R(BIT, R) BASIC_OP_R(R, SET(BIT))
#define SET_MR(BIT, MR) BASIC_OP_MR(MR, SET(BIT))

#define SLA FC = (u >> 7) & 1; u <<= 1
#define SLA_FLAGS FZ_EQ0(u); FN = FH = 0
#define SLA_R(R) BASIC_OP_R(R, SLA); SLA_FLAGS
#define SLA_MR(MR) BASIC_OP_MR(MR, SLA); SLA_FLAGS

#define SRA FC = u & 1; u = (int8_t)u >> 1
#define SRA_FLAGS FZ_EQ0(u); FN = FH = 0
#define SRA_R(R) BASIC_OP_R(R, SRA); SRA_FLAGS
#define SRA_MR(MR) BASIC_OP_MR(MR, SRA); SRA_FLAGS

#define SRL FC = u & 1; u >>= 1
#define SRL_FLAGS FZ_EQ0(u); FN = FH = 0
#define SRL_R(R) BASIC_OP_R(R, SRL); SRL_FLAGS
#define SRL_MR(MR) BASIC_OP_MR(MR, SRL); SRL_FLAGS

#define SUB_FLAGS(X, Y) FZ_EQ0((X) - (Y)); FN = 1; FCH_SUB(X, Y)
#define SUB_R(R) SUB_FLAGS(RA, REG(R)); RA -= REG(R)
#define SUB_MR(MR) u = READMR(MR); SUB_FLAGS(RA, u); RA -= u
#define SUB_N u = READ_N; SUB_FLAGS(RA, u); RA -= u

#define SBC_FLAGS(X, Y, C) FZ_EQ0((X) - (Y) - (C)); FN = 1; FCH_SBC(X, Y, C)
#define SBC_R(R) u = REG(R); c = FC; SBC_FLAGS(RA, u, c); RA -= u + c
#define SBC_MR(MR) u = READMR(MR); c = FC; SBC_FLAGS(RA, u, c); RA -= u + c
#define SBC_N u = READ_N; c = FC; SBC_FLAGS(RA, u, c); RA -= u + c

#define SWAP u = (u << 4) | (u >> 4)
#define SWAP_FLAGS FZ_EQ0(u); FN = FH = FC = 0
#define SWAP_R(R) BASIC_OP_R(R, SWAP); SWAP_FLAGS
#define SWAP_MR(MR) BASIC_OP_MR(MR, SWAP); SWAP_FLAGS

#define XOR_FLAGS FZ_EQ0(RA); FN = FH = FC = 0
#define XOR_R(R) RA ^= REG(R); XOR_FLAGS
#define XOR_MR(MR) RA ^= READMR(MR); XOR_FLAGS
#define XOR_N RA ^= READ_N; XOR_FLAGS

static void execute_instruction(Emulator* e) {
  int8_t s;
  uint8_t u;
  uint16_t u16;
  uint8_t c;
  uint8_t opcode;
  Address new_pc;

  if (INTR(enable)) {
    INTR(enable) = FALSE;
    INTR(IME) = TRUE;
  }

  if (INTR(halt)) {
    mcycle(e);
    return;
  }

  if (INTR(halt_DI)) {
    /* When execution continues after the interrupt occurs, there are no
     * additional cycles spent reading the opcode (perhaps because it has
     * already been fetched?) */
    opcode = read_u8(e, e->reg.PC);
    /* HALT bug. When interrupts are disabled during a HALT, the following byte
     * will be duplicated when decoding. */
    e->reg.PC--;
    INTR(halt_DI) = FALSE;
  } else {
    opcode = read_u8_cy(e, e->reg.PC);
  }
  new_pc = e->reg.PC + s_opcode_bytes[opcode];

  if (opcode == 0xcb) {
    uint8_t opcode = read_u8_cy(e, e->reg.PC + 1);
    switch (opcode) {
      case 0x00: RLC_R(B); break;
      case 0x01: RLC_R(C); break;
      case 0x02: RLC_R(D); break;
      case 0x03: RLC_R(E); break;
      case 0x04: RLC_R(H); break;
      case 0x05: RLC_R(L); break;
      case 0x06: RLC_MR(HL); break;
      case 0x07: RLC_R(A); break;
      case 0x08: RRC_R(B); break;
      case 0x09: RRC_R(C); break;
      case 0x0a: RRC_R(D); break;
      case 0x0b: RRC_R(E); break;
      case 0x0c: RRC_R(H); break;
      case 0x0d: RRC_R(L); break;
      case 0x0e: RRC_MR(HL); break;
      case 0x0f: RRC_R(A); break;
      case 0x10: RL_R(B); break;
      case 0x11: RL_R(C); break;
      case 0x12: RL_R(D); break;
      case 0x13: RL_R(E); break;
      case 0x14: RL_R(H); break;
      case 0x15: RL_R(L); break;
      case 0x16: RL_MR(HL); break;
      case 0x17: RL_R(A); break;
      case 0x18: RR_R(B); break;
      case 0x19: RR_R(C); break;
      case 0x1a: RR_R(D); break;
      case 0x1b: RR_R(E); break;
      case 0x1c: RR_R(H); break;
      case 0x1d: RR_R(L); break;
      case 0x1e: RR_MR(HL); break;
      case 0x1f: RR_R(A); break;
      case 0x20: SLA_R(B); break;
      case 0x21: SLA_R(C); break;
      case 0x22: SLA_R(D); break;
      case 0x23: SLA_R(E); break;
      case 0x24: SLA_R(H); break;
      case 0x25: SLA_R(L); break;
      case 0x26: SLA_MR(HL); break;
      case 0x27: SLA_R(A); break;
      case 0x28: SRA_R(B); break;
      case 0x29: SRA_R(C); break;
      case 0x2a: SRA_R(D); break;
      case 0x2b: SRA_R(E); break;
      case 0x2c: SRA_R(H); break;
      case 0x2d: SRA_R(L); break;
      case 0x2e: SRA_MR(HL); break;
      case 0x2f: SRA_R(A); break;
      case 0x30: SWAP_R(B); break;
      case 0x31: SWAP_R(C); break;
      case 0x32: SWAP_R(D); break;
      case 0x33: SWAP_R(E); break;
      case 0x34: SWAP_R(H); break;
      case 0x35: SWAP_R(L); break;
      case 0x36: SWAP_MR(HL); break;
      case 0x37: SWAP_R(A); break;
      case 0x38: SRL_R(B); break;
      case 0x39: SRL_R(C); break;
      case 0x3a: SRL_R(D); break;
      case 0x3b: SRL_R(E); break;
      case 0x3c: SRL_R(H); break;
      case 0x3d: SRL_R(L); break;
      case 0x3e: SRL_MR(HL); break;
      case 0x3f: SRL_R(A); break;
      case 0x40: BIT_R(0, B); break;
      case 0x41: BIT_R(0, C); break;
      case 0x42: BIT_R(0, D); break;
      case 0x43: BIT_R(0, E); break;
      case 0x44: BIT_R(0, H); break;
      case 0x45: BIT_R(0, L); break;
      case 0x46: BIT_MR(0, HL); break;
      case 0x47: BIT_R(0, A); break;
      case 0x48: BIT_R(1, B); break;
      case 0x49: BIT_R(1, C); break;
      case 0x4a: BIT_R(1, D); break;
      case 0x4b: BIT_R(1, E); break;
      case 0x4c: BIT_R(1, H); break;
      case 0x4d: BIT_R(1, L); break;
      case 0x4e: BIT_MR(1, HL); break;
      case 0x4f: BIT_R(1, A); break;
      case 0x50: BIT_R(2, B); break;
      case 0x51: BIT_R(2, C); break;
      case 0x52: BIT_R(2, D); break;
      case 0x53: BIT_R(2, E); break;
      case 0x54: BIT_R(2, H); break;
      case 0x55: BIT_R(2, L); break;
      case 0x56: BIT_MR(2, HL); break;
      case 0x57: BIT_R(2, A); break;
      case 0x58: BIT_R(3, B); break;
      case 0x59: BIT_R(3, C); break;
      case 0x5a: BIT_R(3, D); break;
      case 0x5b: BIT_R(3, E); break;
      case 0x5c: BIT_R(3, H); break;
      case 0x5d: BIT_R(3, L); break;
      case 0x5e: BIT_MR(3, HL); break;
      case 0x5f: BIT_R(3, A); break;
      case 0x60: BIT_R(4, B); break;
      case 0x61: BIT_R(4, C); break;
      case 0x62: BIT_R(4, D); break;
      case 0x63: BIT_R(4, E); break;
      case 0x64: BIT_R(4, H); break;
      case 0x65: BIT_R(4, L); break;
      case 0x66: BIT_MR(4, HL); break;
      case 0x67: BIT_R(4, A); break;
      case 0x68: BIT_R(5, B); break;
      case 0x69: BIT_R(5, C); break;
      case 0x6a: BIT_R(5, D); break;
      case 0x6b: BIT_R(5, E); break;
      case 0x6c: BIT_R(5, H); break;
      case 0x6d: BIT_R(5, L); break;
      case 0x6e: BIT_MR(5, HL); break;
      case 0x6f: BIT_R(5, A); break;
      case 0x70: BIT_R(6, B); break;
      case 0x71: BIT_R(6, C); break;
      case 0x72: BIT_R(6, D); break;
      case 0x73: BIT_R(6, E); break;
      case 0x74: BIT_R(6, H); break;
      case 0x75: BIT_R(6, L); break;
      case 0x76: BIT_MR(6, HL); break;
      case 0x77: BIT_R(6, A); break;
      case 0x78: BIT_R(7, B); break;
      case 0x79: BIT_R(7, C); break;
      case 0x7a: BIT_R(7, D); break;
      case 0x7b: BIT_R(7, E); break;
      case 0x7c: BIT_R(7, H); break;
      case 0x7d: BIT_R(7, L); break;
      case 0x7e: BIT_MR(7, HL); break;
      case 0x7f: BIT_R(7, A); break;
      case 0x80: RES_R(0, B); break;
      case 0x81: RES_R(0, C); break;
      case 0x82: RES_R(0, D); break;
      case 0x83: RES_R(0, E); break;
      case 0x84: RES_R(0, H); break;
      case 0x85: RES_R(0, L); break;
      case 0x86: RES_MR(0, HL); break;
      case 0x87: RES_R(0, A); break;
      case 0x88: RES_R(1, B); break;
      case 0x89: RES_R(1, C); break;
      case 0x8a: RES_R(1, D); break;
      case 0x8b: RES_R(1, E); break;
      case 0x8c: RES_R(1, H); break;
      case 0x8d: RES_R(1, L); break;
      case 0x8e: RES_MR(1, HL); break;
      case 0x8f: RES_R(1, A); break;
      case 0x90: RES_R(2, B); break;
      case 0x91: RES_R(2, C); break;
      case 0x92: RES_R(2, D); break;
      case 0x93: RES_R(2, E); break;
      case 0x94: RES_R(2, H); break;
      case 0x95: RES_R(2, L); break;
      case 0x96: RES_MR(2, HL); break;
      case 0x97: RES_R(2, A); break;
      case 0x98: RES_R(3, B); break;
      case 0x99: RES_R(3, C); break;
      case 0x9a: RES_R(3, D); break;
      case 0x9b: RES_R(3, E); break;
      case 0x9c: RES_R(3, H); break;
      case 0x9d: RES_R(3, L); break;
      case 0x9e: RES_MR(3, HL); break;
      case 0x9f: RES_R(3, A); break;
      case 0xa0: RES_R(4, B); break;
      case 0xa1: RES_R(4, C); break;
      case 0xa2: RES_R(4, D); break;
      case 0xa3: RES_R(4, E); break;
      case 0xa4: RES_R(4, H); break;
      case 0xa5: RES_R(4, L); break;
      case 0xa6: RES_MR(4, HL); break;
      case 0xa7: RES_R(4, A); break;
      case 0xa8: RES_R(5, B); break;
      case 0xa9: RES_R(5, C); break;
      case 0xaa: RES_R(5, D); break;
      case 0xab: RES_R(5, E); break;
      case 0xac: RES_R(5, H); break;
      case 0xad: RES_R(5, L); break;
      case 0xae: RES_MR(5, HL); break;
      case 0xaf: RES_R(5, A); break;
      case 0xb0: RES_R(6, B); break;
      case 0xb1: RES_R(6, C); break;
      case 0xb2: RES_R(6, D); break;
      case 0xb3: RES_R(6, E); break;
      case 0xb4: RES_R(6, H); break;
      case 0xb5: RES_R(6, L); break;
      case 0xb6: RES_MR(6, HL); break;
      case 0xb7: RES_R(6, A); break;
      case 0xb8: RES_R(7, B); break;
      case 0xb9: RES_R(7, C); break;
      case 0xba: RES_R(7, D); break;
      case 0xbb: RES_R(7, E); break;
      case 0xbc: RES_R(7, H); break;
      case 0xbd: RES_R(7, L); break;
      case 0xbe: RES_MR(7, HL); break;
      case 0xbf: RES_R(7, A); break;
      case 0xc0: SET_R(0, B); break;
      case 0xc1: SET_R(0, C); break;
      case 0xc2: SET_R(0, D); break;
      case 0xc3: SET_R(0, E); break;
      case 0xc4: SET_R(0, H); break;
      case 0xc5: SET_R(0, L); break;
      case 0xc6: SET_MR(0, HL); break;
      case 0xc7: SET_R(0, A); break;
      case 0xc8: SET_R(1, B); break;
      case 0xc9: SET_R(1, C); break;
      case 0xca: SET_R(1, D); break;
      case 0xcb: SET_R(1, E); break;
      case 0xcc: SET_R(1, H); break;
      case 0xcd: SET_R(1, L); break;
      case 0xce: SET_MR(1, HL); break;
      case 0xcf: SET_R(1, A); break;
      case 0xd0: SET_R(2, B); break;
      case 0xd1: SET_R(2, C); break;
      case 0xd2: SET_R(2, D); break;
      case 0xd3: SET_R(2, E); break;
      case 0xd4: SET_R(2, H); break;
      case 0xd5: SET_R(2, L); break;
      case 0xd6: SET_MR(2, HL); break;
      case 0xd7: SET_R(2, A); break;
      case 0xd8: SET_R(3, B); break;
      case 0xd9: SET_R(3, C); break;
      case 0xda: SET_R(3, D); break;
      case 0xdb: SET_R(3, E); break;
      case 0xdc: SET_R(3, H); break;
      case 0xdd: SET_R(3, L); break;
      case 0xde: SET_MR(3, HL); break;
      case 0xdf: SET_R(3, A); break;
      case 0xe0: SET_R(4, B); break;
      case 0xe1: SET_R(4, C); break;
      case 0xe2: SET_R(4, D); break;
      case 0xe3: SET_R(4, E); break;
      case 0xe4: SET_R(4, H); break;
      case 0xe5: SET_R(4, L); break;
      case 0xe6: SET_MR(4, HL); break;
      case 0xe7: SET_R(4, A); break;
      case 0xe8: SET_R(5, B); break;
      case 0xe9: SET_R(5, C); break;
      case 0xea: SET_R(5, D); break;
      case 0xeb: SET_R(5, E); break;
      case 0xec: SET_R(5, H); break;
      case 0xed: SET_R(5, L); break;
      case 0xee: SET_MR(5, HL); break;
      case 0xef: SET_R(5, A); break;
      case 0xf0: SET_R(6, B); break;
      case 0xf1: SET_R(6, C); break;
      case 0xf2: SET_R(6, D); break;
      case 0xf3: SET_R(6, E); break;
      case 0xf4: SET_R(6, H); break;
      case 0xf5: SET_R(6, L); break;
      case 0xf6: SET_MR(6, HL); break;
      case 0xf7: SET_R(6, A); break;
      case 0xf8: SET_R(7, B); break;
      case 0xf9: SET_R(7, C); break;
      case 0xfa: SET_R(7, D); break;
      case 0xfb: SET_R(7, E); break;
      case 0xfc: SET_R(7, H); break;
      case 0xfd: SET_R(7, L); break;
      case 0xfe: SET_MR(7, HL); break;
      case 0xff: SET_R(7, A); break;
    }
  } else {
    switch (opcode) {
      case 0x00: break;
      case 0x01: LD_RR_NN(BC); break;
      case 0x02: LD_MR_R(BC, A); break;
      case 0x03: INC_RR(BC); break;
      case 0x04: INC_R(B); break;
      case 0x05: DEC_R(B); break;
      case 0x06: LD_R_N(B); break;
      case 0x07: RLCA; break;
      case 0x08: LD_MNN_SP; break;
      case 0x09: ADD_HL_RR(BC); break;
      case 0x0a: LD_R_MR(A, BC); break;
      case 0x0b: DEC_RR(BC); break;
      case 0x0c: INC_R(C); break;
      case 0x0d: DEC_R(C); break;
      case 0x0e: LD_R_N(C); break;
      case 0x0f: RRCA; break;
      case 0x10: NI; break;
      case 0x11: LD_RR_NN(DE); break;
      case 0x12: LD_MR_R(DE, A); break;
      case 0x13: INC_RR(DE); break;
      case 0x14: INC_R(D); break;
      case 0x15: DEC_R(D); break;
      case 0x16: LD_R_N(D); break;
      case 0x17: RLA; break;
      case 0x18: JR_N; break;
      case 0x19: ADD_HL_RR(DE); break;
      case 0x1a: LD_R_MR(A, DE); break;
      case 0x1b: DEC_RR(DE); break;
      case 0x1c: INC_R(E); break;
      case 0x1d: DEC_R(E); break;
      case 0x1e: LD_R_N(E); break;
      case 0x1f: RRA; break;
      case 0x20: JR_F_N(COND_NZ); break;
      case 0x21: LD_RR_NN(HL); break;
      case 0x22: LD_MR_R(HL, A); REG(HL)++; break;
      case 0x23: INC_RR(HL); break;
      case 0x24: INC_R(H); break;
      case 0x25: DEC_R(H); break;
      case 0x26: LD_R_N(H); break;
      case 0x27: DAA; break;
      case 0x28: JR_F_N(COND_Z); break;
      case 0x29: ADD_HL_RR(HL); break;
      case 0x2a: LD_R_MR(A, HL); REG(HL)++; break;
      case 0x2b: DEC_RR(HL); break;
      case 0x2c: INC_R(L); break;
      case 0x2d: DEC_R(L); break;
      case 0x2e: LD_R_N(L); break;
      case 0x2f: CPL; break;
      case 0x30: JR_F_N(COND_NC); break;
      case 0x31: LD_RR_NN(SP); break;
      case 0x32: LD_MR_R(HL, A); REG(HL)--; break;
      case 0x33: INC_RR(SP); break;
      case 0x34: INC_MR(HL); break;
      case 0x35: DEC_MR(HL); break;
      case 0x36: LD_MR_N(HL); break;
      case 0x37: SCF; break;
      case 0x38: JR_F_N(COND_C); break;
      case 0x39: ADD_HL_RR(SP); break;
      case 0x3a: LD_R_MR(A, HL); REG(HL)--; break;
      case 0x3b: DEC_RR(SP); break;
      case 0x3c: INC_R(A); break;
      case 0x3d: DEC_R(A); break;
      case 0x3e: LD_R_N(A); break;
      case 0x3f: CCF; break;
      case 0x40: LD_R_R(B, B); break;
      case 0x41: LD_R_R(B, C); break;
      case 0x42: LD_R_R(B, D); break;
      case 0x43: LD_R_R(B, E); break;
      case 0x44: LD_R_R(B, H); break;
      case 0x45: LD_R_R(B, L); break;
      case 0x46: LD_R_MR(B, HL); break;
      case 0x47: LD_R_R(B, A); break;
      case 0x48: LD_R_R(C, B); break;
      case 0x49: LD_R_R(C, C); break;
      case 0x4a: LD_R_R(C, D); break;
      case 0x4b: LD_R_R(C, E); break;
      case 0x4c: LD_R_R(C, H); break;
      case 0x4d: LD_R_R(C, L); break;
      case 0x4e: LD_R_MR(C, HL); break;
      case 0x4f: LD_R_R(C, A); break;
      case 0x50: LD_R_R(D, B); break;
      case 0x51: LD_R_R(D, C); break;
      case 0x52: LD_R_R(D, D); break;
      case 0x53: LD_R_R(D, E); break;
      case 0x54: LD_R_R(D, H); break;
      case 0x55: LD_R_R(D, L); break;
      case 0x56: LD_R_MR(D, HL); break;
      case 0x57: LD_R_R(D, A); break;
      case 0x58: LD_R_R(E, B); break;
      case 0x59: LD_R_R(E, C); break;
      case 0x5a: LD_R_R(E, D); break;
      case 0x5b: LD_R_R(E, E); break;
      case 0x5c: LD_R_R(E, H); break;
      case 0x5d: LD_R_R(E, L); break;
      case 0x5e: LD_R_MR(E, HL); break;
      case 0x5f: LD_R_R(E, A); break;
      case 0x60: LD_R_R(H, B); break;
      case 0x61: LD_R_R(H, C); break;
      case 0x62: LD_R_R(H, D); break;
      case 0x63: LD_R_R(H, E); break;
      case 0x64: LD_R_R(H, H); break;
      case 0x65: LD_R_R(H, L); break;
      case 0x66: LD_R_MR(H, HL); break;
      case 0x67: LD_R_R(H, A); break;
      case 0x68: LD_R_R(L, B); break;
      case 0x69: LD_R_R(L, C); break;
      case 0x6a: LD_R_R(L, D); break;
      case 0x6b: LD_R_R(L, E); break;
      case 0x6c: LD_R_R(L, H); break;
      case 0x6d: LD_R_R(L, L); break;
      case 0x6e: LD_R_MR(L, HL); break;
      case 0x6f: LD_R_R(L, A); break;
      case 0x70: LD_MR_R(HL, B); break;
      case 0x71: LD_MR_R(HL, C); break;
      case 0x72: LD_MR_R(HL, D); break;
      case 0x73: LD_MR_R(HL, E); break;
      case 0x74: LD_MR_R(HL, H); break;
      case 0x75: LD_MR_R(HL, L); break;
      case 0x76: HALT; break;
      case 0x77: LD_MR_R(HL, A); break;
      case 0x78: LD_R_R(A, B); break;
      case 0x79: LD_R_R(A, C); break;
      case 0x7a: LD_R_R(A, D); break;
      case 0x7b: LD_R_R(A, E); break;
      case 0x7c: LD_R_R(A, H); break;
      case 0x7d: LD_R_R(A, L); break;
      case 0x7e: LD_R_MR(A, HL); break;
      case 0x7f: LD_R_R(A, A); break;
      case 0x80: ADD_R(B); break;
      case 0x81: ADD_R(C); break;
      case 0x82: ADD_R(D); break;
      case 0x83: ADD_R(E); break;
      case 0x84: ADD_R(H); break;
      case 0x85: ADD_R(L); break;
      case 0x86: ADD_MR(HL); break;
      case 0x87: ADD_R(A); break;
      case 0x88: ADC_R(B); break;
      case 0x89: ADC_R(C); break;
      case 0x8a: ADC_R(D); break;
      case 0x8b: ADC_R(E); break;
      case 0x8c: ADC_R(H); break;
      case 0x8d: ADC_R(L); break;
      case 0x8e: ADC_MR(HL); break;
      case 0x8f: ADC_R(A); break;
      case 0x90: SUB_R(B); break;
      case 0x91: SUB_R(C); break;
      case 0x92: SUB_R(D); break;
      case 0x93: SUB_R(E); break;
      case 0x94: SUB_R(H); break;
      case 0x95: SUB_R(L); break;
      case 0x96: SUB_MR(HL); break;
      case 0x97: SUB_R(A); break;
      case 0x98: SBC_R(B); break;
      case 0x99: SBC_R(C); break;
      case 0x9a: SBC_R(D); break;
      case 0x9b: SBC_R(E); break;
      case 0x9c: SBC_R(H); break;
      case 0x9d: SBC_R(L); break;
      case 0x9e: SBC_MR(HL); break;
      case 0x9f: SBC_R(A); break;
      case 0xa0: AND_R(B); break;
      case 0xa1: AND_R(C); break;
      case 0xa2: AND_R(D); break;
      case 0xa3: AND_R(E); break;
      case 0xa4: AND_R(H); break;
      case 0xa5: AND_R(L); break;
      case 0xa6: AND_MR(HL); break;
      case 0xa7: AND_R(A); break;
      case 0xa8: XOR_R(B); break;
      case 0xa9: XOR_R(C); break;
      case 0xaa: XOR_R(D); break;
      case 0xab: XOR_R(E); break;
      case 0xac: XOR_R(H); break;
      case 0xad: XOR_R(L); break;
      case 0xae: XOR_MR(HL); break;
      case 0xaf: XOR_R(A); break;
      case 0xb0: OR_R(B); break;
      case 0xb1: OR_R(C); break;
      case 0xb2: OR_R(D); break;
      case 0xb3: OR_R(E); break;
      case 0xb4: OR_R(H); break;
      case 0xb5: OR_R(L); break;
      case 0xb6: OR_MR(HL); break;
      case 0xb7: OR_R(A); break;
      case 0xb8: CP_R(B); break;
      case 0xb9: CP_R(C); break;
      case 0xba: CP_R(D); break;
      case 0xbb: CP_R(E); break;
      case 0xbc: CP_R(H); break;
      case 0xbd: CP_R(L); break;
      case 0xbe: CP_MR(HL); break;
      case 0xbf: CP_R(A); break;
      case 0xc0: RET_F(COND_NZ); break;
      case 0xc1: POP_RR(BC); break;
      case 0xc2: JP_F_NN(COND_NZ); break;
      case 0xc3: JP_NN; break;
      case 0xc4: CALL_F_NN(COND_NZ); break;
      case 0xc5: PUSH_RR(BC); break;
      case 0xc6: ADD_N; break;
      case 0xc7: CALL(0x00); break;
      case 0xc8: RET_F(COND_Z); break;
      case 0xc9: RET; break;
      case 0xca: JP_F_NN(COND_Z); break;
      case 0xcb: INVALID; break;
      case 0xcc: CALL_F_NN(COND_Z); break;
      case 0xcd: CALL_NN; break;
      case 0xce: ADC_N; break;
      case 0xcf: CALL(0x08); break;
      case 0xd0: RET_F(COND_NC); break;
      case 0xd1: POP_RR(DE); break;
      case 0xd2: JP_F_NN(COND_NC); break;
      case 0xd3: INVALID; break;
      case 0xd4: CALL_F_NN(COND_NC); break;
      case 0xd5: PUSH_RR(DE); break;
      case 0xd6: SUB_N; break;
      case 0xd7: CALL(0x10); break;
      case 0xd8: RET_F(COND_C); break;
      case 0xd9: RETI; break;
      case 0xda: JP_F_NN(COND_C); break;
      case 0xdb: INVALID; break;
      case 0xdc: CALL_F_NN(COND_C); break;
      case 0xdd: INVALID; break;
      case 0xde: SBC_N; break;
      case 0xdf: CALL(0x18); break;
      case 0xe0: LD_MFF00_N_R(A); break;
      case 0xe1: POP_RR(HL); break;
      case 0xe2: LD_MFF00_R_R(C, A); break;
      case 0xe3: INVALID; break;
      case 0xe4: INVALID; break;
      case 0xe5: PUSH_RR(HL); break;
      case 0xe6: AND_N; break;
      case 0xe7: CALL(0x20); break;
      case 0xe8: ADD_SP_N; break;
      case 0xe9: JP_RR(HL); break;
      case 0xea: LD_MN_R(A); break;
      case 0xeb: INVALID; break;
      case 0xec: INVALID; break;
      case 0xed: INVALID; break;
      case 0xee: XOR_N; break;
      case 0xef: CALL(0x28); break;
      case 0xf0: LD_R_MFF00_N(A); break;
      case 0xf1: POP_AF; break;
      case 0xf2: LD_R_MFF00_R(A, C); break;
      case 0xf3: DI; break;
      case 0xf4: INVALID; break;
      case 0xf5: PUSH_AF; break;
      case 0xf6: OR_N; break;
      case 0xf7: CALL(0x30); break;
      case 0xf8: LD_HL_SP_N; break;
      case 0xf9: LD_RR_RR(SP, HL); break;
      case 0xfa: LD_R_MN(A); break;
      case 0xfb: EI; break;
      case 0xfc: INVALID; break;
      case 0xfd: INVALID; break;
      case 0xfe: CP_N; break;
      case 0xff: CALL(0x38); break;
      default: INVALID; break;
    }
  }
  e->reg.PC = new_pc;
}

static void handle_interrupts(Emulator* e) {
  if (!(e->interrupts.IME || e->interrupts.halt)) {
    return;
  }

  uint8_t interrupts = e->interrupts.new_IF & e->interrupts.IE;
  if (interrupts == 0) {
    return;
  }

  Bool delay = FALSE;
  uint8_t mask = 0;
  Address vector;
  if (interrupts & INTERRUPT_VBLANK_MASK) {
    DEBUG(interrupt, ">> VBLANK interrupt [frame = %u] [cy: %u]\n",
          e->ppu.frame, e->cycles);
    vector = 0x40;
    mask = INTERRUPT_VBLANK_MASK;
  } else if (interrupts & INTERRUPT_LCD_STAT_MASK) {
    DEBUG(interrupt, ">> LCD_STAT interrupt [%c%c%c%c] [cy: %u]\n",
          e->ppu.stat.y_compare.irq ? 'Y' : '.',
          e->ppu.stat.mode2.irq ? 'O' : '.', e->ppu.stat.vblank.irq ? 'V' : '.',
          e->ppu.stat.hblank.irq ? 'H' : '.', e->cycles);
    vector = 0x48;
    mask = INTERRUPT_LCD_STAT_MASK;
  } else if (interrupts & INTERRUPT_TIMER_MASK) {
    DEBUG(interrupt, ">> TIMER interrupt\n");
    vector = 0x50;
    mask = INTERRUPT_TIMER_MASK;
    delay = e->interrupts.halt;
  } else if (interrupts & INTERRUPT_SERIAL_MASK) {
    DEBUG(interrupt, ">> SERIAL interrupt\n");
    vector = 0x58;
    mask = INTERRUPT_SERIAL_MASK;
  } else if (interrupts & INTERRUPT_JOYPAD_MASK) {
    DEBUG(interrupt, ">> JOYPAD interrupt\n");
    vector = 0x60;
    mask = INTERRUPT_JOYPAD_MASK;
  } else {
    INFO(interrupt, "handle_interrupts: Unhandled interrupt!\n");
    return;
  }

  if (delay) {
    mcycle(e);
  }

  if (e->interrupts.halt_DI) {
    DEBUG(interrupt, "Interrupt fired during HALT w/ disabled interrupts.\n");
  } else {
    e->interrupts.new_IF &= ~mask;
    Address new_pc = REG(PC);
    CALL(vector);
    REG(PC) = new_pc;
    e->interrupts.IME = FALSE;
    mcycle(e);
    mcycle(e);
  }
  e->interrupts.halt = FALSE;
}

static void step_emulator(Emulator* e) {
  print_emulator_info(e);
  execute_instruction(e);
  handle_interrupts(e);
}

typedef uint32_t EmulatorEvent;
enum {
  EMULATOR_EVENT_NEW_FRAME = 0x1,
  EMULATOR_EVENT_AUDIO_BUFFER_FULL = 0x2,
  EMULATOR_EVENT_TIMEOUT = 0x4,
};

static void reset_audio_buffer(Emulator* e) {
  e->apu.buffer->position = e->apu.buffer->data;
}

/* TODO: remove this global */
static struct timespec s_start_time;
static double get_time_ms(void) {
  struct timespec from = s_start_time;
  struct timespec to;
  int result = clock_gettime(CLOCK_MONOTONIC, &to);
  assert(result == 0);
  double ms = (double)(to.tv_sec - from.tv_sec) * 1000;
  if (to.tv_nsec < from.tv_nsec) {
    ms -= 1000;
    to.tv_nsec += 1000000000;
  }
  return ms + (double)(to.tv_nsec - from.tv_nsec) / 1000000;
}

static EmulatorEvent run_emulator_until_event(Emulator* e,
                                              EmulatorEvent last_event,
                                              uint32_t requested_samples,
                                              double until_ms) {
  if (last_event & EMULATOR_EVENT_NEW_FRAME) {
    e->ppu.new_frame_edge = FALSE;
  }
  if (last_event & EMULATOR_EVENT_AUDIO_BUFFER_FULL) {
    reset_audio_buffer(e);
  }

  AudioBuffer* buffer = e->apu.buffer;
  assert(requested_samples <= buffer->end - buffer->data);

  EmulatorEvent result = 0;
  Bool running = TRUE;
  while (running) {
    int i;
    for (i = 0; running && i < EMULATOR_INSTRUCTION_QUANTA; ++i) {
      if (e->ppu.new_frame_edge) {
        result |= EMULATOR_EVENT_NEW_FRAME;
        running = FALSE;
      }
      size_t samples = buffer->position - buffer->data;
      if (samples >= requested_samples) {
        result |= EMULATOR_EVENT_AUDIO_BUFFER_FULL;
        running = FALSE;
      }
      step_emulator(e);
    }
    if (get_time_ms() >= until_ms) {
      result |= EMULATOR_EVENT_TIMEOUT;
      running = FALSE;
    }
  }
  return result;
}

/* SDL stuff */

#ifndef NO_SDL

#include <SDL/SDL.h>
#include <SDL/SDL_main.h>

#define RENDER_SCALE 4
#define RENDER_WIDTH (SCREEN_WIDTH * RENDER_SCALE)
#define RENDER_HEIGHT (SCREEN_HEIGHT * RENDER_SCALE)
#define AUDIO_FREQUENCY 44100
#define AUDIO_FORMAT AUDIO_U16SYS
#define AUDIO_CONVERT_SAMPLE_U16SYS(X) (X)
#define AUDIO_CHANNELS 2
#define AUDIO_SAMPLES 4096
#define AUDIO_SAMPLE_SIZE 2
typedef uint16_t AudioBufferSample;
/* Try to keep the audio buffer filled to |number of samples| *
 * AUDIO_TARGET_BUFFER_SIZE_MULTIPLIER samples. */
#define AUDIO_TARGET_BUFFER_SIZE_MULTIPLIER 1.5
#define AUDIO_MAX_BUFFER_SIZE_MULTIPLIER 4
/* One buffer will be requested every AUDIO_BUFFER_REFILL_MS milliseconds. */
#define AUDIO_BUFFER_REFILL_MS \
  ((AUDIO_SAMPLES / AUDIO_CHANNELS) * MILLISECONDS_PER_SECOND / AUDIO_FREQUENCY)
/* If the emulator is running behind by AUDIO_MAX_FAST_DESYNC_MS milliseconds
 * (or ahead by AUDIO_MAX_FAST_DESYNC_MS), it won't try to catch up, and
 * instead just forcibly resync. */
#define AUDIO_MAX_SLOW_DESYNC_MS (0.5 * AUDIO_BUFFER_REFILL_MS)
#define AUDIO_MAX_FAST_DESYNC_MS (2 * AUDIO_BUFFER_REFILL_MS)
#define POLL_EVENT_MS 10.0
#define VIDEO_FRAME_MS \
  ((double)MILLISECONDS_PER_SECOND * PPU_FRAME_CYCLES / CPU_CYCLES_PER_SECOND)
#define SDL_SURFACE_COUNT 2
#define SAVE_EXTENSION ".sav"

static int s_log_level_sdl = 1;

typedef struct {
  SDL_AudioSpec spec;
  uint8_t* buffer;
  uint8_t* buffer_end;
  uint8_t* read_pos;
  uint8_t* write_pos;
  size_t buffer_capacity;         /* Total capacity in bytes of the buffer. */
  size_t buffer_available;        /* Number of bytes available for reading. */
  size_t buffer_target_available; /* Try to keep the buffer this size. */
  uint32_t freq_counter;          /* Counter used for resampling
                                     [0..APU_CYCLES_PER_SECOND). */
  Bool ready; /* Set to TRUE when audio is first rendered. */
} SDLAudio;

typedef struct {
  SDL_Surface* surface[SDL_SURFACE_COUNT];
  uint8_t used_surfaces;
  SDLAudio audio;
  uint32_t last_event_cycles; /* GB CPU cycle count of last event. */
  double last_event_real_ms;  /* Wall clock time of last event. */
} SDL;

static void sdl_destroy(SDL* sdl) {
  int i;
  for (i = 0; i < SDL_SURFACE_COUNT; ++i) {
    if (sdl->surface[i]) {
      SDL_FreeSurface(sdl->surface[i]);
    }
    sdl->surface[i] = NULL;
  }
  SDL_Quit();
}

static Result sdl_init_video(SDL* sdl) {
  CHECK_MSG(SDL_Init(SDL_INIT_EVERYTHING) == 0, "SDL_init failed.\n");
  SDL_Surface* surface = SDL_SetVideoMode(RENDER_WIDTH, RENDER_HEIGHT, 32, 0);
  CHECK_MSG(surface != NULL, "SDL_SetVideoMode failed.\n");
  sdl->surface[0] = surface;
  int i;
  for (i = 1; i < SDL_SURFACE_COUNT; ++i) {
    sdl->surface[i] = SDL_CreateRGBSurface(
        SDL_SWSURFACE, surface->w, surface->h, 32, surface->format->Rmask,
        surface->format->Gmask, surface->format->Bmask, surface->format->Amask);
    CHECK_MSG(sdl->surface[i], "SDL_CreateRGBSurface failed.\n");
  }
  return OK;
error:
  sdl_destroy(sdl);
  return ERROR;
}

static void sdl_audio_callback(void* userdata, uint8_t* dst, int len) {
  SDL* sdl = userdata;
  SDLAudio* audio = &sdl->audio;
  if (len > (int)audio->buffer_available) {
    DEBUG(sdl, "!!! audio underflow. avail %zd < requested %u\n",
          audio->buffer_available, len);
    len = audio->buffer_available;
  }
  if (audio->read_pos + len > audio->buffer_end) {
    size_t len1 = audio->buffer_end - audio->read_pos;
    size_t len2 = len - len1;
    memcpy(dst, audio->read_pos, len1);
    memcpy(dst + len1, audio->buffer, len2);
    audio->read_pos = audio->buffer + len2;
  } else {
    memcpy(dst, audio->read_pos, len);
    audio->read_pos += len;
  }
  audio->buffer_available -= len;
}

static Result sdl_init_audio(SDL* sdl) {
  sdl->last_event_cycles = 0;
  sdl->last_event_real_ms = get_time_ms();

  sdl->audio.spec.freq = AUDIO_FREQUENCY;
  sdl->audio.spec.format = AUDIO_FORMAT;
  sdl->audio.spec.channels = AUDIO_CHANNELS;
  sdl->audio.spec.samples = AUDIO_SAMPLES;
  sdl->audio.spec.callback = sdl_audio_callback;
  sdl->audio.spec.userdata = sdl;
  CHECK_MSG(SDL_OpenAudio(&sdl->audio.spec, NULL) == 0,
            "SDL_OpenAudio failed.\n");

  sdl->audio.buffer_target_available =
      (size_t)(sdl->audio.spec.size * AUDIO_TARGET_BUFFER_SIZE_MULTIPLIER);

  /* Enough for 1 second of audio. */
  size_t buffer_capacity =
      (size_t)(sdl->audio.spec.size * AUDIO_MAX_BUFFER_SIZE_MULTIPLIER);
  sdl->audio.buffer_capacity = buffer_capacity;

  sdl->audio.buffer = malloc(buffer_capacity);
  CHECK_MSG(sdl->audio.buffer != NULL,
            "SDL audio buffer allocation failed.\n");
  memset(sdl->audio.buffer, 0, buffer_capacity);

  sdl->audio.buffer_end = sdl->audio.buffer + buffer_capacity;
  sdl->audio.read_pos = sdl->audio.write_pos = sdl->audio.buffer;
  return OK;
error:
  return ERROR;
}

static uint32_t get_gb_channel_samples(SDL* sdl, size_t buffer_bytes) {
  size_t samples = buffer_bytes / (AUDIO_CHANNELS * AUDIO_SAMPLE_SIZE) + 1;
  return (uint32_t)((double)samples * APU_CYCLES_PER_SECOND /
                    sdl->audio.spec.freq) *
         SOUND_OUTPUT_COUNT;
}

static Result init_audio_buffer(SDL* sdl, AudioBuffer* audio_buffer) {
  uint32_t gb_channel_samples =
      get_gb_channel_samples(sdl, sdl->audio.spec.size) +
      AUDIO_BUFFER_EXTRA_CHANNEL_SAMPLES;
  size_t buffer_size = gb_channel_samples * sizeof(audio_buffer->data[0]);
  audio_buffer->data = malloc(buffer_size);
  CHECK_MSG(audio_buffer->data != NULL, "Audio buffer allocation failed.\n");
  audio_buffer->end = audio_buffer->data + gb_channel_samples;
  audio_buffer->position = audio_buffer->data;
  return OK;
error:
  return ERROR;
}

static Bool sdl_poll_events(Emulator* e) {
  Bool running = TRUE;
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    switch (event.type) {
      case SDL_KEYDOWN:
      case SDL_KEYUP: {
        Bool set = event.type == SDL_KEYDOWN;
        switch (event.key.keysym.sym) {
          case SDLK_1: if (set) e->config.disable_sound[CHANNEL1] ^= 1; break;
          case SDLK_2: if (set) e->config.disable_sound[CHANNEL2] ^= 1; break;
          case SDLK_3: if (set) e->config.disable_sound[CHANNEL3] ^= 1; break;
          case SDLK_4: if (set) e->config.disable_sound[CHANNEL4] ^= 1; break;
          case SDLK_b: if (set) e->config.disable_bg ^= 1; break;
          case SDLK_w: if (set) e->config.disable_window ^= 1; break;
          case SDLK_o: if (set) e->config.disable_obj ^= 1; break;
          case SDLK_UP: e->joypad.up = set; break;
          case SDLK_DOWN: e->joypad.down = set; break;
          case SDLK_LEFT: e->joypad.left = set; break;
          case SDLK_RIGHT: e->joypad.right = set; break;
          case SDLK_z: e->joypad.B = set; break;
          case SDLK_x: e->joypad.A = set; break;
          case SDLK_RETURN: e->joypad.start = set; break;
          case SDLK_BACKSPACE: e->joypad.select = set; break;
          case SDLK_ESCAPE: running = FALSE; break;
          case SDLK_TAB: e->config.no_sync = set; break;
          case SDLK_SPACE: if (set) e->config.paused ^= 1; break;
          case SDLK_n:
            if (set) {
              e->config.step = 1;
              e->config.paused = 0;
            }
            break;
          default: break;
        }
        break;
      }
      case SDL_QUIT: running = FALSE; break;
      default: break;
    }
  }
  return running;
}

static void sdl_flip_surface(SDL* sdl, Bool force) {
  if (sdl->used_surfaces > 0 &&
      (force || sdl->used_surfaces == SDL_SURFACE_COUNT)) {
    DEBUG(sdl, "@@@ %.1f: flip surface\n", get_time_ms());
    SDL_Flip(sdl->surface[0]);
    sdl->used_surfaces--;
    /* Cycle the surfaces */
    SDL_Surface* temp = sdl->surface[0];
    int i;
    for (i = 1; i < SDL_SURFACE_COUNT - 1; ++i) {
      sdl->surface[i - 1] = sdl->surface[i];
    }
    sdl->surface[SDL_SURFACE_COUNT - 1] = temp;
  }
}

static void sdl_render_surface(SDL* sdl, Emulator* e) {
  sdl_flip_surface(sdl, FALSE);
  assert(sdl->used_surfaces < SDL_SURFACE_COUNT);
  SDL_Surface* surface = sdl->surface[sdl->used_surfaces];
  if (SDL_LockSurface(surface) == 0) {
    uint32_t* pixels = surface->pixels;
    int sx, sy;
    for (sy = 0; sy < SCREEN_HEIGHT; sy++) {
      for (sx = 0; sx < SCREEN_WIDTH; sx++) {
        int i, j;
        RGBA pixel = e->frame_buffer[sy * SCREEN_WIDTH + sx];
        uint8_t r = (pixel >> 16) & 0xff;
        uint8_t g = (pixel >> 8) & 0xff;
        uint8_t b = (pixel >> 0) & 0xff;
        uint32_t mapped = SDL_MapRGB(surface->format, r, g, b);
        for (j = 0; j < RENDER_SCALE; j++) {
          for (i = 0; i < RENDER_SCALE; i++) {
            int rx = sx * RENDER_SCALE + i;
            int ry = sy * RENDER_SCALE + j;
            pixels[ry * RENDER_WIDTH + rx] = mapped;
          }
        }
      }
    }
    SDL_UnlockSurface(surface);
    sdl->used_surfaces++;
  }
}

static void sdl_synchronize(SDL* sdl, Emulator* e) {
  double now_ms = get_time_ms();
  double gb_ms = (double)(e->cycles - sdl->last_event_cycles) *
                 MILLISECONDS_PER_SECOND / CPU_CYCLES_PER_SECOND;
  double real_ms = now_ms - sdl->last_event_real_ms;
  double delta_ms = gb_ms - real_ms;
  double delay_until_ms = now_ms + delta_ms;
  if (delta_ms < -AUDIO_MAX_SLOW_DESYNC_MS ||
      delta_ms > AUDIO_MAX_FAST_DESYNC_MS) {
    DEBUG(sdl, "!!! %.1f: desync [gb=%.1fms real=%.1fms]\n", now_ms, gb_ms,
          real_ms);
    /* Major desync; don't try to catch up, just reset. But our audio buffer
     * is probably behind (or way ahead), so pause to refill. */
    sdl->last_event_real_ms = now_ms;
    SDL_PauseAudio(1);
    sdl->audio.ready = FALSE;
    SDL_LockAudio();
    sdl->audio.read_pos = sdl->audio.write_pos = sdl->audio.buffer;
    sdl->audio.buffer_available = 0;
    SDL_UnlockAudio();
  } else {
    if (real_ms < gb_ms) {
      DEBUG(sdl, "... %.1f: waiting %.1fms [gb=%.1fms real=%.1fms]\n", now_ms,
            delta_ms, gb_ms, real_ms);
      do {
        if (delta_ms > 1) {
          SDL_Delay(delta_ms - 0.1);
        } else {
          sched_yield();
        }
        now_ms = get_time_ms();
        delta_ms = delay_until_ms - now_ms;
      } while (delta_ms > 0);
    }
    sdl->last_event_real_ms = delay_until_ms;
  }
  sdl->last_event_cycles = e->cycles;
}

/* Returns TRUE if there was overflow. */
static Bool sdl_write_audio_sample(SDLAudio* audio, uint16_t sample) {
  if (audio->buffer_available < audio->buffer_capacity) {
    AudioBufferSample* dst = (AudioBufferSample*)audio->write_pos;
    *dst = AUDIO_CONVERT_SAMPLE_U16SYS(sample);
    audio->buffer_available += AUDIO_SAMPLE_SIZE;
    audio->write_pos += AUDIO_SAMPLE_SIZE;
    assert(audio->write_pos <= audio->buffer_end);
    if (audio->write_pos == audio->buffer_end) {
      audio->write_pos = audio->buffer;
    }
    return FALSE;
  } else {
    return TRUE;
  }
}

static void sdl_render_audio(SDL* sdl, Emulator* e) {
  const uint32_t freq = sdl->audio.spec.freq;
  assert(AUDIO_CHANNELS == SOUND_OUTPUT_COUNT);

  Bool overflow = FALSE;
  SDLAudio* audio = &sdl->audio;
  uint32_t accumulator[AUDIO_CHANNELS];
  ZERO_MEMORY(accumulator);
  uint32_t divisor = 0;

  uint16_t* src = e->apu.buffer->data;
  uint16_t* src_end = e->apu.buffer->position;

  SDL_LockAudio();
  size_t old_buffer_available = audio->buffer_available;
  size_t i;
  for (; src < src_end; src += AUDIO_CHANNELS) {
    sdl->audio.freq_counter += freq;
    for (i = 0; i < AUDIO_CHANNELS; ++i) {
      accumulator[i] += src[i];
    }
    divisor++;
    if (VALUE_WRAPPED(sdl->audio.freq_counter, APU_CYCLES_PER_SECOND)) {
      for (i = 0; i < AUDIO_CHANNELS; ++i) {
        uint16_t sample = accumulator[i] / divisor;
        if (sdl_write_audio_sample(audio, sample)) {
          overflow = TRUE;
          break;
        }
        accumulator[i] = 0;
      }
      if (overflow) {
        break;
      }
      divisor = 0;
    }
  }
  size_t new_buffer_available = audio->buffer_available;
  SDL_UnlockAudio();

  if (overflow) {
    DEBUG(sdl, "!!! audio overflow (old size = %zu)\n", old_buffer_available);
  } else {
    DEBUG(sdl, "+++ %.1f: buf: %zu -> %zu\n", get_time_ms(),
          old_buffer_available, new_buffer_available);
  }
  if (!audio->ready && new_buffer_available >= audio->buffer_target_available) {
    DEBUG(sdl, "*** %.1f: audio buffer ready, size = %zu.\n", get_time_ms(),
          new_buffer_available);
    audio->ready = TRUE;
    SDL_PauseAudio(0);
  }
}

static void get_save_filename(const char* rom_filename,
                              char* out,
                              size_t out_size) {
  char* last_dot = strrchr(rom_filename, '.');
  if (last_dot == NULL) {
    snprintf(out, out_size, "%s" SAVE_EXTENSION, rom_filename);
  } else {
    snprintf(out, out_size, "%.*s" SAVE_EXTENSION,
             (int)(last_dot - rom_filename), rom_filename);
  }
}

static Result read_ext_ram_from_file(Emulator* e, const char* filename) {
  FILE* f = NULL;
  if (e->ext_ram.battery_type == BATTERY_TYPE_WITH_BATTERY) {
    f = fopen(filename, "rb");
    CHECK_MSG(f, "unable to open file \"%s\".\n", filename);
    uint8_t* data = e->ext_ram.data;
    size_t size = e->ext_ram.size;
    CHECK_MSG(fread(data, size, 1, f) == 1, "fread failed.\n");
    fclose(f);
  }
  return OK;
error:
  if (f) {
    fclose(f);
  }
  return ERROR;
}

static Result write_ext_ram_to_file(Emulator* e, const char* filename) {
  FILE* f = NULL;
  if (e->ext_ram.battery_type == BATTERY_TYPE_WITH_BATTERY) {
    f = fopen(filename, "wb");
    CHECK_MSG(f, "unable to open file \"%s\".\n", filename);
    uint8_t* data = e->ext_ram.data;
    size_t size = e->ext_ram.size;
    CHECK_MSG(fwrite(data, size, 1, f) == 1, "fwrite failed.\n");
    fclose(f);
  }
  return OK;
error:
  if (f) {
    fclose(f);
  }
  return ERROR;
}

int main(int argc, char** argv) {
  clock_gettime(CLOCK_MONOTONIC, &s_start_time);
  --argc; ++argv;
  int result = 1;

#if 0
  s_never_trace = 1;
  s_log_level_memory = 0;
  s_log_level_ppu = 0;
  s_log_level_apu = 0;
  s_log_level_io = 0;
  s_log_level_interrupt = 0;
#endif

  RomData rom_data;
  Emulator emulator;
  Emulator* e = &emulator;
  SDL sdl;
  AudioBuffer audio_buffer;

  ZERO_MEMORY(rom_data);
  ZERO_MEMORY(emulator);
  ZERO_MEMORY(sdl);
  ZERO_MEMORY(audio_buffer);

  CHECK_MSG(argc == 1, "no rom file given.\n");
  const char* rom_filename = argv[0];
  CHECK(SUCCESS(read_rom_data_from_file(rom_filename, &rom_data)));
  size_t save_filename_length =
      strlen(rom_filename) + strlen(SAVE_EXTENSION) + 1;
  char* save_filename = alloca(save_filename_length);
  get_save_filename(rom_filename, save_filename, save_filename_length);

  CHECK(SUCCESS(sdl_init_video(&sdl)));
  CHECK(SUCCESS(sdl_init_audio(&sdl)));
  CHECK(SUCCESS(init_audio_buffer(&sdl, &audio_buffer)));

  CHECK(SUCCESS(init_emulator(e, &rom_data, &audio_buffer)));
  read_ext_ram_from_file(e, save_filename);

  double now_ms = get_time_ms();
  double next_poll_event_ms = now_ms + POLL_EVENT_MS;
  double next_flip_ms = now_ms + VIDEO_FRAME_MS;
  EmulatorEvent event = 0;
  while (TRUE) {
    if (e->config.paused) {
      if (!sdl_poll_events(e)) {
        break;
      }
      SDL_PauseAudio(e->config.paused || !sdl.audio.ready);
      SDL_Delay(VIDEO_FRAME_MS);
      continue;
    }

    size_t buffer_needed =
        sdl.audio.spec.size - sdl.audio.buffer_available % sdl.audio.spec.size;
    uint32_t requested_samples = get_gb_channel_samples(&sdl, buffer_needed);
    double timeout_ms = MIN(next_poll_event_ms, next_flip_ms);
    event = run_emulator_until_event(e, event, requested_samples, timeout_ms);
    now_ms = get_time_ms();
    if (event & EMULATOR_EVENT_TIMEOUT) {
      if (now_ms >= next_flip_ms) {
        sdl_flip_surface(&sdl, TRUE);
        while (next_flip_ms <= now_ms) {
          next_flip_ms = now_ms + VIDEO_FRAME_MS;
        }
      } else if (now_ms >= next_poll_event_ms) {
        if (!sdl_poll_events(e)) {
          break;
        }
        while (next_poll_event_ms <= now_ms) {
          next_poll_event_ms = now_ms + POLL_EVENT_MS;
        }
      }
    }
    if (event & EMULATOR_EVENT_NEW_FRAME) {
      sdl_render_surface(&sdl, e);
      if (e->config.step) {
        e->config.paused = TRUE;
        e->config.step = FALSE;
      }
    }
    sdl_render_audio(&sdl, e);
    reset_audio_buffer(e);
    if (!e->config.no_sync) {
      sdl_synchronize(&sdl, e);
    }
  }

  write_ext_ram_to_file(e, save_filename);
  result = 0;
error:
  sdl_destroy(&sdl);
  return result;
}

#endif /* NO_SDL */
