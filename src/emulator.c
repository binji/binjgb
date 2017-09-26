/*
 * Copyright (C) 2016 Ben Smith
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "emulator.h"

#define MAXIMUM_ROM_SIZE MEGABYTES(8)
#define MINIMUM_ROM_SIZE KILOBYTES(32)
#define MAX_CART_INFOS (MAXIMUM_ROM_SIZE / MINIMUM_ROM_SIZE)
#define VIDEO_RAM_SIZE KILOBYTES(8)
#define WORK_RAM_SIZE KILOBYTES(8)
#define EXT_RAM_MAX_SIZE KILOBYTES(32)
#define WAVE_RAM_SIZE 16
#define HIGH_RAM_SIZE 127

#define OBJ_PER_LINE_COUNT 10
#define OBJ_PALETTE_COUNT 2

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

#define FOREACH_EXT_RAM_SIZE(V)           \
  V(EXT_RAM_SIZE_NONE, 0, 0)              \
  V(EXT_RAM_SIZE_2K, 1, KILOBYTES(2))     \
  V(EXT_RAM_SIZE_8K, 2, KILOBYTES(8))     \
  V(EXT_RAM_SIZE_32K, 3, KILOBYTES(32))   \
  V(EXT_RAM_SIZE_128K, 4, KILOBYTES(128)) \
  V(EXT_RAM_SIZE_64K, 5, KILOBYTES(64))

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
} MbcType;

typedef enum {
  EXT_RAM_TYPE_NO_RAM,
  EXT_RAM_TYPE_WITH_RAM,
} ExtRamType;

typedef enum {
  BATTERY_TYPE_NO_BATTERY,
  BATTERY_TYPE_WITH_BATTERY,
} BatteryType;

typedef struct {
  MbcType mbc_type;
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
} LfsrWidth;

typedef enum {
  DMA_INACTIVE = 0,
  DMA_TRIGGERED = 1,
  DMA_ACTIVE = 2,
} DmaState;

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
} Mbc1, Huc1, Mmm01;

typedef struct {
  u8 byte_2000_2fff;
  u8 byte_3000_3fff;
} Mbc5;

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
    Mbc1 mbc1;
    Mmm01 mmm01;
    Huc1 huc1;
    Mbc5 mbc5;
  };
} MemoryMapState;

typedef struct {
  MemoryMapType type;
  MaskedAddress addr;
} MemoryTypeAddressPair;

typedef struct {
  JoypadButtons buttons;
  JoypadSelect joypad_select;
  u8 last_p10_p13;
} Joypad;

typedef struct {
  Bool ime;      /* Interrupt Master Enable */
  u8 ie;         /* Interrupt Enable */
  u8 if_;        /* Interrupt Request, delayed by 1 M-cycle for some IRQs. */
  u8 new_if;     /* The new value of IF, updated in 1 M-cycle. */
  Bool enable;   /* Set after EI instruction. This delays updating IME. */
  Bool halt;     /* Halted, waiting for an interrupt. */
  Bool halt_di;  /* Halted w/ disabled interrupts. */
  Bool halt_bug; /* Halt bug occurred. */
  Bool stop;     /* Stopped, waiting for an interrupt. */
} Interrupt;

typedef struct {
  u8 tima;                 /* Incremented at rate defined by clock_select */
  u8 tma;                  /* When TIMA overflows, it is set to this value */
  TimerClock clock_select; /* Select the rate of TIMA */
  u16 div_counter;         /* Internal clock counter, upper 8 bits are DIV. */
  TimaState tima_state;    /* Used to implement TIMA overflow delay. */
  Bool on;
} Timer;

typedef struct {
  Bool transferring;
  SerialClock clock;
  u8 sb; /* Serial transfer data. */
  u8 transferred_bits;
  Cycles cycles;
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
  Cycles sample_time; /* Time (in cycles) the sample was read. */
  u8 sample_data;     /* Last sample generated, 0..1 */
  u32 period;         /* Calculated from the frequency. */
  u8 position;        /* 0..31 */
  u32 cycles;         /* 0..period */
  Bool playing;       /* TRUE if the channel has been triggered but the DAC not
                              disabled. */
} Wave;

/* Channel 4 */
typedef struct {
  u8 clock_shift;
  LfsrWidth lfsr_width;
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
  u8 so_volume[SOUND_OUTPUT_COUNT];
  Bool so_output[SOUND_COUNT][SOUND_OUTPUT_COUNT];
  Bool enabled;
  Sweep sweep;
  Wave wave;
  Noise noise;
  Channel channel[APU_CHANNEL_COUNT];
  u8 frame;      /* 0..FRAME_SEQUENCER_COUNT */
  Cycles cycles; /* Raw cycle counter */
  Bool initialized;
} Apu;

typedef struct {
  Bool display;
  TileMapSelect window_tile_map_select;
  Bool window_display;
  TileDataSelect bg_tile_data_select;
  TileMapSelect bg_tile_map_select;
  ObjSize obj_size;
  Bool obj_display;
  Bool bg_display;
} Lcdc;

typedef struct {
  Bool irq;
  Bool trigger;
} StatInterrupt;

typedef struct {
  StatInterrupt y_compare;
  StatInterrupt mode2;
  StatInterrupt vblank;
  StatInterrupt hblank;
  Bool ly_eq_lyc;       /* TRUE if ly=lyc, delayed by 1 M-cycle. */
  PPUMode mode;         /* The current PPU mode. */
  Bool if_;             /* Internal interrupt flag for STAT interrupts. */
  PPUMode trigger_mode; /* This mode is used for checking STAT IRQ triggers. */
  Bool new_ly_eq_lyc;   /* The new value for ly_eq_lyc, updated in 1 M-cycle. */
} Stat;

typedef struct {
  Lcdc lcdc;                      /* LCD control */
  Stat stat;                      /* LCD status */
  u8 scy;                         /* Screen Y */
  u8 scx;                         /* Screen X */
  u8 ly;                          /* Line Y */
  u8 lyc;                         /* Line Y Compare */
  u8 wy;                          /* Window Y */
  u8 wx;                          /* Window X */
  Palette bgp;                    /* BG Palette */
  Palette obp[OBJ_PALETTE_COUNT]; /* OBJ Palettes */
  PPUState state;
  u32 state_cycles;
  u32 line_cycles;                /* Counts cycles until line_y changes. */
  u32 frame;                      /* The currently rendering frame. */
  u8 last_ly;                     /* LY from the previous M-cycle. */
  u8 render_x;                    /* Currently rendering X coordinate. */
  u8 line_y;   /* The currently rendering line. Can be different than LY. */
  u8 win_y;    /* The window Y is only incremented when rendered. */
  u8 frame_wy; /* wy is cached per frame. */
  Obj line_obj[OBJ_PER_LINE_COUNT]; /* Cached from OAM during mode2. */
  u8 line_obj_count;     /* Number of sprites to draw on this line. */
  u8 oam_index;          /* Current sprite index in mode 2. */
  Bool rendering_window; /* TRUE when this line is rendering the window. */
  Bool new_frame_edge;
  u8 display_delay_frames; /* Wait this many frames before displaying. */
} Ppu;

typedef struct {
  DmaState state;
  MemoryTypeAddressPair source;
  Cycles cycles;
} Dma;

typedef struct {
  u32 header; /* Set to SAVE_STATE_HEADER; makes it easier to save state. */
  u8 cart_info_index;
  MemoryMapState memory_map_state;
  Registers reg;
  u8 vram[VIDEO_RAM_SIZE];
  ExtRam ext_ram;
  u8 wram[WORK_RAM_SIZE];
  Interrupt interrupt;
  Obj oam[OBJ_COUNT];
  Joypad joyp;
  Serial serial;
  Timer timer;
  Apu apu;
  Ppu ppu;
  Dma dma;
  u8 hram[HIGH_RAM_SIZE];
  Cycles cycles;
  Bool is_cgb;
  EmulatorEvent last_event;
} EmulatorState;

const size_t s_emulator_state_size = sizeof(EmulatorState);

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
  JoypadCallbackInfo joypad_info;
} Emulator;


/* Abbreviations of commonly accessed values. */
#define APU (e->state.apu)
#define CHANNEL1 CHANNEL(1)
#define CHANNEL2 CHANNEL(2)
#define CHANNEL3 CHANNEL(3)
#define CHANNEL4 CHANNEL(4)
#define CHANNEL(i) (APU.channel[APU_CHANNEL##i])
#define CYCLES (e->state.cycles)
#define DMA (e->state.dma)
#define EXT_RAM (e->state.ext_ram)
#define HRAM (e->state.hram)
#define INTR (e->state.interrupt)
#define JOYP (e->state.joyp)
#define LCDC (PPU.lcdc)
#define MMAP_STATE (e->state.memory_map_state)
#define NOISE (APU.noise)
#define OAM (e->state.oam)
#define PPU (e->state.ppu)
#define REG (e->state.reg)
#define SERIAL (e->state.serial)
#define STAT (PPU.stat)
#define SWEEP (APU.sweep)
#define TIMER (e->state.timer)
#define VRAM (e->state.vram)
#define WAVE (APU.wave)
#define WRAM (e->state.wram)


#define DIV_CEIL(numer, denom) (((numer) + (denom) - 1) / (denom))
#define VALUE_WRAPPED(X, MAX) ((X) >= (MAX) ? ((X) -= (MAX), TRUE) : FALSE)

#define SAVE_STATE_VERSION (2)
#define SAVE_STATE_HEADER (u32)(0x6b57a7e0 + SAVE_STATE_VERSION)

#ifndef HOOK0
#define HOOK0(name)
#endif

#ifndef HOOK
#define HOOK(name, ...)
#endif

/* Configurable constants */
#define RGBA_WHITE 0xffffffffu
#define RGBA_LIGHT_GRAY 0xffaaaaaau
#define RGBA_DARK_GRAY 0xff555555u
#define RGBA_BLACK 0xff000000u

/* ROM header stuff */
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

#define CART_INFO_SHIFT 15
#define ROM_BANK_SHIFT 14
#define EXT_RAM_BANK_SHIFT 13

/* Cycle counts */
#define CPU_MCYCLE 4
#define APU_CYCLES 2
#define PPU_ENABLE_DISPLAY_DELAY_FRAMES 4
#define PPU_MODE2_CYCLES 80
#define PPU_MODE3_MIN_CYCLES 172
#define DMA_CYCLES 648
#define DMA_DELAY_CYCLES 8
#define SERIAL_CYCLES (CPU_CYCLES_PER_SECOND / 8192)

/* Video */
#define TILE_WIDTH 8
#define TILE_HEIGHT 8
#define TILE_ROW_BYTES 2
#define TILE_MAP_WIDTH 32
#define WINDOW_MAX_X 166
#define WINDOW_X_OFFSET 7

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
#define SOUND_OUTPUT_MAX_VOLUME 7

/* Additional samples so the AudioBuffer doesn't overflow. This could happen
 * because the audio buffer is updated at the granularity of an instruction, so
 * the most extra frames that could be added is equal to the Apu cycle count
 * of the slowest instruction. */
#define AUDIO_BUFFER_EXTRA_FRAMES 256

#define WAVE_TRIGGER_CORRUPTION_OFFSET_CYCLES APU_CYCLES
#define WAVE_TRIGGER_DELAY_CYCLES (3 * APU_CYCLES)

#define FRAME_SEQUENCER_COUNT 8
#define FRAME_SEQUENCER_CYCLES 8192 /* 512Hz */
#define FRAME_SEQUENCER_UPDATE_ENVELOPE_FRAME 7

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

