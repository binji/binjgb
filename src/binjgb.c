/*
 * Copyright (C) 2016 Ben Smith
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdarg.h>

#include "emulator.h"
#include "host.h"
#include "options.h"

#define SAVE_EXTENSION ".sav"
#define SAVE_STATE_EXTENSION ".state"

#define GLYPH_WIDTH 3
#define GLYPH_HEIGHT 5
#define GLYPHS_PER_LINE ((SCREEN_WIDTH / (GLYPH_WIDTH + 1)) - 1)

#define STATUS_TEXT_X 2
#define STATUS_TEXT_Y (SCREEN_HEIGHT - GLYPH_HEIGHT - 2)
#define STATUS_TEXT_RGBA MAKE_RGBA(255, 0, 0, 255)
#define STATUS_TEXT_TIMEOUT 120 /* Frames */

typedef enum Layer {
  LAYER_BG,
  LAYER_WINDOW,
  LAYER_OBJ,
} Layer;

typedef struct Overlay {
  HostTexture* texture;
  RGBA data[SCREEN_WIDTH * SCREEN_HEIGHT];
} Overlay;

typedef struct StatusText {
  char data[GLYPHS_PER_LINE + 1];
  size_t len;
  u32 timeout;
} StatusText;

static struct Emulator* e;
static struct Host* host;

static const char* s_rom_filename;
static const char* s_read_joypad_filename;
static const char* s_write_joypad_filename;
static const char* s_save_state_filename;
static Bool s_running = TRUE;
static Bool s_step_frame;
static Bool s_paused;
static f32 s_audio_volume = 0.5f;
static Bool s_rewinding;
static Ticks s_rewind_start;
static u32 s_random_seed = 0xcabba6e5;
static u32 s_builtin_palette;
static Bool s_force_dmg;
static Bool s_use_sgb_border;
static u32 s_cgb_color_curve;
static u32 s_render_scale = 4;

static u32 s_audio_frequency = 44100;
static u32 s_audio_frames = 2048; /* ~46ms of latency at 44.1kHz */
static u32 s_rewind_frames_per_base_state = 45;
static u32 s_rewind_buffer_capacity_megabytes = 32;
static f32 s_rewind_scale = 1.5f;

static Overlay s_overlay;
static StatusText s_status_text;

/* tom-thumb font: https://robey.lag.net/2010/01/23/tiny-monospace-font.html
 * license: CC0
 */
static const u16 s_font[] = {
    0x0000, 0x4124, 0x005a, 0xbefa, 0x4f3c, 0x8542, 0xd7b6, 0x0024, 0x8928,
    0x2922, 0x02aa, 0x0ba0, 0x2800, 0x0380, 0x4000, 0x2548, 0x76dc, 0x4934,
    0xe546, 0x7146, 0x93da, 0x719e, 0xf79c, 0x254e, 0xf7de, 0x73de, 0x0820,
    0x2820, 0x88a8, 0x1c70, 0x2a22, 0x414e, 0xc7d4, 0xb7d4, 0x75d6, 0xc49c,
    0x76d6, 0xe79e, 0x279e, 0xd79c, 0xb7da, 0xe92e, 0x5648, 0xb5da, 0xe492,
    0xb7fa, 0xbffa, 0x56d4, 0x25d6, 0xded4, 0xafd6, 0x711c, 0x492e, 0xd6da,
    0x4ada, 0xbfda, 0xb55a, 0x495a, 0xe54e, 0xe49e, 0x1110, 0xf24e, 0x0054,
    0xe000, 0x0022, 0xf730, 0x76b2, 0xc4e0, 0xd6e8, 0xcee0, 0x4ba8, 0x53dd,
    0xb6b2, 0x4904, 0x5641, 0xadd2, 0xe926, 0xbff0, 0xb6b0, 0x56a0, 0x2ed7,
    0x9add, 0x24e0, 0x79e0, 0xc974, 0xd6d0, 0x5ed0, 0xffd0, 0xa950, 0x535b,
    0xef70, 0xc8ac, 0x4824, 0x6a26, 0x003c, 0xfffe,
};

static void clear_overlay(void) {
  memset(s_overlay.data, 0, sizeof(s_overlay.data));
}

