/*
 * Copyright (C) 2017 Ben Smith
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include "emulator-debug.h"

#include <inttypes.h>
#include <stdarg.h>

#define MAX_TRACE_STACK 16
#define MAX_BREAKPOINTS 256
static Bool s_trace_stack[MAX_TRACE_STACK] = {FALSE};
static size_t s_trace_stack_top = 1;
static LogLevel s_log_level[NUM_LOG_SYSTEMS] = {1, 1, 1, 1, 1, 1};
static Breakpoint s_breakpoints[MAX_BREAKPOINTS];
static Address s_breakpoint_mask[2];
static const Breakpoint s_invalid_breakpoint;
static int s_breakpoint_count;
static int s_breakpoint_max_id;

#define HOOK0(name) HOOK_##name(e, __func__)
#define HOOK(name, ...) HOOK_##name(e, __func__, __VA_ARGS__)
#define HOOK0_FALSE(name) HOOK_##name(e, __func__)

#define DECLARE_LOG_HOOK(system, level, name, format) \
  static void HOOK_##name(Emulator* e, const char* func_name, ...);

#define DEFINE_LOG_HOOK(system, level, name, format)                        \
  void HOOK_##name(Emulator* e, const char* func_name, ...) {               \
    if (s_log_level[LOG_SYSTEM_##system] >= LOG_LEVEL_##level) {            \
      va_list args;                                                         \
      va_start(args, func_name);                                            \
      fprintf(stdout, "%10" PRIu64 ": %-30s:", e->state.ticks, func_name); \
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
  X(A, D, write_square_wave_period_info_iii, "freq: %u tick: %u period: %u")   \
  X(A, D, write_wave_period_info_iii, "freq: %u tick: %u period: %u")          \
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
  X(I, D, speed_switch_i, "speed switch to %dx")                               \
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
  X(M, D, set_rom_bank_ihi, "(index: %d, bank: %d) = %#06x")                   \
  X(M, D, write_during_dma_ab, "(%#04x, %#02x) during DMA")                    \
  X(M, D, write_io_ignored_as, "(%#04x, %#02x) ignored")                       \
  X(M, D, write_ram_disabled_ab, "(%#04x, %#02x) ignored, ram disabled")

static Bool HOOK_emulator_step(Emulator*, const char* func_name);
static void HOOK_read_rom_ib(Emulator*, const char* func_name, u32 rom_addr,
                             u8 value);
static void HOOK_exec_op_ai(Emulator*, const char* func_name, Address,
                            u8 opcode);
static void HOOK_exec_cb_op_i(Emulator*, const char* func_name, u8 opcode);

FOREACH_LOG_HOOKS(DECLARE_LOG_HOOK)

#include "emulator.c"

FOREACH_LOG_HOOKS(DEFINE_LOG_HOOK)

static const char* s_opcode_mnemonic[256] = {
  "nop", "ld bc,%hu", "ld [bc],a", "inc bc", "inc b", "dec b", "ld b,%hhu",
  "rlca", "ld [$%04x],sp", "add hl,bc", "ld a,[bc]", "dec bc", "inc c", "dec c",
  "ld c,%hhu", "rrca", "stop", "ld de,%hu", "ld [de],a", "inc de", "inc d",
  "dec d", "ld d,%hhu", "rla", "jr %+hhd", "add hl,de", "ld a,[de]", "dec de",
  "inc e", "dec e", "ld e,%hhu", "rra", "jr nz,%+hhd", "ld hl,%hu",
  "ld [hl+],a", "inc hl", "inc h", "dec h", "ld h,%hhu", "daa", "jr z,%+hhd",
  "add hl,hl", "ld a,[hl+]", "dec hl", "inc l", "dec l", "ld l,%hhu", "cpl",
  "jr nc,%+hhd", "ld sp,%hu", "ld [hl-],a", "inc sp", "inc [hl]", "dec [hl]",
  "ld [hl],%hhu", "scf", "jr c,%+hhd", "add hl,sp", "ld a,[hl-]", "dec sp",
  "inc a", "dec a", "ld a,%hhu", "ccf", "ld b,b", "ld b,c", "ld b,d", "ld b,e",
  "ld b,h", "ld b,l", "ld b,[hl]", "ld b,a", "ld c,b", "ld c,c", "ld c,d",
  "ld c,e", "ld c,h", "ld c,l", "ld c,[hl]", "ld c,a", "ld d,b", "ld d,c",
  "ld d,d", "ld d,e", "ld d,h", "ld d,l", "ld d,[hl]", "ld d,a", "ld e,b",
  "ld e,c", "ld e,d", "ld e,e", "ld e,h", "ld e,l", "ld e,[hl]", "ld e,a",
  "ld h,b", "ld h,c", "ld h,d", "ld h,e", "ld h,h", "ld h,l", "ld h,[hl]",
  "ld h,a", "ld l,b", "ld l,c", "ld l,d", "ld l,e", "ld l,h", "ld l,l",
  "ld l,[hl]", "ld l,a", "ld [hl],b", "ld [hl],c", "ld [hl],d", "ld [hl],e",
  "ld [hl],h", "ld [hl],l", "halt", "ld [hl],a", "ld a,b", "ld a,c", "ld a,d",
  "ld a,e", "ld a,h", "ld a,l", "ld a,[hl]", "ld a,a", "add a,b", "add a,c",
  "add a,d", "add a,e", "add a,h", "add a,l", "add a,[hl]", "add a,a",
  "adc a,b", "adc a,c", "adc a,d", "adc a,e", "adc a,h", "adc a,l",
  "adc a,[hl]", "adc a,a", "sub a,b", "sub a,c", "sub a,d", "sub a,e",
  "sub a,h", "sub a,l", "sub a,[hl]", "sub a,a", "sbc a,b", "sbc a,c",
  "sbc a,d", "sbc a,e", "sbc a,h", "sbc a,l", "sbc a,[hl]", "sbc a,a",
  "and a,b", "and a,c", "and a,d", "and a,e", "and a,h", "and a,l",
  "and a,[hl]", "and a,a", "xor a,b", "xor a,c", "xor a,d", "xor a,e",
  "xor a,h", "xor a,l", "xor a,[hl]", "xor a,a", "or a,b", "or a,c", "or a,d",
  "or a,e", "or a,h", "or a,l", "or a,[hl]", "or a,a", "cp a,b", "cp a,c",
  "cp a,d", "cp a,e", "cp a,h", "cp a,l", "cp a,[hl]", "cp a,a", "ret nz",
  "pop bc", "jp nz,$%04hx", "jp $%04hx", "call nz,$%04hx", "push bc",
  "add a,%hhu", "rst $00", "ret z", "ret", "jp z,$%04hx", NULL, "call z,$%04hx",
  "call $%04hx", "adc a,%hhu", "rst $08", "ret nc", "pop de", "jp nc,$%04hx",
  NULL, "call nc,$%04hx", "push de", "sub a,%hhu", "rst $10", "ret c", "reti",
  "jp c,$%04hx", NULL, "call c,$%04hx", NULL, "sbc a,%hhu", "rst $18",
  "ldh [$ff%02hhx],a", "pop hl", "ld [$ff00+c],a", NULL, NULL, "push hl",
  "and a,%hhu", "rst $20", "add sp,%hhd", "jp hl", "ld [$%04hx],a", NULL, NULL,
  NULL, "xor a,%hhu", "rst $28", "ldh a,[$ff%02hhx]", "pop af",
  "ld a,[$ff00+c]", "di", NULL, "push af", "or a,%hhu", "rst $30",
  "ld hl,sp%+hhd", "ld sp,hl", "ld a,[$%04hx]", "ei", NULL, NULL, "cp a,%hhu",
  "rst $38",
};

static const char* s_cb_opcode_mnemonic[256] = {
    "rlc b",      "rlc c",   "rlc d",      "rlc e",   "rlc h",      "rlc l",
    "rlc [hl]",   "rlc a",   "rrc b",      "rrc c",   "rrc d",      "rrc e",
    "rrc h",      "rrc l",   "rrc [hl]",   "rrc a",   "rl b",       "rl c",
    "rl d",       "rl e",    "rl h",       "rl l",    "rl [hl]",    "rl a",
    "rr b",       "rr c",    "rr d",       "rr e",    "rr h",       "rr l",
    "rr [hl]",    "rr a",    "sla b",      "sla c",   "sla d",      "sla e",
    "sla h",      "sla l",   "sla [hl]",   "sla a",   "sra b",      "sra c",
    "sra d",      "sra e",   "sra h",      "sra l",   "sra [hl]",   "sra a",
    "swap b",     "swap c",  "swap d",     "swap e",  "swap h",     "swap l",
    "swap [hl]",  "swap a",  "srl b",      "srl c",   "srl d",      "srl e",
    "srl h",      "srl l",   "srl [hl]",   "srl a",   "bit 0,b",    "bit 0,c",
    "bit 0,d",    "bit 0,e", "bit 0,h",    "bit 0,l", "bit 0,[hl]", "bit 0,a",
    "bit 1,b",    "bit 1,c", "bit 1,d",    "bit 1,e", "bit 1,h",    "bit 1,l",
    "bit 1,[hl]", "bit 1,a", "bit 2,b",    "bit 2,c", "bit 2,d",    "bit 2,e",
    "bit 2,h",    "bit 2,l", "bit 2,[hl]", "bit 2,a", "bit 3,b",    "bit 3,c",
    "bit 3,d",    "bit 3,e", "bit 3,h",    "bit 3,l", "bit 3,[hl]", "bit 3,a",
    "bit 4,b",    "bit 4,c", "bit 4,d",    "bit 4,e", "bit 4,h",    "bit 4,l",
    "bit 4,[hl]", "bit 4,a", "bit 5,b",    "bit 5,c", "bit 5,d",    "bit 5,e",
    "bit 5,h",    "bit 5,l", "bit 5,[hl]", "bit 5,a", "bit 6,b",    "bit 6,c",
    "bit 6,d",    "bit 6,e", "bit 6,h",    "bit 6,l", "bit 6,[hl]", "bit 6,a",
    "bit 7,b",    "bit 7,c", "bit 7,d",    "bit 7,e", "bit 7,h",    "bit 7,l",
    "bit 7,[hl]", "bit 7,a", "res 0,b",    "res 0,c", "res 0,d",    "res 0,e",
    "res 0,h",    "res 0,l", "res 0,[hl]", "res 0,a", "res 1,b",    "res 1,c",
    "res 1,d",    "res 1,e", "res 1,h",    "res 1,l", "res 1,[hl]", "res 1,a",
    "res 2,b",    "res 2,c", "res 2,d",    "res 2,e", "res 2,h",    "res 2,l",
    "res 2,[hl]", "res 2,a", "res 3,b",    "res 3,c", "res 3,d",    "res 3,e",
    "res 3,h",    "res 3,l", "res 3,[hl]", "res 3,a", "res 4,b",    "res 4,c",
    "res 4,d",    "res 4,e", "res 4,h",    "res 4,l", "res 4,[hl]", "res 4,a",
    "res 5,b",    "res 5,c", "res 5,d",    "res 5,e", "res 5,h",    "res 5,l",
    "res 5,[hl]", "res 5,a", "res 6,b",    "res 6,c", "res 6,d",    "res 6,e",
    "res 6,h",    "res 6,l", "res 6,[hl]", "res 6,a", "res 7,b",    "res 7,c",
    "res 7,d",    "res 7,e", "res 7,h",    "res 7,l", "res 7,[hl]", "res 7,a",
    "set 0,b",    "set 0,c", "set 0,d",    "set 0,e", "set 0,h",    "set 0,l",
    "set 0,[hl]", "set 0,a", "set 1,b",    "set 1,c", "set 1,d",    "set 1,e",
    "set 1,h",    "set 1,l", "set 1,[hl]", "set 1,a", "set 2,b",    "set 2,c",
    "set 2,d",    "set 2,e", "set 2,h",    "set 2,l", "set 2,[hl]", "set 2,a",
    "set 3,b",    "set 3,c", "set 3,d",    "set 3,e", "set 3,h",    "set 3,l",
    "set 3,[hl]", "set 3,a", "set 4,b",    "set 4,c", "set 4,d",    "set 4,e",
    "set 4,h",    "set 4,l", "set 4,[hl]", "set 4,a", "set 5,b",    "set 5,c",
    "set 5,d",    "set 5,e", "set 5,h",    "set 5,l", "set 5,[hl]", "set 5,a",
    "set 6,b",    "set 6,c", "set 6,d",    "set 6,e", "set 6,h",    "set 6,l",
    "set 6,[hl]", "set 6,a", "set 7,b",    "set 7,c", "set 7,d",    "set 7,e",
    "set 7,h",    "set 7,l", "set 7,[hl]", "set 7,a",
};

static void sprint_hex(char* buffer, u8 val) {
  const char hex_digits[] = "0123456789abcdef";
  buffer[0] = hex_digits[(val >> 4) & 0xf];
  buffer[1] = hex_digits[val & 0xf];
}

int opcode_bytes(u8 opcode) { return s_opcode_bytes[opcode]; }

void emulator_get_opcode_mnemonic(u16 opcode, char* buffer, size_t size) {
  const char* orig_fmt;
  u8 num_bytes = 1;
  if (opcode >= 0x100) {
    assert((opcode & 0xff00) == 0xcb00);
    orig_fmt = s_cb_opcode_mnemonic[opcode & 0xff];
  } else {
    num_bytes = s_opcode_bytes[opcode];
    orig_fmt = s_opcode_mnemonic[opcode];
  }
  char fmt[32];
  char* dst = fmt;
  const char* src;
  for (src = orig_fmt; *src;) {
    if (*src == '%') {
      *dst++ = *src++;

      /* Replace the format specifier with a string so we can print XXXX
       * instead of numbers. This is super hacky, but eh. */

