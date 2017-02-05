/*
 * Copyright (C) 2016 Ben Smith
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include <assert.h>
#include <inttypes.h>
#include <string.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/time.h>

#define SUCCESS(x) ((x) == OK)

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
#define CHECK(x) do if (!(x)) { goto error; } while(0)
#define ON_ERROR_RETURN \
  error:                \
  return ERROR
#define ON_ERROR_CLOSE_FILE_AND_RETURN \
  error:                               \
  if (f) {                             \
    fclose(f);                         \
  }                                    \
  return ERROR

#define UNREACHABLE(...) PRINT_ERROR(__VA_ARGS__), exit(1)
#define VALUE_WRAPPED(X, MAX) ((X) >= (MAX) ? ((X) -= (MAX), TRUE) : FALSE)

typedef int8_t s8;
typedef int32_t s32;
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef double f64;
typedef u16 Address;
typedef u16 MaskedAddress;
typedef u32 RGBA;
typedef enum { FALSE = 0, TRUE = 1 } Bool;

/* Configurable constants */
#define RGBA_WHITE 0xffffffffu
#define RGBA_LIGHT_GRAY 0xffaaaaaau
#define RGBA_DARK_GRAY 0xff555555u
#define RGBA_BLACK 0xff000000u

/* ROM header stuff */
#define MAX_CART_INFOS 256 /* 8Mb / 32k */
#define LOGO_START_ADDR 0x104
#define LOGO_END_ADDR 0x133
#define TITLE_START_ADDR 0x134
#define TITLE_MAX_LENGTH 0x10
#define CGB_FLAG_ADDR 0x143
#define SGB_FLAG_ADDR 0x146
#define CART_TYPE_ADDR 0x147
#define ROM_SIZE_ADDR 0x148
#define EXT_RAM_SIZE_ADDR 0x149
#define HEADER_CHECKSUM_ADDR 0x14d
#define GLOBAL_CHECKSUM_START_ADDR 0x14e
#define HEADER_CHECKSUM_RANGE_START 0x134
#define HEADER_CHECKSUM_RANGE_END 0x14c

/* Sizes */
#define MINIMUM_ROM_SIZE 32768
#define VIDEO_RAM_SIZE 8192
#define WORK_RAM_SIZE 8192
#define EXT_RAM_MAX_SIZE 32768
#define WAVE_RAM_SIZE 16
#define HIGH_RAM_SIZE 127
#define CART_INFO_SHIFT 15
#define ROM_BANK_SHIFT 14
#define EXT_RAM_BANK_SHIFT 13

/* Cycle counts */
#define MILLISECONDS_PER_SECOND 1000
#define MICROSECONDS_PER_SECOND 1000000
#define MICROSECONDS_PER_MILLISECOND \
  (MICROSECONDS_PER_SECOND / MILLISECONDS_PER_SECOND)
#define CPU_CYCLES_PER_SECOND 4194304
#define CPU_MCYCLE 4
#define APU_CYCLES_PER_SECOND (CPU_CYCLES_PER_SECOND / APU_CYCLES)
#define APU_CYCLES 2 /* APU runs at 2MHz */
#define PPU_MODE2_CYCLES 80
#define PPU_MODE3_MIN_CYCLES 172
#define PPU_LINE_CYCLES 456
#define PPU_VBLANK_CYCLES (PPU_LINE_CYCLES * 10)
#define PPU_FRAME_CYCLES (PPU_LINE_CYCLES * SCREEN_HEIGHT_WITH_VBLANK)
#define PPU_ENABLE_DISPLAY_DELAY_FRAMES 4
#define DMA_CYCLES 648
#define DMA_DELAY_CYCLES 8
#define SERIAL_CYCLES (CPU_CYCLES_PER_SECOND / 8192)

/* Memory map */
#define ADDR_MASK_4K 0x0fff
#define ADDR_MASK_8K 0x1fff
#define ADDR_MASK_16K 0x3fff

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
#define MBC5_RAM_BANK_SELECT_MASK 0xf
#define HUC1_ROM_BANK_LO_SELECT_MASK 0x3f
#define HUC1_BANK_HI_SELECT_MASK 0x3
#define HUC1_BANK_HI_SHIFT 6

#define OAM_START_ADDR 0xfe00
#define OAM_END_ADDR 0xfe9f
#define IO_START_ADDR 0xff00
#define APU_START_ADDR 0xff10
#define WAVE_RAM_START_ADDR 0xff30
#define HIGH_RAM_START_ADDR 0xff80
#define IE_ADDR 0xffff

#define OAM_TRANSFER_SIZE (OAM_END_ADDR - OAM_START_ADDR + 1)

/* Video */
#define SCREEN_WIDTH 160
#define SCREEN_HEIGHT 144
#define SCREEN_HEIGHT_WITH_VBLANK 154
#define TILE_HEIGHT 8
#define TILE_ROW_BYTES 2
#define TILE_MAP_WIDTH 32
#define WINDOW_MAX_X 166
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
#define NOISE_DIVISOR_COUNT 8
#define ENVELOPE_MAX_PERIOD 8
#define ENVELOPE_MAX_VOLUME 15
#define DUTY_CYCLE_COUNT 8
#define SOUND_OUTPUT_COUNT 2
#define SOUND_OUTPUT_MAX_VOLUME 7
/* Additional samples so the AudioBuffer doesn't overflow. This could happen
 * because the audio buffer is updated at the granularity of an instruction, so
 * the most extra frames that could be added is equal to the APU cycle count
 * of the slowest instruction. */
#define AUDIO_BUFFER_EXTRA_FRAMES 256

#define WAVE_TRIGGER_CORRUPTION_OFFSET_CYCLES APU_CYCLES
#define WAVE_TRIGGER_DELAY_CYCLES (3 * APU_CYCLES)

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

#define GET_LO(HI, LO) (LO)
#define GET_BITMASK(HI, LO) ((1 << ((HI) - (LO) + 1)) - 1)
#define UNPACK(X, BITS) (((X) >> BITS(GET_LO)) & BITS(GET_BITMASK))
#define PACK(X, BITS) (((X) & BITS(GET_BITMASK)) << BITS(GET_LO))
#define BITS(X, HI, LO) X(HI, LO)
#define BIT(X, B) X(B, B)

#define CPU_FLAG_Z(X) BIT(X, 7)
#define CPU_FLAG_N(X) BIT(X, 6)
#define CPU_FLAG_H(X) BIT(X, 5)
#define CPU_FLAG_C(X) BIT(X, 4)

#define JOYP_UNUSED 0xc0
#define JOYP_RESULT_MASK 0x0f
#define JOYP_JOYPAD_SELECT(X) BITS(X, 5, 4)
#define JOYP_DPAD_DOWN(X) BIT(X, 3)
#define JOYP_DPAD_UP(X) BIT(X, 2)
#define JOYP_DPAD_LEFT(X) BIT(X, 1)
#define JOYP_DPAD_RIGHT(X) BIT(X, 0)
#define JOYP_BUTTON_START(X) BIT(X, 3)
#define JOYP_BUTTON_SELECT(X) BIT(X, 2)
#define JOYP_BUTTON_B(X) BIT(X, 1)
#define JOYP_BUTTON_A(X) BIT(X, 0)
#define SC_UNUSED 0x7e
#define SC_TRANSFER_START(X) BIT(X, 7)
#define SC_SHIFT_CLOCK(X) BIT(X, 0)
#define TAC_UNUSED 0xf8
#define TAC_TIMER_ON(X) BIT(X, 2)
#define TAC_CLOCK_SELECT(X) BITS(X, 1, 0)
#define IF_UNUSED 0xe0
#define IF_ALL 0x1f
#define IF_JOYPAD 0x10
#define IF_SERIAL 0x08
#define IF_TIMER 0x04
#define IF_STAT 0x02
#define IF_VBLANK 0x01
#define LCDC_DISPLAY(X) BIT(X, 7)
#define LCDC_WINDOW_TILE_MAP_SELECT(X) BIT(X, 6)
#define LCDC_WINDOW_DISPLAY(X) BIT(X, 5)
#define LCDC_BG_TILE_DATA_SELECT(X) BIT(X, 4)
#define LCDC_BG_TILE_MAP_SELECT(X) BIT(X, 3)
#define LCDC_OBJ_SIZE(X) BIT(X, 2)
#define LCDC_OBJ_DISPLAY(X) BIT(X, 1)
#define LCDC_BG_DISPLAY(X) BIT(X, 0)
#define STAT_UNUSED 0x80
#define STAT_YCOMPARE_INTR(X) BIT(X, 6)
#define STAT_MODE2_INTR(X) BIT(X, 5)
#define STAT_VBLANK_INTR(X) BIT(X, 4)
#define STAT_HBLANK_INTR(X) BIT(X, 3)
#define STAT_YCOMPARE(X) BIT(X, 2)
#define STAT_MODE(X) BITS(X, 1, 0)
#define PALETTE_COLOR3(X) BITS(X, 7, 6)
#define PALETTE_COLOR2(X) BITS(X, 5, 4)
#define PALETTE_COLOR1(X) BITS(X, 3, 2)
#define PALETTE_COLOR0(X) BITS(X, 1, 0)
#define NR10_UNUSED 0x80
#define NR10_SWEEP_PERIOD(X) BITS(X, 6, 4)
#define NR10_SWEEP_DIRECTION(X) BIT(X, 3)
#define NR10_SWEEP_SHIFT(X) BITS(X, 2, 0)
#define NRX1_UNUSED 0x3f
#define NRX1_WAVE_DUTY(X) BITS(X, 7, 6)
#define NRX1_LENGTH(X) BITS(X, 5, 0)
#define NRX2_INITIAL_VOLUME(X) BITS(X, 7, 4)
#define NRX2_DAC_ENABLED(X) BITS(X, 7, 3)
#define NRX2_ENVELOPE_DIRECTION(X) BIT(X, 3)
#define NRX2_ENVELOPE_PERIOD(X) BITS(X, 2, 0)
#define NRX4_UNUSED 0xbf
#define NRX4_INITIAL(X) BIT(X, 7)
#define NRX4_LENGTH_ENABLED(X) BIT(X, 6)
#define NRX4_FREQUENCY_HI(X) BITS(X, 2, 0)
#define NR30_UNUSED 0x7f
#define NR30_DAC_ENABLED(X) BIT(X, 7)
#define NR32_UNUSED 0x9f
#define NR32_SELECT_WAVE_VOLUME(X) BITS(X, 6, 5)
#define NR43_CLOCK_SHIFT(X) BITS(X, 7, 4)
#define NR43_LFSR_WIDTH(X) BIT(X, 3)
#define NR43_DIVISOR(X) BITS(X, 2, 0)
#define NR50_VIN_SO2(X) BIT(X, 7)
#define NR50_SO2_VOLUME(X) BITS(X, 6, 4)
#define NR50_VIN_SO1(X) BIT(X, 3)
#define NR50_SO1_VOLUME(X) BITS(X, 2, 0)
#define NR51_SOUND4_SO2(X) BIT(X, 7)
#define NR51_SOUND3_SO2(X) BIT(X, 6)
#define NR51_SOUND2_SO2(X) BIT(X, 5)
#define NR51_SOUND1_SO2(X) BIT(X, 4)
#define NR51_SOUND4_SO1(X) BIT(X, 3)
#define NR51_SOUND3_SO1(X) BIT(X, 2)
#define NR51_SOUND2_SO1(X) BIT(X, 1)
#define NR51_SOUND1_SO1(X) BIT(X, 0)
#define NR52_UNUSED 0x70
#define NR52_ALL_SOUND_ENABLED(X) BIT(X, 7)
#define NR52_SOUND4_ON(X) BIT(X, 3)
#define NR52_SOUND3_ON(X) BIT(X, 2)
#define NR52_SOUND2_ON(X) BIT(X, 1)
#define NR52_SOUND1_ON(X) BIT(X, 0)

#define OBJ_PRIORITY(X) BIT(X, 7)
#define OBJ_YFLIP(X) BIT(X, 6)
#define OBJ_XFLIP(X) BIT(X, 5)
#define OBJ_PALETTE(X) BIT(X, 4)

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

#define FOREACH_CART_TYPE(V)                                               \
  V(CART_TYPE_ROM_ONLY, 0x0, NO_MBC, NO_RAM, NO_BATTERY)                   \
  V(CART_TYPE_MBC1, 0x1, MBC1, NO_RAM, NO_BATTERY)                         \
  V(CART_TYPE_MBC1_RAM, 0x2, MBC1, WITH_RAM, NO_BATTERY)                   \
  V(CART_TYPE_MBC1_RAM_BATTERY, 0x3, MBC1, WITH_RAM, WITH_BATTERY)         \
  V(CART_TYPE_MBC2, 0x5, MBC2, NO_RAM, NO_BATTERY)                         \
  V(CART_TYPE_MBC2_BATTERY, 0x6, MBC2, NO_RAM, WITH_BATTERY)               \
  V(CART_TYPE_ROM_RAM, 0x8, NO_MBC, WITH_RAM, NO_BATTERY)                  \
  V(CART_TYPE_ROM_RAM_BATTERY, 0x9, NO_MBC, WITH_RAM, WITH_BATTERY)        \
  V(CART_TYPE_MMM01, 0xb, MMM01, NO_RAM, NO_BATTERY)                       \
  V(CART_TYPE_MMM01_RAM, 0xc, MMM01, WITH_RAM, NO_BATTERY)                 \
  V(CART_TYPE_MMM01_RAM_BATTERY, 0xd, MMM01, WITH_RAM, WITH_BATTERY)       \
  V(CART_TYPE_MBC3_TIMER_BATTERY, 0xf, MBC3, NO_RAM, WITH_BATTERY)         \
  V(CART_TYPE_MBC3_TIMER_RAM_BATTERY, 0x10, MBC3, WITH_RAM, WITH_BATTERY)  \
  V(CART_TYPE_MBC3, 0x11, MBC3, NO_RAM, NO_BATTERY)                        \
  V(CART_TYPE_MBC3_RAM, 0x12, MBC3, WITH_RAM, NO_BATTERY)                  \
  V(CART_TYPE_MBC3_RAM_BATTERY, 0x13, MBC3, WITH_RAM, WITH_BATTERY)        \
  V(CART_TYPE_MBC4, 0x15, MBC4, NO_RAM, NO_BATTERY)                        \
  V(CART_TYPE_MBC4_RAM, 0x16, MBC4, WITH_RAM, NO_BATTERY)                  \
  V(CART_TYPE_MBC4_RAM_BATTERY, 0x17, MBC4, WITH_RAM, WITH_BATTERY)        \
  V(CART_TYPE_MBC5, 0x19, MBC5, NO_RAM, NO_BATTERY)                        \
  V(CART_TYPE_MBC5_RAM, 0x1a, MBC5, WITH_RAM, NO_BATTERY)                  \
  V(CART_TYPE_MBC5_RAM_BATTERY, 0x1b, MBC5, WITH_RAM, WITH_BATTERY)        \
  V(CART_TYPE_MBC5_RUMBLE, 0x1c, MBC5, NO_RAM, NO_BATTERY)                 \
  V(CART_TYPE_MBC5_RUMBLE_RAM, 0x1d, MBC5, WITH_RAM, NO_BATTERY)           \
  V(CART_TYPE_MBC5_RUMBLE_RAM_BATTERY, 0x1e, MBC5, WITH_RAM, WITH_BATTERY) \
  V(CART_TYPE_POCKET_CAMERA, 0xfc, NO_MBC, NO_RAM, NO_BATTERY)             \
  V(CART_TYPE_BANDAI_TAMA5, 0xfd, TAMA5, NO_RAM, NO_BATTERY)               \
  V(CART_TYPE_HUC3, 0xfe, HUC3, NO_RAM, NO_BATTERY)                        \
  V(CART_TYPE_HUC1_RAM_BATTERY, 0xff, HUC1, WITH_RAM, WITH_BATTERY)

#define FOREACH_ROM_SIZE(V) \
  V(ROM_SIZE_32K, 0, 2)     \
  V(ROM_SIZE_64K, 1, 4)     \
  V(ROM_SIZE_128K, 2, 8)    \
  V(ROM_SIZE_256K, 3, 16)   \
  V(ROM_SIZE_512K, 4, 32)   \
  V(ROM_SIZE_1M, 5, 64)     \
  V(ROM_SIZE_2M, 6, 128)    \
  V(ROM_SIZE_4M, 7, 256)    \
  V(ROM_SIZE_8M, 8, 512)

#define FOREACH_EXT_RAM_SIZE(V)   \
  V(EXT_RAM_SIZE_NONE, 0, 0)      \
  V(EXT_RAM_SIZE_2K, 1, 2048)     \
  V(EXT_RAM_SIZE_8K, 2, 8192)     \
  V(EXT_RAM_SIZE_32K, 3, 32768)   \
  V(EXT_RAM_SIZE_128K, 4, 131072) \
  V(EXT_RAM_SIZE_64K, 5, 65536)

#define FOREACH_PPU_MODE(V) \
  V(PPU_MODE_HBLANK, 0)     \
  V(PPU_MODE_VBLANK, 1)     \
  V(PPU_MODE_MODE2, 2)      \
  V(PPU_MODE_MODE3, 3)

#define FOREACH_PPU_STATE(V)          \
  V(PPU_STATE_HBLANK, 0)              \
  V(PPU_STATE_HBLANK_PLUS_4, 1)       \
  V(PPU_STATE_VBLANK, 2)              \
  V(PPU_STATE_VBLANK_PLUS_4, 3)       \
  V(PPU_STATE_VBLANK_LY_0, 4)         \
  V(PPU_STATE_VBLANK_LY_0_PLUS_4, 5)  \
  V(PPU_STATE_VBLANK_LINE_Y_0, 6)     \
  V(PPU_STATE_LCD_ON_MODE2, 7)        \
  V(PPU_STATE_MODE2, 8)               \
  V(PPU_STATE_MODE3_EARLY_TRIGGER, 9) \
  V(PPU_STATE_MODE3, 10)              \
  V(PPU_STATE_MODE3_COMMON, 11)

#define DEFINE_ENUM(name, code, ...) name = code,
#define DEFINE_IO_REG_ENUM(name, code, ...) IO_##name##_ADDR = code,
#define DEFINE_APU_REG_ENUM(name, code, ...) APU_##name##_ADDR = code,
#define DEFINE_STRING(name, code, ...) [code] = #name,

static const char* get_enum_string(const char** strings, size_t string_count,
                                   size_t value) {
  const char* result = value < string_count ? strings[value] : "unknown";
  return result ? result : "unknown";
}

#define DEFINE_NAMED_ENUM(NAME, Name, name, foreach, enum_def)               \
  typedef enum { foreach (enum_def) NAME##_COUNT } Name;                     \
  static Bool is_##name##_valid(Name value) { return value < NAME##_COUNT; } \
  static const char* get_##name##_string(Name value) {                       \
    static const char* s_strings[] = {foreach (DEFINE_STRING)};              \
    return get_enum_string(s_strings, ARRAY_SIZE(s_strings), value);         \
  }

DEFINE_NAMED_ENUM(RESULT, Result, result, FOREACH_RESULT, DEFINE_ENUM)
DEFINE_NAMED_ENUM(CGB_FLAG, CgbFlag, cgb_flag, FOREACH_CGB_FLAG, DEFINE_ENUM)
DEFINE_NAMED_ENUM(SGB_FLAG, SgbFlag, sgb_flag, FOREACH_SGB_FLAG, DEFINE_ENUM)
DEFINE_NAMED_ENUM(CART_TYPE, CartType, cart_type, FOREACH_CART_TYPE,
                  DEFINE_ENUM)
DEFINE_NAMED_ENUM(ROM_SIZE, RomSize, rom_size, FOREACH_ROM_SIZE, DEFINE_ENUM)
DEFINE_NAMED_ENUM(EXT_RAM_SIZE, ExtRamSize, ext_ram_size, FOREACH_EXT_RAM_SIZE,
                  DEFINE_ENUM)
DEFINE_NAMED_ENUM(IO_REG, IOReg, io_reg, FOREACH_IO_REG, DEFINE_IO_REG_ENUM)
DEFINE_NAMED_ENUM(APU_REG, APUReg, apu_reg, FOREACH_APU_REG,
                  DEFINE_APU_REG_ENUM)
DEFINE_NAMED_ENUM(PPU_MODE, PPUMode, ppu_mode, FOREACH_PPU_MODE, DEFINE_ENUM)
DEFINE_NAMED_ENUM(PPU_STATE, PPUState, ppu_state, FOREACH_PPU_STATE,
                  DEFINE_ENUM)

static u32 s_rom_bank_count[] = {
#define V(name, code, bank_count) [code] = bank_count,
    FOREACH_ROM_SIZE(V)
#undef V
};
#define ROM_BANK_COUNT(e) s_rom_bank_count[(e)->cart_info->rom_size]
#define ROM_BANK_MASK(e) (ROM_BANK_COUNT(e) - 1)