static void fill_rect(int l, int t, int r, int b, RGBA color) {
  assert(l <= r && t <= b && l >= 0 && r < SCREEN_WIDTH && t >= 0 &&
         b < SCREEN_HEIGHT);
  int i, j;
  RGBA* dst = &s_overlay.data[t * SCREEN_WIDTH + l];
  for (j = t; j < b; ++j) {
    for (i = l; i < r; ++i) {
      *dst++ = color;
    }
    dst += SCREEN_WIDTH - (r - l);
  }
}

static void draw_char(int x, int y, RGBA color, char c) {
  /* For now, don't clamp. */
  u8 uc = (u8)c;
  assert(x >= 0 && y >= 0 && x + GLYPH_WIDTH <= SCREEN_WIDTH &&
         y + GLYPH_HEIGHT <= SCREEN_HEIGHT);
  if (uc < 32 || uc >= 128) return;
  u16 data = s_font[uc - 32];
  Bool has_descender = data & 1;
  data >>= 1;
  if (has_descender) y += 1;
  int i, j;
  RGBA* dst = &s_overlay.data[y * SCREEN_WIDTH + x];
  for (j = 0; j < GLYPH_HEIGHT; ++j) {
    for (i = 0; i < GLYPH_WIDTH; ++i) {
      if (data & 1) *dst = color;
      data >>= 1;
      dst++;
    }
    dst += SCREEN_WIDTH - GLYPH_WIDTH;
  }
}

static void draw_str(int x, int y, RGBA color, const char* s) {
  while (*s) {
    draw_char(x, y, color, *s);
    ++s;
    x += GLYPH_WIDTH + 1;
  }
}

static void set_status_text(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  s_status_text.len =
      vsnprintf(s_status_text.data, sizeof(s_status_text.data), fmt, args);
  va_end(args);
  s_status_text.timeout = STATUS_TEXT_TIMEOUT;
}

static void update_overlay(void) {
  clear_overlay();
  if (s_status_text.timeout) {
    --s_status_text.timeout;
    fill_rect(STATUS_TEXT_X - 1, STATUS_TEXT_Y - 1,
              STATUS_TEXT_X + s_status_text.len * (GLYPH_WIDTH + 1) + 1,
              STATUS_TEXT_Y + GLYPH_HEIGHT + 1, MAKE_RGBA(224, 224, 224, 255));
    draw_str(STATUS_TEXT_X, STATUS_TEXT_Y, STATUS_TEXT_RGBA,
             s_status_text.data);
    host_upload_texture(host, s_overlay.texture, SCREEN_WIDTH, SCREEN_HEIGHT,
                        s_overlay.data);
    host_render_screen_overlay(host, s_overlay.texture);
  }
}

static void inc_audio_volume(f32 delta) {
  s_audio_volume = CLAMP(s_audio_volume + delta, 0, 1);
  host_set_audio_volume(host, s_audio_volume);
  set_status_text("Volume: %3.f%%", s_audio_volume * 100);
}

static void toggle_audio_channel(int channel) {
  EmulatorConfig emu_config = emulator_get_config(e);
  emu_config.disable_sound[channel] ^= 1;
  emulator_set_config(e, &emu_config);
  set_status_text("Audio channels: %c%c%c%c",
                  emu_config.disable_sound[APU_CHANNEL1] ? '_' : '1',
                  emu_config.disable_sound[APU_CHANNEL2] ? '_' : '2',
                  emu_config.disable_sound[APU_CHANNEL3] ? '_' : '3',
                  emu_config.disable_sound[APU_CHANNEL4] ? '_' : '4');
}

static void inc_palette(int delta) {
  s_builtin_palette = (s_builtin_palette + delta + BUILTIN_PALETTE_COUNT) %
                      BUILTIN_PALETTE_COUNT;
  emulator_set_builtin_palette(e, s_builtin_palette);
  set_status_text("Palette: %d", s_builtin_palette);
}

static void toggle_layer(Layer layer) {
  EmulatorConfig emu_config = emulator_get_config(e);
  switch (layer) {
    case LAYER_BG: emu_config.disable_bg ^= 1; break;
    case LAYER_WINDOW: emu_config.disable_window ^= 1; break;
    case LAYER_OBJ: emu_config.disable_obj ^= 1; break;
  }
  emulator_set_config(e, &emu_config);
  set_status_text("Layer: %s %s %s", emu_config.disable_bg ? "__" : "bg",
                  emu_config.disable_window ? "___" : "win",
                  emu_config.disable_obj ? "___" : "obj");
}

