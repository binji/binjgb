/*
 * Copyright (C) 2017 Ben Smith
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include "emulator-debug.h"

#include <inttypes.h>
#include <stdarg.h>

static Bool s_trace = 0;
static unsigned s_trace_counter = 0;
static LogLevel s_log_level[NUM_LOG_SYSTEMS] = {1, 1, 1, 1, 1, 1};

#define HOOK0(name) HOOK_##name(e, __func__)
#define HOOK(name, ...) HOOK_##name(e, __func__, __VA_ARGS__)

#define DECLARE_LOG_HOOK(system, level, name, format) \
  static void HOOK_##name(struct Emulator* e, const char* func_name, ...);

#define DEFINE_LOG_HOOK(system, level, name, format)                        \
  void HOOK_##name(Emulator* e, const char* func_name, ...) {               \
    if (s_log_level[LOG_SYSTEM_##system] >= LOG_LEVEL_##level) {            \
      va_list args;                                                         \
      va_start(args, func_name);                                            \
      fprintf(stdout, "%10" PRIu64 ": %-30s:", e->state.cycles, func_name); \
      vfprintf(stdout, format "\n", args);                                  \
      va_end(args);                                                         \
    }                                                                       \
  }

#define LOG_LEVEL_I LOG_LEVEL_INFO
#define LOG_LEVEL_D LOG_LEVEL_DEBUG
#define LOG_LEVEL_V LOG_LEVEL_VERBOSE

#define FOREACH_LOG_HOOKS(X)                                                   \
  X(A, D, apu_power_down_v, "Powered down APU. Clearing registers")            \
  X(A, D, apu_power_up_v, "Powered up APU. Resetting frame and sweep timers")  \
  X(A, D, corrupt_wave_ram_i, "corrupting wave ram [pos: %u]")                 \
  X(A, D, read_wave_ram_while_playing_ab, "(%#02x) while playing => %#02x")    \
  X(A, D, read_wave_ram_while_playing_invalid_a,                               \
    "(%#02x) while playing, invalid (0xff)")                                   \
  X(A, D, sweep_overflow_v, "Disabling from sweep overflow")                   \
  X(A, D, sweep_overflow_2nd_v, "Disabling from 2nd sweep overflow")           \
  X(A, D, sweep_update_frequency_i, "Updated frequency=%u")                    \
  X(A, D, trigger_nr14_info_i, "sweep frequency=%u")                           \
  X(A, D, trigger_nr14_sweep_overflow_v, "disabling, sweep overflow")          \
  X(A, D, trigger_nrx4_info_asii, "(%#04x [%s]) volume=%u, timer=%u")          \
  X(A, V, wave_update_position_iii, "Position: %u => %u [cy: %u]")             \
  X(A, D, write_apu_asb, "(%#04x [%s], %#02x)")                                \
  X(A, D, write_apu_disabled_asb, "(%#04x [%s], %#02x) ignored")               \
  X(A, D, write_noise_period_info_iii,                                         \
    "divisor: %u clock shift: %u period: %u")                                  \
  X(A, V, write_nrx1_abi, "(%#04x, %#02x) length=%u")                          \
  X(A, V, write_nrx2_disable_dac_ab, "(%#04x, %#02x) dac_enabled = false")     \
  X(A, V, write_nrx2_initial_volume_abi, "(%#04x, %#02x) initial_volume=%u")   \
  X(A, V, write_nrx2_zombie_mode_abii,                                         \
    "(%#04x, %#02x) zombie mode: volume %u -> %u")                             \
  X(A, D, write_nrx4_disable_channel_ab, "(%#04x, %#02x) disabling channel")   \
  X(A, D, write_nrx4_extra_length_clock_abi,                                   \
    "(%#04x, %#02x) extra length clock = %u")                                  \
  X(A, V, write_nrx4_info_abii, "(%#04x, %#02x) trigger=%u length_enabled=%u") \
  X(A, D, write_nrx4_trigger_new_length_abi,                                   \
    "(%#04x, %#02x) trigger, new length = %u")                                 \
  X(A, D, write_square_wave_period_info_iii, "freq: %u cycle: %u period: %u")  \
  X(A, D, write_wave_period_info_iii, "freq: %u cycle: %u period: %u")         \
  X(A, D, write_wave_ram_ab, "(%#02x, %#02x)")                                 \
  X(A, D, write_wave_ram_while_playing_ab, "(%#02x, %#02x) while playing")     \
  X(H, D, audio_add_buffer_fzz, "+++ %.1f: buf: %zu -> %zu")                   \
  X(H, D, audio_buffer_ready_fz, "*** %.1f: audio buffer ready, size = %zu")   \
  X(H, D, audio_overflow_z, "!!! audio overflow (old size = %zu)")             \
  X(H, D, audio_underflow_zi, "!!! audio underflow. avail %zu < requested %u") \
  X(H, D, desync_fff, "!!! %.1f: desync [gb=%.1fms real=%.1fms]")              \
  X(H, D, render_present_f, "@@@ %.1f: render present")                        \
  X(H, D, sync_wait_ffff, "... %.1f: waiting %.1fms [gb=%.1fms real=%.1fms]")  \
  X(P, D, disable_display_v, "Disabling display")                              \
  X(P, D, read_io_ignored_as, "(%#04x [%s]) ignored")                          \
  X(P, D, read_oam_in_use_a, "(%#04x): returning 0xff because in use")         \
  X(P, D, read_vram_in_use_a, "(%#04x): returning 0xff because in use")        \
  X(P, V, trigger_stat_from_write_cccii,                                       \
    "STAT from write [%c%c%c] [LY: %u] [cy: %u]")                              \
  X(P, D, trigger_timer_i, ">> trigger TIMER [cy: %u]")                        \
  X(P, V, trigger_y_compare_ii, ">> trigger Y compare [LY: %u] [cy: %u]")      \
  X(P, D, write_oam_in_use_ab, "(%#04x, %#02x): ignored because in use")       \
  X(P, D, write_vram_in_use_ab, "(%#04x, %#02x) ignored, using vram")          \
  X(I, D, enable_display_v, "Enabling display")                                \
  X(I, V, read_io_asb, "(%#04x [%s]) = %#02x")                                 \
  X(I, V, write_io_asb, "(%#04x [%s], %#02x)")                                 \
  X(N, D, interrupt_during_halt_di_v,                                          \
    "Interrupt fired during HALT w/ disabled interrupt.")                      \
  X(N, D, joypad_interrupt_v, ">> JOYPAD interrupt")                           \
  X(N, D, serial_interrupt_v, ">> SERIAL interrupt")                           \
  X(N, D, stat_interrupt_cccc, ">> LCD_STAT interrupt [%c%c%c%c]")             \
  X(N, D, timer_interrupt_v, ">> TIMER interrupt")                             \
  X(N, D, trigger_stat_ii, ">> trigger STAT [LY: %u] [cy: %u]")                \
  X(N, D, vblank_interrupt_i, ">> VBLANK interrupt [frame = %u]")              \
  X(M, D, read_during_dma_a, "(%#04x) during DMA")                             \
  X(M, D, read_ram_disabled_a, "(%#04x) ignored, ram disabled")                \
  X(M, D, set_ext_ram_bank_bi, "(%d) = %#06x")                                 \
  X(M, D, set_rom1_bank_hi, "(bank: %d) = %#06x")                              \
  X(M, D, write_during_dma_ab, "(%#04x, %#02x) during DMA")                    \
  X(M, D, write_io_ignored_as, "(%#04x, %#02x) ignored")                       \
  X(M, D, write_ram_disabled_ab, "(%#04x, %#02x) ignored, ram disabled")

static void HOOK_emulator_step(struct Emulator* e, const char* func_name);

FOREACH_LOG_HOOKS(DECLARE_LOG_HOOK)

#include "emulator.c"

FOREACH_LOG_HOOKS(DEFINE_LOG_HOOK)

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

int emulator_opcode_bytes(struct Emulator* e, Address addr) {
  u8 opcode = read_u8(e, addr);
  int num_bytes = s_opcode_bytes[opcode];
  /* Always return at least 1, we typically don't care about detecting the
   * invalid opcodes. */
  return num_bytes ? num_bytes : 1;
}