#define START if (0) ;
#define END else abort();
#define REPLACE(old, new, len)                       \
  else if (strncmp(src, old, len) == 0) {            \
    *dst = 0; /* NULL-terminate so strncat works. */ \
    strncat(dst, new, fmt + sizeof(fmt) - dst - 1);  \
    dst += len;                                      \
    src += len;                                      \
  }

      START
      REPLACE("hu", "4s", 2)
      REPLACE("hhu", "-2s", 3)
      REPLACE("04x", "-4s", 3)
      REPLACE("+hhd", "2.2s", 4)
      REPLACE("04hx", "4.4s", 4)
      REPLACE("02hhx", "-2.2s", 5)
      END

#undef START
#undef END
#undef REPLACE
    } else {
      *dst++ = *src++;
    }
  }
  *dst++ = 0;
  switch (num_bytes) {
    case 0:
    case 1: strncpy(buffer, fmt, size); break;
    case 2: snprintf(buffer, size, fmt, "XX"); break;
    case 3: snprintf(buffer, size, fmt, "XXXX"); break;
  }
}

static int disassemble_instr(u8 data[3], char* buffer, size_t size) {
  char temp[100];
  u8 opcode = data[0];
  u8 num_bytes = s_opcode_bytes[opcode];
  switch (num_bytes) {
    case 1: {
      const char* mnemonic = s_opcode_mnemonic[opcode];
      if (!mnemonic) {
        mnemonic = "*INVALID*";
      }
      strncpy(temp, mnemonic, sizeof(temp) - 1);
      break;
    }
    case 2:
      if (opcode == 0xcb) {
        strncpy(temp, s_cb_opcode_mnemonic[data[1]], sizeof(temp) - 1);
      } else {
        snprintf(temp, sizeof(temp), s_opcode_mnemonic[opcode], data[1]);
      }
      break;
    case 3:
      snprintf(temp, sizeof(temp), s_opcode_mnemonic[opcode],
               (data[2] << 8) | data[1]);
      break;
    default: assert(!"invalid opcode byte length.\n"); break;
  }

  char hex[][3] = {"  ", "  ", "  "};
  switch (num_bytes) {
    case 3: sprint_hex(hex[2], data[2]); /* Fallthrough. */
    case 2: sprint_hex(hex[1], data[1]); /* Fallthrough. */
    case 1: sprint_hex(hex[0], data[0]); break;
  }

  snprintf(buffer, size, "%s %s %s  %-15s", hex[0], hex[1], hex[2], temp);
  return num_bytes;
}