static u32 s_ext_ram_byte_size[] = {
#define V(name, code, byte_size) [code] = byte_size,
    FOREACH_EXT_RAM_SIZE(V)
#undef V
};
#define EXT_RAM_BYTE_SIZE(e) s_ext_ram_byte_size[(e)->cart_info->ext_ram_size]
#define EXT_RAM_BANK_MASK(e) (EXT_RAM_BYTE_SIZE(e) - 1)

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

typedef enum {
  EXT_RAM_TYPE_NO_RAM,
  EXT_RAM_TYPE_WITH_RAM,
} ExtRamType;

typedef enum {
  BATTERY_TYPE_NO_BATTERY,
  BATTERY_TYPE_WITH_BATTERY,
} BatteryType;

typedef struct {
  MBCType mbc_type;
  ExtRamType ext_ram_type;
  BatteryType battery_type;
} CartTypeInfo;

static CartTypeInfo s_cart_type_info[] = {
#define V(name, code, mbc, ram, battery) \
  [code] = {MBC_TYPE_##mbc, EXT_RAM_TYPE_##ram, BATTERY_TYPE_##battery},
    FOREACH_CART_TYPE(V)
#undef V
};

typedef enum {
  MEMORY_MAP_ROM0,
  MEMORY_MAP_ROM1,
  MEMORY_MAP_VRAM,
  MEMORY_MAP_EXT_RAM,
  MEMORY_MAP_WORK_RAM0,
  MEMORY_MAP_WORK_RAM1,
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
/* TIMA is incremented when the given bit of DIV_counter changes from 1 to 0. */
static const u16 s_tima_mask[] = {1 << 9, 1 << 3, 1 << 5, 1 << 7};

typedef enum {
  TIMA_STATE_NORMAL,
  TIMA_STATE_OVERFLOW,
  TIMA_STATE_RESET,
} TimaState;

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
static u8 s_wave_volume_shift[WAVE_VOLUME_COUNT] = {4, 0, 1, 2};

typedef enum {
  LFSR_WIDTH_15 = 0, /* 15-bit LFSR */
  LFSR_WIDTH_7 = 1,  /* 7-bit LFSR */
} LFSRWidth;

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
static u8 s_obj_size_to_height[] = {[OBJ_SIZE_8X8] = 8, [OBJ_SIZE_8X16] = 16};

typedef enum {
  COLOR_WHITE = 0,
  COLOR_LIGHT_GRAY = 1,
  COLOR_DARK_GRAY = 2,
  COLOR_BLACK = 3,
} Color;
static RGBA s_color_to_rgba[] = {[COLOR_WHITE] = RGBA_WHITE,
                                 [COLOR_LIGHT_GRAY] = RGBA_LIGHT_GRAY,
                                 [COLOR_DARK_GRAY] = RGBA_DARK_GRAY,
                                 [COLOR_BLACK] = RGBA_BLACK};

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
  u8* data;
  size_t size;
} FileData;

typedef struct {
  u8 data[EXT_RAM_MAX_SIZE];
  size_t size;
  BatteryType battery_type;
} ExtRam;

typedef struct {
  size_t offset; /* Offset of cart in FileData. */
  u8* data;      /* == FileData.data + offset */
  size_t size;
  CgbFlag cgb_flag;
  SgbFlag sgb_flag;
  CartType cart_type;
  RomSize rom_size;
  ExtRamSize ext_ram_size;
} CartInfo;

struct Emulator;

typedef struct {
  u8 byte_2000_3fff;
  u8 byte_4000_5fff;
  BankMode bank_mode;
} MBC1, HUC1, MMM01;

typedef struct {
  u8 byte_2000_2fff;
  u8 byte_3000_3fff;
} MBC5;

typedef struct {
  u8 (*read_ext_ram)(struct Emulator*, MaskedAddress);
  void (*write_rom)(struct Emulator*, MaskedAddress, u8);
  void (*write_ext_ram)(struct Emulator*, MaskedAddress, u8);
} MemoryMap;

typedef struct {
  u32 rom1_base;
  u32 ext_ram_base;
  Bool ext_ram_enabled;
  union {
    MBC1 mbc1;
    MMM01 mmm01;
    HUC1 huc1;
    MBC5 mbc5;
  };
} MemoryMapState;

typedef struct {
  MemoryMapType type;
  MaskedAddress addr;
} MemoryTypeAddressPair;

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

typedef struct { Color color[PALETTE_COLOR_COUNT]; } Palette;

typedef struct {
  u8 y;
  u8 x;
  u8 tile;
  u8 byte3;
  ObjPriority priority;
  Bool yflip;
  Bool xflip;
  u8 palette;
} Obj;

typedef struct {
  Bool down, up, left, right;
  Bool start, select, B, A;
  JoypadSelect joypad_select;
  u8 last_p10_p13;
} Joypad;

typedef struct {
  Bool IME;      /* Interrupt Master Enable */
  u8 IE;         /* Interrupt Enable */
  u8 IF;         /* Interrupt Request, delayed by 1 M-cycle for some IRQs. */
  u8 new_IF;     /* The new value of IF, updated in 1 M-cycle. */
  Bool enable;   /* Set after EI instruction. This delays updating IME. */
  Bool halt;     /* Halted, waiting for an interrupt. */
  Bool halt_DI;  /* Halted w/ disabled interrupts. */
  Bool halt_bug; /* Halt bug occurred. */
  Bool stop;     /* Stopped, waiting for an interrupt. */
} Interrupt;

typedef struct {
  u8 TIMA;                 /* Incremented at rate defined by clock_select */
  u8 TMA;                  /* When TIMA overflows, it is set to this value */
  TimerClock clock_select; /* Select the rate of TIMA */
  u16 DIV_counter;         /* Internal clock counter, upper 8 bits are DIV. */
  TimaState TIMA_state;    /* Used to implement TIMA overflow delay. */
  Bool on;
} Timer;

typedef struct {
  Bool transferring;
  SerialClock clock;
  u8 SB; /* Serial transfer data. */
  u8 transferred_bits;
  u32 cycles;
} Serial;

typedef struct {
  u8 period;
  SweepDirection direction;
  u8 shift;
  u16 frequency;
  u8 timer; /* 0..period */
  Bool enabled;
  Bool calculated_subtract;
} Sweep;

typedef struct {
  u8 initial_volume;
  EnvelopeDirection direction;
  u8 period;
  u8 volume;      /* 0..15 */
  u32 timer;      /* 0..period */
  Bool automatic; /* TRUE when MAX/MIN has not yet been reached. */
} Envelope;

/* Channel 1 and 2 */
typedef struct {
  WaveDuty duty;
  u8 sample;   /* Last sample generated, 0..1 */
  u32 period;  /* Calculated from the frequency. */
  u8 position; /* Position in the duty cycle, 0..7 */
  u32 cycles;  /* 0..period */
} SquareWave;

/* Channel 3 */
typedef struct {
  WaveVolume volume;
  u8 volume_shift;
  u8 ram[WAVE_RAM_SIZE];
  u32 sample_time; /* Time (in cycles) the sample was read. */
  u8 sample_data;  /* Last sample generated, 0..1 */
  u32 period;      /* Calculated from the frequency. */
  u8 position;     /* 0..31 */
  u32 cycles;      /* 0..period */
  Bool playing;    /* TRUE if the channel has been triggered but the DAC not
                           disabled. */
} Wave;

/* Channel 4 */
typedef struct {
  u8 clock_shift;
  LFSRWidth lfsr_width;
  u8 divisor; /* 0..NOISE_DIVISOR_COUNT */
  u8 sample;  /* Last sample generated, 0..1 */
  u16 lfsr;   /* Linear feedback shift register, 15- or 7-bit. */
  u32 period; /* Calculated from the clock_shift and divisor. */
  u32 cycles; /* 0..period */
} Noise;

typedef struct {
  SquareWave square_wave; /* Channel 1, 2 */
  Envelope envelope;      /* Channel 1, 2, 4 */
  u16 frequency;          /* Channel 1, 2, 3 */
  u16 length;             /* All channels */
  Bool length_enabled;    /* All channels */
  Bool dac_enabled;
  Bool status; /* Status bit for NR52 */
} Channel;

typedef struct {
  u32 frequency;    /* Sample frequency, as N samples per second */
  u32 freq_counter; /* Used for resampling; [0..APU_CYCLES_PER_SECOND). */
  u32 accumulator[SOUND_OUTPUT_COUNT];
  u32 divisor;
  u8* data; /* Unsigned 8-bit 2-channel samples @ |frequency| */
  u8* end;
  u8* position;
} AudioBuffer;

typedef struct {
  u8 so_volume[SOUND_OUTPUT_COUNT];
  Bool so_output[SOUND_OUTPUT_COUNT][SOUND_COUNT];
  Bool enabled;
  Sweep sweep;
  Wave wave;
  Noise noise;
  Channel channel[CHANNEL_COUNT];
  u8 frame;         /* 0..FRAME_SEQUENCER_COUNT */
  u32 frame_cycles; /* 0..FRAME_SEQUENCER_CYCLES */
  u32 cycles;       /* Raw cycle counter */
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
  Bool trigger;
} LCDStatusInterrupt;

typedef struct {
  LCDStatusInterrupt y_compare;
  LCDStatusInterrupt mode2;
  LCDStatusInterrupt vblank;
  LCDStatusInterrupt hblank;
  Bool LY_eq_LYC;       /* TRUE if LY=LYC, delayed by 1 M-cycle. */
  PPUMode mode;         /* The current PPU mode. */
  Bool IF;              /* Internal interrupt flag for STAT interrupts. */
  PPUMode trigger_mode; /* This mode is used for checking STAT IRQ triggers. */
  Bool new_LY_eq_LYC;   /* The new value for LY_eq_LYC, updated in 1 M-cycle. */
} LCDStatus;

typedef struct {
  LCDControl LCDC;                /* LCD control */
  LCDStatus STAT;                 /* LCD status */
  u8 SCY;                         /* Screen Y */
  u8 SCX;                         /* Screen X */
  u8 LY;                          /* Line Y */
  u8 LYC;                         /* Line Y Compare */
  u8 WY;                          /* Window Y */
  u8 WX;                          /* Window X */
  Palette BGP;                    /* BG Palette */
  Palette OBP[OBJ_PALETTE_COUNT]; /* OBJ Palettes */
  PPUState state;
  u32 state_cycles;
  u32 line_cycles;                /* Counts cycles until line_y changes. */
  u32 frame;                      /* The currently rendering frame. */
  u8 last_LY;                     /* LY from the previous M-cycle. */
  u8 render_x;                    /* Currently rendering X coordinate. */
  u8 line_y;   /* The currently rendering line. Can be different than LY. */
  u8 win_y;    /* The window Y is only incremented when rendered. */
  u8 frame_WY; /* WY is cached per frame. */
  Obj line_obj[OBJ_PER_LINE_COUNT]; /* Cached from OAM during mode2. */
  u8 line_obj_count;     /* Number of sprites to draw on this line. */
  u8 oam_index;          /* Current sprite index in mode 2. */
  Bool rendering_window; /* TRUE when this line is rendering the window. */
  Bool new_frame_edge;
  u8 display_delay_frames; /* Wait this many frames before displaying. */
} PPU;

typedef struct {
  DMAState state;
  MemoryTypeAddressPair source;
  u32 cycles;
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
  Bool fullscreen;
  Bool allow_simulataneous_dpad_opposites;
} EmulatorConfig;

typedef struct {
  u8 cart_info_index;
  MemoryMapState memory_map_state;
  Registers reg;
  u8 vram[VIDEO_RAM_SIZE];
  ExtRam ext_ram;
  u8 wram[WORK_RAM_SIZE];
  Interrupt interrupt;
  Obj oam[OBJ_COUNT];
  Joypad JOYP;
  Serial serial;
  Timer timer;
  APU apu;
  PPU ppu;
  DMA dma;
  u8 hram[HIGH_RAM_SIZE];
  u32 cycles;
  Bool is_cgb;
} EmulatorState;

typedef struct JoypadCallback {
  void (*func)(struct Emulator* e, void* user_data);
  void* user_data;
} JoypadCallback;

typedef u32 EmulatorEvent;
enum {
  EMULATOR_EVENT_NEW_FRAME = 0x1,
  EMULATOR_EVENT_AUDIO_BUFFER_FULL = 0x2,
};

typedef struct Emulator {
  EmulatorConfig config;
  FileData file_data;
  CartInfo cart_infos[MAX_CART_INFOS];
  u32 cart_info_count;
  CartInfo* cart_info; /* Cached for convenience. */
  MemoryMap memory_map;
  EmulatorState state;
  FrameBuffer frame_buffer;
  AudioBuffer audio_buffer;
  JoypadCallback joypad_callback;
  EmulatorEvent last_event;
} Emulator;

static Bool s_never_trace = 0;
static Bool s_trace = 0;
static u32 s_trace_counter = 0;
static int s_log_level_memory = 1;
static int s_log_level_ppu = 0;
static int s_log_level_apu = 1;
static int s_log_level_io = 1;
static int s_log_level_interrupt = 1;

static void write_apu(Emulator*, MaskedAddress, u8);
static void write_io(Emulator*, MaskedAddress, u8);
static Result init_memory_map(Emulator*);
static void log_cart_info(CartInfo*);

static Result get_file_size(FILE* f, long* out_size) {
  CHECK_MSG(fseek(f, 0, SEEK_END) >= 0, "fseek to end failed.\n");
  long size = ftell(f);
  CHECK_MSG(size >= 0, "ftell failed.");
  CHECK_MSG(fseek(f, 0, SEEK_SET) >= 0, "fseek to beginning failed.\n");
  *out_size = size;
  return OK;
  ON_ERROR_RETURN;
}

static Result read_data_from_file(Emulator* e, const char* filename) {
  FILE* f = fopen(filename, "rb");
  CHECK_MSG(f, "unable to open file \"%s\".\n", filename);
  long size;
  CHECK(SUCCESS(get_file_size(f, &size)));
  CHECK_MSG(size >= MINIMUM_ROM_SIZE, "size < minimum rom size (%u).\n",
            MINIMUM_ROM_SIZE);
  u8* data = malloc(size); /* Leaks. */
  CHECK_MSG(data, "allocation failed.\n");
  CHECK_MSG(fread(data, size, 1, f) == 1, "fread failed.\n");
  fclose(f);
  e->file_data.data = data;
  e->file_data.size = size;
  return OK;
  ON_ERROR_CLOSE_FILE_AND_RETURN;
}

static void set_cart_info(Emulator* e, u8 index) {
  e->state.cart_info_index = index;
  e->cart_info = &e->cart_infos[index];
  if (!(e->cart_info->data && SUCCESS(init_memory_map(e)))) {
    UNREACHABLE("Unable to switch cart (%d).\n", index);
  }
}

static Result get_cart_info(FileData* file_data, size_t offset,
                            CartInfo* cart_info) {
  /* Simple checksum on logo data so we don't have to include it here. :) */
  u8* data = file_data->data + offset;
  size_t i;
  u32 logo_checksum = 0;
  for (i = LOGO_START_ADDR; i <= LOGO_END_ADDR; ++i) {
    logo_checksum = (logo_checksum << 1) ^ data[i];
  }
  CHECK(logo_checksum == 0xe06c8834);
  cart_info->offset = offset;
  cart_info->data = data;
  cart_info->rom_size = data[ROM_SIZE_ADDR];
  CHECK_MSG(is_rom_size_valid(cart_info->rom_size),
            "Invalid ROM size code: %u", cart_info->rom_size);
  u32 rom_byte_size = s_rom_bank_count[cart_info->rom_size] << ROM_BANK_SHIFT;
  cart_info->size = rom_byte_size;

  cart_info->cgb_flag = data[CGB_FLAG_ADDR];
  cart_info->sgb_flag = data[SGB_FLAG_ADDR];
  cart_info->cart_type = data[CART_TYPE_ADDR];
  CHECK_MSG(is_cart_type_valid(cart_info->cart_type), "Invalid cart type: %u\n",
            cart_info->cart_type);
  cart_info->ext_ram_size = data[EXT_RAM_SIZE_ADDR];
  CHECK_MSG(is_ext_ram_size_valid(cart_info->ext_ram_size),
            "Invalid ext ram size: %u\n", cart_info->ext_ram_size);
  return OK;
  ON_ERROR_RETURN;
}

static Result get_cart_infos(Emulator* e) {
  u32 i;
  for (i = 0; i < MAX_CART_INFOS; ++i) {
    size_t offset = i << CART_INFO_SHIFT;
    if (offset + MINIMUM_ROM_SIZE > e->file_data.size) break;
    if (SUCCESS(get_cart_info(&e->file_data, offset, &e->cart_infos[i]))) {
      if (s_cart_type_info[e->cart_infos[i].cart_type].mbc_type ==
          MBC_TYPE_MMM01) {
        /* MMM01 has the cart header at the end. */
        set_cart_info(e, i);
        return OK;
      }
      e->cart_info_count++;
    }
  }
  CHECK_MSG(e->cart_info_count != 0, "Invalid ROM.\n");
  set_cart_info(e, 0);
  return OK;
  ON_ERROR_RETURN;
}

static Result validate_header_checksum(CartInfo* cart_info) {
  u8 checksum = 0;
  size_t i = 0;
  for (i = HEADER_CHECKSUM_RANGE_START; i <= HEADER_CHECKSUM_RANGE_END; ++i) {
    checksum = checksum - cart_info->data[i] - 1;
  }
  return checksum == cart_info->data[HEADER_CHECKSUM_ADDR] ? OK : ERROR;
}

static void log_cart_info(CartInfo* cart_info) {
  char* title_start = (char*)cart_info->data + TITLE_START_ADDR;
  char* title_end = memchr(title_start, '\0', TITLE_MAX_LENGTH);
  int title_length = title_end ? title_end - title_start : TITLE_MAX_LENGTH;
  LOG("title: \"%.*s\"\n", title_length, title_start);
  LOG("cgb flag: %s\n", get_cgb_flag_string(cart_info->cgb_flag));
  LOG("sgb flag: %s\n", get_sgb_flag_string(cart_info->sgb_flag));
  LOG("cart type: %s\n", get_cart_type_string(cart_info->cart_type));
  LOG("rom size: %s\n", get_rom_size_string(cart_info->rom_size));
  LOG("ext ram size: %s\n", get_ext_ram_size_string(cart_info->ext_ram_size));
  LOG("header checksum: 0x%02x [%s]\n", cart_info->data[HEADER_CHECKSUM_ADDR],
      get_result_string(validate_header_checksum(cart_info)));
}

static void dummy_write(Emulator* e, MaskedAddress addr, u8 value) {}

static u8 dummy_read(Emulator* e, MaskedAddress addr) {
  return INVALID_READ_BYTE;
}

static void set_rom1_bank(Emulator* e, u16 bank) {
  u32 new_base = (bank & ROM_BANK_MASK(e)) << ROM_BANK_SHIFT;
  u32* base = &e->state.memory_map_state.rom1_base;
  if (new_base != *base) {
    DEBUG(memory, "%s(bank: %d) = 0x%06x\n", __func__, bank, new_base);
  }
  *base = new_base;
}

static void set_ext_ram_bank(Emulator* e, u8 bank) {
  u32 new_base = (bank & EXT_RAM_BANK_MASK(e)) << EXT_RAM_BANK_SHIFT;
  u32* base = &e->state.memory_map_state.ext_ram_base;
  if (new_base != *base) {
    DEBUG(memory, "%s(%d) = 0x%06x\n", __func__, bank, new_base);
  }
  *base = new_base;
}

#define INFO_READ_RAM_DISABLED \
  INFO(memory, "%s(0x%04x) ignored, ram disabled.\n", __func__, addr)
#define INFO_WRITE_RAM_DISABLED                                               \
  INFO(memory, "%s(0x%04x, 0x%02x) ignored, ram disabled.\n", __func__, addr, \
       value);

static u8 gb_read_ext_ram(Emulator* e, MaskedAddress addr) {
  MemoryMapState* memory_map = &e->state.memory_map_state;
  if (memory_map->ext_ram_enabled) {
    assert(addr <= ADDR_MASK_8K);
    return e->state.ext_ram.data[memory_map->ext_ram_base | addr];
  } else {
    INFO_READ_RAM_DISABLED;
    return INVALID_READ_BYTE;
  }
}

static void gb_write_ext_ram(Emulator* e, MaskedAddress addr, u8 value) {
  MemoryMapState* memory_map = &e->state.memory_map_state;
  if (memory_map->ext_ram_enabled) {
    assert(addr <= ADDR_MASK_8K);
    e->state.ext_ram.data[memory_map->ext_ram_base | addr] = value;
  } else {
    INFO_WRITE_RAM_DISABLED;
  }
}

static void mbc1_write_rom(Emulator* e, MaskedAddress addr, u8 value) {
  MemoryMapState* memory_map = &e->state.memory_map_state;
  MBC1* mbc1 = &memory_map->mbc1;
  switch (addr >> 13) {
    case 0: /* 0000-1fff */
      memory_map->ext_ram_enabled =
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
  }

  u16 rom1_bank = mbc1->byte_2000_3fff & MBC1_ROM_BANK_LO_SELECT_MASK;
  if (rom1_bank == 0) {
    rom1_bank++;
  }

  u8 ext_ram_bank = 0;
  if (mbc1->bank_mode == BANK_MODE_ROM) {
    rom1_bank |= (mbc1->byte_4000_5fff & MBC1_BANK_HI_SELECT_MASK)
                 << MBC1_BANK_HI_SHIFT;
  } else if (e->cart_info_count > 1 && mbc1->byte_4000_5fff > 0) {
    /* All MBC1M roms seem to have carts at 0x40000 intervals. */
    set_cart_info(e, mbc1->byte_4000_5fff << 3);
  } else {
    ext_ram_bank = mbc1->byte_4000_5fff & MBC1_BANK_HI_SELECT_MASK;
  }

  set_rom1_bank(e, rom1_bank);
  set_ext_ram_bank(e, ext_ram_bank);
}

static void mbc2_write_rom(Emulator* e, MaskedAddress addr, u8 value) {
  MemoryMapState* memory_map = &e->state.memory_map_state;
  switch (addr >> 13) {
    case 0: /* 0000-1fff */
      if ((addr & MBC2_ADDR_SELECT_BIT_MASK) == 0) {
        memory_map->ext_ram_enabled =
            (value & MBC_RAM_ENABLED_MASK) == MBC_RAM_ENABLED_VALUE;
      }
      break;
    case 1: { /* 2000-3fff */
      u16 rom1_bank;
      if ((addr & MBC2_ADDR_SELECT_BIT_MASK) != 0) {
        rom1_bank = value & MBC2_ROM_BANK_SELECT_MASK & ROM_BANK_MASK(e);
        if (rom1_bank == 0) {
          rom1_bank++;
        }
        set_rom1_bank(e, rom1_bank);
      }
      break;
    }
  }
}

static u8 mbc2_read_ram(Emulator* e, MaskedAddress addr) {
  if (e->state.memory_map_state.ext_ram_enabled) {
    return e->state.ext_ram.data[addr & MBC2_RAM_ADDR_MASK];
  } else {
    INFO_READ_RAM_DISABLED;
    return INVALID_READ_BYTE;
  }
}

static void mbc2_write_ram(Emulator* e, MaskedAddress addr, u8 value) {
  if (e->state.memory_map_state.ext_ram_enabled) {
    e->state.ext_ram.data[addr & MBC2_RAM_ADDR_MASK] =
        value & MBC2_RAM_VALUE_MASK;
  } else {
    INFO_WRITE_RAM_DISABLED;
  }
}

static void mbc3_write_rom(Emulator* e, MaskedAddress addr, u8 value) {
  MemoryMapState* memory_map = &e->state.memory_map_state;
  switch (addr >> 13) {
    case 0: /* 0000-1fff */
      memory_map->ext_ram_enabled =
          (value & MBC_RAM_ENABLED_MASK) == MBC_RAM_ENABLED_VALUE;
      break;
    case 1: { /* 2000-3fff */
      set_rom1_bank(e, value & MBC3_ROM_BANK_SELECT_MASK & ROM_BANK_MASK(e));
      break;
    }
    case 2: /* 4000-5fff */
      set_ext_ram_bank(e, value & MBC3_RAM_BANK_SELECT_MASK);
      break;
    default:
      break;
  }
}

static void mbc5_write_rom(Emulator* e, MaskedAddress addr, u8 value) {
  MemoryMapState* memory_map = &e->state.memory_map_state;
  switch (addr >> 12) {
    case 0: case 1: /* 0000-1fff */
      memory_map->ext_ram_enabled =
          (value & MBC_RAM_ENABLED_MASK) == MBC_RAM_ENABLED_VALUE;
      break;
    case 2: /* 2000-2fff */
      memory_map->mbc5.byte_2000_2fff = value;
      break;
    case 3: /* 3000-3fff */
      memory_map->mbc5.byte_3000_3fff = value;
      break;
    case 4: case 5: /* 4000-5fff */
      set_ext_ram_bank(e, value & MBC5_RAM_BANK_SELECT_MASK);
      break;
    default:
      break;
  }

  set_rom1_bank(e, ((memory_map->mbc5.byte_3000_3fff & 1) << 8) |
                       memory_map->mbc5.byte_2000_2fff);
}

static void huc1_write_rom(Emulator* e, MaskedAddress addr, u8 value) {
  MemoryMapState* memory_map = &e->state.memory_map_state;
  HUC1* huc1 = &memory_map->huc1;
  switch (addr >> 13) {
    case 0: /* 0000-1fff */
      memory_map->ext_ram_enabled =
          (value & MBC_RAM_ENABLED_MASK) == MBC_RAM_ENABLED_VALUE;
      break;
    case 1: /* 2000-3fff */
      huc1->byte_2000_3fff = value;
      break;
    case 2: /* 4000-5fff */
      huc1->byte_4000_5fff = value;
      break;
    case 3: /* 6000-7fff */
      huc1->bank_mode = (BankMode)(value & 1);
      break;
  }

  u16 rom1_bank = huc1->byte_2000_3fff & HUC1_ROM_BANK_LO_SELECT_MASK;
  if (rom1_bank == 0) {
    rom1_bank++;
  }

  u8 ext_ram_bank;
  if (huc1->bank_mode == BANK_MODE_ROM) {
    rom1_bank |= (huc1->byte_4000_5fff & HUC1_BANK_HI_SELECT_MASK)
                 << HUC1_BANK_HI_SHIFT;
    ext_ram_bank = 0;
  } else {
    ext_ram_bank = huc1->byte_4000_5fff & HUC1_BANK_HI_SELECT_MASK;
  }
  set_rom1_bank(e, rom1_bank);
  set_ext_ram_bank(e, ext_ram_bank);
}

static void mmm01_write_rom(Emulator* e, MaskedAddress addr, u8 value) {
  MMM01* mmm01 = &e->state.memory_map_state.mmm01;
  switch (addr >> 13) {
    case 0: { /* 0000-1fff */
      /* ROM size should be power-of-two. */
      assert((e->cart_info->size & (e->cart_info->size - 1)) == 0);
      u32 rom_offset =
          (mmm01->byte_2000_3fff << ROM_BANK_SHIFT) & (e->cart_info->size - 1);
      set_cart_info(e, rom_offset >> CART_INFO_SHIFT);
      break;
    }
    case 1: /* 2000-3fff */
      mmm01->byte_2000_3fff = value;
      break;
  }
}

static Result init_memory_map(Emulator* e) {
  CartTypeInfo* cart_type_info = &s_cart_type_info[e->cart_info->cart_type];
  MemoryMap* memory_map = &e->memory_map;

  switch (cart_type_info->ext_ram_type) {
    case EXT_RAM_TYPE_WITH_RAM:
      assert(is_ext_ram_size_valid(e->cart_info->ext_ram_size));
      memory_map->read_ext_ram = gb_read_ext_ram;
      memory_map->write_ext_ram = gb_write_ext_ram;
      e->state.ext_ram.size = s_ext_ram_byte_size[e->cart_info->ext_ram_size];
      break;
    default:
    case EXT_RAM_TYPE_NO_RAM:
      memory_map->read_ext_ram = dummy_read;
      memory_map->write_ext_ram = dummy_write;
      e->state.ext_ram.size = 0;
      break;
  }

  switch (cart_type_info->mbc_type) {
    case MBC_TYPE_NO_MBC:
      memory_map->write_rom = dummy_write;
      break;
    case MBC_TYPE_MBC1:
      memory_map->write_rom = mbc1_write_rom;
      break;
    case MBC_TYPE_MBC2:
      memory_map->write_rom = mbc2_write_rom;
      memory_map->read_ext_ram = mbc2_read_ram;
      memory_map->write_ext_ram = mbc2_write_ram;
      e->state.ext_ram.size = MBC2_RAM_SIZE;
      break;
    case MBC_TYPE_MMM01:
      memory_map->write_rom = mmm01_write_rom;
      break;
    case MBC_TYPE_MBC3:
      memory_map->write_rom = mbc3_write_rom;
      /* TODO handle MBC3 RTC */
      break;
    case MBC_TYPE_MBC5:
      memory_map->write_rom = mbc5_write_rom;
      break;
    case MBC_TYPE_HUC1:
      memory_map->write_rom = huc1_write_rom;
      break;
    default:
      PRINT_ERROR("memory map for %s not implemented.\n",
                  get_cart_type_string(e->cart_info->cart_type));
      return ERROR;
  }

  e->state.ext_ram.battery_type = cart_type_info->battery_type;
  return OK;
}

static u16 get_af_reg(Registers* reg) {
  return (reg->A << 8) | PACK(reg->F.Z, CPU_FLAG_Z) |
         PACK(reg->F.N, CPU_FLAG_N) | PACK(reg->F.H, CPU_FLAG_H) |
         PACK(reg->F.C, CPU_FLAG_C);
}

static void set_af_reg(Registers* reg, u16 af) {
  reg->A = af >> 8;
  reg->F.Z = UNPACK(af, CPU_FLAG_Z);
  reg->F.N = UNPACK(af, CPU_FLAG_N);
  reg->F.H = UNPACK(af, CPU_FLAG_H);
  reg->F.C = UNPACK(af, CPU_FLAG_C);
}

static Result init_emulator(Emulator* e) {
  static u8 s_initial_wave_ram[WAVE_RAM_SIZE] = {
      0x60, 0x0d, 0xda, 0xdd, 0x50, 0x0f, 0xad, 0xed,
      0xc0, 0xde, 0xf0, 0x0d, 0xbe, 0xef, 0xfe, 0xed,
  };
  CHECK(SUCCESS(get_cart_infos(e)));
  log_cart_info(e->cart_info);
  e->state.memory_map_state.rom1_base = 1 << ROM_BANK_SHIFT;
  set_af_reg(&e->state.reg, 0x01b0);
  e->state.reg.BC = 0x0013;
  e->state.reg.DE = 0x00d8;
  e->state.reg.HL = 0x014d;
  e->state.reg.SP = 0xfffe;
  e->state.reg.PC = 0x0100;
  e->state.interrupt.IME = FALSE;
  e->state.timer.DIV_counter = 0xAC00;
  /* Enable apu first, so subsequent writes succeed. */
  write_apu(e, APU_NR52_ADDR, 0xf1);
  write_apu(e, APU_NR11_ADDR, 0x80);
  write_apu(e, APU_NR12_ADDR, 0xf3);
  write_apu(e, APU_NR14_ADDR, 0x80);
  write_apu(e, APU_NR50_ADDR, 0x77);
  write_apu(e, APU_NR51_ADDR, 0xf3);
  memcpy(&e->state.apu.wave.ram, s_initial_wave_ram, WAVE_RAM_SIZE);
  /* Turn down the volume on channel1, it is playing by default (because of the
   * GB startup sound), but we don't want to hear it when starting the
   * emulator. */
  e->state.apu.channel[CHANNEL1].envelope.volume = 0;
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
  ON_ERROR_RETURN;
}

static MemoryTypeAddressPair make_pair(MemoryMapType type, Address addr) {
  MemoryTypeAddressPair result;
  result.type = type;
  result.addr = addr;
  return result;
}

static MemoryTypeAddressPair map_address(Address addr) {
  switch (addr >> 12) {
    case 0x0: case 0x1: case 0x2: case 0x3:
      return make_pair(MEMORY_MAP_ROM0, addr & ADDR_MASK_16K);
    case 0x4: case 0x5: case 0x6: case 0x7:
      return make_pair(MEMORY_MAP_ROM1, addr & ADDR_MASK_16K);
    case 0x8: case 0x9:
      return make_pair(MEMORY_MAP_VRAM, addr & ADDR_MASK_8K);
    case 0xA: case 0xB:
      return make_pair(MEMORY_MAP_EXT_RAM, addr & ADDR_MASK_8K);
    case 0xC: case 0xE: /* mirror of 0xc000..0xcfff */
      return make_pair(MEMORY_MAP_WORK_RAM0, addr & ADDR_MASK_4K);
    case 0xD:
      return make_pair(MEMORY_MAP_WORK_RAM1, addr & ADDR_MASK_4K);
    default: case 0xF:
      switch ((addr >> 8) & 0xf) {
        default: /* 0xf000 - 0xfdff: mirror of 0xd000-0xddff */
          return make_pair(MEMORY_MAP_WORK_RAM1, addr & ADDR_MASK_4K);
        case 0xe:
          if (addr <= OAM_END_ADDR) { /* 0xfe00 - 0xfe9f */
            return make_pair(MEMORY_MAP_OAM, addr - OAM_START_ADDR);
          } else { /* 0xfea0 - 0xfeff */
            return make_pair(MEMORY_MAP_UNUSED, addr);
          }
          break;
        case 0xf:
          switch ((addr >> 4) & 0xf) {
            case 0: case 4: case 5: case 6: case 7:
              /* 0xff00 - 0xff0f, 0xff40 - 0xff7f */
              return make_pair(MEMORY_MAP_IO, addr - IO_START_ADDR);
            case 1: case 2: /* 0xff10 - 0xff2f */
              return make_pair(MEMORY_MAP_APU, addr - APU_START_ADDR);
            case 3: /* 0xff30 - 0xff3f */
              return make_pair(MEMORY_MAP_WAVE_RAM, addr - WAVE_RAM_START_ADDR);
            case 0xf:
              if (addr == IE_ADDR) {
                return make_pair(MEMORY_MAP_IO, addr - IO_START_ADDR);
              }
              /* fallthrough */
            default: /* 0xff80 - 0xfffe */
              return make_pair(MEMORY_MAP_HIGH_RAM, addr - HIGH_RAM_START_ADDR);
          }
      }
  }
}

static u8 read_vram(Emulator* e, MaskedAddress addr) {
  if (e->state.ppu.STAT.mode == PPU_MODE_MODE3) {
    DEBUG(ppu, "read_vram(0x%04x): returning 0xff because in use.\n", addr);
    return INVALID_READ_BYTE;
  } else {
    assert(addr <= ADDR_MASK_8K);
    return e->state.vram[addr];
  }
}

static Bool is_using_oam(Emulator* e) {
  return e->state.ppu.STAT.mode == PPU_MODE_MODE2 ||
         e->state.ppu.STAT.mode == PPU_MODE_MODE3;
}

static u8 read_oam(Emulator* e, MaskedAddress addr) {
  if (is_using_oam(e)) {
    DEBUG(ppu, "read_oam(0x%04x): returning 0xff because in use.\n", addr);
    return INVALID_READ_BYTE;
  }

  u8 obj_index = addr >> 2;
  Obj* obj = &e->state.oam[obj_index];
  switch (addr & 3) {
    case 0: return obj->y + OBJ_Y_OFFSET;
    case 1: return obj->x + OBJ_X_OFFSET;
    case 2: return obj->tile;
    case 3: return obj->byte3;
  }
  UNREACHABLE("invalid OAM address: 0x%04x\n", addr);
}

static u8 read_joyp_p10_p13(Emulator* e) {
  u8 result = 0;
  if (e->state.JOYP.joypad_select == JOYPAD_SELECT_BUTTONS ||
      e->state.JOYP.joypad_select == JOYPAD_SELECT_BOTH) {
    result |= PACK(e->state.JOYP.start, JOYP_BUTTON_START) |
              PACK(e->state.JOYP.select, JOYP_BUTTON_SELECT) |
              PACK(e->state.JOYP.B, JOYP_BUTTON_B) |
              PACK(e->state.JOYP.A, JOYP_BUTTON_A);
  }

  Bool left = e->state.JOYP.left;
  Bool right = e->state.JOYP.right;
  Bool up = e->state.JOYP.up;
  Bool down = e->state.JOYP.down;
  if (!e->config.allow_simulataneous_dpad_opposites) {
    if (left && right) {
      left = FALSE;
    } else if (up && down) {
      up = FALSE;
    }
  }

  if (e->state.JOYP.joypad_select == JOYPAD_SELECT_DPAD ||
      e->state.JOYP.joypad_select == JOYPAD_SELECT_BOTH) {
    result |= PACK(down, JOYP_DPAD_DOWN) | PACK(up, JOYP_DPAD_UP) |
              PACK(left, JOYP_DPAD_LEFT) | PACK(right, JOYP_DPAD_RIGHT);
  }
  /* The bits are low when the buttons are pressed. */
  return ~result;
}

static u8 read_io(Emulator* e, MaskedAddress addr) {
  switch (addr) {
    case IO_JOYP_ADDR:
      if (e->joypad_callback.func) {
        e->joypad_callback.func(e, e->joypad_callback.user_data);
      }
      return JOYP_UNUSED |
             PACK(e->state.JOYP.joypad_select, JOYP_JOYPAD_SELECT) |
             (read_joyp_p10_p13(e) & JOYP_RESULT_MASK);
    case IO_SB_ADDR:
      return e->state.serial.SB;
    case IO_SC_ADDR:
      return SC_UNUSED | PACK(e->state.serial.transferring, SC_TRANSFER_START) |
             PACK(e->state.serial.clock, SC_SHIFT_CLOCK);
    case IO_DIV_ADDR:
      return e->state.timer.DIV_counter >> 8;
    case IO_TIMA_ADDR:
      return e->state.timer.TIMA;
    case IO_TMA_ADDR:
      return e->state.timer.TMA;
    case IO_TAC_ADDR:
      return TAC_UNUSED | PACK(e->state.timer.on, TAC_TIMER_ON) |
             PACK(e->state.timer.clock_select, TAC_CLOCK_SELECT);
    case IO_IF_ADDR:
      return IF_UNUSED | e->state.interrupt.IF;
    case IO_LCDC_ADDR:
      return PACK(e->state.ppu.LCDC.display, LCDC_DISPLAY) |
             PACK(e->state.ppu.LCDC.window_tile_map_select,
                  LCDC_WINDOW_TILE_MAP_SELECT) |
             PACK(e->state.ppu.LCDC.window_display, LCDC_WINDOW_DISPLAY) |
             PACK(e->state.ppu.LCDC.bg_tile_data_select,
                  LCDC_BG_TILE_DATA_SELECT) |
             PACK(e->state.ppu.LCDC.bg_tile_map_select,
                  LCDC_BG_TILE_MAP_SELECT) |
             PACK(e->state.ppu.LCDC.obj_size, LCDC_OBJ_SIZE) |
             PACK(e->state.ppu.LCDC.obj_display, LCDC_OBJ_DISPLAY) |
             PACK(e->state.ppu.LCDC.bg_display, LCDC_BG_DISPLAY);
    case IO_STAT_ADDR:
      return STAT_UNUSED |
             PACK(e->state.ppu.STAT.y_compare.irq, STAT_YCOMPARE_INTR) |
             PACK(e->state.ppu.STAT.mode2.irq, STAT_MODE2_INTR) |
             PACK(e->state.ppu.STAT.vblank.irq, STAT_VBLANK_INTR) |
             PACK(e->state.ppu.STAT.hblank.irq, STAT_HBLANK_INTR) |
             PACK(e->state.ppu.STAT.LY_eq_LYC, STAT_YCOMPARE) |
             PACK(e->state.ppu.STAT.mode, STAT_MODE);
    case IO_SCY_ADDR:
      return e->state.ppu.SCY;
    case IO_SCX_ADDR:
      return e->state.ppu.SCX;
    case IO_LY_ADDR:
      return e->state.ppu.LY;
    case IO_LYC_ADDR:
      return e->state.ppu.LYC;
    case IO_DMA_ADDR:
      return INVALID_READ_BYTE; /* Write only. */
    case IO_BGP_ADDR:
      return PACK(e->state.ppu.BGP.color[3], PALETTE_COLOR3) |
             PACK(e->state.ppu.BGP.color[2], PALETTE_COLOR2) |
             PACK(e->state.ppu.BGP.color[1], PALETTE_COLOR1) |
             PACK(e->state.ppu.BGP.color[0], PALETTE_COLOR0);
    case IO_OBP0_ADDR:
      return PACK(e->state.ppu.OBP[0].color[3], PALETTE_COLOR3) |
             PACK(e->state.ppu.OBP[0].color[2], PALETTE_COLOR2) |
             PACK(e->state.ppu.OBP[0].color[1], PALETTE_COLOR1) |
             PACK(e->state.ppu.OBP[0].color[0], PALETTE_COLOR0);
    case IO_OBP1_ADDR:
      return PACK(e->state.ppu.OBP[1].color[3], PALETTE_COLOR3) |
             PACK(e->state.ppu.OBP[1].color[2], PALETTE_COLOR2) |
             PACK(e->state.ppu.OBP[1].color[1], PALETTE_COLOR1) |
             PACK(e->state.ppu.OBP[1].color[0], PALETTE_COLOR0);
    case IO_WY_ADDR:
      return e->state.ppu.WY;
    case IO_WX_ADDR:
      return e->state.ppu.WX;
    case IO_IE_ADDR:
      return e->state.interrupt.IE;
    default:
      INFO(io, "%s(0x%04x [%s]) ignored.\n", __func__, addr,
           get_io_reg_string(addr));
      return INVALID_READ_BYTE;
  }
}

static u8 read_nrx1_reg(Channel* channel) {
  return PACK(channel->square_wave.duty, NRX1_WAVE_DUTY);
}

static u8 read_nrx2_reg(Channel* channel) {
  return PACK(channel->envelope.initial_volume, NRX2_INITIAL_VOLUME) |
         PACK(channel->envelope.direction, NRX2_ENVELOPE_DIRECTION) |
         PACK(channel->envelope.period, NRX2_ENVELOPE_PERIOD);
}

static u8 read_nrx4_reg(Channel* channel) {
  return PACK(channel->length_enabled, NRX4_LENGTH_ENABLED);
}

static u8 read_apu(Emulator* e, MaskedAddress addr) {
  APU* apu = &e->state.apu;
  switch (addr) {
    case APU_NR10_ADDR:
      return NR10_UNUSED | PACK(apu->sweep.period, NR10_SWEEP_PERIOD) |
             PACK(apu->sweep.direction, NR10_SWEEP_DIRECTION) |
             PACK(apu->sweep.shift, NR10_SWEEP_SHIFT);
    case APU_NR11_ADDR:
      return NRX1_UNUSED | read_nrx1_reg(&apu->channel[CHANNEL1]);
    case APU_NR12_ADDR:
      return read_nrx2_reg(&apu->channel[CHANNEL1]);
    case APU_NR14_ADDR:
      return NRX4_UNUSED | read_nrx4_reg(&apu->channel[CHANNEL1]);
    case APU_NR21_ADDR:
      return NRX1_UNUSED | read_nrx1_reg(&apu->channel[CHANNEL2]);
    case APU_NR22_ADDR:
      return read_nrx2_reg(&apu->channel[CHANNEL2]);
    case APU_NR24_ADDR:
      return NRX4_UNUSED | read_nrx4_reg(&apu->channel[CHANNEL2]);
    case APU_NR30_ADDR:
      return NR30_UNUSED |
             PACK(apu->channel[CHANNEL3].dac_enabled, NR30_DAC_ENABLED);
    case APU_NR32_ADDR:
      return NR32_UNUSED | PACK(apu->wave.volume, NR32_SELECT_WAVE_VOLUME);
    case APU_NR34_ADDR:
      return NRX4_UNUSED | read_nrx4_reg(&apu->channel[CHANNEL3]);
    case APU_NR42_ADDR:
      return read_nrx2_reg(&apu->channel[CHANNEL4]);
    case APU_NR43_ADDR:
      return PACK(apu->noise.clock_shift, NR43_CLOCK_SHIFT) |
             PACK(apu->noise.lfsr_width, NR43_LFSR_WIDTH) |
             PACK(apu->noise.divisor, NR43_DIVISOR);
    case APU_NR44_ADDR:
      return NRX4_UNUSED | read_nrx4_reg(&apu->channel[CHANNEL4]);
    case APU_NR50_ADDR:
      return PACK(apu->so_output[1][VIN], NR50_VIN_SO2) |
             PACK(apu->so_volume[1], NR50_SO2_VOLUME) |
             PACK(apu->so_output[0][VIN], NR50_VIN_SO1) |
             PACK(apu->so_volume[0], NR50_SO1_VOLUME);
    case APU_NR51_ADDR:
      return PACK(apu->so_output[1][SOUND4], NR51_SOUND4_SO2) |
             PACK(apu->so_output[1][SOUND3], NR51_SOUND3_SO2) |
             PACK(apu->so_output[1][SOUND2], NR51_SOUND2_SO2) |
             PACK(apu->so_output[1][SOUND1], NR51_SOUND1_SO2) |
             PACK(apu->so_output[0][SOUND4], NR51_SOUND4_SO1) |
             PACK(apu->so_output[0][SOUND3], NR51_SOUND3_SO1) |
             PACK(apu->so_output[0][SOUND2], NR51_SOUND2_SO1) |
             PACK(apu->so_output[0][SOUND1], NR51_SOUND1_SO1);
    case APU_NR52_ADDR:
      return NR52_UNUSED | PACK(apu->enabled, NR52_ALL_SOUND_ENABLED) |
             PACK(apu->channel[CHANNEL4].status, NR52_SOUND4_ON) |
             PACK(apu->channel[CHANNEL3].status, NR52_SOUND3_ON) |
             PACK(apu->channel[CHANNEL2].status, NR52_SOUND2_ON) |
             PACK(apu->channel[CHANNEL1].status, NR52_SOUND1_ON);
    default:
      return INVALID_READ_BYTE;
  }
}

static u8 read_wave_ram(Emulator* e, MaskedAddress addr) {
  Wave* wave = &e->state.apu.wave;
  if (e->state.apu.channel[CHANNEL3].status) {
    /* If the wave channel is playing, the byte is read from the sample
     * position. On DMG, this is only allowed if the read occurs exactly when
     * it is being accessed by the Wave channel. */
    u8 result;
    if (e->state.is_cgb || e->state.cycles == wave->sample_time) {
      result = wave->ram[wave->position >> 1];
      DEBUG(apu, "%s(0x%02x) while playing => 0x%02x (cy: %u)\n", __func__,
            addr, result, e->state.cycles);
    } else {
      result = INVALID_READ_BYTE;
      DEBUG(apu, "%s(0x%02x) while playing, invalid (0xff) (cy: %u).\n",
            __func__, addr, e->state.cycles);
    }
    return result;
  } else {
    return wave->ram[addr];
  }
}

static Bool is_dma_access_ok(Emulator* e, MemoryTypeAddressPair pair) {
  /* TODO: need to figure out bus conflicts during DMA for non-OAM accesses. */
  return e->state.dma.state != DMA_ACTIVE || pair.type != MEMORY_MAP_OAM;
}

static u8 read_u8_no_dma_check(Emulator* e, MemoryTypeAddressPair pair) {
  switch (pair.type) {
    case MEMORY_MAP_ROM0:
      assert(pair.addr < e->cart_info->size);
      return e->cart_info->data[pair.addr];
    case MEMORY_MAP_ROM1: {
      u32 rom_addr = e->state.memory_map_state.rom1_base| pair.addr;
      assert(rom_addr < e->cart_info->size);
      return e->cart_info->data[rom_addr];
    }
    case MEMORY_MAP_VRAM:
      return read_vram(e, pair.addr);
    case MEMORY_MAP_EXT_RAM:
      return e->memory_map.read_ext_ram(e, pair.addr);
    case MEMORY_MAP_WORK_RAM0:
      return e->state.wram[pair.addr];
    case MEMORY_MAP_WORK_RAM1:
      return e->state.wram[0x1000 + pair.addr];
    case MEMORY_MAP_OAM:
      return read_oam(e, pair.addr);
    case MEMORY_MAP_UNUSED:
      return INVALID_READ_BYTE;
    case MEMORY_MAP_IO: {
      u8 value = read_io(e, pair.addr);
      VERBOSE(io, "read_io(0x%04x [%s]) = 0x%02x [cy: %u]\n", pair.addr,
              get_io_reg_string(pair.addr), value, e->state.cycles);
      return value;
    }
    case MEMORY_MAP_APU:
      return read_apu(e, pair.addr);
    case MEMORY_MAP_WAVE_RAM:
      return read_wave_ram(e, pair.addr);
    case MEMORY_MAP_HIGH_RAM:
      return e->state.hram[pair.addr];
    default:
      UNREACHABLE("invalid address: %u 0x%04x.\n", pair.type, pair.addr);
  }
}

static u8 read_u8(Emulator* e, Address addr) {
  MemoryTypeAddressPair pair = map_address(addr);
  if (!is_dma_access_ok(e, pair)) {
    INFO(memory, "%s(0x%04x) during DMA.\n", __func__, addr);
    return INVALID_READ_BYTE;
  }

  return read_u8_no_dma_check(e, pair);
}

static void write_vram(Emulator* e, MaskedAddress addr, u8 value) {
  if (e->state.ppu.STAT.mode == PPU_MODE_MODE3) {
    DEBUG(ppu, "%s(0x%04x, 0x%02x) ignored, using vram.\n", __func__, addr,
          value);
    return;
  }

  assert(addr <= ADDR_MASK_8K);
  e->state.vram[addr] = value;
}

static void write_oam_no_mode_check(Emulator* e, MaskedAddress addr, u8 value) {
  Obj* obj = &e->state.oam[addr >> 2];
  switch (addr & 3) {
    case 0: obj->y = value - OBJ_Y_OFFSET; break;
    case 1: obj->x = value - OBJ_X_OFFSET; break;
    case 2: obj->tile = value; break;
    case 3:
      obj->byte3 = value;
      obj->priority = UNPACK(value, OBJ_PRIORITY);
      obj->yflip = UNPACK(value, OBJ_YFLIP);
      obj->xflip = UNPACK(value, OBJ_XFLIP);
      obj->palette = UNPACK(value, OBJ_PALETTE);
      break;
  }
}

static void write_oam(Emulator* e, MaskedAddress addr, u8 value) {
  if (is_using_oam(e)) {
    INFO(ppu, "%s(0x%04x, 0x%02x): ignored because in use.\n", __func__, addr,
         value);
    return;
  }

  write_oam_no_mode_check(e, addr, value);
}

static void increment_tima(Emulator* e) {
  if (++e->state.timer.TIMA == 0) {
    DEBUG(interrupt, ">> trigger TIMER [cy: %u]\n",
          e->state.cycles + CPU_MCYCLE);
    e->state.timer.TIMA_state = TIMA_STATE_OVERFLOW;
    e->state.interrupt.new_IF |= IF_TIMER;
  }
}

static void write_div_counter(Emulator* e, u16 DIV_counter) {
  if (e->state.timer.on) {
    u16 falling_edge =
        ((e->state.timer.DIV_counter ^ DIV_counter) & ~DIV_counter);
    if ((falling_edge & s_tima_mask[e->state.timer.clock_select]) != 0) {
      increment_tima(e);
    }
  }
  e->state.timer.DIV_counter = DIV_counter;
}

/* Trigger is only TRUE on the cycle where it transitioned to the new state;
 * "check" is TRUE as long as at continues to be in that state. This is
 * necessary because the internal STAT IF flag is set when "triggered", and
 * cleared only when the "check" returns FALSE for all STAT IF bits. HBLANK and
 * VBLANK don't have a special trigger, so "trigger" and "check" are equal for
 * those modes. */
#define TRIGGER_MODE_IS(X) (STAT->trigger_mode == PPU_MODE_##X)
#define TRIGGER_HBLANK (TRIGGER_MODE_IS(HBLANK) && STAT->hblank.irq)
#define TRIGGER_VBLANK (TRIGGER_MODE_IS(VBLANK) && STAT->vblank.irq)
#define TRIGGER_MODE2 (STAT->mode2.trigger && STAT->mode2.irq)
#define CHECK_MODE2 (TRIGGER_MODE_IS(MODE2) && STAT->mode2.irq)
#define TRIGGER_Y_COMPARE (STAT->y_compare.trigger && STAT->y_compare.irq)
#define CHECK_Y_COMPARE (STAT->new_LY_eq_LYC && STAT->y_compare.irq)
#define SHOULD_TRIGGER_STAT \
  (TRIGGER_HBLANK || TRIGGER_VBLANK || TRIGGER_MODE2 || TRIGGER_Y_COMPARE)

static void check_stat(Emulator* e) {
  LCDStatus* STAT = &e->state.ppu.STAT;
  if (!STAT->IF && SHOULD_TRIGGER_STAT) {
    VERBOSE(ppu, ">> trigger STAT [LY: %u] [cy: %u]\n", e->state.ppu.LY,
            e->state.cycles + CPU_MCYCLE);
    e->state.interrupt.new_IF |= IF_STAT;
    if (!(TRIGGER_VBLANK || TRIGGER_Y_COMPARE)) {
      e->state.interrupt.IF |= IF_STAT;
    }
    STAT->IF = TRUE;
  } else if (!(TRIGGER_HBLANK || TRIGGER_VBLANK || CHECK_MODE2 ||
               CHECK_Y_COMPARE)) {
    STAT->IF = FALSE;
  }
}

static void check_ly_eq_lyc(Emulator* e, Bool write) {
  LCDStatus* STAT = &e->state.ppu.STAT;
  if (e->state.ppu.LY == e->state.ppu.LYC ||
      (write && e->state.ppu.last_LY == SCREEN_HEIGHT_WITH_VBLANK - 1 &&
       e->state.ppu.last_LY == e->state.ppu.LYC)) {
    VERBOSE(ppu, ">> trigger Y compare [LY: %u] [cy: %u]\n", e->state.ppu.LY,
            e->state.cycles + CPU_MCYCLE);
    STAT->y_compare.trigger = TRUE;
    STAT->new_LY_eq_LYC = TRUE;
  } else {
    STAT->y_compare.trigger = FALSE;
    STAT->LY_eq_LYC = STAT->new_LY_eq_LYC = FALSE;
    if (write) {
      /* If STAT was triggered this frame due to Y compare, cancel it.
       * There's probably a nicer way to do this. */
      if ((e->state.interrupt.new_IF ^ e->state.interrupt.IF) &
          e->state.interrupt.new_IF & IF_STAT) {
        if (!SHOULD_TRIGGER_STAT) {
          e->state.interrupt.new_IF &= ~IF_STAT;
        }
      }
    }
  }
}

static void check_joyp_intr(Emulator* e) {
  u8 p10_p13 = read_joyp_p10_p13(e);
  /* JOYP interrupt only triggers on p10-p13 going from high to low (i.e. not
   * pressed to pressed). */
  if ((p10_p13 ^ e->state.JOYP.last_p10_p13) & ~p10_p13) {
    e->state.interrupt.new_IF |= IF_JOYPAD;
    e->state.JOYP.last_p10_p13 = p10_p13;
  }
}

static void write_io(Emulator* e, MaskedAddress addr, u8 value) {
  DEBUG(io, "%s(0x%04x [%s], 0x%02x) [cy: %u]\n", __func__, addr,
        get_io_reg_string(addr), value, e->state.cycles);
  switch (addr) {
    case IO_JOYP_ADDR:
      e->state.JOYP.joypad_select = UNPACK(value, JOYP_JOYPAD_SELECT);
      check_joyp_intr(e);
      break;
    case IO_SB_ADDR:
      e->state.serial.SB = value;
      break;
    case IO_SC_ADDR:
      e->state.serial.transferring = UNPACK(value, SC_TRANSFER_START);
      e->state.serial.clock = UNPACK(value, SC_SHIFT_CLOCK);
      if (e->state.serial.transferring) {
        e->state.serial.cycles = 0;
        e->state.serial.transferred_bits = 0;
      }
      break;
    case IO_DIV_ADDR:
      write_div_counter(e, 0);
      break;
    case IO_TIMA_ADDR:
      if (e->state.timer.on) {
        if (e->state.timer.TIMA_state == TIMA_STATE_OVERFLOW) {
          /* Cancel the overflow and interrupt if written on the same cycle. */
          e->state.timer.TIMA_state = TIMA_STATE_NORMAL;
          e->state.interrupt.new_IF &= ~IF_TIMER;
          e->state.timer.TIMA = value;
        } else if (e->state.timer.TIMA_state != TIMA_STATE_RESET) {
          /* Only update TIMA if it wasn't reset this cycle. */
          e->state.timer.TIMA = value;
        }
      } else {
        e->state.timer.TIMA = value;
      }
      break;
    case IO_TMA_ADDR:
      e->state.timer.TMA = value;
      if (e->state.timer.on && e->state.timer.TIMA_state == TIMA_STATE_RESET) {
        e->state.timer.TIMA = value;
      }
      break;
    case IO_TAC_ADDR: {
      Bool old_timer_on = e->state.timer.on;
      u16 old_tima_mask = s_tima_mask[e->state.timer.clock_select];
      e->state.timer.clock_select = UNPACK(value, TAC_CLOCK_SELECT);
      e->state.timer.on = UNPACK(value, TAC_TIMER_ON);
      /* TIMA is incremented when a specific bit of DIV_counter transitions
       * from 1 to 0. This can happen as a result of writing to DIV, or in this
       * case modifying which bit we're looking at. */
      Bool tima_tick = FALSE;
      if (!old_timer_on) {
        u16 tima_mask = s_tima_mask[e->state.timer.clock_select];
        if (e->state.timer.on) {
          tima_tick = (e->state.timer.DIV_counter & old_tima_mask) != 0;
        } else {
          tima_tick = (e->state.timer.DIV_counter & old_tima_mask) != 0 &&
                      (e->state.timer.DIV_counter & tima_mask) == 0;
        }
        if (tima_tick) {
          increment_tima(e);
        }
      }
      break;
    }
    case IO_IF_ADDR:
      e->state.interrupt.new_IF = e->state.interrupt.IF = value;
      break;
    case IO_LCDC_ADDR: {
      LCDControl* LCDC = &e->state.ppu.LCDC;
      Bool was_enabled = LCDC->display;
      LCDC->display = UNPACK(value, LCDC_DISPLAY);
      LCDC->window_tile_map_select = UNPACK(value, LCDC_WINDOW_TILE_MAP_SELECT);
      LCDC->window_display = UNPACK(value, LCDC_WINDOW_DISPLAY);
      LCDC->bg_tile_data_select = UNPACK(value, LCDC_BG_TILE_DATA_SELECT);
      LCDC->bg_tile_map_select = UNPACK(value, LCDC_BG_TILE_MAP_SELECT);
      LCDC->obj_size = UNPACK(value, LCDC_OBJ_SIZE);
      LCDC->obj_display = UNPACK(value, LCDC_OBJ_DISPLAY);
      LCDC->bg_display = UNPACK(value, LCDC_BG_DISPLAY);
      if (was_enabled ^ LCDC->display) {
        e->state.ppu.STAT.mode = PPU_MODE_HBLANK;
        e->state.ppu.LY = e->state.ppu.line_y = 0;
        if (LCDC->display) {
          DEBUG(ppu, "Enabling display. [cy: %u]\n", e->state.cycles);
          e->state.ppu.state = PPU_STATE_LCD_ON_MODE2;
          e->state.ppu.state_cycles = PPU_MODE2_CYCLES;
          e->state.ppu.line_cycles = PPU_LINE_CYCLES - CPU_MCYCLE;
          e->state.ppu.display_delay_frames = PPU_ENABLE_DISPLAY_DELAY_FRAMES;
          e->state.ppu.STAT.trigger_mode = PPU_MODE_MODE2;
        } else {
          DEBUG(ppu, "Disabling display. [cy: %u]\n", e->state.cycles);
          /* Clear the framebuffer. */
          size_t i;
          for (i = 0; i < ARRAY_SIZE(e->frame_buffer); ++i) {
            e->frame_buffer[i] = RGBA_WHITE;
          }
          e->state.ppu.new_frame_edge = TRUE;
        }
      }
      break;
    }
    case IO_STAT_ADDR: {
      LCDStatus* STAT = &e->state.ppu.STAT;
      if (e->state.ppu.LCDC.display) {
        Bool hblank = TRIGGER_MODE_IS(HBLANK) && !STAT->hblank.irq;
        Bool vblank = TRIGGER_MODE_IS(VBLANK) && !STAT->vblank.irq;
        Bool y_compare = STAT->new_LY_eq_LYC && !STAT->y_compare.irq;
        if (!STAT->IF && (hblank || vblank || y_compare)) {
          VERBOSE(ppu,
                  ">> trigger STAT from write [%c%c%c] [LY: %u] [cy: %u]\n",
                  y_compare ? 'Y' : '.', vblank ? 'V' : '.', hblank ? 'H' : '.',
                  e->state.ppu.LY, e->state.cycles + CPU_MCYCLE);
          e->state.interrupt.new_IF |= IF_STAT;
          e->state.interrupt.IF |= IF_STAT;
          STAT->IF = TRUE;
        }
      }
      e->state.ppu.STAT.y_compare.irq = UNPACK(value, STAT_YCOMPARE_INTR);
      e->state.ppu.STAT.mode2.irq = UNPACK(value, STAT_MODE2_INTR);
      e->state.ppu.STAT.vblank.irq = UNPACK(value, STAT_VBLANK_INTR);
      e->state.ppu.STAT.hblank.irq = UNPACK(value, STAT_HBLANK_INTR);
      break;
    }
    case IO_SCY_ADDR:
      e->state.ppu.SCY = value;
      break;
    case IO_SCX_ADDR:
      e->state.ppu.SCX = value;
      break;
    case IO_LY_ADDR:
      break;
    case IO_LYC_ADDR:
      e->state.ppu.LYC = value;
      if (e->state.ppu.LCDC.display) {
        check_ly_eq_lyc(e, TRUE);
        check_stat(e);
      }
      break;
    case IO_DMA_ADDR:
      /* DMA can be restarted. */
      e->state.dma.state =
          (e->state.dma.state != DMA_INACTIVE ? e->state.dma.state
                                              : DMA_TRIGGERED);
      e->state.dma.source = map_address(value << 8);
      e->state.dma.cycles = 0;
      break;
    case IO_BGP_ADDR:
      e->state.ppu.BGP.color[3] = UNPACK(value, PALETTE_COLOR3);
      e->state.ppu.BGP.color[2] = UNPACK(value, PALETTE_COLOR2);
      e->state.ppu.BGP.color[1] = UNPACK(value, PALETTE_COLOR1);
      e->state.ppu.BGP.color[0] = UNPACK(value, PALETTE_COLOR0);
      break;
    case IO_OBP0_ADDR:
      e->state.ppu.OBP[0].color[3] = UNPACK(value, PALETTE_COLOR3);
      e->state.ppu.OBP[0].color[2] = UNPACK(value, PALETTE_COLOR2);
      e->state.ppu.OBP[0].color[1] = UNPACK(value, PALETTE_COLOR1);
      e->state.ppu.OBP[0].color[0] = UNPACK(value, PALETTE_COLOR0);
      break;
    case IO_OBP1_ADDR:
      e->state.ppu.OBP[1].color[3] = UNPACK(value, PALETTE_COLOR3);
      e->state.ppu.OBP[1].color[2] = UNPACK(value, PALETTE_COLOR2);
      e->state.ppu.OBP[1].color[1] = UNPACK(value, PALETTE_COLOR1);
      e->state.ppu.OBP[1].color[0] = UNPACK(value, PALETTE_COLOR0);
      break;
    case IO_WY_ADDR:
      e->state.ppu.WY = value;
      break;
    case IO_WX_ADDR:
      e->state.ppu.WX = value;
      break;
    case IO_IE_ADDR:
      e->state.interrupt.IE = value;
      break;
    default:
      INFO(memory, "%s(0x%04x, 0x%02x) ignored.\n", __func__, addr, value);
      break;
  }
}

#define CHANNEL_INDEX(c) ((c) - e->state.apu.channel)

static void write_nrx1_reg(Emulator* e, Channel* channel, u8 value) {
  if (e->state.apu.enabled) {
    channel->square_wave.duty = UNPACK(value, NRX1_WAVE_DUTY);
  }
  channel->length = NRX1_MAX_LENGTH - UNPACK(value, NRX1_LENGTH);
  VERBOSE(apu, "write_nrx1_reg(%zu, 0x%02x) length=%u\n",
          CHANNEL_INDEX(channel), value, channel->length);
}

static void write_nrx2_reg(Emulator* e, Channel* channel, u8 value) {
  channel->envelope.initial_volume = UNPACK(value, NRX2_INITIAL_VOLUME);
  channel->dac_enabled = UNPACK(value, NRX2_DAC_ENABLED) != 0;
  if (!channel->dac_enabled) {
    channel->status = FALSE;
    VERBOSE(apu, "write_nrx2_reg(%zu, 0x%02x) dac_enabled = false\n",
            CHANNEL_INDEX(channel), value);
  }
  if (channel->status) {
    if (channel->envelope.period == 0 && channel->envelope.automatic) {
      u8 new_volume = (channel->envelope.volume + 1) & ENVELOPE_MAX_VOLUME;
      VERBOSE(apu, "write_nrx2_reg(%zu, 0x%02x) zombie mode: volume %u -> %u\n",
              CHANNEL_INDEX(channel), value, channel->envelope.volume,
              new_volume);
      channel->envelope.volume = new_volume;
    }
  }
  channel->envelope.direction = UNPACK(value, NRX2_ENVELOPE_DIRECTION);
  channel->envelope.period = UNPACK(value, NRX2_ENVELOPE_PERIOD);
  VERBOSE(apu, "write_nrx2_reg(%zu, 0x%02x) initial_volume=%u\n",
          CHANNEL_INDEX(channel), value, channel->envelope.initial_volume);
}

static void write_nrx3_reg(Emulator* e, Channel* channel, u8 value) {
  channel->frequency = (channel->frequency & ~0xff) | value;
}

/* Returns TRUE if this channel was triggered. */
static Bool write_nrx4_reg(Emulator* e, Channel* channel, u8 value,
                           u16 max_length) {
  Bool trigger = UNPACK(value, NRX4_INITIAL);
  Bool was_length_enabled = channel->length_enabled;
  channel->length_enabled = UNPACK(value, NRX4_LENGTH_ENABLED);
  channel->frequency &= 0xff;
  channel->frequency |= UNPACK(value, NRX4_FREQUENCY_HI) << 8;

  /* Extra length clocking occurs on NRX4 writes if the next APU frame isn't a
   * length counter frame. This only occurs on transition from disabled to
   * enabled. */
  Bool next_frame_is_length = (e->state.apu.frame & 1) == 1;
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
  envelope->automatic = TRUE;
  /* If the next APU frame will update the envelope, increment the timer. */
  if (e->state.apu.frame + 1 == FRAME_SEQUENCER_UPDATE_ENVELOPE_FRAME) {
    envelope->timer++;
  }
  DEBUG(apu, "%s: volume=%u, timer=%u\n", __func__, envelope->volume,
        envelope->timer);
}

static u16 calculate_sweep_frequency(Sweep* sweep) {
  u16 f = sweep->frequency;
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

static void write_wave_period(Channel* channel, Wave* wave) {
  wave->period = ((SOUND_MAX_FREQUENCY + 1) - channel->frequency) * 2;
  DEBUG(apu, "%s: freq: %u cycle: %u period: %u\n", __func__,
        channel->frequency, wave->cycles, wave->period);
}

static void write_square_wave_period(Channel* channel, SquareWave* wave) {
  wave->period = ((SOUND_MAX_FREQUENCY + 1) - channel->frequency) * 4;
  DEBUG(apu, "%s: freq: %u cycle: %u period: %u\n", __func__,
        channel->frequency, wave->cycles, wave->period);
}

static void write_noise_period(Noise* noise) {
  static const u8 s_divisors[NOISE_DIVISOR_COUNT] = {8,  16, 32, 48,
                                                     64, 80, 96, 112};
  u8 divisor = s_divisors[noise->divisor];
  assert(noise->divisor < NOISE_DIVISOR_COUNT);
  noise->period = divisor << noise->clock_shift;
  DEBUG(apu, "%s: divisor: %u clock shift: %u period: %u\n", __func__,
        divisor, noise->clock_shift, noise->period);
}

static void write_apu(Emulator* e, MaskedAddress addr, u8 value) {
  if (!e->state.apu.enabled) {
    if (!e->state.is_cgb && (addr == APU_NR11_ADDR || addr == APU_NR21_ADDR ||
                             addr == APU_NR31_ADDR || addr == APU_NR41_ADDR)) {
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

  APU* apu = &e->state.apu;
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
      sweep->period = UNPACK(value, NR10_SWEEP_PERIOD);
      sweep->direction = UNPACK(value, NR10_SWEEP_DIRECTION);
      sweep->shift = UNPACK(value, NR10_SWEEP_SHIFT);
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
        channel1->square_wave.cycles = channel1->square_wave.period;
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
        channel2->square_wave.cycles = channel2->square_wave.period;
      }
      break;
    }
    case APU_NR30_ADDR:
      channel3->dac_enabled = UNPACK(value, NR30_DAC_ENABLED);
      if (!channel3->dac_enabled) {
        channel3->status = FALSE;
        wave->playing = FALSE;
      }
      break;
    case APU_NR31_ADDR:
      channel3->length = NR31_MAX_LENGTH - value;
      break;
    case APU_NR32_ADDR:
      wave->volume = UNPACK(value, NR32_SELECT_WAVE_VOLUME);
      assert(wave->volume < WAVE_VOLUME_COUNT);
      wave->volume_shift = s_wave_volume_shift[wave->volume];
      break;
    case APU_NR33_ADDR:
      write_nrx3_reg(e, channel3, value);
      write_wave_period(channel3, wave);
      break;
    case APU_NR34_ADDR: {
      Bool trigger = write_nrx4_reg(e, channel3, value, NR31_MAX_LENGTH);
      write_wave_period(channel3, wave);
      if (trigger) {
        if (!e->state.is_cgb && wave->playing) {
          /* Triggering the wave channel while it is already playing will
           * corrupt the wave RAM on DMG. */
          if (wave->cycles == WAVE_TRIGGER_CORRUPTION_OFFSET_CYCLES) {
            assert(wave->position < 32);
            u8 position = (wave->position + 1) & 31;
            u8 byte = wave->ram[position >> 1];
            switch (position >> 3) {
              case 0:
                wave->ram[0] = byte;
                break;
              case 1:
              case 2:
              case 3:
                memcpy(&wave->ram[0], &wave->ram[(position >> 1) & 12], 4);
                break;
            }
            DEBUG(apu, "%s: corrupting wave ram. (cy: %u) (pos: %u)\n",
                  __func__, e->state.cycles, position);
          }
        }

        wave->position = 0;
        wave->cycles = wave->period + WAVE_TRIGGER_DELAY_CYCLES;
        wave->playing = TRUE;
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
      noise->clock_shift = UNPACK(value, NR43_CLOCK_SHIFT);
      noise->lfsr_width = UNPACK(value, NR43_LFSR_WIDTH);
      noise->divisor = UNPACK(value, NR43_DIVISOR);
      write_noise_period(noise);
      break;
    }
    case APU_NR44_ADDR: {
      Bool trigger = write_nrx4_reg(e, channel4, value, NRX1_MAX_LENGTH);
      if (trigger) {
        write_noise_period(noise);
        trigger_nrx4_envelope(e, &channel4->envelope);
        noise->lfsr = 0x7fff;
        noise->cycles = noise->period;
      }
      break;
    }
    case APU_NR50_ADDR:
      apu->so_output[1][VIN] = UNPACK(value, NR50_VIN_SO2);
      apu->so_volume[1] = UNPACK(value, NR50_SO2_VOLUME);
      apu->so_output[0][VIN] = UNPACK(value, NR50_VIN_SO1);
      apu->so_volume[0] = UNPACK(value, NR50_SO1_VOLUME);
      break;
    case APU_NR51_ADDR:
      apu->so_output[1][SOUND4] = UNPACK(value, NR51_SOUND4_SO2);
      apu->so_output[1][SOUND3] = UNPACK(value, NR51_SOUND3_SO2);
      apu->so_output[1][SOUND2] = UNPACK(value, NR51_SOUND2_SO2);
      apu->so_output[1][SOUND1] = UNPACK(value, NR51_SOUND1_SO2);
      apu->so_output[0][SOUND4] = UNPACK(value, NR51_SOUND4_SO1);
      apu->so_output[0][SOUND3] = UNPACK(value, NR51_SOUND3_SO1);
      apu->so_output[0][SOUND2] = UNPACK(value, NR51_SOUND2_SO1);
      apu->so_output[0][SOUND1] = UNPACK(value, NR51_SOUND1_SO1);
      break;
    case APU_NR52_ADDR: {
      Bool was_enabled = apu->enabled;
      Bool is_enabled = UNPACK(value, NR52_ALL_SOUND_ENABLED);
      if (was_enabled && !is_enabled) {
        DEBUG(apu, "Powered down APU. Clearing registers.\n");
        int i;
        for (i = 0; i < APU_REG_COUNT; ++i) {
          if (i != APU_NR52_ADDR) {
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

static void write_wave_ram(Emulator* e, MaskedAddress addr, u8 value) {
  Wave* wave = &e->state.apu.wave;
  if (e->state.apu.channel[CHANNEL3].status) {
    /* If the wave channel is playing, the byte is written to the sample
     * position. On DMG, this is only allowed if the write occurs exactly when
     * it is being accessed by the Wave channel. */
    if (e->state.is_cgb || e->state.cycles == wave->sample_time) {
      wave->ram[wave->position >> 1] = value;
      DEBUG(apu, "%s(0x%02x, 0x%02x) while playing.\n", __func__, addr, value);
    }
  } else {
    wave->ram[addr] = value;
    DEBUG(apu, "%s(0x%02x, 0x%02x)\n", __func__, addr, value);
  }
}

static void write_u8(Emulator* e, Address addr, u8 value) {
  MemoryTypeAddressPair pair = map_address(addr);
  if (!is_dma_access_ok(e, pair)) {
    INFO(memory, "%s(0x%04x, 0x%02x) during DMA.\n", __func__, addr, value);
    return;
  }

  switch (pair.type) {
    case MEMORY_MAP_ROM0:
      return e->memory_map.write_rom(e, pair.addr, value);
    case MEMORY_MAP_ROM1:
      return e->memory_map.write_rom(e, pair.addr + 0x4000, value);
    case MEMORY_MAP_VRAM:
      return write_vram(e, pair.addr, value);
    case MEMORY_MAP_EXT_RAM:
      return e->memory_map.write_ext_ram(e, pair.addr, value);
    case MEMORY_MAP_WORK_RAM0:
      e->state.wram[pair.addr] = value;
      break;
    case MEMORY_MAP_WORK_RAM1:
      e->state.wram[0x1000 + pair.addr] = value;
      break;
    case MEMORY_MAP_OAM:
      return write_oam(e, pair.addr, value);
    case MEMORY_MAP_UNUSED:
      break;
    case MEMORY_MAP_IO:
      return write_io(e, pair.addr, value);
    case MEMORY_MAP_APU:
      return write_apu(e, pair.addr, value);
    case MEMORY_MAP_WAVE_RAM:
      return write_wave_ram(e, pair.addr, value);
    case MEMORY_MAP_HIGH_RAM:
      e->state.hram[pair.addr] = value;
      break;
  }
}

static void dma_mcycle(Emulator* e) {
  if (e->state.dma.state == DMA_INACTIVE) {
    return;
  }
  if (e->state.dma.cycles < DMA_DELAY_CYCLES) {
    e->state.dma.cycles += CPU_MCYCLE;
    if (e->state.dma.cycles >= DMA_DELAY_CYCLES) {
      e->state.dma.cycles = DMA_DELAY_CYCLES;
      e->state.dma.state = DMA_ACTIVE;
    }
    return;
  }

  u8 addr_offset = (e->state.dma.cycles - DMA_DELAY_CYCLES) >> 2;
  assert(addr_offset < OAM_TRANSFER_SIZE);
  MemoryTypeAddressPair pair = e->state.dma.source;
  pair.addr += addr_offset;
  u8 value = read_u8_no_dma_check(e, pair);
  write_oam_no_mode_check(e, addr_offset, value);
  e->state.dma.cycles += CPU_MCYCLE;
  if (VALUE_WRAPPED(e->state.dma.cycles, DMA_CYCLES)) {
    e->state.dma.state = DMA_INACTIVE;
  }
}

static void ppu_mode2_mcycle(Emulator* e) {
  PPU* ppu = &e->state.ppu;
  if (!ppu->LCDC.obj_display || e->config.disable_obj ||
      ppu->line_obj_count >= OBJ_PER_LINE_COUNT) {
    return;
  }

  /* 80 cycles / 40 sprites == 2 cycles/sprite == 2 sprites per M-cycle. */
  int i;
  u8 obj_height = s_obj_size_to_height[ppu->LCDC.obj_size];
  u8 y = ppu->line_y;
  for (i = 0; i < 2 && ppu->line_obj_count < OBJ_PER_LINE_COUNT; ++i) {
    /* Put the visible sprites into line_obj, but insert them so sprites with
     * smaller X-coordinates are earlier. */
    Obj* o = &e->state.oam[ppu->oam_index];
    u8 rel_y = y - o->y;
    if (rel_y < obj_height) {
      int j = ppu->line_obj_count;
      while (j > 0 && o->x < ppu->line_obj[j - 1].x) {
        ppu->line_obj[j] = ppu->line_obj[j - 1];
        j--;
      }
      ppu->line_obj[j] = *o;
      ppu->line_obj_count++;
    }
    ppu->oam_index++;
  }
}

static u32 mode3_cycle_count(Emulator* e) {
  s32 buckets[SCREEN_WIDTH / 8 + 2];
  ZERO_MEMORY(buckets);
  u8 scx_fine = e->state.ppu.SCX & 7;
  u32 cycles = PPU_MODE3_MIN_CYCLES + scx_fine;
  Bool has_zero = FALSE;
  int i;
  for (i = 0; i < e->state.ppu.line_obj_count; ++i) {
    Obj* o = &e->state.ppu.line_obj[i];
    u8 x = o->x + OBJ_X_OFFSET;
    if (x >= SCREEN_WIDTH + OBJ_X_OFFSET) {
      continue;
    }
    if (!has_zero && x == 0) {
      has_zero = TRUE;
      cycles += scx_fine;
    }
    x += scx_fine;
    int bucket = x >> 3;
    buckets[bucket] = MAX(buckets[bucket], 5 - (x & 7));
    cycles += 6;
  }
  for (i = 0; i < (int)ARRAY_SIZE(buckets); ++i) {
    cycles += buckets[i];
  }
  return cycles;
}

static u8 reverse_bits_u8(u8 x) {
  return ((x >> 7) & 0x01) | ((x >> 5) & 0x02) | ((x >> 3) & 0x04) |
         ((x >> 1) & 0x08) | ((x << 1) & 0x10) | ((x << 3) & 0x20) |
         ((x << 5) & 0x40) | ((x << 7) & 0x80);
}

static void ppu_mode3_mcycle(Emulator* e) {
  int i;
  PPU* ppu = &e->state.ppu;
  u8 x = ppu->render_x;
  u8 y = ppu->line_y;
  if (x + 4 > SCREEN_WIDTH) {
    return;
  }
  u8* vram = e->state.vram;
  /* Each M-cycle writes 4 pixels. */
  Color pixels[4];
  ZERO_MEMORY(pixels);
  Bool bg_is_zero[4];
  memset(bg_is_zero, TRUE, sizeof(bg_is_zero));

  TileDataSelect data_select = ppu->LCDC.bg_tile_data_select;
  for (i = 0; i < 4; ++i) {
    u8 xi = x + i;
    ppu->rendering_window =
        ppu->rendering_window ||
        (ppu->LCDC.window_display && !e->config.disable_window &&
         xi + WINDOW_X_OFFSET >= ppu->WX && ppu->WX <= WINDOW_MAX_X &&
         y >= ppu->frame_WY);
    Bool display_bg = ppu->LCDC.bg_display && !e->config.disable_bg;
    if (ppu->rendering_window || display_bg) {
      TileMapSelect map_select;
      u8 mx, my;
      if (ppu->rendering_window) {
        map_select = ppu->LCDC.window_tile_map_select;
        mx = xi + WINDOW_X_OFFSET - ppu->WX;
        my = ppu->win_y;
      } else {
        map_select = ppu->LCDC.bg_tile_map_select;
        mx = ppu->SCX + xi;
        my = ppu->SCY + y;
      }
      u8* map = &vram[map_select == TILE_MAP_9800_9BFF ? 0x1800 : 0x1C00];
      u16 tile_index = map[((my >> 3) * TILE_MAP_WIDTH) | (mx >> 3)];
      if (data_select == TILE_DATA_8800_97FF) {
        tile_index = 256 + (s8)tile_index;
      }
      u16 tile_addr = (tile_index * TILE_HEIGHT + (my & 7)) * TILE_ROW_BYTES;
      u8 lo = vram[tile_addr];
      u8 hi = vram[tile_addr + 1];
      u8 shift = 7 - (mx & 7);
      u8 palette_index = (((hi >> shift) & 1) << 1) | ((lo >> shift) & 1);
      pixels[i] = ppu->BGP.color[palette_index];
      bg_is_zero[i] = palette_index == 0;
    }
  }

  if (ppu->LCDC.obj_display && !e->config.disable_obj) {
    u8 obj_height = s_obj_size_to_height[ppu->LCDC.obj_size];
    int n;
    for (n = ppu->line_obj_count - 1; n >= 0; --n) {
      Obj* o = &ppu->line_obj[n];
      /* Does [x, x + 4) intersect [o->x, o->x + 8)? Note that the sums must
       * wrap at 256 (i.e. arithmetic is 8-bit). */
      s8 ox_start = o->x - x;
      s8 ox_end = ox_start + 7; /* ox_end is inclusive. */
      u8 oy = y - o->y;
      if (((u8)ox_start >= 4 && (u8)ox_end >= 8) || oy >= obj_height) {
        continue;
      }

      if (o->yflip) {
        oy = obj_height - 1 - oy;
      }

      u8 tile_index = o->tile;
      if (obj_height == 16) {
        if (oy < 8) {
          /* Top tile of 8x16 sprite. */
          tile_index &= 0xfe;
        } else {
          /* Bottom tile of 8x16 sprite. */
          tile_index |= 0x01;
          oy -= 8;
        }
      }
      u16 tile_addr = (tile_index * TILE_HEIGHT + (oy & 7)) * TILE_ROW_BYTES;
      u8 lo = vram[tile_addr];
      u8 hi = vram[tile_addr + 1];
      if (!o->xflip) {
        lo = reverse_bits_u8(lo);
        hi = reverse_bits_u8(hi);
      }

      int tile_data_offset = MAX(0, -ox_start);
      assert(tile_data_offset >= 0 && tile_data_offset < 8);
      lo >>= tile_data_offset;
      hi >>= tile_data_offset;

      int start = MAX(0, ox_start);
      assert(start >= 0 && start < 4);
      int end = MIN(3, ox_end); /* end is inclusive. */
      assert(end >= 0 && end < 4);
      for (i = start; i <= end; ++i, lo >>= 1, hi >>= 1) {
        u8 palette_index = ((hi & 1) << 1) | (lo & 1);
        if (palette_index != 0 &&
            (o->priority == OBJ_PRIORITY_ABOVE_BG || bg_is_zero[i])) {
          pixels[i] = ppu->OBP[o->palette].color[palette_index];
        }
      }
    }
  }

  RGBA* rgba_pixels = &e->frame_buffer[y * SCREEN_WIDTH + x];
  for (i = 0; i < 4; ++i) {
    rgba_pixels[i] = s_color_to_rgba[pixels[i]];
  }

  ppu->render_x += 4;
}

static void ppu_mcycle(Emulator* e) {
  PPU* ppu = &e->state.ppu;
  LCDStatus* STAT = &ppu->STAT;
  if (!ppu->LCDC.display) {
    return;
  }

  PPUMode last_trigger_mode = STAT->trigger_mode;
  Bool last_mode2_trigger = STAT->mode2.trigger;
  Bool last_y_compare_trigger = STAT->y_compare.trigger;

  STAT->mode2.trigger = FALSE;
  STAT->y_compare.trigger = FALSE;
  STAT->LY_eq_LYC = STAT->new_LY_eq_LYC;
  ppu->last_LY = ppu->LY;

  switch (STAT->mode) {
    case PPU_MODE_MODE2: ppu_mode2_mcycle(e); break;
    case PPU_MODE_MODE3: ppu_mode3_mcycle(e); break;
    default: break;
  }

  ppu->state_cycles -= CPU_MCYCLE;
  ppu->line_cycles -= CPU_MCYCLE;
  if (ppu->state_cycles == 0) {
    switch (ppu->state) {
      case PPU_STATE_HBLANK:
      case PPU_STATE_VBLANK_PLUS_4:
        ppu->line_y++;
        ppu->LY++;
        ppu->line_cycles = PPU_LINE_CYCLES;
        check_ly_eq_lyc(e, FALSE);
        ppu->state_cycles = CPU_MCYCLE;

        if (ppu->state == PPU_STATE_HBLANK) {
          STAT->mode2.trigger = TRUE;
          if (ppu->LY == SCREEN_HEIGHT) {
            ppu->state = PPU_STATE_VBLANK;
            STAT->trigger_mode = PPU_MODE_VBLANK;
            ppu->frame++;
            e->state.interrupt.new_IF |= IF_VBLANK;
            if (ppu->display_delay_frames == 0) {
              ppu->new_frame_edge = TRUE;
            } else {
              ppu->display_delay_frames--;
            }
          } else {
            ppu->state = PPU_STATE_HBLANK_PLUS_4;
            STAT->trigger_mode = PPU_MODE_MODE2;
            if (ppu->rendering_window) {
              ppu->win_y++;
            }
          }
        } else {
          assert(ppu->state == PPU_STATE_VBLANK_PLUS_4);
          if (ppu->LY == SCREEN_HEIGHT_WITH_VBLANK - 1) {
            ppu->state = PPU_STATE_VBLANK_LY_0;
          } else {
            ppu->state_cycles = PPU_LINE_CYCLES;
          }
        }
        break;

      case PPU_STATE_HBLANK_PLUS_4:
        ppu->state = PPU_STATE_MODE2;
        ppu->state_cycles = PPU_MODE2_CYCLES;
        STAT->mode = PPU_MODE_MODE2;
        ppu->oam_index = 0;
        ppu->line_obj_count = 0;
        break;

      case PPU_STATE_VBLANK:
        ppu->state = PPU_STATE_VBLANK_PLUS_4;
        ppu->state_cycles = PPU_LINE_CYCLES - CPU_MCYCLE;
        STAT->mode = PPU_MODE_VBLANK;
        break;

      case PPU_STATE_VBLANK_LY_0:
        ppu->state = PPU_STATE_VBLANK_LY_0_PLUS_4;
        ppu->state_cycles = CPU_MCYCLE;
        ppu->LY = 0;
        break;

      case PPU_STATE_VBLANK_LY_0_PLUS_4:
        ppu->state = PPU_STATE_VBLANK_LINE_Y_0;
        ppu->state_cycles = PPU_LINE_CYCLES - CPU_MCYCLE - CPU_MCYCLE;
        check_ly_eq_lyc(e, FALSE);
        break;

      case PPU_STATE_VBLANK_LINE_Y_0:
        ppu->state = PPU_STATE_HBLANK_PLUS_4;
        ppu->state_cycles = CPU_MCYCLE;
        ppu->line_cycles = PPU_LINE_CYCLES;
        ppu->line_y = 0;
        ppu->frame_WY = ppu->WY;
        ppu->win_y = 0;
        STAT->mode2.trigger = TRUE;
        STAT->mode = PPU_MODE_HBLANK;
        STAT->trigger_mode = PPU_MODE_MODE2;
        break;

      case PPU_STATE_LCD_ON_MODE2:
      case PPU_STATE_MODE2:
        ppu->state_cycles = mode3_cycle_count(e);
        if (ppu->state == PPU_STATE_LCD_ON_MODE2 ||
            (ppu->state_cycles & 3) != 0) {
          ppu->state = PPU_STATE_MODE3;
        } else {
          ppu->state = PPU_STATE_MODE3_EARLY_TRIGGER;
          ppu->state_cycles--;
        }
        ppu->state_cycles &= ~3;
        STAT->mode = STAT->trigger_mode = PPU_MODE_MODE3;
        ppu->render_x = 0;
        ppu->rendering_window = FALSE;
        break;

      case PPU_STATE_MODE3_EARLY_TRIGGER:
        ppu->state = PPU_STATE_MODE3_COMMON;
        ppu->state_cycles = CPU_MCYCLE;
        STAT->trigger_mode = PPU_MODE_HBLANK;
        break;

      case PPU_STATE_MODE3:
        STAT->trigger_mode = PPU_MODE_HBLANK;
        /* fallthrough */

      case PPU_STATE_MODE3_COMMON:
        ppu->state = PPU_STATE_HBLANK;
        ppu->state_cycles = ppu->line_cycles;
        STAT->mode = PPU_MODE_HBLANK;
        break;

      case PPU_STATE_COUNT:
        assert(0);
        break;
    }
  }
  if (STAT->trigger_mode != last_trigger_mode ||
      STAT->mode2.trigger != last_mode2_trigger ||
      STAT->y_compare.trigger != last_y_compare_trigger) {
    check_stat(e);
  }
}

static void timer_mcycle(Emulator* e) {
  if (e->state.timer.on) {
    if (e->state.timer.TIMA_state == TIMA_STATE_OVERFLOW) {
      e->state.timer.TIMA_state = TIMA_STATE_RESET;
      e->state.timer.TIMA = e->state.timer.TMA;
    } else if (e->state.timer.TIMA_state == TIMA_STATE_RESET) {
      e->state.timer.TIMA_state = TIMA_STATE_NORMAL;
    }
  }
  write_div_counter(e, e->state.timer.DIV_counter + CPU_MCYCLE);
}

static void update_channel_sweep(Channel* channel, Sweep* sweep) {
  if (!sweep->enabled) {
    return;
  }

  u8 period = sweep->period;
  if (--sweep->timer == 0) {
    if (period) {
      sweep->timer = period;
      u16 new_frequency = calculate_sweep_frequency(sweep);
      if (new_frequency > SOUND_MAX_FREQUENCY) {
        DEBUG(apu, "%s: disabling from sweep overflow\n", __func__);
        channel->status = FALSE;
      } else {
        if (sweep->shift) {
          DEBUG(apu, "%s: updated frequency=%u\n", __func__, new_frequency);
          sweep->frequency = channel->frequency = new_frequency;
          write_square_wave_period(channel, &channel->square_wave);
        }

        /* Perform another overflow check. */
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

static u8 update_square_wave(SquareWave* wave) {
  static u8 duty[WAVE_DUTY_COUNT][DUTY_CYCLE_COUNT] =
      {[WAVE_DUTY_12_5] = {0, 0, 0, 0, 0, 0, 0, 1},
       [WAVE_DUTY_25] = {1, 0, 0, 0, 0, 0, 0, 1},
       [WAVE_DUTY_50] = {1, 0, 0, 0, 0, 1, 1, 1},
       [WAVE_DUTY_75] = {0, 1, 1, 1, 1, 1, 1, 0}};

  if (wave->cycles <= APU_CYCLES) {
    wave->cycles += wave->period;
    wave->position = (wave->position + 1) % DUTY_CYCLE_COUNT;
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

static void update_envelope(Envelope* envelope) {
  if (envelope->period) {
    if (envelope->automatic && --envelope->timer == 0) {
      envelope->timer = envelope->period;
      u8 delta = envelope->direction == ENVELOPE_ATTENUATE ? -1 : 1;
      u8 volume = envelope->volume + delta;
      if (volume < ENVELOPE_MAX_VOLUME) {
        envelope->volume = volume;
      } else {
        envelope->automatic = FALSE;
      }
    }
  } else {
    envelope->timer = ENVELOPE_MAX_PERIOD;
  }
}

static u8 update_wave(APU* apu, Wave* wave) {
  if (wave->cycles <= APU_CYCLES) {
    wave->position = (wave->position + 1) % WAVE_SAMPLE_COUNT;
    wave->sample_time = apu->cycles - APU_CYCLES + wave->cycles;
    u8 byte = wave->ram[wave->position >> 1];
    if ((wave->position & 1) == 0) {
      wave->sample_data = byte >> 4; /* High nybble. */
    } else {
      wave->sample_data = byte & 0x0f; /* Low nybble. */
    }
    VERBOSE(apu, "update_wave: position: %u => %u (cy: %u)\n", wave->position,
            wave->sample_data, wave->sample_time);
    wave->cycles += wave->period;
  }
  wave->cycles -= APU_CYCLES;
  return wave->sample_data;
}

static u8 update_noise(Noise* noise) {
  if (noise->clock_shift <= NOISE_MAX_CLOCK_SHIFT) {
    if (noise->cycles <= APU_CYCLES) {
      noise->cycles += noise->period;
      u16 bit = (noise->lfsr ^ (noise->lfsr >> 1)) & 1;
      if (noise->lfsr_width == LFSR_WIDTH_7) {
        noise->lfsr = ((noise->lfsr >> 1) & ~0x40) | (bit << 6);
      } else {
        noise->lfsr = ((noise->lfsr >> 1) & ~0x4000) | (bit << 14);
      }
      noise->sample = ~noise->lfsr & 1;
    }
    noise->cycles -= APU_CYCLES;
  }
  return noise->sample;
}

/* Convert from 1-bit sample to 8-bit sample. */
#define CHANNELX_SAMPLE(channel, sample) \
  ((-(sample) & (channel)->envelope.volume) << 4)
/* Convert from 4-bit sample to 8-bit sample. */
#define CHANNEL3_SAMPLE(wave, sample) (((sample) >> (wave)->volume_shift) << 4)

static void write_audio_frame(AudioBuffer* buffer, u8 so1, u8 so2) {
  assert(buffer->position + 2 <= buffer->end);
  buffer->accumulator[0] += so1;
  buffer->accumulator[1] += so2;
  buffer->divisor++;
  buffer->freq_counter += buffer->frequency;
  if (VALUE_WRAPPED(buffer->freq_counter, APU_CYCLES_PER_SECOND)) {
    *buffer->position++ = buffer->accumulator[0] / buffer->divisor;
    *buffer->position++ = buffer->accumulator[1] / buffer->divisor;
    buffer->accumulator[0] = 0;
    buffer->accumulator[1] = 0;
    buffer->divisor = 0;
  }
}

static void apu_mix_sample(Emulator* e, int channel, u8 sample,
                           u16* out_frame) {
  if (!e->config.disable_sound[channel]) {
    out_frame[0] += sample * e->state.apu.so_output[0][channel];
    out_frame[1] += sample * e->state.apu.so_output[1][channel];
  }
}

static void apu_update_channel_1(Emulator* e, Bool length, Bool envelope,
                                 Bool sweep, u16* out_frame) {
  Channel* channel1 = &e->state.apu.channel[CHANNEL1];
  u8 sample = 0;
  if (channel1->status) {
    if (sweep) update_channel_sweep(channel1, &e->state.apu.sweep);
    sample = update_square_wave(&channel1->square_wave);
  }
  if (length) update_channel_length(channel1);
  if (channel1->status) {
    if (envelope) update_envelope(&channel1->envelope);
    apu_mix_sample(e, CHANNEL1, CHANNELX_SAMPLE(channel1, sample), out_frame);
  }
}

static void apu_update_channel_2(Emulator* e, Bool length, Bool envelope,
                                 u16* out_frame) {
  Channel* channel2 = &e->state.apu.channel[CHANNEL2];
  u8 sample = 0;
  if (channel2->status) {
    sample = update_square_wave(&channel2->square_wave);
  }
  if (length) update_channel_length(channel2);
  if (channel2->status) {
    if (envelope) update_envelope(&channel2->envelope);
    apu_mix_sample(e, CHANNEL2, CHANNELX_SAMPLE(channel2, sample), out_frame);
  }
}

static void apu_update_channel_3(Emulator* e, Bool length, u16* out_frame) {
  Channel* channel3 = &e->state.apu.channel[CHANNEL3];
  u8 sample = 0;
  if (channel3->status) {
    sample = update_wave(&e->state.apu, &e->state.apu.wave);
  }
  if (length) update_channel_length(channel3);
  if (channel3->status) {
    apu_mix_sample(e, CHANNEL3, CHANNEL3_SAMPLE(&e->state.apu.wave, sample),
                   out_frame);
  }
}

static void apu_update_channel_4(Emulator* e, Bool length, Bool envelope,
                                 u16* out_frame) {
  Channel* channel4 = &e->state.apu.channel[CHANNEL4];
  u8 sample = 0;
  if (channel4->status) {
    sample = update_noise(&e->state.apu.noise);
  }
  if (length) update_channel_length(channel4);
  if (channel4->status) {
    if (envelope) update_envelope(&channel4->envelope);
    apu_mix_sample(e, CHANNEL4, CHANNELX_SAMPLE(channel4, sample), out_frame);
  }
}

static void apu_update_channels(Emulator* e, Bool length, Bool envelope,
                                Bool sweep) {
  u16 frame[2] = {0, 0};
  apu_update_channel_1(e, length, envelope, sweep, frame);
  apu_update_channel_2(e, length, envelope, frame);
  apu_update_channel_3(e, length, frame);
  apu_update_channel_4(e, length, envelope, frame);
  frame[0] *= (e->state.apu.so_volume[0] + 1);
  frame[0] /= ((SOUND_OUTPUT_MAX_VOLUME + 1) * CHANNEL_COUNT);
  frame[1] *= (e->state.apu.so_volume[1] + 1);
  frame[1] /= ((SOUND_OUTPUT_MAX_VOLUME + 1) * CHANNEL_COUNT);
  write_audio_frame(&e->audio_buffer, frame[0], frame[1]);
}

static void apu_update(Emulator* e) {
  e->state.apu.cycles += APU_CYCLES;
  e->state.apu.frame_cycles += APU_CYCLES;
  if (VALUE_WRAPPED(e->state.apu.frame_cycles, FRAME_SEQUENCER_CYCLES)) {
    e->state.apu.frame = (e->state.apu.frame + 1) % FRAME_SEQUENCER_COUNT;
    switch (e->state.apu.frame) {
      case 0: case 4: apu_update_channels(e, TRUE, FALSE, FALSE); return;
      case 2: case 6: apu_update_channels(e, TRUE, FALSE, TRUE); return;
      case 7: apu_update_channels(e, FALSE, TRUE, FALSE); return;
    }
  }
  apu_update_channels(e, FALSE, FALSE, FALSE);
}

static void apu_mcycle(Emulator* e) {
  if (e->state.apu.enabled) {
    /* Synchronize with CPU cycle counter. */
    e->state.apu.cycles = e->state.cycles;
    apu_update(e);
    apu_update(e);
  } else {
    write_audio_frame(&e->audio_buffer, 0, 0);
    write_audio_frame(&e->audio_buffer, 0, 0);
  }
}

static void serial_mcycle(Emulator* e) {
  if (!e->state.serial.transferring) {
    return;
  }
  if (e->state.serial.clock == SERIAL_CLOCK_INTERNAL) {
    e->state.serial.cycles += CPU_MCYCLE;
    if (VALUE_WRAPPED(e->state.serial.cycles, SERIAL_CYCLES)) {
      /* Since we're never connected to another device, always shift in 0xff. */
      e->state.serial.SB = (e->state.serial.SB << 1) | 1;
      e->state.serial.transferred_bits++;
      if (VALUE_WRAPPED(e->state.serial.transferred_bits, 8)) {
        e->state.serial.transferring = 0;
        e->state.interrupt.new_IF |= IF_SERIAL;
      }
    }
  }
}

static void mcycle(Emulator* e) {
  e->state.interrupt.IF = e->state.interrupt.new_IF;
  dma_mcycle(e);
  ppu_mcycle(e);
  timer_mcycle(e);
  apu_mcycle(e);
  serial_mcycle(e);
  e->state.cycles += CPU_MCYCLE;
}

static u8 read_u8_cy(Emulator* e, Address addr) {
  mcycle(e);
  return read_u8(e, addr);
}

static u16 read_u16_cy(Emulator* e, Address addr) {
  u8 lo = read_u8_cy(e, addr);
  u8 hi = read_u8_cy(e, addr + 1);
  return (hi << 8) | lo;
}

static void write_u8_cy(Emulator* e, Address addr, u8 value) {
  mcycle(e);
  write_u8(e, addr, value);
}

static void write_u16_cy(Emulator* e, Address addr, u16 value) {
  write_u8_cy(e, addr + 1, value >> 8);
  write_u8_cy(e, addr, value);
}

static u8 s_opcode_bytes[] = {
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

static void sprint_hex(char* buffer, u8 val) {
  const char hex_digits[] = "0123456789abcdef";
  buffer[0] = hex_digits[(val >> 4) & 0xf];
  buffer[1] = hex_digits[val & 0xf];
}

static void print_instruction(Emulator* e, Address addr) {
  char buffer[64];
  char bytes[][3] = {"  ", "  "};
  const char* mnemonic = "*INVALID*";

  u8 opcode = read_u8(e, addr);
  u8 num_bytes = s_opcode_bytes[opcode];
  switch (num_bytes) {
    case 0: break;
    case 1: mnemonic = s_opcode_mnemonic[opcode]; break;
    case 2: {
      u8 byte = read_u8(e, addr + 1);
      sprint_hex(bytes[0], byte);
      if (opcode == 0xcb) {
        mnemonic = s_cb_opcode_mnemonic[byte];
      } else {
        snprintf(buffer, sizeof(buffer), s_opcode_mnemonic[opcode], byte);
        mnemonic = buffer;
      }
      break;
    }
    case 3: {
      u8 byte1 = read_u8(e, addr + 1);
      u8 byte2 = read_u8(e, addr + 2);
      sprint_hex(bytes[0], byte1);
      sprint_hex(bytes[1], byte2);
      snprintf(buffer, sizeof(buffer), s_opcode_mnemonic[opcode],
               (byte2 << 8) | byte1);
      mnemonic = buffer;
      break;
    }
    default: assert(!"invalid opcode byte length.\n"); break;
  }

  char bank[3] = "??";
  MemoryTypeAddressPair pair = map_address(addr);
  if (pair.type == MEMORY_MAP_ROM1) {
    sprint_hex(bank, e->state.memory_map_state.rom1_base >> ROM_BANK_SHIFT);
  }

  (void)mnemonic;
  LOG("[%s]%#06x: %02x %s %s  %-15s", bank, addr, opcode, bytes[0], bytes[1],
      mnemonic);
}

static void print_emulator_info(Emulator* e) {
  if (!s_never_trace && s_trace && !e->state.interrupt.halt) {
    LOG("A:%02X F:%c%c%c%c BC:%04X DE:%04x HL:%04x SP:%04x PC:%04x",
        e->state.reg.A, e->state.reg.F.Z ? 'Z' : '-',
        e->state.reg.F.N ? 'N' : '-', e->state.reg.F.H ? 'H' : '-',
        e->state.reg.F.C ? 'C' : '-', e->state.reg.BC, e->state.reg.DE,
        e->state.reg.HL, e->state.reg.SP, e->state.reg.PC);
    LOG(" (cy: %u)", e->state.cycles);
    if (s_log_level_ppu >= 1) {
      LOG(" ppu:%c%u", e->state.ppu.LCDC.display ? '+' : '-',
             e->state.ppu.STAT.mode);
    }
    if (s_log_level_ppu >= 2) {
      LOG(" LY:%u", e->state.ppu.LY);
    }
    LOG(" |");
    print_instruction(e, e->state.reg.PC);
    LOG("\n");
    if (s_trace_counter > 0) {
      if (--s_trace_counter == 0) {
        s_trace = FALSE;
      }
    }
  }
}

#define INVALID UNREACHABLE("invalid opcode 0x%02x!\n", opcode);

#define REG(R) e->state.reg.R
#define INTR(m) e->state.interrupt.m
#define CY mcycle(e)
#define RA REG(A)
#define RSP REG(SP)
#define FZ e->state.reg.F.Z
#define FC e->state.reg.F.C
#define FH e->state.reg.F.H
#define FN e->state.reg.F.N
#define FZ_EQ0(X) FZ = (u8)(X) == 0
#define MASK8(X) ((X) & 0xf)
#define MASK16(X) ((X) & 0xfff)
#define READ8(X) read_u8_cy(e, X)
#define READ16(X) read_u16_cy(e, X)
#define WRITE8(X, V) write_u8_cy(e, X, V)
#define WRITE16(X, V) write_u16_cy(e, X, V)
#define READ_N READ8(REG(PC) + 1)
#define READ_NN READ16(REG(PC) + 1)
#define READMR(MR) READ8(REG(MR))
#define WRITEMR(MR, V) WRITE8(REG(MR), V)
#define BASIC_OP_R(R, OP) u = REG(R); OP; REG(R) = u
#define BASIC_OP_MR(MR, OP) u = READMR(MR); OP; WRITEMR(MR, u)
#define FC_ADD(X, Y) FC = ((X) + (Y) > 0xff)
#define FH_ADD(X, Y) FH = (MASK8(X) + MASK8(Y) > 0xf)
#define FCH_ADD(X, Y) FC_ADD(X, Y); FH_ADD(X, Y)
#define FC_ADD16(X, Y) FC = ((X) + (Y) > 0xffff)
#define FH_ADD16(X, Y) FH = (MASK16(X) + MASK16(Y) > 0xfff)
#define FCH_ADD16(X, Y) FC_ADD16(X, Y); FH_ADD16(X, Y)
#define ADD_FLAGS(X, Y) FZ_EQ0((X) + (Y)); FN = 0; FCH_ADD(X, Y)
#define ADD_FLAGS16(X, Y) FN = 0; FCH_ADD16(X, Y)
#define ADD_SP_FLAGS(Y) FZ = FN = 0; FCH_ADD((u8)RSP, (u8)(Y))
#define ADD_R(R) ADD_FLAGS(RA, REG(R)); RA += REG(R)
#define ADD_MR(MR) u = READMR(MR); ADD_FLAGS(RA, u); RA += u
#define ADD_N u = READ_N; ADD_FLAGS(RA, u); RA += u
#define ADD_HL_RR(RR) CY; ADD_FLAGS16(REG(HL), REG(RR)); REG(HL) += REG(RR)
#define ADD_SP_N s = (s8)READ_N; ADD_SP_FLAGS(s); RSP += s; CY; CY
#define FC_ADC(X, Y, C) FC = ((X) + (Y) + (C) > 0xff)
#define FH_ADC(X, Y, C) FH = (MASK8(X) + MASK8(Y) + C > 0xf)
#define FCH_ADC(X, Y, C) FC_ADC(X, Y, C); FH_ADC(X, Y, C)
#define ADC_FLAGS(X, Y, C) FZ_EQ0((X) + (Y) + (C)); FN = 0; FCH_ADC(X, Y, C)
#define ADC_R(R) u = REG(R); c = FC; ADC_FLAGS(RA, u, c); RA += u + c
#define ADC_MR(MR) u = READMR(MR); c = FC; ADC_FLAGS(RA, u, c); RA += u + c
#define ADC_N u = READ_N; c = FC; ADC_FLAGS(RA, u, c); RA += u + c
#define AND_FLAGS FZ_EQ0(RA); FH = 1; FN = FC = 0
#define AND_R(R) RA &= REG(R); AND_FLAGS
#define AND_MR(MR) RA &= READMR(MR); AND_FLAGS
#define AND_N RA &= READ_N; AND_FLAGS
#define BIT_FLAGS(BIT, X) FZ_EQ0((X) & (1 << (BIT))); FN = 0; FH = 1
#define BIT_R(BIT, R) u = REG(R); BIT_FLAGS(BIT, u)
#define BIT_MR(BIT, MR) u = READMR(MR); BIT_FLAGS(BIT, u)
#define CALL(X) CY; RSP -= 2; WRITE16(RSP, new_pc); new_pc = X
#define CALL_NN u16 = READ_NN; CALL(u16)
#define CALL_F_NN(COND) u16 = READ_NN; if (COND) { CALL(u16); }
#define CCF FC ^= 1; FN = FH = 0
#define CP_FLAGS(X, Y) FZ_EQ0((X) - (Y)); FN = 1; FCH_SUB(X, Y)
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
#define HALT                                     \
  if (INTR(IME)) {                               \
    INTR(halt) = TRUE;                           \
  } else if (INTR(IE) & INTR(new_IF) & IF_ALL) { \
    INTR(halt_bug) = TRUE;                       \
  } else {                                       \
    INTR(halt) = TRUE;                           \
    INTR(halt_DI) = TRUE;                        \
  }
#define INC u++
#define INC_FLAGS FZ_EQ0(u); FN = 0; FH = MASK8(u) == 0
#define INC_R(R) BASIC_OP_R(R, INC); INC_FLAGS
#define INC_RR(RR) REG(RR)++; CY
#define INC_MR(MR) BASIC_OP_MR(MR, INC); INC_FLAGS
#define JP_F_NN(COND) u16 = READ_NN; if (COND) { new_pc = u16; CY; }
#define JP_RR(RR) new_pc = REG(RR)
#define JP_NN new_pc = READ_NN; CY
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
#define LD_HL_SP_N s = (s8)READ_N; ADD_SP_FLAGS(s); REG(HL) = RSP + s; CY
#define OR_FLAGS FZ_EQ0(RA); FN = FH = FC = 0
#define OR_R(R) RA |= REG(R); OR_FLAGS
#define OR_MR(MR) RA |= READMR(MR); OR_FLAGS
#define OR_N RA |= READ_N; OR_FLAGS
#define POP_RR(RR) REG(RR) = READ16(RSP); RSP += 2
#define POP_AF set_af_reg(&e->state.reg, READ16(RSP)); RSP += 2
#define PUSH_RR(RR) CY; RSP -= 2; WRITE16(RSP, REG(RR))
#define PUSH_AF CY; RSP -= 2; WRITE16(RSP, get_af_reg(&e->state.reg))
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
#define SRA FC = u & 1; u = (s8)u >> 1
#define SRA_FLAGS FZ_EQ0(u); FN = FH = 0
#define SRA_R(R) BASIC_OP_R(R, SRA); SRA_FLAGS
#define SRA_MR(MR) BASIC_OP_MR(MR, SRA); SRA_FLAGS
#define SRL FC = u & 1; u >>= 1
#define SRL_FLAGS FZ_EQ0(u); FN = FH = 0
#define SRL_R(R) BASIC_OP_R(R, SRL); SRL_FLAGS
#define SRL_MR(MR) BASIC_OP_MR(MR, SRL); SRL_FLAGS
#define STOP INTR(stop) = TRUE;
#define FC_SUB(X, Y) FC = ((int)(X) - (int)(Y) < 0)
#define FH_SUB(X, Y) FH = ((int)MASK8(X) - (int)MASK8(Y) < 0)
#define FCH_SUB(X, Y) FC_SUB(X, Y); FH_SUB(X, Y)
#define SUB_FLAGS(X, Y) FZ_EQ0((X) - (Y)); FN = 1; FCH_SUB(X, Y)
#define SUB_R(R) SUB_FLAGS(RA, REG(R)); RA -= REG(R)
#define SUB_MR(MR) u = READMR(MR); SUB_FLAGS(RA, u); RA -= u
#define SUB_N u = READ_N; SUB_FLAGS(RA, u); RA -= u
#define FC_SBC(X, Y, C) FC = ((int)(X) - (int)(Y) - (int)(C) < 0)
#define FH_SBC(X, Y, C) FH = ((int)MASK8(X) - (int)MASK8(Y) - (int)C < 0)
#define FCH_SBC(X, Y, C) FC_SBC(X, Y, C); FH_SBC(X, Y, C)
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
  s8 s;
  u8 u, c, opcode;
  u16 u16;
  Address new_pc;

  if (INTR(stop)) {
    return;
  }

  if (INTR(enable)) {
    INTR(enable) = FALSE;
    INTR(IME) = TRUE;
  }

  if (INTR(halt)) {
    mcycle(e);
    return;
  }

  if (INTR(halt_bug)) {
    /* When interrupts are disabled during a HALT, the following byte will be
     * duplicated when decoding. */
    opcode = read_u8(e, e->state.reg.PC);
    e->state.reg.PC--;
    INTR(halt_bug) = FALSE;
  } else {
    opcode = read_u8_cy(e, e->state.reg.PC);
  }
  new_pc = e->state.reg.PC + s_opcode_bytes[opcode];

#define REG_OPS(code, name)            \
  case code + 0: name##_R(B); break;   \
  case code + 1: name##_R(C); break;   \
  case code + 2: name##_R(D); break;   \
  case code + 3: name##_R(E); break;   \
  case code + 4: name##_R(H); break;   \
  case code + 5: name##_R(L); break;   \
  case code + 6: name##_MR(HL); break; \
  case code + 7: name##_R(A); break;
#define REG_OPS_N(code, name, N)          \
  case code + 0: name##_R(N, B); break;   \
  case code + 1: name##_R(N, C); break;   \
  case code + 2: name##_R(N, D); break;   \
  case code + 3: name##_R(N, E); break;   \
  case code + 4: name##_R(N, H); break;   \
  case code + 5: name##_R(N, L); break;   \
  case code + 6: name##_MR(N, HL); break; \
  case code + 7: name##_R(N, A); break;
#define LD_R_OPS(code, R) REG_OPS_N(code, LD_R, R)

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
    case 0x10: STOP; break;
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
    case 0x20: JR_F_N(!FZ); break;
    case 0x21: LD_RR_NN(HL); break;
    case 0x22: LD_MR_R(HL, A); REG(HL)++; break;
    case 0x23: INC_RR(HL); break;
    case 0x24: INC_R(H); break;
    case 0x25: DEC_R(H); break;
    case 0x26: LD_R_N(H); break;
    case 0x27: DAA; break;
    case 0x28: JR_F_N(FZ); break;
    case 0x29: ADD_HL_RR(HL); break;
    case 0x2a: LD_R_MR(A, HL); REG(HL)++; break;
    case 0x2b: DEC_RR(HL); break;
    case 0x2c: INC_R(L); break;
    case 0x2d: DEC_R(L); break;
    case 0x2e: LD_R_N(L); break;
    case 0x2f: CPL; break;
    case 0x30: JR_F_N(!FC); break;
    case 0x31: LD_RR_NN(SP); break;
    case 0x32: LD_MR_R(HL, A); REG(HL)--; break;
    case 0x33: INC_RR(SP); break;
    case 0x34: INC_MR(HL); break;
    case 0x35: DEC_MR(HL); break;
    case 0x36: LD_MR_N(HL); break;
    case 0x37: SCF; break;
    case 0x38: JR_F_N(FC); break;
    case 0x39: ADD_HL_RR(SP); break;
    case 0x3a: LD_R_MR(A, HL); REG(HL)--; break;
    case 0x3b: DEC_RR(SP); break;
    case 0x3c: INC_R(A); break;
    case 0x3d: DEC_R(A); break;
    case 0x3e: LD_R_N(A); break;
    case 0x3f: CCF; break;
    LD_R_OPS(0x40, B)
    LD_R_OPS(0x48, C)
    LD_R_OPS(0x50, D)
    LD_R_OPS(0x58, E)
    LD_R_OPS(0x60, H)
    LD_R_OPS(0x68, L)
    case 0x70: LD_MR_R(HL, B); break;
    case 0x71: LD_MR_R(HL, C); break;
    case 0x72: LD_MR_R(HL, D); break;
    case 0x73: LD_MR_R(HL, E); break;
    case 0x74: LD_MR_R(HL, H); break;
    case 0x75: LD_MR_R(HL, L); break;
    case 0x76: HALT; break;
    case 0x77: LD_MR_R(HL, A); break;
    LD_R_OPS(0x78, A)
    REG_OPS(0x80, ADD)
    REG_OPS(0x88, ADC)
    REG_OPS(0x90, SUB)
    REG_OPS(0x98, SBC)
    REG_OPS(0xa0, AND)
    REG_OPS(0xa8, XOR)
    REG_OPS(0xb0, OR)
    REG_OPS(0xb8, CP)
    case 0xc0: RET_F(!FZ); break;
    case 0xc1: POP_RR(BC); break;
    case 0xc2: JP_F_NN(!FZ); break;
    case 0xc3: JP_NN; break;
    case 0xc4: CALL_F_NN(!FZ); break;
    case 0xc5: PUSH_RR(BC); break;
    case 0xc6: ADD_N; break;
    case 0xc7: CALL(0x00); break;
    case 0xc8: RET_F(FZ); break;
    case 0xc9: RET; break;
    case 0xca: JP_F_NN(FZ); break;
    case 0xcb:
      switch (read_u8_cy(e, REG(PC) + 1)) {
        REG_OPS(0x00, RLC)
        REG_OPS(0x08, RRC)
        REG_OPS(0x10, RL)
        REG_OPS(0x18, RR)
        REG_OPS(0x20, SLA)
        REG_OPS(0x28, SRA)
        REG_OPS(0x30, SWAP)
        REG_OPS(0x38, SRL)
        REG_OPS_N(0x40, BIT, 0)
        REG_OPS_N(0x48, BIT, 1)
        REG_OPS_N(0x50, BIT, 2)
        REG_OPS_N(0x58, BIT, 3)
        REG_OPS_N(0x60, BIT, 4)
        REG_OPS_N(0x68, BIT, 5)
        REG_OPS_N(0x70, BIT, 6)
        REG_OPS_N(0x78, BIT, 7)
        REG_OPS_N(0x80, RES, 0)
        REG_OPS_N(0x88, RES, 1)
        REG_OPS_N(0x90, RES, 2)
        REG_OPS_N(0x98, RES, 3)
        REG_OPS_N(0xa0, RES, 4)
        REG_OPS_N(0xa8, RES, 5)
        REG_OPS_N(0xb0, RES, 6)
        REG_OPS_N(0xb8, RES, 7)
        REG_OPS_N(0xc0, SET, 0)
        REG_OPS_N(0xc8, SET, 1)
        REG_OPS_N(0xd0, SET, 2)
        REG_OPS_N(0xd8, SET, 3)
        REG_OPS_N(0xe0, SET, 4)
        REG_OPS_N(0xe8, SET, 5)
        REG_OPS_N(0xf0, SET, 6)
        REG_OPS_N(0xf8, SET, 7)
      }
      break;
    case 0xcc: CALL_F_NN(FZ); break;
    case 0xcd: CALL_NN; break;
    case 0xce: ADC_N; break;
    case 0xcf: CALL(0x08); break;
    case 0xd0: RET_F(!FC); break;
    case 0xd1: POP_RR(DE); break;
    case 0xd2: JP_F_NN(!FC); break;
    case 0xd4: CALL_F_NN(!FC); break;
    case 0xd5: PUSH_RR(DE); break;
    case 0xd6: SUB_N; break;
    case 0xd7: CALL(0x10); break;
    case 0xd8: RET_F(FC); break;
    case 0xd9: RETI; break;
    case 0xda: JP_F_NN(FC); break;
    case 0xdc: CALL_F_NN(FC); break;
    case 0xde: SBC_N; break;
    case 0xdf: CALL(0x18); break;
    case 0xe0: LD_MFF00_N_R(A); break;
    case 0xe1: POP_RR(HL); break;
    case 0xe2: LD_MFF00_R_R(C, A); break;
    case 0xe5: PUSH_RR(HL); break;
    case 0xe6: AND_N; break;
    case 0xe7: CALL(0x20); break;
    case 0xe8: ADD_SP_N; break;
    case 0xe9: JP_RR(HL); break;
    case 0xea: LD_MN_R(A); break;
    case 0xee: XOR_N; break;
    case 0xef: CALL(0x28); break;
    case 0xf0: LD_R_MFF00_N(A); break;
    case 0xf1: POP_AF; break;
    case 0xf2: LD_R_MFF00_R(A, C); break;
    case 0xf3: DI; break;
    case 0xf5: PUSH_AF; break;
    case 0xf6: OR_N; break;
    case 0xf7: CALL(0x30); break;
    case 0xf8: LD_HL_SP_N; break;
    case 0xf9: LD_RR_RR(SP, HL); break;
    case 0xfa: LD_R_MN(A); break;
    case 0xfb: EI; break;
    case 0xfe: CP_N; break;
    case 0xff: CALL(0x38); break;
    default: INVALID; break;
  }
  e->state.reg.PC = new_pc;
}

static void handle_interrupts(Emulator* e) {
  if (!(e->state.interrupt.IME || e->state.interrupt.halt)) {
    return;
  }

  u8 interrupt =
      e->state.interrupt.new_IF & e->state.interrupt.IE & IF_ALL;
  if (interrupt == 0) {
    return;
  }

  Bool delay = FALSE;
  u8 mask = 0;
  Address vector = 0;
  if (interrupt & IF_VBLANK) {
    DEBUG(interrupt, ">> VBLANK interrupt [frame = %u] [cy: %u]\n",
          e->state.ppu.frame, e->state.cycles);
    vector = 0x40;
    mask = IF_VBLANK;
  } else if (interrupt & IF_STAT) {
    DEBUG(interrupt, ">> LCD_STAT interrupt [%c%c%c%c] [cy: %u]\n",
          e->state.ppu.STAT.y_compare.irq ? 'Y' : '.',
          e->state.ppu.STAT.mode2.irq ? 'O' : '.',
          e->state.ppu.STAT.vblank.irq ? 'V' : '.',
          e->state.ppu.STAT.hblank.irq ? 'H' : '.', e->state.cycles);
    vector = 0x48;
    mask = IF_STAT;
#if 0
    /* I'm pretty sure this is right, but it currently breaks a lot of tests;
     * need to figure out how to fix the rest of the tests to handle this extra
     * delay. */
    delay = e->state.interrupt.halt && e->state.ppu.STAT.mode2.irq;
#endif
  } else if (interrupt & IF_TIMER) {
    DEBUG(interrupt, ">> TIMER interrupt\n");
    vector = 0x50;
    mask = IF_TIMER;
    delay = e->state.interrupt.halt;
  } else if (interrupt & IF_SERIAL) {
    DEBUG(interrupt, ">> SERIAL interrupt\n");
    vector = 0x58;
    mask = IF_SERIAL;
  } else if (interrupt & IF_JOYPAD) {
    DEBUG(interrupt, ">> JOYPAD interrupt\n");
    vector = 0x60;
    mask = IF_JOYPAD;
  }

  if (delay) {
    mcycle(e);
  }

  if (e->state.interrupt.halt_DI) {
    DEBUG(interrupt, "Interrupt fired during HALT w/ disabled interrupt.\n");
    INTR(halt_DI) = FALSE;
  } else {
    e->state.interrupt.new_IF &= ~mask;
    Address new_pc = REG(PC);
    CALL(vector);
    REG(PC) = new_pc;
    e->state.interrupt.IME = FALSE;
    mcycle(e);
    mcycle(e);
  }
  e->state.interrupt.halt = e->state.interrupt.stop = FALSE;
}

static void step_emulator(Emulator* e) {
  print_emulator_info(e);
  execute_instruction(e);
  handle_interrupts(e);
}

/* TODO: remove this global */
static struct timeval s_start_time;
static void init_time(void) {
  int result = gettimeofday(&s_start_time, NULL);
  assert(result == 0);
}

static f64 get_time_ms(void) {
  struct timeval from = s_start_time;
  struct timeval to;
  int result = gettimeofday(&to, NULL);
  assert(result == 0);
  f64 ms = (f64)(to.tv_sec - from.tv_sec) * MILLISECONDS_PER_SECOND;
  if (to.tv_usec < from.tv_usec) {
    ms -= MILLISECONDS_PER_SECOND;
    to.tv_usec += MICROSECONDS_PER_SECOND;
  }
  return ms + (f64)(to.tv_usec - from.tv_usec) / MICROSECONDS_PER_MILLISECOND;
}

static EmulatorEvent run_emulator(Emulator* e, u32 max_audio_frames) {
  if (e->last_event & EMULATOR_EVENT_NEW_FRAME) {
    e->state.ppu.new_frame_edge = FALSE;
  }
  if (e->last_event & EMULATOR_EVENT_AUDIO_BUFFER_FULL) {
    e->audio_buffer.position = e->audio_buffer.data;
  }
  check_joyp_intr(e);

  u8* max_audio_position =
      e->audio_buffer.data + max_audio_frames * SOUND_OUTPUT_COUNT;
  assert(max_audio_position <= e->audio_buffer.end);
  EmulatorEvent event = 0;
  while (event == 0) {
    step_emulator(e);
    if (e->state.ppu.new_frame_edge) {
      event |= EMULATOR_EVENT_NEW_FRAME;
    }
    if (e->audio_buffer.position >= max_audio_position) {
      event |= EMULATOR_EVENT_AUDIO_BUFFER_FULL;
    }
  }
  return e->last_event = event;
}

static Result init_audio_buffer(Emulator* e, u32 frequency, u32 frames) {
  AudioBuffer* audio_buffer = &e->audio_buffer;
  size_t buffer_size =
      (frames + AUDIO_BUFFER_EXTRA_FRAMES) * SOUND_OUTPUT_COUNT;
  audio_buffer->data = malloc(buffer_size); /* Leaks. */
  CHECK_MSG(audio_buffer->data != NULL, "Audio buffer allocation failed.\n");
  audio_buffer->end = audio_buffer->data + buffer_size;
  audio_buffer->position = audio_buffer->data;
  audio_buffer->frequency = frequency;
  return OK;
  ON_ERROR_RETURN;
}


/* SDL stuff */

#ifndef NO_SDL

#include <SDL.h>

#define DESTROY_IF(ptr, destroy) \
  if (ptr) {                     \
    destroy(ptr);                \
    ptr = NULL;                  \
  }

#define RENDER_SCALE 4
#define RENDER_WIDTH (SCREEN_WIDTH * RENDER_SCALE)
#define RENDER_HEIGHT (SCREEN_HEIGHT * RENDER_SCALE)
#define AUDIO_SPEC_FREQUENCY 44100
#define AUDIO_SPEC_FORMAT AUDIO_U16
#define AUDIO_SPEC_CHANNELS 2
#define AUDIO_SPEC_SAMPLES 2048
#define AUDIO_SPEC_SAMPLE_SIZE sizeof(HostAudioSample)
#define AUDIO_FRAME_SIZE (AUDIO_SPEC_SAMPLE_SIZE * AUDIO_SPEC_CHANNELS)
#define AUDIO_CONVERT_SAMPLE_FROM_U8(X) ((X) << 8)
typedef u16 HostAudioSample;
/* Try to keep the audio buffer filled to |number of frames| *
 * AUDIO_TARGET_BUFFER_SIZE_MULTIPLIER frames. */
#define AUDIO_TARGET_BUFFER_SIZE_MULTIPLIER 1.5
#define AUDIO_MAX_BUFFER_SIZE_MULTIPLIER 4
/* One buffer will be requested every AUDIO_BUFFER_REFILL_MS milliseconds. */
#define AUDIO_BUFFER_REFILL_MS                                            \
  ((AUDIO_SPEC_SAMPLES / AUDIO_SPEC_CHANNELS) * MILLISECONDS_PER_SECOND / \
   AUDIO_SPEC_FREQUENCY)
/* If the emulator is running behind by AUDIO_MAX_SLOW_DESYNC_MS milliseconds
 * (or ahead by AUDIO_MAX_FAST_DESYNC_MS), it won't try to catch up, and
 * instead just forcibly resync. */
#define AUDIO_MAX_SLOW_DESYNC_MS (0.5 * AUDIO_BUFFER_REFILL_MS)
#define AUDIO_MAX_FAST_DESYNC_MS (2 * AUDIO_BUFFER_REFILL_MS)
#define VIDEO_FRAME_MS \
  ((f64)MILLISECONDS_PER_SECOND * PPU_FRAME_CYCLES / CPU_CYCLES_PER_SECOND)
#define SAVE_EXTENSION ".sav"
#define SAVE_STATE_EXTENSION ".state"
#define SAVE_STATE_VERSION (2)
#define SAVE_STATE_HEADER (u32)(0x6b57a7e0 + SAVE_STATE_VERSION)
#define SAVE_STATE_FILE_SIZE (sizeof(u32) + sizeof(EmulatorState))

static int s_log_level_host = 1;

typedef struct {
  SDL_AudioDeviceID dev;
  SDL_AudioSpec spec;
  u8* buffer;
  u8* buffer_end;
  u8* read_pos;
  u8* write_pos;
  size_t buffer_capacity;         /* Total capacity in bytes of the buffer. */
  size_t buffer_available;        /* Number of bytes available for reading. */
  size_t buffer_target_available; /* Try to keep the buffer this size. */
  Bool ready; /* Set to TRUE when audio is first rendered. */
} HostAudio;

typedef struct {
  const char* save_state_filename;
  SDL_Window* window;
  SDL_Renderer* renderer;
  SDL_Texture* texture;
  HostAudio audio;
  u32 last_sync_cycles;  /* GB CPU cycle count of last synchronization. */
  f64 last_sync_real_ms; /* Wall clock time of last synchronization. */
} Host;

static Result host_init_renderer(Host* host, Bool vsync) {
  DESTROY_IF(host->renderer, SDL_DestroyRenderer);
  host->renderer = SDL_CreateRenderer(host->window, -1,
                                      vsync ? SDL_RENDERER_PRESENTVSYNC : 0);
  CHECK_MSG(host->renderer != NULL, "SDL_CreateRenderer failed.\n");
  CHECK_MSG(SDL_RenderSetLogicalSize(host->renderer, SCREEN_WIDTH,
                                     SCREEN_HEIGHT) == 0,
            "SDL_SetRendererLogicalSize failed.\n");
  DESTROY_IF(host->texture, SDL_DestroyTexture);
  host->texture = SDL_CreateTexture(host->renderer, SDL_PIXELFORMAT_ARGB8888,
                                    SDL_TEXTUREACCESS_STREAMING, SCREEN_WIDTH,
                                    SCREEN_HEIGHT);
  CHECK_MSG(host->texture != NULL, "SDL_CreateTexture failed.\n");
  return OK;
error:
  DESTROY_IF(host->texture, SDL_DestroyTexture);
  DESTROY_IF(host->renderer, SDL_DestroyRenderer);
  return ERROR;
}

static Result host_init_video(Host* host) {
  CHECK_MSG(SDL_Init(SDL_INIT_EVERYTHING) == 0, "SDL_init failed.\n");
  host->window =
      SDL_CreateWindow("binjgb", SDL_WINDOWPOS_UNDEFINED,
                       SDL_WINDOWPOS_UNDEFINED, RENDER_WIDTH, RENDER_HEIGHT, 0);
  CHECK_MSG(host->window != NULL, "SDL_CreateWindow failed.\n");
  CHECK(SUCCESS(host_init_renderer(host, TRUE)));
  return OK;
error:
  SDL_Quit();
  return ERROR;
}

static void host_audio_callback(void* userdata, u8* dst, int len) {
  memset(dst, 0, len);
  Host* host = userdata;
  HostAudio* audio = &host->audio;
  if (len > (int)audio->buffer_available) {
    DEBUG(host, "!!! audio underflow. avail %zd < requested %u\n",
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

static Result host_init_audio(Host* host) {
  host->last_sync_cycles = 0;
  host->last_sync_real_ms = get_time_ms();

  SDL_AudioSpec want;
  want.freq = AUDIO_SPEC_FREQUENCY;
  want.format = AUDIO_SPEC_FORMAT;
  want.channels = AUDIO_SPEC_CHANNELS;
  want.samples = AUDIO_SPEC_SAMPLES;
  want.callback = host_audio_callback;
  want.userdata = host;
  host->audio.dev = SDL_OpenAudioDevice(NULL, 0, &want, &host->audio.spec, 0);
  CHECK_MSG(host->audio.dev != 0, "SDL_OpenAudioDevice failed.\n");

  host->audio.buffer_target_available =
      (size_t)(host->audio.spec.size * AUDIO_TARGET_BUFFER_SIZE_MULTIPLIER);

  size_t buffer_capacity =
      (size_t)(host->audio.spec.size * AUDIO_MAX_BUFFER_SIZE_MULTIPLIER);
  host->audio.buffer_capacity = buffer_capacity;

  host->audio.buffer = malloc(buffer_capacity); /* Leaks. */
  CHECK_MSG(host->audio.buffer != NULL, "Audio buffer allocation failed.\n");
  memset(host->audio.buffer, 0, buffer_capacity);

  host->audio.buffer_end = host->audio.buffer + buffer_capacity;
  host->audio.read_pos = host->audio.write_pos = host->audio.buffer;
  return OK;
  ON_ERROR_RETURN;
}

static Result read_state_from_file(Emulator* e, const char* filename) {
  FILE* f = fopen(filename, "rb");
  CHECK_MSG(f, "unable to open file \"%s\".\n", filename);
  long size;
  CHECK(SUCCESS(get_file_size(f, &size)));
  CHECK_MSG(size == SAVE_STATE_FILE_SIZE,
            "save state file is wrong size: %ld, expected %ld.\n", size,
            SAVE_STATE_FILE_SIZE);
  u32 header;
  CHECK_MSG(fread(&header, sizeof(header), 1, f) == 1, "fread failed.\n");
  CHECK_MSG(header == SAVE_STATE_HEADER, "header mismatch: %u, expected %u.\n",
            header, SAVE_STATE_HEADER);
  CHECK_MSG(fread(&e->state, sizeof(e->state), 1, f) == 1, "fread failed.\n");
  fclose(f);
  set_cart_info(e, e->state.cart_info_index);
  return OK;
  ON_ERROR_CLOSE_FILE_AND_RETURN;
}

static Result write_state_to_file(Emulator* e, const char* filename) {
  FILE* f = fopen(filename, "wb");
  CHECK_MSG(f, "unable to open file \"%s\".\n", filename);
  u32 header = SAVE_STATE_HEADER;
  CHECK_MSG(fwrite(&header, sizeof(header), 1, f) == 1, "fwrite failed.\n");
  CHECK_MSG(fwrite(&e->state, sizeof(e->state), 1, f) == 1, "fwrite failed.\n");
  fclose(f);
  return OK;
  ON_ERROR_CLOSE_FILE_AND_RETURN;
}

static Bool host_poll_events(Emulator* e, Host* host) {
  Bool running = TRUE;
  SDL_Event event;
  const char* ss_filename = host->save_state_filename;
  EmulatorConfig old_config = e->config;
  while (SDL_PollEvent(&event)) {
    switch (event.type) {
      case SDL_KEYDOWN:
        switch (event.key.keysym.scancode) {
          case SDL_SCANCODE_1: e->config.disable_sound[CHANNEL1] ^= 1; break;
          case SDL_SCANCODE_2: e->config.disable_sound[CHANNEL2] ^= 1; break;
          case SDL_SCANCODE_3: e->config.disable_sound[CHANNEL3] ^= 1; break;
          case SDL_SCANCODE_4: e->config.disable_sound[CHANNEL4] ^= 1; break;
          case SDL_SCANCODE_B: e->config.disable_bg ^= 1; break;
          case SDL_SCANCODE_W: e->config.disable_window ^= 1; break;
          case SDL_SCANCODE_O: e->config.disable_obj ^= 1; break;
          case SDL_SCANCODE_F6: write_state_to_file(e, ss_filename); break;
          case SDL_SCANCODE_F9: read_state_from_file(e, ss_filename); break;
          case SDL_SCANCODE_N: e->config.step = 1; e->config.paused = 0; break;
          case SDL_SCANCODE_SPACE: e->config.paused ^= 1; break;
          case SDL_SCANCODE_ESCAPE: running = FALSE; break;
          default: break;
        }
        /* fall through */
      case SDL_KEYUP: {
        Bool down = event.type == SDL_KEYDOWN;
        switch (event.key.keysym.scancode) {
          case SDL_SCANCODE_TAB: e->config.no_sync = down; break;
          case SDL_SCANCODE_F11: if (!down) e->config.fullscreen ^= 1; break;
          default: break;
        }
        break;
      }
      case SDL_QUIT: running = FALSE; break;
      default: break;
    }
  }
  if (old_config.no_sync != e->config.no_sync) {
    host_init_renderer(host, !e->config.no_sync);
  }
  if (old_config.fullscreen != e->config.fullscreen) {
    SDL_SetWindowFullscreen(
        host->window, e->config.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
  }
  return running;
}

static void host_render_video(Host* host, Emulator* e) {
  SDL_RenderClear(host->renderer);
  void* pixels;
  int pitch;
  if (SDL_LockTexture(host->texture, NULL, &pixels, &pitch) == 0) {
    int y;
    for (y = 0; y < SCREEN_HEIGHT; y++) {
      memcpy((u8*)pixels + y * pitch, &e->frame_buffer[y * SCREEN_WIDTH],
             SCREEN_WIDTH * sizeof(RGBA));
    }
    SDL_UnlockTexture(host->texture);
    SDL_RenderCopy(host->renderer, host->texture, NULL, NULL);
  }
  DEBUG(host, "@@@ %.1f: render present\n", get_time_ms());
  SDL_RenderPresent(host->renderer);
}

static void host_synchronize(Host* host, Emulator* e) {
  f64 now_ms = get_time_ms();
  f64 gb_ms = (f64)(e->state.cycles - host->last_sync_cycles) *
              MILLISECONDS_PER_SECOND / CPU_CYCLES_PER_SECOND;
  f64 real_ms = now_ms - host->last_sync_real_ms;
  f64 delta_ms = gb_ms - real_ms;
  f64 delay_until_ms = now_ms + delta_ms;
  if (delta_ms < -AUDIO_MAX_SLOW_DESYNC_MS ||
      delta_ms > AUDIO_MAX_FAST_DESYNC_MS) {
    DEBUG(host, "!!! %.1f: desync [gb=%.1fms real=%.1fms]\n", now_ms, gb_ms,
          real_ms);
    /* Major desync; don't try to catch up, just reset. But our audio buffer
     * is probably behind (or way ahead), so pause to refill. */
    host->last_sync_real_ms = now_ms;
    SDL_PauseAudioDevice(host->audio.dev, 1);
    host->audio.ready = FALSE;
    SDL_LockAudioDevice(host->audio.dev);
    host->audio.read_pos = host->audio.write_pos = host->audio.buffer;
    host->audio.buffer_available = 0;
    SDL_UnlockAudioDevice(host->audio.dev);
  } else {
    if (real_ms < gb_ms) {
      DEBUG(host, "... %.1f: waiting %.1fms [gb=%.1fms real=%.1fms]\n", now_ms,
            delta_ms, gb_ms, real_ms);
      do {
        SDL_Delay(delta_ms);
        now_ms = get_time_ms();
        delta_ms = delay_until_ms - now_ms;
      } while (delta_ms > 0);
    }
    host->last_sync_real_ms = delay_until_ms;
  }
  host->last_sync_cycles = e->state.cycles;
}

static void host_render_audio(Host* host, Emulator* e) {
  assert(AUDIO_SPEC_CHANNELS == SOUND_OUTPUT_COUNT);
  HostAudio* audio = &host->audio;
  u8* src = e->audio_buffer.data;
  u8* src_end = e->audio_buffer.position;

  SDL_LockAudioDevice(audio->dev);
  size_t old_buffer_available = audio->buffer_available;
  size_t src_frames = (src_end - src) / SOUND_OUTPUT_COUNT;
  size_t max_dst_frames =
      (audio->buffer_capacity - audio->buffer_available) / AUDIO_FRAME_SIZE;
  size_t frames = MIN(src_frames, max_dst_frames);
  HostAudioSample* dst = (HostAudioSample*)audio->write_pos;
  HostAudioSample* dst_end = (HostAudioSample*)audio->buffer_end;
  for (size_t i = 0; i < frames; i++) {
    assert(dst + 2 <= dst_end);
    *dst++ = AUDIO_CONVERT_SAMPLE_FROM_U8(*src++);
    *dst++ = AUDIO_CONVERT_SAMPLE_FROM_U8(*src++);
    if (dst == dst_end) {
      dst = (HostAudioSample*)audio->buffer;
    }
  }
  audio->write_pos = (u8*)dst;
  audio->buffer_available += frames * AUDIO_FRAME_SIZE;
  size_t new_buffer_available = audio->buffer_available;
  SDL_UnlockAudioDevice(audio->dev);

  if (frames < src_frames) {
    DEBUG(host, "!!! audio overflow (old size = %zu)\n", old_buffer_available);
  } else {
    DEBUG(host, "+++ %.1f: buf: %zu -> %zu\n", get_time_ms(),
          old_buffer_available, new_buffer_available);
  }
  if (!audio->ready && new_buffer_available >= audio->buffer_target_available) {
    DEBUG(host, "*** %.1f: audio buffer ready, size = %zu.\n", get_time_ms(),
          new_buffer_available);
    audio->ready = TRUE;
    SDL_PauseAudioDevice(audio->dev, 0);
  }
}

static const char* replace_extension(const char* filename,
                                     const char* extension) {
  size_t length = strlen(filename) + strlen(extension) + 1; /* +1 for \0. */
  char* result = malloc(length); /* Leaks. */
  char* last_dot = strrchr(filename, '.');
  if (last_dot == NULL) {
    snprintf(result, length, "%s%s", filename, extension);
  } else {
    snprintf(result, length, "%.*s%s", (int)(last_dot - filename), filename,
             extension);
  }
  return result;
}

#define DEFINE_SAVE_EXT_RAM(name, mode, fileop)                            \
  static Result name(Emulator* e, const char* filename) {                  \
    FILE* f = NULL;                                                        \
    if (e->state.ext_ram.battery_type == BATTERY_TYPE_WITH_BATTERY) {      \
      f = fopen(filename, mode);                                           \
      CHECK_MSG(f, "unable to open file \"%s\".\n", filename);             \
      CHECK_MSG(                                                           \
          fileop(e->state.ext_ram.data, e->state.ext_ram.size, 1, f) == 1, \
          #fileop " failed.\n");                                           \
      fclose(f);                                                           \
    }                                                                      \
    return OK;                                                             \
    ON_ERROR_CLOSE_FILE_AND_RETURN;                                        \
  }

DEFINE_SAVE_EXT_RAM(read_ext_ram_from_file, "rb", fread)
DEFINE_SAVE_EXT_RAM(write_ext_ram_to_file, "wb", fwrite)

static Emulator s_emulator;
static Host s_host;

static void joypad_callback(Emulator* e, void* user_data) {
  Host* sdl = user_data;
  const u8* state = SDL_GetKeyboardState(NULL);
  e->state.JOYP.up = state[SDL_SCANCODE_UP];
  e->state.JOYP.down = state[SDL_SCANCODE_DOWN];
  e->state.JOYP.left = state[SDL_SCANCODE_LEFT];
  e->state.JOYP.right = state[SDL_SCANCODE_RIGHT];
  e->state.JOYP.B = state[SDL_SCANCODE_Z];
  e->state.JOYP.A = state[SDL_SCANCODE_X];
  e->state.JOYP.start = state[SDL_SCANCODE_RETURN];
  e->state.JOYP.select = state[SDL_SCANCODE_BACKSPACE];
}

int main(int argc, char** argv) {
  init_time();
  --argc; ++argv;
  int result = 1;

  CHECK_MSG(argc == 1, "no rom file given.\n");
  const char* rom_filename = argv[0];
  Emulator* e = &s_emulator;
  CHECK(SUCCESS(read_data_from_file(e, rom_filename)));
  CHECK(SUCCESS(host_init_video(&s_host)));
  CHECK(SUCCESS(host_init_audio(&s_host)));
  CHECK(SUCCESS(init_audio_buffer(e, s_host.audio.spec.freq,
                                  s_host.audio.spec.size / AUDIO_FRAME_SIZE)));
  CHECK(SUCCESS(init_emulator(e)));

  e->joypad_callback.func = joypad_callback;
  e->joypad_callback.user_data = &s_host;

  const char* save_filename = replace_extension(rom_filename, SAVE_EXTENSION);
  s_host.save_state_filename =
      replace_extension(rom_filename, SAVE_STATE_EXTENSION);
  read_ext_ram_from_file(e, save_filename);

  while (TRUE) {
    if (!host_poll_events(e, &s_host)) {
      break;
    }
    if (e->config.paused) {
      SDL_PauseAudioDevice(s_host.audio.dev,
                           e->config.paused || !s_host.audio.ready);
      SDL_Delay(VIDEO_FRAME_MS);
      continue;
    }

    size_t size = s_host.audio.spec.size -
                  s_host.audio.buffer_available % s_host.audio.spec.size;
    EmulatorEvent event = run_emulator(e, size / AUDIO_FRAME_SIZE);
    if (!e->config.no_sync) {
      host_synchronize(&s_host, e);
    }
    if (event & EMULATOR_EVENT_NEW_FRAME) {
      host_render_video(&s_host, e);
      if (e->config.step) {
        e->config.paused = TRUE;
        e->config.step = FALSE;
      }
    }
    if (event & EMULATOR_EVENT_AUDIO_BUFFER_FULL) {
      host_render_audio(&s_host, e);
    }
  }

  write_ext_ram_to_file(e, save_filename);
  result = 0;
error:
  SDL_Quit();
  return result;
}

#endif /* NO_SDL */