int emulator_disassemble(Emulator* e, Address addr, char* buffer, size_t size) {
  char temp[64];
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
        snprintf(temp, sizeof(temp), s_opcode_mnemonic[opcode], byte);
        mnemonic = temp;
      }
      break;
    }
    case 3: {
      u8 byte1 = read_u8(e, addr + 1);
      u8 byte2 = read_u8(e, addr + 2);
      sprint_hex(bytes[0], byte1);
      sprint_hex(bytes[1], byte2);
      snprintf(temp, sizeof(temp), s_opcode_mnemonic[opcode],
               (byte2 << 8) | byte1);
      mnemonic = temp;
      break;
    }
    default: assert(!"invalid opcode byte length.\n"); break;
  }

  char bank[3] = "??";
  MemoryTypeAddressPair pair = map_address(addr);
  if (pair.type == MEMORY_MAP_ROM1) {
    sprint_hex(bank, e->state.memory_map_state.rom1_base >> ROM_BANK_SHIFT);
  }

  snprintf(buffer, size, "[%s]%#06x: %02x %s %s  %-15s", bank, addr, opcode,
           bytes[0], bytes[1], mnemonic);
  return num_bytes ? num_bytes : 1;
}

static void print_instruction(Emulator* e, Address addr) {
  char temp[64];
  emulator_disassemble(e, addr, temp, sizeof(temp));
  printf("%s", temp);
}