int emulator_disassemble(Emulator* e, Address addr, char* buffer, size_t size) {
  char instr[120];
  char hex[][3] = {"  ", "  ", "  "};

  u8 data[3] = {read_u8_raw(e, addr), read_u8_raw(e, addr + 1),
                read_u8_raw(e, addr + 2)};
  int num_bytes = disassemble_instr(data, instr, sizeof(instr));

  char bank[3] = "??";
  if (addr < 0x4000) {
    sprint_hex(bank, e->state.memory_map_state.rom_base[0] >> ROM_BANK_SHIFT);
  } else if (addr < 0x8000) {
    sprint_hex(bank, e->state.memory_map_state.rom_base[1] >> ROM_BANK_SHIFT);
  }

  snprintf(buffer, size, "[%s]%#06x: %s", bank, addr, instr);
  return num_bytes ? num_bytes : 1;
}

static void print_instruction(Emulator* e, Address addr) {
  char temp[64];
  emulator_disassemble(e, addr, temp, sizeof(temp));
  printf("%s", temp);
}

void emulator_disassemble_rom(Emulator* e, u32 rom_addr, char* buffer,
                              size_t size) {
  char instr[100];
  u8* rom = e->cart_info->data;
  u8 data[3] = {rom[rom_addr], rom[rom_addr + 1], rom[rom_addr + 2]};
  disassemble_instr(data, instr, sizeof(instr));
  int bank = rom_addr >> ROM_BANK_SHIFT;
  Address addr = rom_addr & 0x3fff;
  if (bank > 0) {
    addr += 0x4000;
  }
  snprintf(buffer, size, "[%02x]%#06x: %s", bank, addr, instr);
}

