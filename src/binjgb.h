/*
 * Copyright (C) 2017 Ben Smith
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#ifndef BINJGB_H_
#define BINJGB_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define ZERO_MEMORY(x) memset(&(x), 0, sizeof(x))

#define SUCCESS(x) ((x) == OK)
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

typedef int8_t s8;
typedef int32_t s32;
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef float f32;
typedef double f64;
typedef u16 Address;
typedef u16 MaskedAddress;
typedef u32 RGBA;
typedef enum Bool { FALSE = 0, TRUE = 1 } Bool;

#define MAXIMUM_ROM_SIZE 8388608
#define MINIMUM_ROM_SIZE 32768
#define MAX_CART_INFOS (MAXIMUM_ROM_SIZE / MINIMUM_ROM_SIZE)
#define VIDEO_RAM_SIZE 8192
#define WORK_RAM_SIZE 8192
#define EXT_RAM_MAX_SIZE 32768
#define WAVE_RAM_SIZE 16
#define HIGH_RAM_SIZE 127

#define SCREEN_WIDTH 160
#define SCREEN_HEIGHT 144
#define SCREEN_HEIGHT_WITH_VBLANK 154
#define OBJ_COUNT 40
#define OBJ_PER_LINE_COUNT 10
#define OBJ_PALETTE_COUNT 2
#define PALETTE_COLOR_COUNT 4
#define SOUND_OUTPUT_COUNT 2

#define MILLISECONDS_PER_SECOND 1000
#define MICROSECONDS_PER_SECOND 1000000
#define MICROSECONDS_PER_MILLISECOND \
  (MICROSECONDS_PER_SECOND / MILLISECONDS_PER_SECOND)

#define CPU_CYCLES_PER_SECOND 4194304
#define APU_CYCLES_PER_SECOND 2097152
#define PPU_LINE_CYCLES 456
#define PPU_VBLANK_CYCLES (PPU_LINE_CYCLES * 10)
#define PPU_FRAME_CYCLES (PPU_LINE_CYCLES * SCREEN_HEIGHT_WITH_VBLANK)

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

static inline const char* get_enum_string(const char** strings,
                                          size_t string_count, size_t value) {
  const char* result = value < string_count ? strings[value] : "unknown";
  return result ? result : "unknown";
}

#define DEFINE_NAMED_ENUM(NAME, Name, name, foreach, enum_def)       \
  typedef enum { foreach (enum_def) NAME##_COUNT } Name;             \
  static inline Bool is_##name##_valid(Name value) {                 \
    return value < NAME##_COUNT;                                     \
  }                                                                  \
  static inline const char* get_##name##_string(Name value) {        \
    static const char* s_strings[] = {foreach (DEFINE_STRING)};      \
    return get_enum_string(s_strings, ARRAY_SIZE(s_strings), value); \
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

typedef enum {
  COLOR_WHITE = 0,
  COLOR_LIGHT_GRAY = 1,
  COLOR_DARK_GRAY = 2,
  COLOR_BLACK = 3,
} Color;

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
  Bool status;     /* Status bit for NR52 */
  u32 accumulator; /* Accumulates samples for resampling. */
} Channel;

typedef struct {
  u32 frequency;    /* Sample frequency, as N samples per second */
  u32 freq_counter; /* Used for resampling; [0..APU_CYCLES_PER_SECOND). */
  u32 divisor;
  u8* data; /* Unsigned 8-bit 2-channel samples @ |frequency| */
  u8* end;
  u8* position;
} AudioBuffer;

typedef struct {
  u8 so_volume[SOUND_OUTPUT_COUNT];
  Bool so_output[SOUND_COUNT][SOUND_OUTPUT_COUNT];
  Bool enabled;
  Sweep sweep;
  Wave wave;
  Noise noise;
  Channel channel[CHANNEL_COUNT];
  u8 frame;         /* 0..FRAME_SEQUENCER_COUNT */
  u32 cycles;       /* Raw cycle counter */
  Bool initialized;
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

Result get_file_size(FILE* f, long* out_size);
Result read_rom_data_from_file(Emulator* e, const char* filename);
Result init_audio_buffer(Emulator* e, u32 frequency, u32 frames);
Result init_emulator(Emulator* e);
void step_emulator(Emulator* e);
EmulatorEvent run_emulator(Emulator* e, u32 max_audio_frames);
u8 read_u8(Emulator* e, Address addr);
void write_u8(Emulator* e, Address addr, u8 value);

#ifdef __cplusplus
}
#endif

#endif /* BINJGB_H_ */