Registers emulator_get_registers(struct Emulator* e) { return REG; }

void HOOK_emulator_step(Emulator* e, const char* func_name) {
  if (s_trace && !e->state.interrupt.halt) {
    printf("A:%02X F:%c%c%c%c BC:%04X DE:%04x HL:%04x SP:%04x PC:%04x", REG.A,
           REG.F.Z ? 'Z' : '-', REG.F.N ? 'N' : '-', REG.F.H ? 'H' : '-',
           REG.F.C ? 'C' : '-', REG.BC, REG.DE, REG.HL, REG.SP, REG.PC);
    printf(" (cy: %" PRIu64 ")", e->state.cycles);
    if (s_log_level[LOG_SYSTEM_PPU] >= 1) {
      printf(" ppu:%c%u", PPU.lcdc.display ? '+' : '-', PPU.stat.mode);
    }
    if (s_log_level[LOG_SYSTEM_PPU] >= 2) {
      printf(" LY:%u", PPU.ly);
    }
    printf(" |");
    print_instruction(e, REG.PC);
    printf("\n");
    if (s_trace_counter > 0) {
      if (--s_trace_counter == 0) {
        s_trace = FALSE;
      }
    }
  }
}

void emulator_set_log_level(LogSystem system, LogLevel level) {
  assert(system < NUM_LOG_SYSTEMS);
  s_log_level[system] = level;
}

void emulator_set_trace(Bool trace) {
  s_trace = TRUE;
}

const char* emulator_get_log_system_name(LogSystem system) {
  switch (system) {
#define V(SHORT_NAME, name, NAME) \
  case LOG_SYSTEM_##NAME:         \
    return #name;
  FOREACH_LOG_SYSTEM(V)
#undef V
    default:
      return "<unknown log system>";
  }
}

LogLevel emulator_get_log_level(LogSystem system) {
  assert(system < NUM_LOG_SYSTEMS);
  return s_log_level[system];
}

TileDataSelect emulator_get_tile_data_select(struct Emulator* e) {
  return PPU.lcdc.bg_tile_data_select;
}

TileMapSelect emulator_get_tile_map_select(struct Emulator* e,
                                           LayerType layer_type) {
  switch (layer_type) {
    case LAYER_TYPE_BG:
      return PPU.lcdc.bg_tile_map_select;
    case LAYER_TYPE_WINDOW:
      return PPU.lcdc.window_tile_map_select;
    default:
      return TILE_MAP_9800_9BFF;
  }
}

Palette emulator_get_palette(struct Emulator* e, PaletteType type) {
  switch (type) {
    case PALETTE_TYPE_BGP:
      return PPU.bgp;
    case PALETTE_TYPE_OBP0:
      return PPU.obp[0];
    case PALETTE_TYPE_OBP1:
      return PPU.obp[1];
    default: {
      Palette palette;
      palette.color[0] = COLOR_WHITE;
      palette.color[1] = COLOR_LIGHT_GRAY;
      palette.color[2] = COLOR_DARK_GRAY;
      palette.color[3] = COLOR_BLACK;
      return palette;
    }
  }
}