Registers emulator_get_registers(Emulator* e) { return REG; }

int emulator_get_max_breakpoint_id(void) {
  return s_breakpoint_max_id;
}

static Bool is_breakpoint_valid(int id) {
  return id >= 0 && id < s_breakpoint_max_id && s_breakpoints[id].valid;
}

Breakpoint emulator_get_breakpoint(int id) {
  return is_breakpoint_valid(id) ? s_breakpoints[id] : s_invalid_breakpoint;
}

static Bool address_matches_bank(Emulator* e, Address addr, int bank) {
  return addr >= 0x8000 || emulator_get_rom_bank(e, addr) == bank;
}

Breakpoint emulator_get_breakpoint_by_address(Emulator* e, Address addr) {
  if (s_breakpoint_count == 0) {
    return s_invalid_breakpoint;
  }
  int id;
  for (id = 0; id < s_breakpoint_max_id; ++id) {
    Breakpoint* bp = &s_breakpoints[id];
    if (bp->valid && bp->addr == addr &&
        address_matches_bank(e, addr, bp->bank)) {
      return s_breakpoints[id];
    }
  }
  return s_invalid_breakpoint;
}

static void calculate_breakpoint_mask(void) {
  s_breakpoint_mask[0] = 0xffffu;
  s_breakpoint_mask[1] = 0xffffu;
  int id;
  for (id = 0; id < s_breakpoint_max_id; ++id) {
    Breakpoint* bp = &s_breakpoints[id];
    if (!(bp->valid && bp->enabled)) {
      continue;
    }
    s_breakpoint_mask[0] &= ~bp->addr;
    s_breakpoint_mask[1] &= bp->addr;
  }
}