static void set_no_sync(Bool set) {
  HostConfig host_config = host_get_config(host);
  host_config.no_sync = set;
  host_set_config(host, &host_config);
}

static void toggle_fullscreen(void) {
  HostConfig host_config = host_get_config(host);
  host_config.fullscreen ^= 1;
  host_set_config(host, &host_config);
}

static void save_state(void) {
  if (SUCCESS(emulator_write_state_to_file(e, s_save_state_filename))) {
    set_status_text("saved state");
  } else {
    set_status_text("unable to save state");
  }
}

static void load_state(void) {
  if (SUCCESS(emulator_read_state_from_file(e, s_save_state_filename))) {
    set_status_text("loaded state");
  } else {
    set_status_text("unable to load state");
  }
}

static void begin_rewind(void) {
  if (!s_rewinding) {
    host_begin_rewind(host);
    s_rewinding = TRUE;
    s_rewind_start = emulator_get_ticks(e);
  }
}

static void rewind_by(Ticks delta) {
  Ticks now = emulator_get_ticks(e);
  Ticks then = now;
  if (now >= delta) {
    then = now - delta;
    host_rewind_to_ticks(host, then);
  }

  Ticks oldest = host_get_rewind_oldest_ticks(host);
  Ticks total = s_rewind_start - oldest;
  Ticks then_diff = then - oldest;
  int num_ticks = then_diff * (GLYPHS_PER_LINE - 2) / total;

  char buffer[GLYPHS_PER_LINE + 1];
  buffer[0] = '|';
  int i;
  for (i = 1; i < GLYPHS_PER_LINE - 1; ++i) {
    buffer[i] = i < num_ticks ? '=' : ' ';
  }
  buffer[GLYPHS_PER_LINE - 1] = '|';
  buffer[GLYPHS_PER_LINE] = 0;

  u32 day, hr, min, sec, ms;
  emulator_ticks_to_time(then, &day, &hr, &min, &sec, &ms);
  char time[64];
  snprintf(time, sizeof(time), "%u:%02u:%02u.%02u", day * 24 + hr, min, sec,
           ms / 10);
  size_t len = strlen(time);
  memcpy(&buffer[(GLYPHS_PER_LINE - len) / 2], time, len);

  set_status_text("%s", buffer);
}

static void end_rewind(void) {
  host_end_rewind(host);
  s_rewinding = FALSE;
}

static void key_down(HostHookContext* ctx, HostKeycode code) {
  switch (code) {
    case HOST_KEYCODE_1: toggle_audio_channel(APU_CHANNEL1); break;
    case HOST_KEYCODE_2: toggle_audio_channel(APU_CHANNEL2); break;
    case HOST_KEYCODE_3: toggle_audio_channel(APU_CHANNEL3); break;
    case HOST_KEYCODE_4: toggle_audio_channel(APU_CHANNEL4); break;
    case HOST_KEYCODE_B: toggle_layer(LAYER_BG); break;
    case HOST_KEYCODE_W: toggle_layer(LAYER_WINDOW); break;
    case HOST_KEYCODE_O: toggle_layer(LAYER_OBJ); break;
    case HOST_KEYCODE_F6: save_state(); break;
    case HOST_KEYCODE_F9: load_state(); break;
    case HOST_KEYCODE_N: s_step_frame = TRUE; s_paused = FALSE; break;
    case HOST_KEYCODE_SPACE: s_paused ^= 1; break;
    case HOST_KEYCODE_ESCAPE: s_running = FALSE; break;
    case HOST_KEYCODE_LSHIFT: set_no_sync(TRUE); break;
    case HOST_KEYCODE_MINUS: inc_audio_volume(-0.05f); break;
    case HOST_KEYCODE_EQUALS: inc_audio_volume(+0.05f); break;
    case HOST_KEYCODE_BACKSPACE: begin_rewind(); break;
    case HOST_KEYCODE_LEFTBRACKET: inc_palette(-1); break;
    case HOST_KEYCODE_RIGHTBRACKET: inc_palette(1); break;
    default: break;
  }
}

static void key_up(HostHookContext* ctx, HostKeycode code) {
  switch (code) {
    case HOST_KEYCODE_LSHIFT: set_no_sync(FALSE); break;
    case HOST_KEYCODE_F11: toggle_fullscreen(); break;
    case HOST_KEYCODE_BACKSPACE: end_rewind(); break;
    default: break;
  }
}

