/*
 * Copyright (C) 2017 Ben Smith
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>

#include "emulator-debug.h"
#include "options.h"
#include "host.h"

#include "imgui.h"

#define SAVE_EXTENSION ".sav"

static const char* s_rom_filename;
static f32 s_font_scale = 1.0f;

static void print_log_systems(void) {
  PRINT_ERROR("valid log systems:\n");
  for (int i = 0; i < NUM_LOG_SYSTEMS; ++i) {
    PRINT_ERROR("  %s\n",
                emulator_get_log_system_name(static_cast<LogSystem>(i)));
  }
}

static void usage(int argc, char** argv) {
  PRINT_ERROR(
      "usage: %s [options] <in.gb>\n"
      "  -h,--help          help\n"
      "  -t,--trace         trace each instruction\n"
      "  -f,--font-scale=F  set the global font scale factor to F\n"
      "  -l,--log S=N       set log level for system S to N\n\n",
      argv[0]);

  print_log_systems();
}

void parse_arguments(int argc, char** argv) {
  static const Option options[] = {
    {'h', "help", 0},
    {'t', "trace", 0},
    {'f', "font-scale", 1},
    {'l', "log", 1},
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

          case 't':
            emulator_set_trace(TRUE);
            break;

          case 'f':
            s_font_scale = atof(result.value);
            break;

          case 'l': {
            const char* log_system_name = result.value;
            const char* equals = strchr(result.value, '=');
            if (!equals) {
              PRINT_ERROR("invalid log level format, should be S=N\n");
              continue;
            }

            LogSystem system = NUM_LOG_SYSTEMS;
            for (int i = 0; i < NUM_LOG_SYSTEMS; ++i) {
              const char* name =
                  emulator_get_log_system_name(static_cast<LogSystem>(i));
              if (strncmp(log_system_name, name, strlen(name)) == 0) {
                system = static_cast<LogSystem>(i);
                break;
              }
            }

            if (system == NUM_LOG_SYSTEMS) {
              PRINT_ERROR("unknown log system: %.*s\n",
                          (int)(equals - result.value), result.value);
              print_log_systems();
              continue;
            }
            emulator_set_log_level(system,
                                   static_cast<LogLevel>(atoi(equals + 1)));
            break;
          }

          default:
            assert(0);
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
    usage(argc, argv);
    goto error;
  }

  option_parser_delete(parser);
  return;

error:
  usage(argc, argv);
  option_parser_delete(parser);
  exit(1);
}

namespace ImGui {

IMGUI_API bool CheckboxNot(const char* label, Bool* v) {
  bool bv = !*v;
  bool result = Checkbox(label, &bv);
  *v = static_cast<Bool>(!bv);
  return result;
}

}  // namespace ImGui

template <typename T>
struct Deleter {
  typedef void (*DeleteFunc)(T*);
  Deleter(T* object, DeleteFunc func) : object(object), func(func) {}
  ~Deleter() {
    func(object);
  }

  T* object;
  DeleteFunc func;
};

static f32 dist_squared(const ImVec2& v1, const ImVec2& v2) {
  return (v1.x - v2.x) * (v1.x - v2.x) + (v1.y - v2.y) * (v1.y - v2.y);
}

static void keep_aspect(ImGuiSizeConstraintCallbackData* data) {
  f32 w = data->DesiredSize.x, h = data->DesiredSize.y;
  f32 aspect = w / h;
  f32 want_aspect = (f32)SCREEN_WIDTH / SCREEN_HEIGHT;
  ImVec2 size1(w, w / want_aspect);
  ImVec2 size2(h * want_aspect, h);
  f32 dist1_squared = dist_squared(size1, data->DesiredSize);
  f32 dist2_squared = dist_squared(size2, data->DesiredSize);
  data->DesiredSize = dist1_squared < dist2_squared ? size1 : size2;
}

static const int kAudioDataSamples = 1000;
static f32 s_audio_data[2][kAudioDataSamples];

static void on_audio_buffer_full(HostHookContext* ctx) {
  AudioBuffer* audio_buffer = emulator_get_audio_buffer(ctx->e);
  int size = audio_buffer->position - audio_buffer->data;

  for (int i = 0; i < kAudioDataSamples; ++i) {
    int index = (i * size / kAudioDataSamples) & ~1;
    s_audio_data[0][i] = audio_buffer->data[index];
    s_audio_data[1][i] = audio_buffer->data[index + 1];
  }
}

/* Copied from binjgb.c; probably will diverge. */
int main(int argc, char** argv) {
  const int audio_frequency = 44100;
  const int audio_frames = 2048;

  parse_arguments(argc, argv);

  FileData rom;
  if (!SUCCESS(file_read(s_rom_filename, &rom))) {
    return 1;
  }

  EmulatorInit emulator_init;
  ZERO_MEMORY(emulator_init);
  emulator_init.rom = rom;
  emulator_init.audio_frequency = audio_frequency;
  emulator_init.audio_frames = audio_frames;
  struct Emulator* e = emulator_new(&emulator_init);
  Deleter<Emulator> de(e, emulator_delete);
  if (e == NULL) {
    return 1;
  }

  HostInit host_init;
  ZERO_MEMORY(host_init);
  host_init.render_scale = 4;
  host_init.audio_frequency = audio_frequency;
  host_init.audio_frames = audio_frames;
  host_init.hooks.audio_buffer_full = on_audio_buffer_full;
  struct Host* host = host_new(&host_init, e);
  Deleter<Host> dh(host, host_delete);
  if (host == NULL) {
    return 1;
  }

  ImGui::GetIO().FontGlobalScale = s_font_scale;

  const char* save_filename = replace_extension(s_rom_filename, SAVE_EXTENSION);
  emulator_read_ext_ram_from_file(e, save_filename);

  f64 refresh_ms = host_get_monitor_refresh_ms(host);
  bool main_open = true;
  bool audio_open = true;
  while (host_poll_events(host)) {
    host_begin_video(host);
    host_run_ms(host, refresh_ms);

    if (ImGui::BeginMainMenuBar()) {
      if (ImGui::BeginMenu("Window")) {
        ImGui::MenuItem("Binjgb", NULL, &main_open);
        ImGui::MenuItem("Audio", NULL, &audio_open);
        ImGui::EndMenu();
      }
      ImGui::EndMainMenuBar();
    }

    ImGui::SetNextWindowPos(ImVec2(16, 48), ImGuiSetCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(SCREEN_WIDTH * host_init.render_scale,
                                    SCREEN_HEIGHT * host_init.render_scale),
                             ImGuiSetCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(32, 32), ImVec2(4000, 4000),
                                        keep_aspect);
    if (main_open) {
      if (ImGui::Begin("Binjgb", &main_open)) {
        intptr_t fb_texture = host_get_frame_buffer_texture(host);
        ImVec2 image_size = ImGui::GetContentRegionAvail();
        ImVec2 uv0(0, 0);
        ImVec2 uv1((f32)SCREEN_WIDTH / HOST_FRAME_BUFFER_TEXTURE_WIDTH,
                   (f32)SCREEN_HEIGHT / HOST_FRAME_BUFFER_TEXTURE_HEIGHT);
        ImGui::Image((ImTextureID)fb_texture, image_size, uv0, uv1);
      }
      ImGui::End();
    }

    if (audio_open) {
      ImGui::SetNextWindowPos(ImVec2(16, 640), ImGuiSetCond_FirstUseEver);
      ImGui::SetNextWindowSize(ImVec2(640, 360), ImGuiSetCond_FirstUseEver);
      if (ImGui::Begin("Audio", &audio_open)) {
        EmulatorConfig config = emulator_get_config(e);
        ImGui::CheckboxNot("channel1", &config.disable_sound[CHANNEL1]);
        ImGui::CheckboxNot("channel2", &config.disable_sound[CHANNEL2]);
        ImGui::CheckboxNot("channel3", &config.disable_sound[CHANNEL3]);
        ImGui::CheckboxNot("channel4", &config.disable_sound[CHANNEL4]);
        emulator_set_config(e, &config);

        ImGui::Spacing();
        ImGui::PlotLines("left", s_audio_data[0], kAudioDataSamples, 0, nullptr,
                         0, 128, ImVec2(0, 80));
        ImGui::PlotLines("right", s_audio_data[1], kAudioDataSamples, 0,
                         nullptr, 0, 128, ImVec2(0, 80));
      }
      ImGui::End();
    }

    host_end_video(host);
  }

  emulator_write_ext_ram_to_file(e, save_filename);
  return 0;
}