int emulator_add_empty_breakpoint(void) {
  int id;
  for (id = 0; id < MAX_BREAKPOINTS; ++id) {
    Breakpoint* bp = &s_breakpoints[id];
    if (!bp->valid) {
      bp->id = id;
      bp->addr = bp->bank = 0;
      bp->enabled = FALSE;
      bp->valid = TRUE;
      s_breakpoint_max_id = MAX(id + 1, s_breakpoint_max_id);
      ++s_breakpoint_count;
      return id;
    }
  }
  return -1;
}

int emulator_add_breakpoint(Emulator* e, Address addr, Bool enabled) {
  int id = emulator_add_empty_breakpoint();
  if (id < 0) {
    return id;
  }
  emulator_set_breakpoint_address(e, id, addr);
  emulator_enable_breakpoint(id, enabled);
  return id;
}

void emulator_set_breakpoint_address(Emulator* e, int id, Address addr) {
  if (!is_breakpoint_valid(id)) {
    return;
  }
  Breakpoint* bp = &s_breakpoints[id];
  bp->addr = addr;
  bp->bank = emulator_get_rom_bank(e, addr);
  calculate_breakpoint_mask();
}

void emulator_enable_breakpoint(int id, Bool enabled) {
  if (!is_breakpoint_valid(id)) {
    return;
  }
  s_breakpoints[id].enabled = enabled;
  calculate_breakpoint_mask();
}

void emulator_remove_breakpoint(int id) {
  if (!is_breakpoint_valid(id)) {
    return;
  }
  s_breakpoints[id].valid = FALSE;
  if (id + 1 == s_breakpoint_max_id) {
    while (s_breakpoint_max_id > 0 &&
           !s_breakpoints[s_breakpoint_max_id - 1].valid) {
      s_breakpoint_max_id--;
    }
  }
  calculate_breakpoint_mask();
  --s_breakpoint_count;
}

int emulator_get_rom_bank(Emulator* e, Address addr) {
  int region = addr >> ROM_BANK_SHIFT;
  if (region < 2) {
    return MMAP_STATE.rom_base[region] >> ROM_BANK_SHIFT;
  } else {
    return -1;
  }
}