PaletteRGBA emulator_get_palette_rgba(struct Emulator* e, PaletteType type) {
  return palette_to_palette_rgba(emulator_get_palette(e, type));
}

void emulator_get_tile_data(struct Emulator* e, TileData out_tile_data) {
  assert((TILE_DATA_TEXTURE_WIDTH % TILE_WIDTH) == 0);
  assert((TILE_DATA_TEXTURE_HEIGHT % TILE_HEIGHT) == 0);
  const int tw = TILE_DATA_TEXTURE_WIDTH / TILE_WIDTH;
  const int th = TILE_DATA_TEXTURE_HEIGHT / TILE_HEIGHT;
  int tx, ty, mx, my;
  MaskedAddress addr = 0;
  for (ty = 0; ty < th; ++ty) {
    for (tx = 0; tx < tw; ++tx) {
      int offset =
          (ty * TILE_WIDTH) * TILE_DATA_TEXTURE_WIDTH + (tx * TILE_HEIGHT);
      for (my = 0; my < TILE_HEIGHT; ++my) {
        for (mx = 0; mx < TILE_WIDTH; ++mx) {
          u8 lo = e->state.vram[addr];
          u8 hi = e->state.vram[addr + 1];
          u8 shift = 7 - (mx & 7);
          u8 palette_index = (((hi >> shift) & 1) << 1) | ((lo >> shift) & 1);
          out_tile_data[offset + mx] = palette_index;
        }
        addr += TILE_ROW_BYTES;
        offset += TILE_DATA_TEXTURE_WIDTH;
      }
    }
  }
}

void emulator_get_tile_map(struct Emulator* e, TileMapSelect map_select,
                           TileMap out_tile_map) {
  size_t offset = map_select == TILE_MAP_9800_9BFF ? 0x1800 : 0x1c00;
  memcpy(out_tile_map, &e->state.vram[offset], TILE_MAP_SIZE);
}

void emulator_get_bg_scroll(struct Emulator* e, u8* x, u8* y) {
  *x = PPU.scx;
  *y = PPU.scy;
}

void emulator_get_window_scroll(struct Emulator* e, u8* x, u8* y) {
  *x = PPU.wx - WINDOW_X_OFFSET;
  *y = PPU.wy;
}

Bool emulator_get_display(struct Emulator* e) {
  return PPU.lcdc.display;
}

Bool emulator_get_bg_display(struct Emulator* e) {
  return PPU.lcdc.bg_display;
}

Bool emulator_get_window_display(struct Emulator* e) {
  return PPU.lcdc.window_display;
}

Bool emulator_get_obj_display(struct Emulator* e) {
  return PPU.lcdc.obj_display;
}

ObjSize emulator_get_obj_size(struct Emulator* e) {
  return PPU.lcdc.obj_size;
}

Obj emulator_get_obj(struct Emulator* e, int index) {
  if (index >= 0 && index < OBJ_COUNT) {
    return e->state.oam[index];
  }
  static Obj s_dummy_obj;
  return s_dummy_obj;
}

Bool obj_is_visible(const Obj* obj) {
  u8 obj_x = obj->x + OBJ_X_OFFSET - 1;
  u8 obj_y = obj->y + OBJ_Y_OFFSET - 1;
  return obj_x < SCREEN_WIDTH + OBJ_X_OFFSET - 1 &&
         obj_y < SCREEN_HEIGHT + OBJ_Y_OFFSET - 1;
}

RGBA color_to_rgba(Color color) {
  assert(color >= COLOR_WHITE && color <= COLOR_BLACK);
  return s_color_to_rgba[color];
}

PaletteRGBA palette_to_palette_rgba(Palette palette) {
  PaletteRGBA result;
  int i;
  for (i = 0; i < PALETTE_COLOR_COUNT; ++i) {
    result.color[i] = s_color_to_rgba[palette.color[i]];
  }
  return result;
}