static CartTypeInfo s_cart_type_info[] = {
#define V(name, code, mbc, ram, battery) \
  [code] = {MBC_TYPE_##mbc, EXT_RAM_TYPE_##ram, BATTERY_TYPE_##battery},
    FOREACH_CART_TYPE(V)
#undef V
};

/* TIMA is incremented when the given bit of DIV_counter changes from 1 to 0. */
static const u16 s_tima_mask[] = {1 << 9, 1 << 3, 1 << 5, 1 << 7};
static u8 s_wave_volume_shift[WAVE_VOLUME_COUNT] = {4, 0, 1, 2};
static u8 s_obj_size_to_height[] = {[OBJ_SIZE_8X8] = 8, [OBJ_SIZE_8X16] = 16};
static RGBA s_color_to_rgba[] = {[COLOR_WHITE] = RGBA_WHITE,
                                 [COLOR_LIGHT_GRAY] = RGBA_LIGHT_GRAY,
                                 [COLOR_DARK_GRAY] = RGBA_DARK_GRAY,
                                 [COLOR_BLACK] = RGBA_BLACK};

static Result init_memory_map(Emulator*);
static void apu_synchronize(Emulator* e);

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
  CHECK_MSG(file_data->size >= offset + rom_byte_size,
            "File size too small (required %ld, got %ld)\n",
            (long)(offset + rom_byte_size), (long)file_data->size);

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

static void dummy_write(Emulator* e, MaskedAddress addr, u8 value) {}

static u8 dummy_read(Emulator* e, MaskedAddress addr) {
  return INVALID_READ_BYTE;
}

static void set_rom1_bank(Emulator* e, u16 bank) {
  u32 new_base = (bank & ROM_BANK_MASK(e)) << ROM_BANK_SHIFT;
  u32* base = &MMAP_STATE.rom1_base;
  if (new_base != *base) {
    HOOK(set_rom1_bank_hi, bank, new_base);
  }
  *base = new_base;
}

static void set_ext_ram_bank(Emulator* e, u8 bank) {
  u32 new_base = (bank & EXT_RAM_BANK_MASK(e)) << EXT_RAM_BANK_SHIFT;
  u32* base = &MMAP_STATE.ext_ram_base;
  if (new_base != *base) {
    HOOK(set_ext_ram_bank_bi, bank, new_base);
  }
  *base = new_base;
}

static u8 gb_read_ext_ram(Emulator* e, MaskedAddress addr) {
  if (MMAP_STATE.ext_ram_enabled) {
    assert(addr <= ADDR_MASK_8K);
    return EXT_RAM.data[MMAP_STATE.ext_ram_base | addr];
  } else {
    HOOK(read_ram_disabled_a, addr);
    return INVALID_READ_BYTE;
  }
}

static void gb_write_ext_ram(Emulator* e, MaskedAddress addr, u8 value) {
  if (MMAP_STATE.ext_ram_enabled) {
    assert(addr <= ADDR_MASK_8K);
    EXT_RAM.data[MMAP_STATE.ext_ram_base | addr] = value;
  } else {
    HOOK(write_ram_disabled_ab, addr, value);
  }
}

static void mbc1_write_rom(Emulator* e, MaskedAddress addr, u8 value) {
  Mbc1* mbc1 = &MMAP_STATE.mbc1;
  switch (addr >> 13) {
    case 0: /* 0000-1fff */
      MMAP_STATE.ext_ram_enabled =
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
  switch (addr >> 13) {
    case 0: /* 0000-1fff */
      if ((addr & MBC2_ADDR_SELECT_BIT_MASK) == 0) {
        MMAP_STATE.ext_ram_enabled =
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
  if (MMAP_STATE.ext_ram_enabled) {
    return EXT_RAM.data[addr & MBC2_RAM_ADDR_MASK];
  } else {
    HOOK(read_ram_disabled_a, addr);
    return INVALID_READ_BYTE;
  }
}

static void mbc2_write_ram(Emulator* e, MaskedAddress addr, u8 value) {
  if (MMAP_STATE.ext_ram_enabled) {
    EXT_RAM.data[addr & MBC2_RAM_ADDR_MASK] = value & MBC2_RAM_VALUE_MASK;
  } else {
    HOOK(write_ram_disabled_ab, addr, value);
  }
}

static void mbc3_write_rom(Emulator* e, MaskedAddress addr, u8 value) {
  switch (addr >> 13) {
    case 0: /* 0000-1fff */
      MMAP_STATE.ext_ram_enabled =
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
  switch (addr >> 12) {
    case 0: case 1: /* 0000-1fff */
      MMAP_STATE.ext_ram_enabled =
          (value & MBC_RAM_ENABLED_MASK) == MBC_RAM_ENABLED_VALUE;
      break;
    case 2: /* 2000-2fff */
      MMAP_STATE.mbc5.byte_2000_2fff = value;
      break;
    case 3: /* 3000-3fff */
      MMAP_STATE.mbc5.byte_3000_3fff = value;
      break;
    case 4: case 5: /* 4000-5fff */
      set_ext_ram_bank(e, value & MBC5_RAM_BANK_SELECT_MASK);
      break;
    default:
      break;
  }

  set_rom1_bank(e, ((MMAP_STATE.mbc5.byte_3000_3fff & 1) << 8) |
                       MMAP_STATE.mbc5.byte_2000_2fff);
}

static void huc1_write_rom(Emulator* e, MaskedAddress addr, u8 value) {
  Huc1* huc1 = &MMAP_STATE.huc1;
  switch (addr >> 13) {
    case 0: /* 0000-1fff */
      MMAP_STATE.ext_ram_enabled =
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
  Mmm01* mmm01 = &MMAP_STATE.mmm01;
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
      EXT_RAM.size = s_ext_ram_byte_size[e->cart_info->ext_ram_size];
      break;
    default:
    case EXT_RAM_TYPE_NO_RAM:
      memory_map->read_ext_ram = dummy_read;
      memory_map->write_ext_ram = dummy_write;
      EXT_RAM.size = 0;
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
      EXT_RAM.size = MBC2_RAM_SIZE;
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

  EXT_RAM.battery_type = cart_type_info->battery_type;
  return OK;
}

static u8 read_vram(Emulator* e, MaskedAddress addr) {
  if (STAT.mode == PPU_MODE_MODE3) {
    HOOK(read_vram_in_use_a, addr);
    return INVALID_READ_BYTE;
  } else {
    assert(addr <= ADDR_MASK_8K);
    return VRAM[addr];
  }
}

static Bool is_using_oam(Emulator* e) {
  return STAT.mode == PPU_MODE_MODE2 || STAT.mode == PPU_MODE_MODE3;
}

static u8 read_oam(Emulator* e, MaskedAddress addr) {
  if (is_using_oam(e)) {
    HOOK(read_oam_in_use_a, addr);
    return INVALID_READ_BYTE;
  }

  u8 obj_index = addr >> 2;
  Obj* obj = &OAM[obj_index];
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
  if (JOYP.joypad_select == JOYPAD_SELECT_BUTTONS ||
      JOYP.joypad_select == JOYPAD_SELECT_BOTH) {
    result |= PACK(JOYP.buttons.start, JOYP_BUTTON_START) |
              PACK(JOYP.buttons.select, JOYP_BUTTON_SELECT) |
              PACK(JOYP.buttons.B, JOYP_BUTTON_B) |
              PACK(JOYP.buttons.A, JOYP_BUTTON_A);
  }

  Bool left = JOYP.buttons.left;
  Bool right = JOYP.buttons.right;
  Bool up = JOYP.buttons.up;
  Bool down = JOYP.buttons.down;
  if (!e->config.allow_simulataneous_dpad_opposites) {
    if (left && right) {
      left = FALSE;
    } else if (up && down) {
      up = FALSE;
    }
  }

  if (JOYP.joypad_select == JOYPAD_SELECT_DPAD ||
      JOYP.joypad_select == JOYPAD_SELECT_BOTH) {
    result |= PACK(down, JOYP_DPAD_DOWN) | PACK(up, JOYP_DPAD_UP) |
              PACK(left, JOYP_DPAD_LEFT) | PACK(right, JOYP_DPAD_RIGHT);
  }
  /* The bits are low when the buttons are pressed. */
  return ~result;
}

static u8 read_io(Emulator* e, MaskedAddress addr) {
  switch (addr) {
    case IO_JOYP_ADDR:
      if (e->joypad_info.callback) {
        e->joypad_info.callback(&JOYP.buttons, e->joypad_info.user_data);
      }
      return JOYP_UNUSED | PACK(JOYP.joypad_select, JOYP_JOYPAD_SELECT) |
             (read_joyp_p10_p13(e) & JOYP_RESULT_MASK);
    case IO_SB_ADDR:
      return SERIAL.sb;
    case IO_SC_ADDR:
      return SC_UNUSED | PACK(SERIAL.transferring, SC_TRANSFER_START) |
             PACK(SERIAL.clock, SC_SHIFT_CLOCK);
    case IO_DIV_ADDR:
      return TIMER.div_counter >> 8;
    case IO_TIMA_ADDR:
      return TIMER.tima;
    case IO_TMA_ADDR:
      return TIMER.tma;
    case IO_TAC_ADDR:
      return TAC_UNUSED | PACK(TIMER.on, TAC_TIMER_ON) |
             PACK(TIMER.clock_select, TAC_CLOCK_SELECT);
    case IO_IF_ADDR:
      return IF_UNUSED | INTR.if_;
    case IO_LCDC_ADDR:
      return PACK(LCDC.display, LCDC_DISPLAY) |
             PACK(LCDC.window_tile_map_select,
                  LCDC_WINDOW_TILE_MAP_SELECT) |
             PACK(LCDC.window_display, LCDC_WINDOW_DISPLAY) |
             PACK(LCDC.bg_tile_data_select, LCDC_BG_TILE_DATA_SELECT) |
             PACK(LCDC.bg_tile_map_select, LCDC_BG_TILE_MAP_SELECT) |
             PACK(LCDC.obj_size, LCDC_OBJ_SIZE) |
             PACK(LCDC.obj_display, LCDC_OBJ_DISPLAY) |
             PACK(LCDC.bg_display, LCDC_BG_DISPLAY);
    case IO_STAT_ADDR:
      return STAT_UNUSED | PACK(STAT.y_compare.irq, STAT_YCOMPARE_INTR) |
             PACK(STAT.mode2.irq, STAT_MODE2_INTR) |
             PACK(STAT.vblank.irq, STAT_VBLANK_INTR) |
             PACK(STAT.hblank.irq, STAT_HBLANK_INTR) |
             PACK(STAT.ly_eq_lyc, STAT_YCOMPARE) |
             PACK(STAT.mode, STAT_MODE);
    case IO_SCY_ADDR:
      return PPU.scy;
    case IO_SCX_ADDR:
      return PPU.scx;
    case IO_LY_ADDR:
      return PPU.ly;
    case IO_LYC_ADDR:
      return PPU.lyc;
    case IO_DMA_ADDR:
      return INVALID_READ_BYTE; /* Write only. */
    case IO_BGP_ADDR:
      return PACK(PPU.bgp.color[3], PALETTE_COLOR3) |
             PACK(PPU.bgp.color[2], PALETTE_COLOR2) |
             PACK(PPU.bgp.color[1], PALETTE_COLOR1) |
             PACK(PPU.bgp.color[0], PALETTE_COLOR0);
    case IO_OBP0_ADDR:
      return PACK(PPU.obp[0].color[3], PALETTE_COLOR3) |
             PACK(PPU.obp[0].color[2], PALETTE_COLOR2) |
             PACK(PPU.obp[0].color[1], PALETTE_COLOR1) |
             PACK(PPU.obp[0].color[0], PALETTE_COLOR0);
    case IO_OBP1_ADDR:
      return PACK(PPU.obp[1].color[3], PALETTE_COLOR3) |
             PACK(PPU.obp[1].color[2], PALETTE_COLOR2) |
             PACK(PPU.obp[1].color[1], PALETTE_COLOR1) |
             PACK(PPU.obp[1].color[0], PALETTE_COLOR0);
    case IO_WY_ADDR:
      return PPU.wy;
    case IO_WX_ADDR:
      return PPU.wx;
    case IO_IE_ADDR:
      return INTR.ie;
    default:
      HOOK(read_io_ignored_as, addr, get_io_reg_string(addr));
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
  apu_synchronize(e);
  switch (addr) {
    case APU_NR10_ADDR:
      return NR10_UNUSED | PACK(SWEEP.period, NR10_SWEEP_PERIOD) |
             PACK(SWEEP.direction, NR10_SWEEP_DIRECTION) |
             PACK(SWEEP.shift, NR10_SWEEP_SHIFT);
    case APU_NR11_ADDR:
      return NRX1_UNUSED | read_nrx1_reg(&CHANNEL1);
    case APU_NR12_ADDR:
      return read_nrx2_reg(&CHANNEL1);
    case APU_NR14_ADDR:
      return NRX4_UNUSED | read_nrx4_reg(&CHANNEL1);
    case APU_NR21_ADDR:
      return NRX1_UNUSED | read_nrx1_reg(&CHANNEL2);
    case APU_NR22_ADDR:
      return read_nrx2_reg(&CHANNEL2);
    case APU_NR24_ADDR:
      return NRX4_UNUSED | read_nrx4_reg(&CHANNEL2);
    case APU_NR30_ADDR:
      return NR30_UNUSED |
             PACK(CHANNEL3.dac_enabled, NR30_DAC_ENABLED);
    case APU_NR32_ADDR:
      return NR32_UNUSED | PACK(WAVE.volume, NR32_SELECT_WAVE_VOLUME);
    case APU_NR34_ADDR:
      return NRX4_UNUSED | read_nrx4_reg(&CHANNEL3);
    case APU_NR42_ADDR:
      return read_nrx2_reg(&CHANNEL4);
    case APU_NR43_ADDR:
      return PACK(NOISE.clock_shift, NR43_CLOCK_SHIFT) |
             PACK(NOISE.lfsr_width, NR43_LFSR_WIDTH) |
             PACK(NOISE.divisor, NR43_DIVISOR);
    case APU_NR44_ADDR:
      return NRX4_UNUSED | read_nrx4_reg(&CHANNEL4);
    case APU_NR50_ADDR:
      return PACK(APU.so_output[VIN][1], NR50_VIN_SO2) |
             PACK(APU.so_volume[1], NR50_SO2_VOLUME) |
             PACK(APU.so_output[VIN][0], NR50_VIN_SO1) |
             PACK(APU.so_volume[0], NR50_SO1_VOLUME);
    case APU_NR51_ADDR:
      return PACK(APU.so_output[SOUND4][1], NR51_SOUND4_SO2) |
             PACK(APU.so_output[SOUND3][1], NR51_SOUND3_SO2) |
             PACK(APU.so_output[SOUND2][1], NR51_SOUND2_SO2) |
             PACK(APU.so_output[SOUND1][1], NR51_SOUND1_SO2) |
             PACK(APU.so_output[SOUND4][0], NR51_SOUND4_SO1) |
             PACK(APU.so_output[SOUND3][0], NR51_SOUND3_SO1) |
             PACK(APU.so_output[SOUND2][0], NR51_SOUND2_SO1) |
             PACK(APU.so_output[SOUND1][0], NR51_SOUND1_SO1);
    case APU_NR52_ADDR:
      return NR52_UNUSED | PACK(APU.enabled, NR52_ALL_SOUND_ENABLED) |
             PACK(CHANNEL4.status, NR52_SOUND4_ON) |
             PACK(CHANNEL3.status, NR52_SOUND3_ON) |
             PACK(CHANNEL2.status, NR52_SOUND2_ON) |
             PACK(CHANNEL1.status, NR52_SOUND1_ON);
    default:
      return INVALID_READ_BYTE;
  }
}

static u8 read_wave_ram(Emulator* e, MaskedAddress addr) {
  apu_synchronize(e);
  if (CHANNEL3.status) {
    /* If the wave channel is playing, the byte is read from the sample
     * position. On DMG, this is only allowed if the read occurs exactly when
     * it is being accessed by the Wave channel. */
    u8 result;
    if (e->state.is_cgb || CYCLES == WAVE.sample_time) {
      result = WAVE.ram[WAVE.position >> 1];
      HOOK(read_wave_ram_while_playing_ab, addr, result);
    } else {
      result = INVALID_READ_BYTE;
      HOOK(read_wave_ram_while_playing_invalid_a, addr);
    }
    return result;
  } else {
    return WAVE.ram[addr];
  }
}

static Bool is_dma_access_ok(Emulator* e, MemoryTypeAddressPair pair) {
  /* TODO: need to figure out bus conflicts during DMA for non-OAM accesses. */
  return DMA.state != DMA_ACTIVE || pair.type != MEMORY_MAP_OAM;
}

static u8 read_u8_no_dma_check(Emulator* e, MemoryTypeAddressPair pair) {
  switch (pair.type) {
    case MEMORY_MAP_ROM0:
      assert(pair.addr < e->cart_info->size);
      return e->cart_info->data[pair.addr];
    case MEMORY_MAP_ROM1: {
      u32 rom_addr = MMAP_STATE.rom1_base| pair.addr;
      assert(rom_addr < e->cart_info->size);
      return e->cart_info->data[rom_addr];
    }
    case MEMORY_MAP_VRAM:
      return read_vram(e, pair.addr);
    case MEMORY_MAP_EXT_RAM:
      return e->memory_map.read_ext_ram(e, pair.addr);
    case MEMORY_MAP_WORK_RAM0:
      return WRAM[pair.addr];
    case MEMORY_MAP_WORK_RAM1:
      return WRAM[0x1000 + pair.addr];
    case MEMORY_MAP_OAM:
      return read_oam(e, pair.addr);
    case MEMORY_MAP_UNUSED:
      return INVALID_READ_BYTE;
    case MEMORY_MAP_IO: {
      u8 value = read_io(e, pair.addr);
      HOOK(read_io_asb, pair.addr, get_io_reg_string(pair.addr), value);
      return value;
    }
    case MEMORY_MAP_APU:
      return read_apu(e, pair.addr);
    case MEMORY_MAP_WAVE_RAM:
      return read_wave_ram(e, pair.addr);
    case MEMORY_MAP_HIGH_RAM:
      return HRAM[pair.addr];
    default:
      UNREACHABLE("invalid address: %u 0x%04x.\n", pair.type, pair.addr);
  }
}

u8 read_u8(Emulator* e, Address addr) {
  MemoryTypeAddressPair pair = map_address(addr);
  if (!is_dma_access_ok(e, pair)) {
    HOOK(read_during_dma_a, addr);
    return INVALID_READ_BYTE;
  }

  return read_u8_no_dma_check(e, pair);
}

static void write_vram(Emulator* e, MaskedAddress addr, u8 value) {
  if (STAT.mode == PPU_MODE_MODE3) {
    HOOK(write_vram_in_use_ab, addr, value);
    return;
  }

  assert(addr <= ADDR_MASK_8K);
  VRAM[addr] = value;
}

static void write_oam_no_mode_check(Emulator* e, MaskedAddress addr, u8 value) {
  Obj* obj = &OAM[addr >> 2];
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
    HOOK(write_oam_in_use_ab, addr, value);
    return;
  }

  write_oam_no_mode_check(e, addr, value);
}

static void increment_tima(Emulator* e) {
  if (++TIMER.tima == 0) {
    HOOK(trigger_timer_i, CYCLES + CPU_MCYCLE);
    TIMER.tima_state = TIMA_STATE_OVERFLOW;
    INTR.new_if |= IF_TIMER;
  }
}

static void write_div_counter(Emulator* e, u16 div_counter) {
  if (TIMER.on) {
    u16 falling_edge = ((TIMER.div_counter ^ div_counter) & ~div_counter);
    if ((falling_edge & s_tima_mask[TIMER.clock_select]) != 0) {
      increment_tima(e);
    }
  }
  TIMER.div_counter = div_counter;
}

/* Trigger is only TRUE on the cycle where it transitioned to the new state;
 * "check" is TRUE as long as at continues to be in that state. This is
 * necessary because the internal STAT IF flag is set when "triggered", and
 * cleared only when the "check" returns FALSE for all STAT IF bits. HBLANK and
 * VBLANK don't have a special trigger, so "trigger" and "check" are equal for
 * those modes. */
#define TRIGGER_MODE_IS(X) (STAT.trigger_mode == PPU_MODE_##X)
#define TRIGGER_HBLANK (TRIGGER_MODE_IS(HBLANK) && STAT.hblank.irq)
#define TRIGGER_VBLANK (TRIGGER_MODE_IS(VBLANK) && STAT.vblank.irq)
#define TRIGGER_MODE2 (STAT.mode2.trigger && STAT.mode2.irq)
#define CHECK_MODE2 (TRIGGER_MODE_IS(MODE2) && STAT.mode2.irq)
#define TRIGGER_Y_COMPARE (STAT.y_compare.trigger && STAT.y_compare.irq)
#define CHECK_Y_COMPARE (STAT.new_ly_eq_lyc && STAT.y_compare.irq)
#define SHOULD_TRIGGER_STAT \
  (TRIGGER_HBLANK || TRIGGER_VBLANK || TRIGGER_MODE2 || TRIGGER_Y_COMPARE)

static void check_stat(Emulator* e) {
  if (!STAT.if_ && SHOULD_TRIGGER_STAT) {
    HOOK(trigger_stat_ii, PPU.ly, CYCLES + CPU_MCYCLE);
    INTR.new_if |= IF_STAT;
    if (!(TRIGGER_VBLANK || TRIGGER_Y_COMPARE)) {
      INTR.if_ |= IF_STAT;
    }
    STAT.if_ = TRUE;
  } else if (!(TRIGGER_HBLANK || TRIGGER_VBLANK || CHECK_MODE2 ||
               CHECK_Y_COMPARE)) {
    STAT.if_ = FALSE;
  }
}

static void check_ly_eq_lyc(Emulator* e, Bool write) {
  if (PPU.ly == PPU.lyc ||
      (write && PPU.last_ly == SCREEN_HEIGHT_WITH_VBLANK - 1 &&
       PPU.last_ly == PPU.lyc)) {
    HOOK(trigger_y_compare_ii, PPU.ly, CYCLES + CPU_MCYCLE);
    STAT.y_compare.trigger = TRUE;
    STAT.new_ly_eq_lyc = TRUE;
  } else {
    STAT.y_compare.trigger = FALSE;
    STAT.ly_eq_lyc = STAT.new_ly_eq_lyc = FALSE;
    if (write) {
      /* If stat was triggered this frame due to Y compare, cancel it.
       * There's probably a nicer way to do this. */
      if ((INTR.new_if ^ INTR.if_) & INTR.new_if & IF_STAT) {
        if (!SHOULD_TRIGGER_STAT) {
          INTR.new_if &= ~IF_STAT;
        }
      }
    }
  }
}

static void check_joyp_intr(Emulator* e) {
  u8 p10_p13 = read_joyp_p10_p13(e);
  /* joyp interrupt only triggers on p10-p13 going from high to low (i.e. not
   * pressed to pressed). */
  if ((p10_p13 ^ JOYP.last_p10_p13) & ~p10_p13) {
    INTR.new_if |= IF_JOYPAD;
    JOYP.last_p10_p13 = p10_p13;
  }
}

static void write_io(Emulator* e, MaskedAddress addr, u8 value) {
  HOOK(write_io_asb, addr, get_io_reg_string(addr), value);
  switch (addr) {
    case IO_JOYP_ADDR:
      JOYP.joypad_select = UNPACK(value, JOYP_JOYPAD_SELECT);
      check_joyp_intr(e);
      break;
    case IO_SB_ADDR:
      SERIAL.sb = value;
      break;
    case IO_SC_ADDR:
      SERIAL.transferring = UNPACK(value, SC_TRANSFER_START);
      SERIAL.clock = UNPACK(value, SC_SHIFT_CLOCK);
      if (SERIAL.transferring) {
        SERIAL.cycles = 0;
        SERIAL.transferred_bits = 0;
      }
      break;
    case IO_DIV_ADDR:
      write_div_counter(e, 0);
      break;
    case IO_TIMA_ADDR:
      if (TIMER.on) {
        if (TIMER.tima_state == TIMA_STATE_OVERFLOW) {
          /* Cancel the overflow and interrupt if written on the same cycle. */
          TIMER.tima_state = TIMA_STATE_NORMAL;
          INTR.new_if &= ~IF_TIMER;
          TIMER.tima = value;
        } else if (TIMER.tima_state != TIMA_STATE_RESET) {
          /* Only update tima if it wasn't reset this cycle. */
          TIMER.tima = value;
        }
      } else {
        TIMER.tima = value;
      }
      break;
    case IO_TMA_ADDR:
      TIMER.tma = value;
      if (TIMER.on && TIMER.tima_state == TIMA_STATE_RESET) {
        TIMER.tima = value;
      }
      break;
    case IO_TAC_ADDR: {
      Bool old_timer_on = TIMER.on;
      u16 old_tima_mask = s_tima_mask[TIMER.clock_select];
      TIMER.clock_select = UNPACK(value, TAC_CLOCK_SELECT);
      TIMER.on = UNPACK(value, TAC_TIMER_ON);
      /* tima is incremented when a specific bit of div_counter transitions
       * from 1 to 0. This can happen as a result of writing to DIV, or in this
       * case modifying which bit we're looking at. */
      Bool tima_tick = FALSE;
      if (!old_timer_on) {
        u16 tima_mask = s_tima_mask[TIMER.clock_select];
        if (TIMER.on) {
          tima_tick = (TIMER.div_counter & old_tima_mask) != 0;
        } else {
          tima_tick = (TIMER.div_counter & old_tima_mask) != 0 &&
                      (TIMER.div_counter & tima_mask) == 0;
        }
        if (tima_tick) {
          increment_tima(e);
        }
      }
      break;
    }
    case IO_IF_ADDR:
      INTR.new_if = INTR.if_ = value;
      break;
    case IO_LCDC_ADDR: {
      Bool was_enabled = LCDC.display;
      LCDC.display = UNPACK(value, LCDC_DISPLAY);
      LCDC.window_tile_map_select = UNPACK(value, LCDC_WINDOW_TILE_MAP_SELECT);
      LCDC.window_display = UNPACK(value, LCDC_WINDOW_DISPLAY);
      LCDC.bg_tile_data_select = UNPACK(value, LCDC_BG_TILE_DATA_SELECT);
      LCDC.bg_tile_map_select = UNPACK(value, LCDC_BG_TILE_MAP_SELECT);
      LCDC.obj_size = UNPACK(value, LCDC_OBJ_SIZE);
      LCDC.obj_display = UNPACK(value, LCDC_OBJ_DISPLAY);
      LCDC.bg_display = UNPACK(value, LCDC_BG_DISPLAY);
      if (was_enabled ^ LCDC.display) {
        STAT.mode = PPU_MODE_HBLANK;
        PPU.ly = PPU.line_y = 0;
        if (LCDC.display) {
          HOOK0(enable_display_v);
          PPU.state = PPU_STATE_LCD_ON_MODE2;
          PPU.state_cycles = PPU_MODE2_CYCLES;
          PPU.line_cycles = PPU_LINE_CYCLES - CPU_MCYCLE;
          PPU.display_delay_frames = PPU_ENABLE_DISPLAY_DELAY_FRAMES;
          STAT.trigger_mode = PPU_MODE_MODE2;
        } else {
          HOOK0(disable_display_v);
          /* Clear the framebuffer. */
          size_t i;
          for (i = 0; i < ARRAY_SIZE(e->frame_buffer); ++i) {
            e->frame_buffer[i] = RGBA_WHITE;
          }
          PPU.new_frame_edge = TRUE;
        }
      }
      break;
    }
    case IO_STAT_ADDR: {
      if (LCDC.display) {
        Bool hblank = TRIGGER_MODE_IS(HBLANK) && !STAT.hblank.irq;
        Bool vblank = TRIGGER_MODE_IS(VBLANK) && !STAT.vblank.irq;
        Bool y_compare = STAT.new_ly_eq_lyc && !STAT.y_compare.irq;
        if (!STAT.if_ && (hblank || vblank || y_compare)) {
          HOOK(trigger_stat_from_write_cccii, y_compare ? 'Y' : '.',
               vblank ? 'V' : '.', hblank ? 'H' : '.', PPU.ly,
               CYCLES + CPU_MCYCLE);
          INTR.new_if |= IF_STAT;
          INTR.if_ |= IF_STAT;
          STAT.if_ = TRUE;
        }
      }
      STAT.y_compare.irq = UNPACK(value, STAT_YCOMPARE_INTR);
      STAT.mode2.irq = UNPACK(value, STAT_MODE2_INTR);
      STAT.vblank.irq = UNPACK(value, STAT_VBLANK_INTR);
      STAT.hblank.irq = UNPACK(value, STAT_HBLANK_INTR);
      break;
    }
    case IO_SCY_ADDR:
      PPU.scy = value;
      break;
    case IO_SCX_ADDR:
      PPU.scx = value;
      break;
    case IO_LY_ADDR:
      break;
    case IO_LYC_ADDR:
      PPU.lyc = value;
      if (LCDC.display) {
        check_ly_eq_lyc(e, TRUE);
        check_stat(e);
      }
      break;
    case IO_DMA_ADDR:
      /* DMA can be restarted. */
      DMA.state = (DMA.state != DMA_INACTIVE ? DMA.state : DMA_TRIGGERED);
      DMA.source = map_address(value << 8);
      DMA.cycles = 0;
      break;
    case IO_BGP_ADDR:
      PPU.bgp.color[3] = UNPACK(value, PALETTE_COLOR3);
      PPU.bgp.color[2] = UNPACK(value, PALETTE_COLOR2);
      PPU.bgp.color[1] = UNPACK(value, PALETTE_COLOR1);
      PPU.bgp.color[0] = UNPACK(value, PALETTE_COLOR0);
      break;
    case IO_OBP0_ADDR:
      PPU.obp[0].color[3] = UNPACK(value, PALETTE_COLOR3);
      PPU.obp[0].color[2] = UNPACK(value, PALETTE_COLOR2);
      PPU.obp[0].color[1] = UNPACK(value, PALETTE_COLOR1);
      PPU.obp[0].color[0] = UNPACK(value, PALETTE_COLOR0);
      break;
    case IO_OBP1_ADDR:
      PPU.obp[1].color[3] = UNPACK(value, PALETTE_COLOR3);
      PPU.obp[1].color[2] = UNPACK(value, PALETTE_COLOR2);
      PPU.obp[1].color[1] = UNPACK(value, PALETTE_COLOR1);
      PPU.obp[1].color[0] = UNPACK(value, PALETTE_COLOR0);
      break;
    case IO_WY_ADDR:
      PPU.wy = value;
      break;
    case IO_WX_ADDR:
      PPU.wx = value;
      break;
    case IO_IE_ADDR:
      INTR.ie = value;
      break;
    default:
      HOOK(write_io_ignored_as, addr, get_io_reg_string(addr), value);
      break;
  }
}

static void write_nrx1_reg(Emulator* e, Channel* channel, Address addr,
                           u8 value) {
  if (APU.enabled) {
    channel->square_wave.duty = UNPACK(value, NRX1_WAVE_DUTY);
  }
  channel->length = NRX1_MAX_LENGTH - UNPACK(value, NRX1_LENGTH);
  HOOK(write_nrx1_abi, addr, value, channel->length);
}

static void write_nrx2_reg(Emulator* e, Channel* channel, Address addr,
                           u8 value) {
  channel->envelope.initial_volume = UNPACK(value, NRX2_INITIAL_VOLUME);
  channel->dac_enabled = UNPACK(value, NRX2_DAC_ENABLED) != 0;
  if (!channel->dac_enabled) {
    channel->status = FALSE;
    HOOK(write_nrx2_disable_dac_ab, addr, value);
  }
  if (channel->status) {
    if (channel->envelope.period == 0 && channel->envelope.automatic) {
      u8 new_volume = (channel->envelope.volume + 1) & ENVELOPE_MAX_VOLUME;
      HOOK(write_nrx2_zombie_mode_abii, addr, value, channel->envelope.volume,
           new_volume);
      channel->envelope.volume = new_volume;
    }
  }
  channel->envelope.direction = UNPACK(value, NRX2_ENVELOPE_DIRECTION);
  channel->envelope.period = UNPACK(value, NRX2_ENVELOPE_PERIOD);
  HOOK(write_nrx2_initial_volume_abi, addr, value,
       channel->envelope.initial_volume);
}

static void write_nrx3_reg(Emulator* e, Channel* channel, u8 value) {
  channel->frequency = (channel->frequency & ~0xff) | value;
}

/* Returns TRUE if this channel was triggered. */
static Bool write_nrx4_reg(Emulator* e, Channel* channel, Address addr,
                           u8 value, u16 max_length) {
  Bool trigger = UNPACK(value, NRX4_INITIAL);
  Bool was_length_enabled = channel->length_enabled;
  channel->length_enabled = UNPACK(value, NRX4_LENGTH_ENABLED);
  channel->frequency &= 0xff;
  channel->frequency |= UNPACK(value, NRX4_FREQUENCY_HI) << 8;

  /* Extra length clocking occurs on NRX4 writes if the next APU frame isn't a
   * length counter frame. This only occurs on transition from disabled to
   * enabled. */
  Bool next_frame_is_length = (APU.frame & 1) == 1;
  if (!was_length_enabled && channel->length_enabled && !next_frame_is_length &&
      channel->length > 0) {
    channel->length--;
    HOOK(write_nrx4_extra_length_clock_abi, addr, value, channel->length);
    if (!trigger && channel->length == 0) {
      HOOK(write_nrx4_disable_channel_ab, addr, value);
      channel->status = FALSE;
    }
  }

  if (trigger) {
    if (channel->length == 0) {
      channel->length = max_length;
      if (channel->length_enabled && !next_frame_is_length) {
        channel->length--;
      }
      HOOK(write_nrx4_trigger_new_length_abi, addr, value, channel->length);
    }
    if (channel->dac_enabled) {
      channel->status = TRUE;
    }
  }

  HOOK(write_nrx4_info_abii, addr, value, trigger, channel->length_enabled);
  return trigger;
}

static void trigger_nrx4_envelope(Emulator* e, Envelope* envelope,
                                  Address addr) {
  envelope->volume = envelope->initial_volume;
  envelope->timer = envelope->period ? envelope->period : ENVELOPE_MAX_PERIOD;
  envelope->automatic = TRUE;
  /* If the next APU frame will update the envelope, increment the timer. */
  if (APU.frame + 1 == FRAME_SEQUENCER_UPDATE_ENVELOPE_FRAME) {
    envelope->timer++;
  }
  HOOK(trigger_nrx4_info_asii, addr, get_apu_reg_string(addr), envelope->volume,
       envelope->timer);
}

static u16 calculate_sweep_frequency(Emulator* e) {
  u16 f = SWEEP.frequency;
  if (SWEEP.direction == SWEEP_DIRECTION_ADDITION) {
    return f + (f >> SWEEP.shift);
  } else {
    SWEEP.calculated_subtract = TRUE;
    return f - (f >> SWEEP.shift);
  }
}

static void trigger_nr14_reg(Emulator* e, Channel* channel) {
  SWEEP.enabled = SWEEP.period || SWEEP.shift;
  SWEEP.frequency = channel->frequency;
  SWEEP.timer = SWEEP.period ? SWEEP.period : SWEEP_MAX_PERIOD;
  SWEEP.calculated_subtract = FALSE;
  if (SWEEP.shift && calculate_sweep_frequency(e) > SOUND_MAX_FREQUENCY) {
    channel->status = FALSE;
    HOOK0(trigger_nr14_sweep_overflow_v);
  } else {
    HOOK(trigger_nr14_info_i, SWEEP.frequency);
  }
}

static void write_wave_period(Emulator* e, Channel* channel) {
  WAVE.period = ((SOUND_MAX_FREQUENCY + 1) - channel->frequency) * 2;
  HOOK(write_wave_period_info_iii, channel->frequency, WAVE.cycles,
       WAVE.period);
}

static void write_square_wave_period(Emulator* e, Channel* channel,
                                     SquareWave* square) {
  square->period = ((SOUND_MAX_FREQUENCY + 1) - channel->frequency) * 4;
  HOOK(write_square_wave_period_info_iii, channel->frequency, square->cycles,
       square->period);
}

static void write_noise_period(Emulator* e) {
  static const u8 s_divisors[NOISE_DIVISOR_COUNT] = {8,  16, 32, 48,
                                                     64, 80, 96, 112};
  u8 divisor = s_divisors[NOISE.divisor];
  assert(NOISE.divisor < NOISE_DIVISOR_COUNT);
  NOISE.period = divisor << NOISE.clock_shift;
  HOOK(write_noise_period_info_iii, divisor, NOISE.clock_shift, NOISE.period);
}

static void write_apu(Emulator* e, MaskedAddress addr, u8 value) {
  if (!APU.enabled) {
    if (!e->state.is_cgb && (addr == APU_NR11_ADDR || addr == APU_NR21_ADDR ||
                             addr == APU_NR31_ADDR || addr == APU_NR41_ADDR)) {
      /* DMG allows writes to the length counters when power is disabled. */
    } else if (addr == APU_NR52_ADDR) {
      /* Always can write to NR52; it's necessary to re-enable power to APU. */
    } else {
      /* Ignore all other writes. */
      HOOK(write_apu_disabled_asb, addr, get_apu_reg_string(addr), value);
      return;
    }
  }

  if (APU.initialized) {
    apu_synchronize(e);
  }

  HOOK(write_apu_asb, addr, get_apu_reg_string(addr), value);
  switch (addr) {
    case APU_NR10_ADDR: {
      SweepDirection old_direction = SWEEP.direction;
      SWEEP.period = UNPACK(value, NR10_SWEEP_PERIOD);
      SWEEP.direction = UNPACK(value, NR10_SWEEP_DIRECTION);
      SWEEP.shift = UNPACK(value, NR10_SWEEP_SHIFT);
      if (old_direction == SWEEP_DIRECTION_SUBTRACTION &&
          SWEEP.direction == SWEEP_DIRECTION_ADDITION &&
          SWEEP.calculated_subtract) {
        CHANNEL1.status = FALSE;
      }
      break;
    }
    case APU_NR11_ADDR:
      write_nrx1_reg(e, &CHANNEL1, addr, value);
      break;
    case APU_NR12_ADDR:
      write_nrx2_reg(e, &CHANNEL1, addr, value);
      break;
    case APU_NR13_ADDR:
      write_nrx3_reg(e, &CHANNEL1, value);
      write_square_wave_period(e, &CHANNEL1, &CHANNEL1.square_wave);
      break;
    case APU_NR14_ADDR: {
      Bool trigger = write_nrx4_reg(e, &CHANNEL1, addr, value, NRX1_MAX_LENGTH);
      write_square_wave_period(e, &CHANNEL1, &CHANNEL1.square_wave);
      if (trigger) {
        trigger_nrx4_envelope(e, &CHANNEL1.envelope, addr);
        trigger_nr14_reg(e, &CHANNEL1);
        CHANNEL1.square_wave.cycles = CHANNEL1.square_wave.period;
      }
      break;
    }
    case APU_NR21_ADDR:
      write_nrx1_reg(e, &CHANNEL2, addr, value);
      break;
    case APU_NR22_ADDR:
      write_nrx2_reg(e, &CHANNEL2, addr, value);
      break;
    case APU_NR23_ADDR:
      write_nrx3_reg(e, &CHANNEL2, value);
      write_square_wave_period(e, &CHANNEL2, &CHANNEL2.square_wave);
      break;
    case APU_NR24_ADDR: {
      Bool trigger = write_nrx4_reg(e, &CHANNEL2, addr, value, NRX1_MAX_LENGTH);
      write_square_wave_period(e, &CHANNEL2, &CHANNEL2.square_wave);
      if (trigger) {
        trigger_nrx4_envelope(e, &CHANNEL2.envelope, addr);
        CHANNEL2.square_wave.cycles = CHANNEL2.square_wave.period;
      }
      break;
    }
    case APU_NR30_ADDR:
      CHANNEL3.dac_enabled = UNPACK(value, NR30_DAC_ENABLED);
      if (!CHANNEL3.dac_enabled) {
        CHANNEL3.status = FALSE;
        WAVE.playing = FALSE;
      }
      break;
    case APU_NR31_ADDR:
      CHANNEL3.length = NR31_MAX_LENGTH - value;
      break;
    case APU_NR32_ADDR:
      WAVE.volume = UNPACK(value, NR32_SELECT_WAVE_VOLUME);
      assert(WAVE.volume < WAVE_VOLUME_COUNT);
      WAVE.volume_shift = s_wave_volume_shift[WAVE.volume];
      break;
    case APU_NR33_ADDR:
      write_nrx3_reg(e, &CHANNEL3, value);
      write_wave_period(e, &CHANNEL3);
      break;
    case APU_NR34_ADDR: {
      Bool trigger = write_nrx4_reg(e, &CHANNEL3, addr, value, NR31_MAX_LENGTH);
      write_wave_period(e, &CHANNEL3);
      if (trigger) {
        if (!e->state.is_cgb && WAVE.playing) {
          /* Triggering the wave channel while it is already playing will
           * corrupt the wave RAM on DMG. */
          if (WAVE.cycles == WAVE_TRIGGER_CORRUPTION_OFFSET_CYCLES) {
            assert(WAVE.position < 32);
            u8 position = (WAVE.position + 1) & 31;
            u8 byte = WAVE.ram[position >> 1];
            switch (position >> 3) {
              case 0:
                WAVE.ram[0] = byte;
                break;
              case 1:
              case 2:
              case 3:
                memcpy(&WAVE.ram[0], &WAVE.ram[(position >> 1) & 12], 4);
                break;
            }
            HOOK(corrupt_wave_ram_i, position);
          }
        }

        WAVE.position = 0;
        WAVE.cycles = WAVE.period + WAVE_TRIGGER_DELAY_CYCLES;
        WAVE.playing = TRUE;
      }
      break;
    }
    case APU_NR41_ADDR:
      write_nrx1_reg(e, &CHANNEL4, addr, value);
      break;
    case APU_NR42_ADDR:
      write_nrx2_reg(e, &CHANNEL4, addr, value);
      break;
    case APU_NR43_ADDR: {
      NOISE.clock_shift = UNPACK(value, NR43_CLOCK_SHIFT);
      NOISE.lfsr_width = UNPACK(value, NR43_LFSR_WIDTH);
      NOISE.divisor = UNPACK(value, NR43_DIVISOR);
      write_noise_period(e);
      break;
    }
    case APU_NR44_ADDR: {
      Bool trigger = write_nrx4_reg(e, &CHANNEL4, addr, value, NRX1_MAX_LENGTH);
      if (trigger) {
        write_noise_period(e);
        trigger_nrx4_envelope(e, &CHANNEL4.envelope, addr);
        NOISE.lfsr = 0x7fff;
        NOISE.cycles = NOISE.period;
      }
      break;
    }
    case APU_NR50_ADDR:
      APU.so_output[VIN][1] = UNPACK(value, NR50_VIN_SO2);
      APU.so_volume[1] = UNPACK(value, NR50_SO2_VOLUME);
      APU.so_output[VIN][0] = UNPACK(value, NR50_VIN_SO1);
      APU.so_volume[0] = UNPACK(value, NR50_SO1_VOLUME);
      break;
    case APU_NR51_ADDR:
      APU.so_output[SOUND4][1] = UNPACK(value, NR51_SOUND4_SO2);
      APU.so_output[SOUND3][1] = UNPACK(value, NR51_SOUND3_SO2);
      APU.so_output[SOUND2][1] = UNPACK(value, NR51_SOUND2_SO2);
      APU.so_output[SOUND1][1] = UNPACK(value, NR51_SOUND1_SO2);
      APU.so_output[SOUND4][0] = UNPACK(value, NR51_SOUND4_SO1);
      APU.so_output[SOUND3][0] = UNPACK(value, NR51_SOUND3_SO1);
      APU.so_output[SOUND2][0] = UNPACK(value, NR51_SOUND2_SO1);
      APU.so_output[SOUND1][0] = UNPACK(value, NR51_SOUND1_SO1);
      break;
    case APU_NR52_ADDR: {
      Bool was_enabled = APU.enabled;
      Bool is_enabled = UNPACK(value, NR52_ALL_SOUND_ENABLED);
      if (was_enabled && !is_enabled) {
        HOOK0(apu_power_down_v);
        int i;
        for (i = 0; i < APU_REG_COUNT; ++i) {
          if (i != APU_NR52_ADDR) {
            write_apu(e, i, 0);
          }
        }
      } else if (!was_enabled && is_enabled) {
        HOOK0(apu_power_up_v);
        APU.frame = 7;
      }
      APU.enabled = is_enabled;
      break;
    }
  }
}

static void write_wave_ram(Emulator* e, MaskedAddress addr, u8 value) {
  apu_synchronize(e);
  if (CHANNEL3.status) {
    /* If the wave channel is playing, the byte is written to the sample
     * position. On DMG, this is only allowed if the write occurs exactly when
     * it is being accessed by the Wave channel. */
    if (e->state.is_cgb || CYCLES == WAVE.sample_time) {
      WAVE.ram[WAVE.position >> 1] = value;
      HOOK(write_wave_ram_while_playing_ab, addr, value);
    }
  } else {
    WAVE.ram[addr] = value;
    HOOK(write_wave_ram_ab, addr, value);
  }
}

void write_u8(Emulator* e, Address addr, u8 value) {
  MemoryTypeAddressPair pair = map_address(addr);
  if (!is_dma_access_ok(e, pair)) {
    HOOK(write_during_dma_ab, addr, value);
    return;
  }

  switch (pair.type) {
    case MEMORY_MAP_ROM0:
      e->memory_map.write_rom(e, pair.addr, value);
      break;
    case MEMORY_MAP_ROM1:
      e->memory_map.write_rom(e, pair.addr + 0x4000, value);
      break;
    case MEMORY_MAP_VRAM:
      write_vram(e, pair.addr, value);
      break;
    case MEMORY_MAP_EXT_RAM:
      e->memory_map.write_ext_ram(e, pair.addr, value);
      break;
    case MEMORY_MAP_WORK_RAM0:
      WRAM[pair.addr] = value;
      break;
    case MEMORY_MAP_WORK_RAM1:
      WRAM[0x1000 + pair.addr] = value;
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
      HRAM[pair.addr] = value;
      break;
  }
}

static void ppu_mode2_mcycle(Emulator* e) {
  if (!LCDC.obj_display || e->config.disable_obj ||
      PPU.line_obj_count >= OBJ_PER_LINE_COUNT) {
    return;
  }

  /* 80 cycles / 40 sprites == 2 cycles/sprite == 2 sprites per M-cycle. */
  int i;
  u8 obj_height = s_obj_size_to_height[LCDC.obj_size];
  u8 y = PPU.line_y;
  for (i = 0; i < 2 && PPU.line_obj_count < OBJ_PER_LINE_COUNT; ++i) {
    /* Put the visible sprites into line_obj, but insert them so sprites with
     * smaller X-coordinates are earlier. */
    Obj* o = &OAM[PPU.oam_index];
    u8 rel_y = y - o->y;
    if (rel_y < obj_height) {
      int j = PPU.line_obj_count;
      while (j > 0 && o->x < PPU.line_obj[j - 1].x) {
        PPU.line_obj[j] = PPU.line_obj[j - 1];
        j--;
      }
      PPU.line_obj[j] = *o;
      PPU.line_obj_count++;
    }
    PPU.oam_index++;
  }
}

static u32 mode3_cycle_count(Emulator* e) {
  s32 buckets[SCREEN_WIDTH / 8 + 2];
  ZERO_MEMORY(buckets);
  u8 scx_fine = PPU.scx & 7;
  u32 cycles = PPU_MODE3_MIN_CYCLES + scx_fine;
  Bool has_zero = FALSE;
  int i;
  for (i = 0; i < PPU.line_obj_count; ++i) {
    Obj* o = &PPU.line_obj[i];
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
  u8 x = PPU.render_x;
  u8 y = PPU.line_y;
  if (x + 4 > SCREEN_WIDTH) {
    return;
  }
  /* Each M-cycle writes 4 pixels. */
  Color pixels[4];
  ZERO_MEMORY(pixels);
  Bool bg_is_zero[4];
  memset(bg_is_zero, TRUE, sizeof(bg_is_zero));

  TileDataSelect data_select = LCDC.bg_tile_data_select;
  for (i = 0; i < 4; ++i) {
    u8 xi = x + i;
    PPU.rendering_window = PPU.rendering_window ||
                           (LCDC.window_display && !e->config.disable_window &&
                            xi + WINDOW_X_OFFSET >= PPU.wx &&
                            PPU.wx <= WINDOW_MAX_X && y >= PPU.frame_wy);
    Bool display_bg = LCDC.bg_display && !e->config.disable_bg;
    if (PPU.rendering_window || display_bg) {
      TileMapSelect map_select;
      u8 mx, my;
      if (PPU.rendering_window) {
        map_select = LCDC.window_tile_map_select;
        mx = xi + WINDOW_X_OFFSET - PPU.wx;
        my = PPU.win_y;
      } else {
        map_select = LCDC.bg_tile_map_select;
        mx = PPU.scx + xi;
        my = PPU.scy + y;
      }
      u8* map = &VRAM[map_select == TILE_MAP_9800_9BFF ? 0x1800 : 0x1C00];
      u16 tile_index = map[((my >> 3) * TILE_MAP_WIDTH) | (mx >> 3)];
      if (data_select == TILE_DATA_8800_97FF) {
        tile_index = 256 + (s8)tile_index;
      }
      u16 tile_addr = (tile_index * TILE_HEIGHT + (my & 7)) * TILE_ROW_BYTES;
      u8 lo = VRAM[tile_addr];
      u8 hi = VRAM[tile_addr + 1];
      u8 shift = 7 - (mx & 7);
      u8 palette_index = (((hi >> shift) & 1) << 1) | ((lo >> shift) & 1);
      pixels[i] = PPU.bgp.color[palette_index];
      bg_is_zero[i] = palette_index == 0;
    }
  }

  if (LCDC.obj_display && !e->config.disable_obj) {
    u8 obj_height = s_obj_size_to_height[LCDC.obj_size];
    int n;
    for (n = PPU.line_obj_count - 1; n >= 0; --n) {
      Obj* o = &PPU.line_obj[n];
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
      u8 lo = VRAM[tile_addr];
      u8 hi = VRAM[tile_addr + 1];
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
          pixels[i] = PPU.obp[o->palette].color[palette_index];
        }
      }
    }
  }

  RGBA* rgba_pixels = &e->frame_buffer[y * SCREEN_WIDTH + x];
  for (i = 0; i < 4; ++i) {
    rgba_pixels[i] = s_color_to_rgba[pixels[i]];
  }

  PPU.render_x += 4;
}

static void ppu_mcycle(Emulator* e) {
  if (!LCDC.display) {
    return;
  }

  STAT.mode2.trigger = FALSE;
  STAT.y_compare.trigger = FALSE;
  STAT.ly_eq_lyc = STAT.new_ly_eq_lyc;
  PPU.last_ly = PPU.ly;

  switch (STAT.mode) {
    case PPU_MODE_MODE2: ppu_mode2_mcycle(e); break;
    case PPU_MODE_MODE3: ppu_mode3_mcycle(e); break;
    default: break;
  }

  PPU.state_cycles -= CPU_MCYCLE;
  PPU.line_cycles -= CPU_MCYCLE;
  if (PPU.state_cycles == 0) {
    switch (PPU.state) {
      case PPU_STATE_HBLANK:
      case PPU_STATE_VBLANK_PLUS_4:
        PPU.line_y++;
        PPU.ly++;
        PPU.line_cycles = PPU_LINE_CYCLES;
        check_ly_eq_lyc(e, FALSE);
        PPU.state_cycles = CPU_MCYCLE;

        if (PPU.state == PPU_STATE_HBLANK) {
          STAT.mode2.trigger = TRUE;
          if (PPU.ly == SCREEN_HEIGHT) {
            PPU.state = PPU_STATE_VBLANK;
            STAT.trigger_mode = PPU_MODE_VBLANK;
            PPU.frame++;
            INTR.new_if |= IF_VBLANK;
            if (PPU.display_delay_frames == 0) {
              PPU.new_frame_edge = TRUE;
            } else {
              PPU.display_delay_frames--;
            }
          } else {
            PPU.state = PPU_STATE_HBLANK_PLUS_4;
            STAT.trigger_mode = PPU_MODE_MODE2;
            if (PPU.rendering_window) {
              PPU.win_y++;
            }
          }
        } else {
          assert(PPU.state == PPU_STATE_VBLANK_PLUS_4);
          if (PPU.ly == SCREEN_HEIGHT_WITH_VBLANK - 1) {
            PPU.state = PPU_STATE_VBLANK_LY_0;
          } else {
            PPU.state_cycles = PPU_LINE_CYCLES;
          }
        }
        check_stat(e);
        break;

      case PPU_STATE_HBLANK_PLUS_4:
        PPU.state = PPU_STATE_MODE2;
        PPU.state_cycles = PPU_MODE2_CYCLES;
        STAT.mode = PPU_MODE_MODE2;
        PPU.oam_index = 0;
        PPU.line_obj_count = 0;
        break;

      case PPU_STATE_VBLANK:
        PPU.state = PPU_STATE_VBLANK_PLUS_4;
        PPU.state_cycles = PPU_LINE_CYCLES - CPU_MCYCLE;
        STAT.mode = PPU_MODE_VBLANK;
        check_stat(e);
        break;

      case PPU_STATE_VBLANK_LY_0:
        PPU.state = PPU_STATE_VBLANK_LY_0_PLUS_4;
        PPU.state_cycles = CPU_MCYCLE;
        PPU.ly = 0;
        break;

      case PPU_STATE_VBLANK_LY_0_PLUS_4:
        PPU.state = PPU_STATE_VBLANK_LINE_Y_0;
        PPU.state_cycles = PPU_LINE_CYCLES - CPU_MCYCLE - CPU_MCYCLE;
        check_ly_eq_lyc(e, FALSE);
        check_stat(e);
        break;

      case PPU_STATE_VBLANK_LINE_Y_0:
        PPU.state = PPU_STATE_HBLANK_PLUS_4;
        PPU.state_cycles = CPU_MCYCLE;
        PPU.line_cycles = PPU_LINE_CYCLES;
        PPU.line_y = 0;
        PPU.frame_wy = PPU.wy;
        PPU.win_y = 0;
        STAT.mode2.trigger = TRUE;
        STAT.mode = PPU_MODE_HBLANK;
        STAT.trigger_mode = PPU_MODE_MODE2;
        check_stat(e);
        break;

      case PPU_STATE_LCD_ON_MODE2:
      case PPU_STATE_MODE2:
        PPU.state_cycles = mode3_cycle_count(e);
        if (PPU.state == PPU_STATE_LCD_ON_MODE2 ||
            (PPU.state_cycles & 3) != 0) {
          PPU.state = PPU_STATE_MODE3;
        } else {
          PPU.state = PPU_STATE_MODE3_EARLY_TRIGGER;
          PPU.state_cycles--;
        }
        PPU.state_cycles &= ~3;
        STAT.mode = STAT.trigger_mode = PPU_MODE_MODE3;
        PPU.render_x = 0;
        PPU.rendering_window = FALSE;
        check_stat(e);
        break;

      case PPU_STATE_MODE3_EARLY_TRIGGER:
        PPU.state = PPU_STATE_MODE3_COMMON;
        PPU.state_cycles = CPU_MCYCLE;
        STAT.trigger_mode = PPU_MODE_HBLANK;
        check_stat(e);
        break;

      case PPU_STATE_MODE3:
        STAT.trigger_mode = PPU_MODE_HBLANK;
        /* fallthrough */

      case PPU_STATE_MODE3_COMMON:
        PPU.state = PPU_STATE_HBLANK;
        PPU.state_cycles = PPU.line_cycles;
        STAT.mode = PPU_MODE_HBLANK;
        check_stat(e);
        break;

      case PPU_STATE_COUNT:
        assert(0);
        break;
    }
  }
}

static void update_sweep(Emulator* e) {
  if (!(CHANNEL1.status && SWEEP.enabled)) {
    return;
  }

  u8 period = SWEEP.period;
  if (--SWEEP.timer == 0) {
    if (period) {
      SWEEP.timer = period;
      u16 new_frequency = calculate_sweep_frequency(e);
      if (new_frequency > SOUND_MAX_FREQUENCY) {
        HOOK0(sweep_overflow_v);
        CHANNEL1.status = FALSE;
      } else {
        if (SWEEP.shift) {
          HOOK(sweep_update_frequency_i, new_frequency);
          SWEEP.frequency = CHANNEL1.frequency = new_frequency;
          write_square_wave_period(e, &CHANNEL1, &CHANNEL1.square_wave);
        }

        /* Perform another overflow check. */
        if (calculate_sweep_frequency(e) > SOUND_MAX_FREQUENCY) {
          HOOK0(sweep_overflow_2nd_v);
          CHANNEL1.status = FALSE;
        }
      }
    } else {
      SWEEP.timer = SWEEP_MAX_PERIOD;
    }
  }
}

static void update_lengths(Emulator* e) {
  int i;
  for (i = 0; i < APU_CHANNEL_COUNT; ++i) {
    Channel* channel = &APU.channel[i];
    if (channel->length_enabled && channel->length > 0) {
      if (--channel->length == 0) {
        channel->status = FALSE;
      }
    }
  }
}

static void update_envelopes(Emulator* e) {
  int i;
  for (i = 0; i < APU_CHANNEL_COUNT; ++i) {
    Envelope* envelope = &APU.channel[i].envelope;
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
}

/* Convert from 1-bit sample to 4-bit sample. */
#define CHANNELX_SAMPLE(channel, sample) \
  (-(sample) & (channel)->envelope.volume)

static void update_square_wave(Channel* channel, u32 total_frames) {
  static u8 duty[WAVE_DUTY_COUNT][DUTY_CYCLE_COUNT] =
      {[WAVE_DUTY_12_5] = {0, 0, 0, 0, 0, 0, 0, 1},
       [WAVE_DUTY_25] = {1, 0, 0, 0, 0, 0, 0, 1},
       [WAVE_DUTY_50] = {1, 0, 0, 0, 0, 1, 1, 1},
       [WAVE_DUTY_75] = {0, 1, 1, 1, 1, 1, 1, 0}};
  SquareWave* square = &channel->square_wave;
  if (channel->status) {
    while (total_frames) {
      u32 frames = square->cycles / APU_CYCLES;
      u8 sample = CHANNELX_SAMPLE(channel, square->sample);
      if (frames <= total_frames) {
        square->cycles = square->period;
        square->position = (square->position + 1) % DUTY_CYCLE_COUNT;
        square->sample = duty[square->duty][square->position];
      } else {
        frames = total_frames;
        square->cycles -= frames * APU_CYCLES;
      }
      channel->accumulator += sample * frames;
      total_frames -= frames;
    }
  }
}

static void update_wave(Emulator* e, u32 apu_cycles, u32 total_frames) {
  if (CHANNEL3.status) {
    while (total_frames) {
      u32 frames = WAVE.cycles / APU_CYCLES;
      /* Modulate 4-bit sample by wave volume. */
      u8 sample = WAVE.sample_data >> WAVE.volume_shift;
      if (frames <= total_frames) {
        WAVE.position = (WAVE.position + 1) % WAVE_SAMPLE_COUNT;
        WAVE.sample_time = apu_cycles + WAVE.cycles;
        u8 byte = WAVE.ram[WAVE.position >> 1];
        if ((WAVE.position & 1) == 0) {
          WAVE.sample_data = byte >> 4; /* High nybble. */
        } else {
          WAVE.sample_data = byte & 0x0f; /* Low nybble. */
        }
        WAVE.cycles = WAVE.period;
        HOOK(wave_update_position_iii, WAVE.position, WAVE.sample_data,
             WAVE.sample_time);
      } else {
        frames = total_frames;
        WAVE.cycles -= frames * APU_CYCLES;
      }
      apu_cycles += frames * APU_CYCLES;
      CHANNEL3.accumulator += sample * frames;
      total_frames -= frames;
    }
  }
}

static void update_noise(Emulator* e, u32 total_frames) {
  if (CHANNEL4.status && NOISE.clock_shift <= NOISE_MAX_CLOCK_SHIFT) {
    while (total_frames) {
      u32 frames = NOISE.cycles / APU_CYCLES;
      u8 sample = CHANNELX_SAMPLE(&CHANNEL4, NOISE.sample);
      if (frames <= total_frames) {
        u16 bit = (NOISE.lfsr ^ (NOISE.lfsr >> 1)) & 1;
        if (NOISE.lfsr_width == LFSR_WIDTH_7) {
          NOISE.lfsr = ((NOISE.lfsr >> 1) & ~0x40) | (bit << 6);
        } else {
          NOISE.lfsr = ((NOISE.lfsr >> 1) & ~0x4000) | (bit << 14);
        }
        NOISE.sample = ~NOISE.lfsr & 1;
        NOISE.cycles = NOISE.period;
      } else {
        frames = total_frames;
        NOISE.cycles -= frames * APU_CYCLES;
      }
      CHANNEL4.accumulator += sample * frames;
      total_frames -= frames;
    }
  }
}

static u32 get_gb_frames_until_next_resampled_frame(Emulator* e) {
  u32 result = 0;
  u32 counter = e->audio_buffer.freq_counter;
  while (!VALUE_WRAPPED(counter, APU_CYCLES_PER_SECOND)) {
    counter += e->audio_buffer.frequency;
    result++;
  }
  return result;
}

static void write_audio_frame(Emulator* e, u32 gb_frames) {
  int i, j;
  AudioBuffer* buffer = &e->audio_buffer;
  buffer->divisor += gb_frames;
  buffer->freq_counter += buffer->frequency * gb_frames;
  if (VALUE_WRAPPED(buffer->freq_counter, APU_CYCLES_PER_SECOND)) {
    for (i = 0; i < SOUND_OUTPUT_COUNT; ++i) {
      u32 accumulator = 0;
      for (j = 0; j < APU_CHANNEL_COUNT; ++j) {
        if (!e->config.disable_sound[j]) {
          accumulator += APU.channel[j].accumulator * APU.so_output[j][i];
        }
      }
      accumulator *= (APU.so_volume[i] + 1) * 16; /* 4bit -> 8bit samples. */
      accumulator /= ((SOUND_OUTPUT_MAX_VOLUME + 1) * APU_CHANNEL_COUNT);
      *buffer->position++ = accumulator / buffer->divisor;
    }
    for (j = 0; j < APU_CHANNEL_COUNT; ++j) {
      APU.channel[j].accumulator = 0;
    }
    buffer->divisor = 0;
  }
  assert(buffer->position <= buffer->end);
}

static void apu_update_channels(Emulator* e, u32 total_frames) {
  while (total_frames) {
    u32 frames = get_gb_frames_until_next_resampled_frame(e);
    frames = MIN(frames, total_frames);
    update_square_wave(&CHANNEL1, frames);
    update_square_wave(&CHANNEL2, frames);
    update_wave(e, APU.cycles, frames);
    update_noise(e, frames);
    write_audio_frame(e, frames);
    APU.cycles += frames * APU_CYCLES;
    total_frames -= frames;
  }
}

static void apu_update(Emulator* e, u32 total_cycles) {
  while (total_cycles) {
    Cycles next_seq_cycles = NEXT_MODULO(APU.cycles, FRAME_SEQUENCER_CYCLES);
    if (next_seq_cycles == FRAME_SEQUENCER_CYCLES) {
      APU.frame = (APU.frame + 1) % FRAME_SEQUENCER_COUNT;
      switch (APU.frame) {
        case 2: case 6: update_sweep(e); /* Fallthrough. */
        case 0: case 4: update_lengths(e); break;
        case 7: update_envelopes(e); break;
      }
    }
    Cycles cycles = MIN(next_seq_cycles, total_cycles);
    apu_update_channels(e, cycles / APU_CYCLES);
    total_cycles -= cycles;
  }
}

static void apu_synchronize(Emulator* e) {
  if (APU.cycles != CYCLES) {
    u32 cycles = CYCLES - APU.cycles;
    if (APU.enabled) {
      apu_update(e, cycles);
      assert(APU.cycles == CYCLES);
    } else {
      for (; cycles; cycles -= APU_CYCLES) {
        write_audio_frame(e, 1);
      }
      APU.cycles = CYCLES;
    }
  }
}

static void dma_mcycle(Emulator* e) {
  if (DMA.state == DMA_INACTIVE) {
    return;
  }
  if (DMA.cycles < DMA_DELAY_CYCLES) {
    DMA.cycles += CPU_MCYCLE;
    if (DMA.cycles >= DMA_DELAY_CYCLES) {
      DMA.cycles = DMA_DELAY_CYCLES;
      DMA.state = DMA_ACTIVE;
    }
    return;
  }

  u8 addr_offset = (DMA.cycles - DMA_DELAY_CYCLES) >> 2;
  assert(addr_offset < OAM_TRANSFER_SIZE);
  MemoryTypeAddressPair pair = DMA.source;
  pair.addr += addr_offset;
  u8 value = read_u8_no_dma_check(e, pair);
  write_oam_no_mode_check(e, addr_offset, value);
  DMA.cycles += CPU_MCYCLE;
  if (VALUE_WRAPPED(DMA.cycles, DMA_CYCLES)) {
    DMA.state = DMA_INACTIVE;
  }
}

static void timer_mcycle(Emulator* e) {
  if (TIMER.on) {
    if (TIMER.tima_state == TIMA_STATE_OVERFLOW) {
      TIMER.tima_state = TIMA_STATE_RESET;
      TIMER.tima = TIMER.tma;
    } else if (TIMER.tima_state == TIMA_STATE_RESET) {
      TIMER.tima_state = TIMA_STATE_NORMAL;
    }
  }
  write_div_counter(e, TIMER.div_counter + CPU_MCYCLE);
}

static void serial_mcycle(Emulator* e) {
  if (!SERIAL.transferring) {
    return;
  }
  if (SERIAL.clock == SERIAL_CLOCK_INTERNAL) {
    SERIAL.cycles += CPU_MCYCLE;
    if (VALUE_WRAPPED(SERIAL.cycles, SERIAL_CYCLES)) {
      /* Since we're never connected to another device, always shift in 0xff. */
      SERIAL.sb = (SERIAL.sb << 1) | 1;
      SERIAL.transferred_bits++;
      if (VALUE_WRAPPED(SERIAL.transferred_bits, 8)) {
        SERIAL.transferring = 0;
        INTR.new_if |= IF_SERIAL;
      }
    }
  }
}

static void mcycle(Emulator* e) {
  INTR.if_ = INTR.new_if;
  dma_mcycle(e);
  ppu_mcycle(e);
  timer_mcycle(e);
  serial_mcycle(e);
  CYCLES += CPU_MCYCLE;
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
  write_u8_cy(e, addr, (u8)value);
}

static u16 get_af_reg(Emulator* e) {
  return (REG.A << 8) | PACK(REG.F.Z, CPU_FLAG_Z) | PACK(REG.F.N, CPU_FLAG_N) |
         PACK(REG.F.H, CPU_FLAG_H) | PACK(REG.F.C, CPU_FLAG_C);
}

static void set_af_reg(Emulator* e, u16 af) {
  REG.A = af >> 8;
  REG.F.Z = UNPACK(af, CPU_FLAG_Z);
  REG.F.N = UNPACK(af, CPU_FLAG_N);
  REG.F.H = UNPACK(af, CPU_FLAG_H);
  REG.F.C = UNPACK(af, CPU_FLAG_C);
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

#define CY mcycle(e)
#define RA REG.A
#define RSP REG.SP
#define FZ REG.F.Z
#define FC REG.F.C
#define FH REG.F.H
#define FN REG.F.N
#define FZ_EQ0(X) FZ = (u8)(X) == 0
#define MASK8(X) ((X) & 0xf)
#define MASK16(X) ((X) & 0xfff)
#define READ8(X) read_u8_cy(e, X)
#define READ16(X) read_u16_cy(e, X)
#define WRITE8(X, V) write_u8_cy(e, X, V)
#define WRITE16(X, V) write_u16_cy(e, X, V)
#define READ_N READ8(REG.PC + 1)
#define READ_NN READ16(REG.PC + 1)
#define READMR(MR) READ8(REG.MR)
#define WRITEMR(MR, V) WRITE8(REG.MR, V)
#define BASIC_OP_R(R, OP) u = REG.R; OP; REG.R = u
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
#define ADD_R(R) ADD_FLAGS(RA, REG.R); RA += REG.R
#define ADD_MR(MR) u = READMR(MR); ADD_FLAGS(RA, u); RA += u
#define ADD_N u = READ_N; ADD_FLAGS(RA, u); RA += u
#define ADD_HL_RR(RR) CY; ADD_FLAGS16(REG.HL, REG.RR); REG.HL += REG.RR
#define ADD_SP_N s = (s8)READ_N; ADD_SP_FLAGS(s); RSP += s; CY; CY
#define FC_ADC(X, Y, C) FC = ((X) + (Y) + (C) > 0xff)
#define FH_ADC(X, Y, C) FH = (MASK8(X) + MASK8(Y) + C > 0xf)
#define FCH_ADC(X, Y, C) FC_ADC(X, Y, C); FH_ADC(X, Y, C)
#define ADC_FLAGS(X, Y, C) FZ_EQ0((X) + (Y) + (C)); FN = 0; FCH_ADC(X, Y, C)
#define ADC_R(R) u = REG.R; c = FC; ADC_FLAGS(RA, u, c); RA += u + c
#define ADC_MR(MR) u = READMR(MR); c = FC; ADC_FLAGS(RA, u, c); RA += u + c
#define ADC_N u = READ_N; c = FC; ADC_FLAGS(RA, u, c); RA += u + c
#define AND_FLAGS FZ_EQ0(RA); FH = 1; FN = FC = 0
#define AND_R(R) RA &= REG.R; AND_FLAGS
#define AND_MR(MR) RA &= READMR(MR); AND_FLAGS
#define AND_N RA &= READ_N; AND_FLAGS
#define BIT_FLAGS(BIT, X) FZ_EQ0((X) & (1 << (BIT))); FN = 0; FH = 1
#define BIT_R(BIT, R) u = REG.R; BIT_FLAGS(BIT, u)
#define BIT_MR(BIT, MR) u = READMR(MR); BIT_FLAGS(BIT, u)
#define CALL(X) CY; RSP -= 2; WRITE16(RSP, new_pc); new_pc = X
#define CALL_NN u16 = READ_NN; CALL(u16)
#define CALL_F_NN(COND) u16 = READ_NN; if (COND) { CALL(u16); }
#define CCF FC ^= 1; FN = FH = 0
#define CP_FLAGS(X, Y) FZ_EQ0((X) - (Y)); FN = 1; FCH_SUB(X, Y)
#define CP_R(R) CP_FLAGS(RA, REG.R)
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
#define DEC_RR(RR) REG.RR--; CY
#define DEC_MR(MR) BASIC_OP_MR(MR, DEC); DEC_FLAGS
#define DI INTR.ime = INTR.enable = FALSE;
#define EI INTR.enable = TRUE;
#define HALT                                   \
  if (INTR.ime) {                              \
    INTR.halt = TRUE;                          \
  } else if (INTR.ie & INTR.new_if & IF_ALL) { \
    INTR.halt_bug = TRUE;                      \
  } else {                                     \
    INTR.halt = TRUE;                          \
    INTR.halt_di = TRUE;                       \
  }
#define INC u++
#define INC_FLAGS FZ_EQ0(u); FN = 0; FH = MASK8(u) == 0
#define INC_R(R) BASIC_OP_R(R, INC); INC_FLAGS
#define INC_RR(RR) REG.RR++; CY
#define INC_MR(MR) BASIC_OP_MR(MR, INC); INC_FLAGS
#define JP_F_NN(COND) u16 = READ_NN; if (COND) { new_pc = u16; CY; }
#define JP_RR(RR) new_pc = REG.RR
#define JP_NN new_pc = READ_NN; CY
#define JR new_pc += s; CY
#define JR_F_N(COND) s = READ_N; if (COND) { JR; }
#define JR_N s = READ_N; JR
#define LD_R_R(RD, RS) REG.RD = REG.RS
#define LD_R_N(R) REG.R = READ_N
#define LD_RR_RR(RRD, RRS) REG.RRD = REG.RRS; CY
#define LD_RR_NN(RR) REG.RR = READ_NN
#define LD_R_MR(R, MR) REG.R = READMR(MR)
#define LD_R_MN(R) REG.R = READ8(READ_NN)
#define LD_MR_R(MR, R) WRITEMR(MR, REG.R)
#define LD_MR_N(MR) WRITEMR(MR, READ_N)
#define LD_MN_R(R) WRITE8(READ_NN, REG.R)
#define LD_MFF00_N_R(R) WRITE8(0xFF00 + READ_N, RA)
#define LD_MFF00_R_R(R1, R2) WRITE8(0xFF00 + REG.R1, REG.R2)
#define LD_R_MFF00_N(R) REG.R = READ8(0xFF00 + READ_N)
#define LD_R_MFF00_R(R1, R2) REG.R1 = READ8(0xFF00 + REG.R2)
#define LD_MNN_SP u16 = READ_NN; WRITE16(u16, RSP)
#define LD_HL_SP_N s = (s8)READ_N; ADD_SP_FLAGS(s); REG.HL = RSP + s; CY
#define OR_FLAGS FZ_EQ0(RA); FN = FH = FC = 0
#define OR_R(R) RA |= REG.R; OR_FLAGS
#define OR_MR(MR) RA |= READMR(MR); OR_FLAGS
#define OR_N RA |= READ_N; OR_FLAGS
#define POP_RR(RR) REG.RR = READ16(RSP); RSP += 2
#define POP_AF set_af_reg(e, READ16(RSP)); RSP += 2
#define PUSH_RR(RR) CY; RSP -= 2; WRITE16(RSP, REG.RR)
#define PUSH_AF CY; RSP -= 2; WRITE16(RSP, get_af_reg(e))
#define RES(BIT) u &= ~(1 << (BIT))
#define RES_R(BIT, R) BASIC_OP_R(R, RES(BIT))
#define RES_MR(BIT, MR) BASIC_OP_MR(MR, RES(BIT))
#define RET new_pc = READ16(RSP); RSP += 2; CY
#define RET_F(COND) CY; if (COND) { RET; }
#define RETI INTR.enable = FALSE; INTR.ime = TRUE; RET
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
#define STOP INTR.stop = TRUE;
#define FC_SUB(X, Y) FC = ((int)(X) - (int)(Y) < 0)
#define FH_SUB(X, Y) FH = ((int)MASK8(X) - (int)MASK8(Y) < 0)
#define FCH_SUB(X, Y) FC_SUB(X, Y); FH_SUB(X, Y)
#define SUB_FLAGS(X, Y) FZ_EQ0((X) - (Y)); FN = 1; FCH_SUB(X, Y)
#define SUB_R(R) SUB_FLAGS(RA, REG.R); RA -= REG.R
#define SUB_MR(MR) u = READMR(MR); SUB_FLAGS(RA, u); RA -= u
#define SUB_N u = READ_N; SUB_FLAGS(RA, u); RA -= u
#define FC_SBC(X, Y, C) FC = ((int)(X) - (int)(Y) - (int)(C) < 0)
#define FH_SBC(X, Y, C) FH = ((int)MASK8(X) - (int)MASK8(Y) - (int)C < 0)
#define FCH_SBC(X, Y, C) FC_SBC(X, Y, C); FH_SBC(X, Y, C)
#define SBC_FLAGS(X, Y, C) FZ_EQ0((X) - (Y) - (C)); FN = 1; FCH_SBC(X, Y, C)
#define SBC_R(R) u = REG.R; c = FC; SBC_FLAGS(RA, u, c); RA -= u + c
#define SBC_MR(MR) u = READMR(MR); c = FC; SBC_FLAGS(RA, u, c); RA -= u + c
#define SBC_N u = READ_N; c = FC; SBC_FLAGS(RA, u, c); RA -= u + c
#define SWAP u = (u << 4) | (u >> 4)
#define SWAP_FLAGS FZ_EQ0(u); FN = FH = FC = 0
#define SWAP_R(R) BASIC_OP_R(R, SWAP); SWAP_FLAGS
#define SWAP_MR(MR) BASIC_OP_MR(MR, SWAP); SWAP_FLAGS
#define XOR_FLAGS FZ_EQ0(RA); FN = FH = FC = 0
#define XOR_R(R) RA ^= REG.R; XOR_FLAGS
#define XOR_MR(MR) RA ^= READMR(MR); XOR_FLAGS
#define XOR_N RA ^= READ_N; XOR_FLAGS

static void dispatch_interrupt(Emulator* e) {
  if (!(INTR.ime || INTR.halt)) {
    return;
  }

  Bool was_halt = INTR.halt;
  INTR.halt = INTR.stop = INTR.ime = FALSE;

  /* Write MSB of PC. */
  RSP--; WRITE8(RSP, REG.PC >> 8);

  /* Now check which interrupt to raise, after having written the MSB of PC.
   * This behavior is needed to pass the ie_push mooneye-gb test. */
  u8 interrupt = INTR.new_if & INTR.ie & IF_ALL;

  Bool delay = FALSE;
  u8 mask = 0;
  Address vector = 0;
  if (interrupt & IF_VBLANK) {
    HOOK(vblank_interrupt_i, PPU.frame);
    vector = 0x40;
    mask = IF_VBLANK;
  } else if (interrupt & IF_STAT) {
    HOOK(stat_interrupt_cccc, STAT.y_compare.irq ? 'Y' : '.',
         STAT.mode2.irq ? 'O' : '.', STAT.vblank.irq ? 'V' : '.',
         STAT.hblank.irq ? 'H' : '.');
    vector = 0x48;
    mask = IF_STAT;
  } else if (interrupt & IF_TIMER) {
    HOOK0(timer_interrupt_v);
    vector = 0x50;
    mask = IF_TIMER;
    delay = was_halt;
  } else if (interrupt & IF_SERIAL) {
    HOOK0(serial_interrupt_v);
    vector = 0x58;
    mask = IF_SERIAL;
  } else if (interrupt & IF_JOYPAD) {
    HOOK0(joypad_interrupt_v);
    vector = 0x60;
    mask = IF_JOYPAD;
  } else {
    /* Interrupt was canceled. */
    vector = 0;
    mask = 0;
  }

  INTR.new_if &= ~mask;

  /* Now write the LSB of PC. */
  RSP--; WRITE8(RSP, REG.PC);
  REG.PC = vector;

  if (delay) {
    mcycle(e);
  }
  mcycle(e);
  mcycle(e);
}

static void execute_instruction(Emulator* e) {
  s8 s;
  u8 u, c, opcode;
  u16 u16;
  Address new_pc;

  Bool should_dispatch =
      (INTR.ime || INTR.halt) && ((INTR.new_if & INTR.ie & IF_ALL) != 0);

  if (INTR.stop && !should_dispatch) {
    return;
  }

  if (INTR.enable) {
    INTR.enable = FALSE;
    INTR.ime = TRUE;
  }

  if (INTR.halt_bug) {
    /* When interrupts are disabled during a HALT, the following byte will be
     * duplicated when decoding. */
    opcode = read_u8(e, REG.PC);
    REG.PC--;
    INTR.halt_bug = FALSE;
  } else {
    opcode = read_u8_cy(e, REG.PC);
  }

  if (should_dispatch) {
    if (INTR.halt_di) {
      HOOK0(interrupt_during_halt_di_v);
      INTR.halt = INTR.halt_di = FALSE;
    } else {
      dispatch_interrupt(e);
      return;
    }
  }

  if (INTR.halt) {
    return;
  }

  new_pc = REG.PC + s_opcode_bytes[opcode];

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
    case 0x22: LD_MR_R(HL, A); REG.HL++; break;
    case 0x23: INC_RR(HL); break;
    case 0x24: INC_R(H); break;
    case 0x25: DEC_R(H); break;
    case 0x26: LD_R_N(H); break;
    case 0x27: DAA; break;
    case 0x28: JR_F_N(FZ); break;
    case 0x29: ADD_HL_RR(HL); break;
    case 0x2a: LD_R_MR(A, HL); REG.HL++; break;
    case 0x2b: DEC_RR(HL); break;
    case 0x2c: INC_R(L); break;
    case 0x2d: DEC_R(L); break;
    case 0x2e: LD_R_N(L); break;
    case 0x2f: CPL; break;
    case 0x30: JR_F_N(!FC); break;
    case 0x31: LD_RR_NN(SP); break;
    case 0x32: LD_MR_R(HL, A); REG.HL--; break;
    case 0x33: INC_RR(SP); break;
    case 0x34: INC_MR(HL); break;
    case 0x35: DEC_MR(HL); break;
    case 0x36: LD_MR_N(HL); break;
    case 0x37: SCF; break;
    case 0x38: JR_F_N(FC); break;
    case 0x39: ADD_HL_RR(SP); break;
    case 0x3a: LD_R_MR(A, HL); REG.HL--; break;
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
      switch (read_u8_cy(e, REG.PC + 1)) {
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
    default: UNREACHABLE("invalid opcode 0x%02x!\n", opcode); break;
  }
  REG.PC = new_pc;
}

static void emulator_step_internal(Emulator* e) {
  HOOK0(emulator_step);
  execute_instruction(e);
}

EmulatorEvent emulator_run_until(struct Emulator* e, Cycles until_cycles) {
  AudioBuffer* ab = &e->audio_buffer;
  if (e->state.last_event & EMULATOR_EVENT_NEW_FRAME) {
    PPU.new_frame_edge = FALSE;
  }
  if (e->state.last_event & EMULATOR_EVENT_AUDIO_BUFFER_FULL) {
    ab->position = ab->data;
  }
  check_joyp_intr(e);

  u64 frames_left = ab->frames - audio_buffer_get_frames(ab);
  Cycles max_audio_cycles =
      APU.cycles +
      (u32)DIV_CEIL(frames_left * CPU_CYCLES_PER_SECOND, ab->frequency);
  EmulatorEvent event = 0;
  while (event == 0) {
    emulator_step_internal(e);
    if (PPU.new_frame_edge) {
      event |= EMULATOR_EVENT_NEW_FRAME;
    }
    if (max_audio_cycles < CYCLES) {
      event |= EMULATOR_EVENT_AUDIO_BUFFER_FULL;
    }
    if (until_cycles < CYCLES) {
      event |= EMULATOR_EVENT_UNTIL_CYCLES;
    }
  }
  apu_synchronize(e);
  return e->state.last_event = event;
}

EmulatorEvent emulator_step(Emulator* e) {
  return emulator_run_until(e, CYCLES + 1);
}

static Result validate_header_checksum(CartInfo* cart_info) {
  u8 checksum = 0;
  size_t i = 0;
  for (i = HEADER_CHECKSUM_RANGE_START; i <= HEADER_CHECKSUM_RANGE_END; ++i) {
    checksum = checksum - cart_info->data[i] - 1;
  }
  return checksum == cart_info->data[HEADER_CHECKSUM_ADDR] ? OK : ERROR;
}

static const char* get_result_string(Result value) {
  static const char* s_strings[] = {[OK] = "OK", [ERROR] = "ERROR"};
  return get_enum_string(s_strings, ARRAY_SIZE(s_strings), value);
}

static void log_cart_info(CartInfo* cart_info) {
  char* title_start = (char*)cart_info->data + TITLE_START_ADDR;
  char* title_end = memchr(title_start, '\0', TITLE_MAX_LENGTH);
  int title_length =
      (int)(title_end ? title_end - title_start : TITLE_MAX_LENGTH);
  printf("title: \"%.*s\"\n", title_length, title_start);
  printf("cgb flag: %s\n", get_cgb_flag_string(cart_info->cgb_flag));
  printf("sgb flag: %s\n", get_sgb_flag_string(cart_info->sgb_flag));
  printf("cart type: %s\n", get_cart_type_string(cart_info->cart_type));
  printf("rom size: %s\n", get_rom_size_string(cart_info->rom_size));
  printf("ext ram size: %s\n",
         get_ext_ram_size_string(cart_info->ext_ram_size));
  printf("header checksum: 0x%02x [%s]\n",
         cart_info->data[HEADER_CHECKSUM_ADDR],
         get_result_string(validate_header_checksum(cart_info)));
}

Result init_audio_buffer(Emulator* e, u32 frequency, u32 frames) {
  AudioBuffer* audio_buffer = &e->audio_buffer;
  audio_buffer->frames = frames;
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

Result init_emulator(Emulator* e) {
  static u8 s_initial_wave_ram[WAVE_RAM_SIZE] = {
      0x60, 0x0d, 0xda, 0xdd, 0x50, 0x0f, 0xad, 0xed,
      0xc0, 0xde, 0xf0, 0x0d, 0xbe, 0xef, 0xfe, 0xed,
  };
  CHECK(SUCCESS(get_cart_infos(e)));
  log_cart_info(e->cart_info);
  MMAP_STATE.rom1_base = 1 << ROM_BANK_SHIFT;
  set_af_reg(e, 0x01b0);
  REG.BC = 0x0013;
  REG.DE = 0x00d8;
  REG.HL = 0x014d;
  REG.SP = 0xfffe;
  REG.PC = 0x0100;
  INTR.ime = FALSE;
  TIMER.div_counter = 0xAC00;
  /* Enable apu first, so subsequent writes succeed. */
  write_apu(e, APU_NR52_ADDR, 0xf1);
  write_apu(e, APU_NR11_ADDR, 0x80);
  write_apu(e, APU_NR12_ADDR, 0xf3);
  write_apu(e, APU_NR14_ADDR, 0x80);
  write_apu(e, APU_NR50_ADDR, 0x77);
  write_apu(e, APU_NR51_ADDR, 0xf3);
  APU.initialized = TRUE;
  memcpy(&WAVE.ram, s_initial_wave_ram, WAVE_RAM_SIZE);
  /* Turn down the volume on channel1, it is playing by default (because of the
   * GB startup sound), but we don't want to hear it when starting the
   * emulator. */
  CHANNEL1.envelope.volume = 0;
  write_io(e, IO_LCDC_ADDR, 0x91);
  write_io(e, IO_SCY_ADDR, 0x00);
  write_io(e, IO_SCX_ADDR, 0x00);
  write_io(e, IO_LYC_ADDR, 0x00);
  write_io(e, IO_BGP_ADDR, 0xfc);
  write_io(e, IO_OBP0_ADDR, 0xff);
  write_io(e, IO_OBP1_ADDR, 0xff);
  write_io(e, IO_IF_ADDR, 0x1);
  write_io(e, IO_IE_ADDR, 0x0);

  APU.cycles = CYCLES = 0;
  return OK;
  ON_ERROR_RETURN;
}

void emulator_set_joypad_buttons(struct Emulator* e, JoypadButtons* buttons) {
  JOYP.buttons = *buttons;
}

void emulator_set_joypad_callback(struct Emulator* e, JoypadCallback callback,
                                  void* user_data) {
  e->joypad_info.callback = callback;
  e->joypad_info.user_data = user_data;
}

JoypadCallbackInfo emulator_get_joypad_callback(struct Emulator* e) {
  return e->joypad_info;
}

void emulator_set_config(struct Emulator* e, const EmulatorConfig* config) {
  e->config = *config;
}

EmulatorConfig emulator_get_config(struct Emulator* e) {
  return e->config;
}

FrameBuffer* emulator_get_frame_buffer(struct Emulator* e) {
  return &e->frame_buffer;
}

AudioBuffer* emulator_get_audio_buffer(struct Emulator* e) {
  return &e->audio_buffer;
}

Cycles emulator_get_cycles(struct Emulator* e) {
  return CYCLES;
}

u32 emulator_get_ppu_frame(struct Emulator* e) {
  return PPU.frame;
}

u32 audio_buffer_get_frames(AudioBuffer* audio_buffer) {
  return (audio_buffer->position - audio_buffer->data) / SOUND_OUTPUT_COUNT;
}

static Result set_rom_file_data(Emulator* e, const FileData* file_data) {
  CHECK_MSG(file_data->size > 0, "File is empty.\n");
  CHECK_MSG((file_data->size & (MINIMUM_ROM_SIZE - 1)) == 0,
            "File size (%ld) should be a multiple of minimum rom size (%ld).\n",
            (long)file_data->size, (long)MINIMUM_ROM_SIZE);
  e->file_data = *file_data;
  return OK;
  ON_ERROR_RETURN;
}

Result emulator_read_state(Emulator* e, const FileData* file_data) {
  CHECK_MSG(file_data->size == sizeof(EmulatorState),
            "save state file is wrong size: %ld, expected %ld.\n",
            (long)file_data->size, (long)sizeof(EmulatorState));
  EmulatorState* new_state = (EmulatorState*)file_data->data;
  CHECK_MSG(new_state->header == SAVE_STATE_HEADER,
            "header mismatch: %u, expected %u.\n", new_state->header,
            SAVE_STATE_HEADER);
  memcpy(&e->state, new_state, sizeof(EmulatorState));
  set_cart_info(e, e->state.cart_info_index);
  return OK;
  ON_ERROR_RETURN;
}

Result emulator_write_state(Emulator* e, FileData* file_data) {
  CHECK(file_data->size >= sizeof(EmulatorState));
  e->state.header = SAVE_STATE_HEADER;
  memcpy(file_data->data, &e->state, file_data->size);
  return OK;
  ON_ERROR_RETURN;
}

Result emulator_read_ext_ram(Emulator* e, const FileData* file_data) {
  if (EXT_RAM.battery_type != BATTERY_TYPE_WITH_BATTERY)
    return OK;

  CHECK_MSG(file_data->size == EXT_RAM.size,
            "save file is wrong size: %ld, expected %ld.\n",
            (long)file_data->size, (long)EXT_RAM.size);
  memcpy(EXT_RAM.data, file_data->data, file_data->size);
  return OK;
  ON_ERROR_RETURN;
}

Result emulator_write_ext_ram(Emulator* e, FileData* file_data) {
  if (EXT_RAM.battery_type != BATTERY_TYPE_WITH_BATTERY)
    return OK;

  CHECK(file_data->size >= EXT_RAM.size);
  memcpy(file_data->data, EXT_RAM.data, file_data->size);
  return OK;
  ON_ERROR_RETURN;
}

Result emulator_read_ext_ram_from_file(struct Emulator* e,
                                       const char* filename) {
  if (EXT_RAM.battery_type != BATTERY_TYPE_WITH_BATTERY)
    return OK;
  Result result = ERROR;
  FileData file_data;
  ZERO_MEMORY(file_data);
  CHECK(SUCCESS(file_read(filename, &file_data)));
  CHECK(SUCCESS(emulator_read_ext_ram(e, &file_data)));
  result = OK;
error:
  file_data_delete(&file_data);
  return result;
}

Result emulator_write_ext_ram_to_file(struct Emulator* e,
                                      const char* filename) {
  if (EXT_RAM.battery_type != BATTERY_TYPE_WITH_BATTERY)
    return OK;

  Result result = ERROR;
  FileData file_data;
  file_data.size = EXT_RAM.size;
  file_data.data = malloc(file_data.size);
  CHECK(SUCCESS(emulator_write_ext_ram(e, &file_data)));
  CHECK(SUCCESS(file_write(filename, &file_data)));
  result = OK;
error:
  file_data_delete(&file_data);
  return result;
}

Result emulator_read_state_from_file(struct Emulator* e, const char* filename) {
  Result result = ERROR;
  FileData file_data;
  ZERO_MEMORY(file_data);
  CHECK(SUCCESS(file_read(filename, &file_data)));
  CHECK(SUCCESS(emulator_read_state(e, &file_data)));
  result = OK;
error:
  file_data_delete(&file_data);
  return result;
}

Result emulator_write_state_to_file(struct Emulator* e, const char* filename) {
  Result result = ERROR;
  FileData file_data;
  file_data.size = sizeof(EmulatorState);
  file_data.data = malloc(file_data.size);
  CHECK(SUCCESS(emulator_write_state(e, &file_data)));
  CHECK(SUCCESS(file_write(filename, &file_data)));
  result = OK;
error:
  file_data_delete(&file_data);
  return result;
}

Emulator* emulator_new(const EmulatorInit* init) {
  Emulator* e = calloc(1, sizeof(Emulator));
  CHECK(SUCCESS(set_rom_file_data(e, &init->rom)));
  CHECK(SUCCESS(init_emulator(e)));
  CHECK(
      SUCCESS(init_audio_buffer(e, init->audio_frequency, init->audio_frames)));
  return e;
error:
  emulator_delete(e);
  return NULL;
}

void emulator_delete(Emulator* e) {
  if (e) {
    free(e->audio_buffer.data);
    free(e);
  }
}