u8 emulator_read_u8_raw(Emulator* e, Address addr) {
  return read_u8_raw(e, addr);
}

void emulator_write_u8_raw(Emulator* e, Address addr, u8 value) {
  write_u8_raw(e, addr, value);
}

// Store as 1-1 mapping of bytes, low 3 bits used only.
static Bool s_rom_usage_enabled = TRUE;
static u8 s_rom_usage[MAXIMUM_ROM_SIZE];

Bool emulator_get_rom_usage_enabled(void) {
  return s_rom_usage_enabled;
}

void emulator_set_rom_usage_enabled(Bool enable) {
  s_rom_usage_enabled = enable;
}

static inline void mark_rom_usage(u32 rom_addr, RomUsage usage) {
  assert(rom_addr < ARRAY_SIZE(s_rom_usage));
  s_rom_usage[rom_addr] |= usage;
}

u8* emulator_get_rom_usage(void) {
  assert(s_rom_usage_enabled);
  return s_rom_usage;
}

void emulator_clear_rom_usage(void) {
  assert(s_rom_usage_enabled);
  memset(s_rom_usage, 0, sizeof(s_rom_usage));
}

void HOOK_read_rom_ib(Emulator* e, const char* func_name, u32 rom_addr,
                      u8 value) {
  if (!s_rom_usage_enabled) {
    return;
  }
  mark_rom_usage(rom_addr, ROM_USAGE_DATA);
}

#define INVALID_ROM_ADDR (~0u)

static u32 get_rom_addr(Emulator* e, Address addr) {
  if (addr < 0x4000) {
    return e->state.memory_map_state.rom_base[0] | (addr & 0x3fff);
  } else if (addr < 0x8000) {
    return e->state.memory_map_state.rom_base[1] | (addr & 0x3fff);
  } else {
    return INVALID_ROM_ADDR;
  }
}

static void mark_rom_usage_for_pc(Emulator* e, u32 rom_addr) {
  if (!s_rom_usage_enabled || rom_addr == INVALID_ROM_ADDR) {
    return;
  }
  u8 opcode = e->cart_info->data[rom_addr];
  u8 count = s_opcode_bytes[opcode];
  mark_rom_usage(rom_addr, ROM_USAGE_CODE | ROM_USAGE_CODE_START);
  switch (count) {
    case 3:
      mark_rom_usage(rom_addr + 2, ROM_USAGE_CODE);
      /* fallthrough */
    case 2:
      mark_rom_usage(rom_addr + 1, ROM_USAGE_CODE);
      /* fallthrough */
  }
}

static Bool address_matches_breakpoint_mask(Address addr) {
  return (addr & s_breakpoint_mask[0]) == 0 &&
         (addr & s_breakpoint_mask[1]) == s_breakpoint_mask[1];
}

static inline Bool hit_breakpoint(Emulator* e) {
  if (s_breakpoint_count == 0) {
    return FALSE;
  }
  u16 pc = e->state.reg.PC;
  if (!address_matches_breakpoint_mask(pc)) {
    return FALSE;
  }
  Bool hit = FALSE;
  int id;
  for (id = 0; id < s_breakpoint_max_id; ++id) {
    Breakpoint* bp = &s_breakpoints[id];
    if (!(bp->valid && bp->enabled && bp->addr == pc &&
          address_matches_bank(e, pc, bp->bank))) {
      continue;
    }
    /* Don't hit the same breakpoint twice in a row. */
    if (bp->hit) {
      bp->hit = FALSE;
      continue;
    }

    hit = bp->hit = TRUE;
  }
  return hit;
}

Bool HOOK_emulator_step(Emulator* e, const char* func_name) {
  if (emulator_get_trace() && INTR.state < CPU_STATE_HALT) {
    printf("A:%02X F:%c%c%c%c BC:%04X DE:%04x HL:%04x SP:%04x PC:%04x", REG.A,
           REG.F.Z ? 'Z' : '-', REG.F.N ? 'N' : '-', REG.F.H ? 'H' : '-',
           REG.F.C ? 'C' : '-', REG.BC, REG.DE, REG.HL, REG.SP, REG.PC);
    printf(" (cy: %" PRIu64 ")", e->state.ticks);
    if (s_log_level[LOG_SYSTEM_PPU] >= 1) {
      printf(" ppu:%c%u", PPU.lcdc.display ? '+' : '-', PPU.stat.mode);
    }
    if (s_log_level[LOG_SYSTEM_PPU] >= 2) {
      printf(" LY:%u", PPU.ly);
    }
    printf(" |");
    print_instruction(e, REG.PC);
    printf("\n");
  }
  if (hit_breakpoint(e)) {
    e->state.event |= EMULATOR_EVENT_BREAKPOINT;
    return TRUE;
  }
  return FALSE;
}