static void usage(int argc, char** argv) {
  PRINT_ERROR(
      "usage: %s [options] <in.gb>\n"
      "  -h,--help               help\n"
      "  -j,--read-joypad FILE   read joypad input from FILE\n"
      "  -J,--write-joypad FILE  write joypad input to FILE\n"
      "  -s,--seed SEED          random seed used for initializing RAM\n"
      "  -P,--palette PAL        use a builtin palette for DMG\n"
      "  -x,--scale SCALE        render scale\n"
      "  -C,--cgb-color COLOR    cgb color curve to use\n"
      "                            0: none\n"
      "                            1: Sameboy (Emulate Hardware)\n"
      "                            2: Gambatte/Gameboy Online\n"
      "     --force-dmg          force running as a DMG (original gameboy)\n"
      "     --sgb-border         draw the super gameboy border\n",
      argv[0]);
}

void parse_arguments(int argc, char** argv) {
  static const Option options[] = {
    {'h', "help", 0},
    {'j', "read-joypad", 1},
    {'J', "write-joypad", 1},
    {'s', "seed", 1},
    {'P', "palette", 1},
    {'x', "scale", 1},
    {'C', "cgb-color", 1},
    {0, "force-dmg", 0},
    {0, "sgb-border", 0},
  };

  struct OptionParser* parser = option_parser_new(
      options, sizeof(options) / sizeof(options[0]), argc, argv);

  int errors = 0;
  int done = 0;
  while (!done) {
    OptionResult result = option_parser_next(parser);
    switch (result.kind) {
      case OPTION_RESULT_KIND_UNKNOWN:
        PRINT_ERROR("ERROR: Unknown option: %s.\n\n", result.arg);
        goto error;

      case OPTION_RESULT_KIND_EXPECTED_VALUE:
        PRINT_ERROR("ERROR: Option --%s requires a value.\n\n",
                    result.option->long_name);
        goto error;

      case OPTION_RESULT_KIND_BAD_SHORT_OPTION:
        PRINT_ERROR("ERROR: Short option -%c is too long: %s.\n\n",
                    result.option->short_name, result.arg);
        goto error;

      case OPTION_RESULT_KIND_OPTION:
        switch (result.option->short_name) {
          case 'h':
            goto error;

          case 'j':
            s_read_joypad_filename = result.value;
            break;

          case 'J':
            s_write_joypad_filename = result.value;
            break;

          case 's':
            s_random_seed = atoi(result.value);
            break;

          case 'P':
            s_builtin_palette = atoi(result.value);
            break;

          case 'x':
            s_render_scale = atoi(result.value);
            break;

          case 'C':
            s_cgb_color_curve = atoi(result.value);
            break;

          default:
            if (strcmp(result.option->long_name, "force-dmg") == 0) {
              s_force_dmg = TRUE;
            } else if (strcmp(result.option->long_name, "sgb-border") == 0) {
              s_use_sgb_border = TRUE;
            } else {
              abort();
            }
            break;
        }
        break;

      case OPTION_RESULT_KIND_ARG:
        s_rom_filename = result.value;
        break;

      case OPTION_RESULT_KIND_DONE:
        done = 1;
        break;
    }
  }

  if (!s_rom_filename) {
    PRINT_ERROR("ERROR: expected input .gb\n\n");
    goto error;
  }

  option_parser_delete(parser);
  return;

error:
  usage(argc, argv);
  option_parser_delete(parser);
  exit(1);
}