static Bool s_opcode_count_enabled = FALSE;
static u32 s_opcode_count[256];
static u32 s_cb_opcode_count[256];
static Bool s_profiling_enabled = FALSE;
static u32 s_profiling_counters[MAXIMUM_ROM_SIZE];

Bool emulator_get_opcode_count_enabled(void) {
  return s_opcode_count_enabled;
}

void emulator_set_opcode_count_enabled(Bool enable) {
  s_opcode_count_enabled = enable;
}

u32* emulator_get_opcode_count(void) {
  assert(s_opcode_count_enabled);
  return s_opcode_count;
}

u32* emulator_get_cb_opcode_count(void) {
  assert(s_opcode_count_enabled);
  return s_cb_opcode_count;
}

Bool emulator_get_profiling_enabled(void) {
  return s_profiling_enabled;
}

void emulator_set_profiling_enabled(Bool enable) {
  s_profiling_enabled = enable;
}

u32* emulator_get_profiling_counters(void) {
  return s_profiling_counters;
}

void HOOK_exec_op_ai(Emulator* e, const char* func_name, Address pc,
                     u8 opcode) {
  u32 rom_addr = get_rom_addr(e, pc);
  mark_rom_usage_for_pc(e, rom_addr);
  if (s_opcode_count_enabled) {
    s_opcode_count[opcode]++;
  }
  if (s_profiling_enabled && rom_addr != INVALID_ROM_ADDR) {
    s_profiling_counters[rom_addr]++;
  }
}

void HOOK_exec_cb_op_i(Emulator* e, const char* func_name, u8 opcode) {
  if (s_opcode_count_enabled) {
    s_cb_opcode_count[opcode]++;
  }
}

void emulator_set_log_level(LogSystem system, LogLevel level) {
  assert(system < NUM_LOG_SYSTEMS);
  s_log_level[system] = level;
}

SetLogLevelError emulator_set_log_level_from_string(const char* s) {
  const char* log_system_name = s;
  const char* equals = strchr(s, '=');
  if (!equals) {
    return SET_LOG_LEVEL_ERROR_INVALID_FORMAT;
  }

  LogSystem system = NUM_LOG_SYSTEMS;
  int i;
  for (i = 0; i < NUM_LOG_SYSTEMS; ++i) {
    const char* name = emulator_get_log_system_name(i);
    if (strncmp(log_system_name, name, strlen(name)) == 0) {
      system = i;
      break;
    }
  }

  if (system == NUM_LOG_SYSTEMS) {
    return SET_LOG_LEVEL_ERROR_UNKNOWN_LOG_SYSTEM;
  }

  emulator_set_log_level(system, atoi(equals + 1));
  return SET_LOG_LEVEL_ERROR_NONE;
}

Bool emulator_get_trace() {
  return s_trace_stack[s_trace_stack_top - 1];
}

void emulator_set_trace(Bool trace) {
  s_trace_stack[s_trace_stack_top - 1] = trace;
}

void emulator_push_trace(Bool trace) {
  assert(s_trace_stack_top < MAX_TRACE_STACK);
  s_trace_stack[s_trace_stack_top++] = trace;
}

void emulator_pop_trace() {
  assert(s_trace_stack_top > 1);
  --s_trace_stack_top;
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

void emulator_print_log_systems(void) {
  PRINT_ERROR("valid log systems:\n");
  int i;
  for (i = 0; i < NUM_LOG_SYSTEMS; ++i) {
    PRINT_ERROR("  %s\n", emulator_get_log_system_name(i));
  }
}

Bool emulator_is_cgb(Emulator* e) { return e->state.is_cgb; }

int emulator_get_rom_size(Emulator* e) {
  return s_rom_bank_count[e->cart_info->rom_size] << ROM_BANK_SHIFT;
}

TileDataSelect emulator_get_tile_data_select(Emulator* e) {
  return PPU.lcdc.bg_tile_data_select;
}

TileMapSelect emulator_get_tile_map_select(Emulator* e, LayerType layer_type) {
  switch (layer_type) {
    case LAYER_TYPE_BG:
      return PPU.lcdc.bg_tile_map_select;
    case LAYER_TYPE_WINDOW:
      return PPU.lcdc.window_tile_map_select;
    default:
      return TILE_MAP_9800_9BFF;
  }
}

Palette emulator_get_palette(Emulator* e, PaletteType type) {
  switch (type) {
    case PALETTE_TYPE_BGP:
    case PALETTE_TYPE_OBP0:
    case PALETTE_TYPE_OBP1:
      return PPU.pal[type - PALETTE_TYPE_BGP].palette;
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

PaletteRGBA emulator_get_palette_rgba(Emulator* e, PaletteType type) {
  return palette_to_palette_rgba(e, type, emulator_get_palette(e, type));
}

PaletteRGBA emulator_get_cgb_palette_rgba(Emulator* e, CgbPaletteType type,
                                          int index) {
  assert(e->state.is_cgb);
  assert(index < 8);
  switch (type) {
    default:
    case CGB_PALETTE_TYPE_BGCP: return e->state.ppu.bgcp.palettes[index];
    case CGB_PALETTE_TYPE_OBCP: return e->state.ppu.obcp.palettes[index];
  }
}

void emulator_get_tile_data(Emulator* e, TileData out_tile_data) {
  assert((TILE_DATA_TEXTURE_WIDTH % TILE_WIDTH) == 0);
  assert((TILE_DATA_TEXTURE_HEIGHT % TILE_HEIGHT) == 0);
  const int banks = 2;
  const int tw = TILE_DATA_TEXTURE_WIDTH / TILE_WIDTH;
  const int th = TILE_DATA_TEXTURE_HEIGHT / TILE_HEIGHT / banks;
  int bank, tx, ty, mx, my;
  MaskedAddress addr = 0;
  for (bank = 0; bank < banks; ++bank) {
    addr = bank * 0x2000;
    for (ty = 0; ty < th; ++ty) {
      for (tx = 0; tx < tw; ++tx) {
        int offset =
            (((bank * th) + ty) * TILE_HEIGHT) * TILE_DATA_TEXTURE_WIDTH +
            (tx * TILE_WIDTH);
        for (my = 0; my < TILE_HEIGHT; ++my) {
          for (mx = 0; mx < TILE_WIDTH; ++mx) {
            u8 lo = e->state.vram.data[addr];
            u8 hi = e->state.vram.data[addr + 1];
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
}

void emulator_get_tile_map(Emulator* e, TileMapSelect map_select,
                           TileMap out_tile_map) {
  size_t offset = map_select == TILE_MAP_9800_9BFF ? 0x1800 : 0x1c00;
  memcpy(out_tile_map, &e->state.vram.data[offset], TILE_MAP_SIZE);
}

void emulator_get_tile_map_attr(Emulator* e, TileMapSelect map_select,
                                TileMap out_tile_map) {
  assert(emulator_is_cgb(e));
  size_t offset = map_select == TILE_MAP_9800_9BFF ? 0x1800 : 0x1c00;
  memcpy(out_tile_map, &e->state.vram.data[offset + 0x2000], TILE_MAP_SIZE);
}


void emulator_get_bg_scroll(Emulator* e, u8* x, u8* y) {
  *x = PPU.scx;
  *y = PPU.scy;
}

void emulator_get_window_scroll(Emulator* e, u8* x, u8* y) {
  *x = PPU.wx - WINDOW_X_OFFSET;
  *y = PPU.wy;
}

Bool emulator_get_display(Emulator* e) {
  return PPU.lcdc.display;
}

Bool emulator_get_bg_display(Emulator* e) {
  return PPU.lcdc.bg_display;
}

Bool emulator_get_window_display(Emulator* e) {
  return PPU.lcdc.window_display;
}

Bool emulator_get_obj_display(Emulator* e) {
  return PPU.lcdc.obj_display;
}

ObjSize emulator_get_obj_size(Emulator* e) {
  return PPU.lcdc.obj_size;
}

Obj emulator_get_obj(Emulator* e, int index) {
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

PaletteRGBA palette_to_palette_rgba(Emulator* e, PaletteType type,
                                    Palette palette) {
  PaletteRGBA result;
  int i;
  for (i = 0; i < PALETTE_COLOR_COUNT; ++i) {
    result.color[i] = e->color_to_rgba[type].color[palette.color[i]];
  }
  return result;
}