void read_ini_file(void) {
  FILE* file = fopen("binjgb.ini", "r");
  if (!file) {
    return;
  }
  while (1) {
    char buffer[1024], *value, *equals, *newline;
    if (!fgets(buffer, sizeof(buffer), file)) {
      break;
    }

    if (buffer[0] == '#' || buffer[0] == '\n') {
      continue;
    }

    equals = strchr(buffer, '=');
    if (!equals) {
      fprintf(stderr, "warning: bad ini line: %s\n", buffer);
      continue;
    }
    *equals = 0;
    value = equals + 1;

    newline = strchr(value, '\n');
    if (newline) {
      *newline = 0;
    }

    if (strcmp(buffer, "autoload") == 0) {
      s_rom_filename = xstrdup(value);
    } else if (strcmp(buffer, "audio-frequency") == 0) {
      s_audio_frequency = atoi(value);
    } else if (strcmp(buffer, "audio-frames") == 0) {
      s_audio_frames = atoi(value);
    } else if (strcmp(buffer, "builtin-palette") == 0) {
      s_builtin_palette = atoi(value);
    } else if (strcmp(buffer, "force-dmg") == 0) {
      s_force_dmg = atoi(value);
    } else if (strcmp(buffer, "cgb-color") == 0) {
      s_cgb_color_curve = atoi(value);
    } else if (strcmp(buffer, "rewind-frames-per-base-state") == 0) {
      s_rewind_frames_per_base_state = atoi(value);
    } else if (strcmp(buffer, "rewind-buffer-capacity-megabytes") == 0) {
      s_rewind_buffer_capacity_megabytes = atoi(value);
    } else if (strcmp(buffer, "rewind-scale") == 0) {
      s_rewind_scale = atof(value);
    } else if (strcmp(buffer, "render-scale") == 0) {
      s_render_scale = atoi(value);
    } else if (strcmp(buffer, "random-seed") == 0) {
      s_random_seed = atoi(value);
    } else if (strcmp(buffer, "sgb-border") == 0) {
      s_use_sgb_border = atoi(value);
    } else {
      fprintf(stderr, "warning: unknown ini key: %s\n", buffer);
    }
  }
  fclose(file);
}

int main(int argc, char** argv) {
  int result = 1;

  read_ini_file();

  parse_arguments(argc, argv);

  FileData rom;
  CHECK(SUCCESS(file_read_aligned(s_rom_filename, MINIMUM_ROM_SIZE, &rom)));

  EmulatorInit emulator_init;
  ZERO_MEMORY(emulator_init);
  emulator_init.rom = rom;
  emulator_init.audio_frequency = s_audio_frequency;
  emulator_init.audio_frames = s_audio_frames;
  emulator_init.random_seed = s_random_seed;
  emulator_init.builtin_palette = s_builtin_palette;
  emulator_init.force_dmg = s_force_dmg;
  emulator_init.cgb_color_curve = s_cgb_color_curve;
  e = emulator_new(&emulator_init);
  CHECK(e != NULL);

  HostInit host_init;
  ZERO_MEMORY(host_init);
  host_init.hooks.key_down = key_down;
  host_init.hooks.key_up = key_up;
  host_init.render_scale = s_render_scale;
  host_init.audio_frequency = s_audio_frequency;
  host_init.audio_frames = s_audio_frames;
  host_init.audio_volume = s_audio_volume;
  host_init.rewind.frames_per_base_state = s_rewind_frames_per_base_state;
  host_init.rewind.buffer_capacity = s_rewind_buffer_capacity_megabytes * MEGABYTES(1);
  host_init.joypad_filename = s_read_joypad_filename;
  host_init.use_sgb_border = s_use_sgb_border;
  host = host_new(&host_init, e);
  CHECK(host != NULL);

  const char* save_filename = replace_extension(s_rom_filename, SAVE_EXTENSION);
  s_save_state_filename =
      replace_extension(s_rom_filename, SAVE_STATE_EXTENSION);
  emulator_read_ext_ram_from_file(e, save_filename);

  s_overlay.texture = host_create_texture(host, SCREEN_WIDTH, SCREEN_HEIGHT,
                                          HOST_TEXTURE_FORMAT_RGBA);

  f64 refresh_ms = host_get_monitor_refresh_ms(host);
  while (s_running && host_poll_events(host)) {
    if (s_rewinding) {
      rewind_by((Ticks)(PPU_FRAME_TICKS * s_rewind_scale));
    } else if (!s_paused) {
      EmulatorEvent event = host_run_ms(host, refresh_ms);
      if (event & EMULATOR_EVENT_INVALID_OPCODE) {
        set_status_text("invalid opcode!");
        s_paused = TRUE;
      }
      if (s_step_frame) {
        host_reset_audio(host);
        s_paused = TRUE;
        s_step_frame = FALSE;
      }
    }

    host_begin_video(host);
    update_overlay();
    host_end_video(host);
  }

  if (s_write_joypad_filename) {
    host_write_joypad_to_file(host, s_write_joypad_filename);
  } else {
    emulator_write_ext_ram_to_file(e, save_filename);
  }

  result = 0;
error:
  if (host) {
    host_delete(host);
  }
  if (e) {
    emulator_delete(e);
  }
  return result;
}
