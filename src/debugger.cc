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

template <typename T>
bool Combo(const char* label, T* value, const char* const* names,
           int name_count) {
  int int_value = static_cast<int>(*value);
  bool result = Combo(label, &int_value, names, name_count);
  *value = static_cast<T>(int_value);
  return result;
}

bool CheckboxNot(const char* label, Bool* v) {
  bool bv = !*v;
  bool result = Checkbox(label, &bv);
  *v = static_cast<Bool>(!bv);
  return result;
}

void Image(HostTexture* texture, const ImVec2& dst_size,
           const ImVec2& src_size) {
  ImVec2 uv0(0, 0);
  ImVec2 uv1(src_size.x / texture->width, src_size.y / texture->height);
  ImGui::Image((ImTextureID)texture->handle, dst_size, uv0, uv1);
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

class Debugger {
 public:
  Debugger();
  Debugger(const Debugger&) = delete;
  Debugger& operator=(const Debugger&) = delete;

  ~Debugger();

  bool Init(const char* filename, int audio_frequency, int audio_frames,
            int font_scale);
  void Run();

 private:
  static void OnAudioBufferFullThunk(HostHookContext* ctx);
  void OnAudioBufferFull();
  void MainMenuBar();
  void EmulatorWindow();
  void AudioWindow();
  void TiledataWindow();

  EmulatorInit emulator_init;
  HostInit host_init;
  Emulator* e;
  Host* host;
  TileDataBuffer tiledata_buffer;
  HostTexture* tiledata_texture;

  const char* save_filename;

  static const int kAudioDataSamples = 1000;
  f32 audio_data[2][kAudioDataSamples];

  bool emulator_window_open;
  bool audio_window_open;
  bool tiledata_window_open;
};

Debugger::Debugger()
    : e(nullptr),
      host(nullptr),
      tiledata_texture(nullptr),
      save_filename(nullptr),
      emulator_window_open(true),
      audio_window_open(true),
      tiledata_window_open(true) {
  ZERO_MEMORY(audio_data);
}

Debugger::~Debugger() {
  emulator_delete(e);
  host_delete(host);
}

bool Debugger::Init(const char* filename, int audio_frequency, int audio_frames,
                int font_scale) {
  FileData rom;
  if (!SUCCESS(file_read(filename, &rom))) {
    return false;
  }

  ZERO_MEMORY(emulator_init);
  emulator_init.rom = rom;
  emulator_init.audio_frequency = audio_frequency;
  emulator_init.audio_frames = audio_frames;
  e = emulator_new(&emulator_init);
  if (e == nullptr) {
    return false;
  }

  ZERO_MEMORY(host_init);
  host_init.render_scale = 4;
  host_init.audio_frequency = audio_frequency;
  host_init.audio_frames = audio_frames;
  host_init.hooks.user_data = this;
  host_init.hooks.audio_buffer_full = OnAudioBufferFullThunk;
  host = host_new(&host_init, e);
  if (host == nullptr) {
    return false;
  }

  tiledata_texture = host_create_texture(host, TILE_DATA_TEXTURE_WIDTH,
                                         TILE_DATA_TEXTURE_HEIGHT);
  save_filename = replace_extension(filename, SAVE_EXTENSION);
  ImGui::GetIO().FontGlobalScale = s_font_scale;
  return true;
}

void Debugger::Run() {
  emulator_read_ext_ram_from_file(e, save_filename);

  f64 refresh_ms = host_get_monitor_refresh_ms(host);
  while (host_poll_events(host)) {
    host_begin_video(host);
    host_run_ms(host, refresh_ms);

    MainMenuBar();
    EmulatorWindow();
    AudioWindow();
    TiledataWindow();

    host_end_video(host);
  }

  emulator_write_ext_ram_to_file(e, save_filename);
}

// static
void Debugger::OnAudioBufferFullThunk(HostHookContext* ctx) {
  Debugger* debugger = static_cast<Debugger*>(ctx->user_data);
  debugger->OnAudioBufferFull();
}

void Debugger::OnAudioBufferFull() {
  AudioBuffer* audio_buffer = emulator_get_audio_buffer(e);
  int size = audio_buffer->position - audio_buffer->data;

  for (int i = 0; i < kAudioDataSamples; ++i) {
    int index = (i * size / kAudioDataSamples) & ~1;
    audio_data[0][i] = audio_buffer->data[index];
    audio_data[1][i] = audio_buffer->data[index + 1];
  }
}

void Debugger::MainMenuBar() {
  if (ImGui::BeginMainMenuBar()) {
    if (ImGui::BeginMenu("Window")) {
      ImGui::MenuItem("Binjgb", NULL, &emulator_window_open);
      ImGui::MenuItem("Audio", NULL, &audio_window_open);
      ImGui::MenuItem("TileData", NULL, &tiledata_window_open);
      ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
  }
}

void Debugger::EmulatorWindow() {
  ImGui::SetNextWindowPos(ImVec2(16, 48), ImGuiSetCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(SCREEN_WIDTH * host_init.render_scale,
                                  SCREEN_HEIGHT * host_init.render_scale),
                           ImGuiSetCond_FirstUseEver);
  ImGui::SetNextWindowSizeConstraints(ImVec2(32, 32), ImVec2(4000, 4000),
                                      keep_aspect);
  if (emulator_window_open) {
    if (ImGui::Begin("Binjgb", &emulator_window_open)) {
      HostTexture* fb_texture = host_get_frame_buffer_texture(host);
      ImVec2 image_size = ImGui::GetContentRegionAvail();
      ImGui::Image(fb_texture, image_size, ImVec2(SCREEN_WIDTH, SCREEN_HEIGHT));
    }
    ImGui::End();
  }
}

void Debugger::AudioWindow() {
  if (audio_window_open) {
    ImGui::SetNextWindowPos(ImVec2(16, 640), ImGuiSetCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(640, 360), ImGuiSetCond_FirstUseEver);
    if (ImGui::Begin("Audio", &audio_window_open)) {
      EmulatorConfig config = emulator_get_config(e);
      ImGui::CheckboxNot("channel1", &config.disable_sound[CHANNEL1]);
      ImGui::CheckboxNot("channel2", &config.disable_sound[CHANNEL2]);
      ImGui::CheckboxNot("channel3", &config.disable_sound[CHANNEL3]);
      ImGui::CheckboxNot("channel4", &config.disable_sound[CHANNEL4]);
      emulator_set_config(e, &config);

      ImGui::Spacing();
      ImGui::PlotLines("left", audio_data[0], kAudioDataSamples, 0, nullptr, 0,
                       128, ImVec2(0, 80));
      ImGui::PlotLines("right", audio_data[1], kAudioDataSamples, 0, nullptr, 0,
                       128, ImVec2(0, 80));
    }
    ImGui::End();
  }
}

void Debugger::TiledataWindow() {
  if (tiledata_window_open) {
    ImGui::SetNextWindowPos(ImVec2(660, 48), ImGuiSetCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(440, 800), ImGuiSetCond_FirstUseEver);
    if (ImGui::Begin("TileData", &tiledata_window_open)) {
      static const int kPaletteCustom = 3;
      static const char* palette_names[] = {
          "BGP", "OBP0", "OBP1", "Custom",
      };
      static int scale = 3;
      static int palette_type = PALETTE_TYPE_BGP;

      ImGui::SliderInt("Scale", &scale, 1, 5);
      ImGui::Combo("Palette", &palette_type, palette_names, 4);
      Palette palette;
      if (palette_type == kPaletteCustom) {
        static Palette custom_palette = {
            {COLOR_WHITE, COLOR_LIGHT_GRAY, COLOR_DARK_GRAY, COLOR_BLACK}};

        for (int i = 0; i < 3; ++i) {
          char label[16];
          snprintf(label, sizeof(label), "Copy from %s", palette_names[i]);
          if (ImGui::Button(label)) {
            custom_palette =
                emulator_get_palette(e, static_cast<PaletteType>(i));
          }
        }

        static const char* color_names[] = {
            "White", "Light Gray", "Dark Gray", "Black",
        };
        ImGui::Combo("Color 0", &custom_palette.color[0], color_names, 4);
        ImGui::Combo("Color 1", &custom_palette.color[1], color_names, 4);
        ImGui::Combo("Color 2", &custom_palette.color[2], color_names, 4);
        ImGui::Combo("Color 3", &custom_palette.color[3], color_names, 4);
        palette = custom_palette;
      } else {
        palette = emulator_get_palette(e, (PaletteType)palette_type);
      }

      emulator_get_tile_data_buffer(e, palette, tiledata_buffer);
      ImVec2 size(TILE_DATA_TEXTURE_WIDTH, TILE_DATA_TEXTURE_HEIGHT);
      host_upload_texture(host, tiledata_texture, size.x, size.y,
                          tiledata_buffer);

      static int tw = 16;
      static bool size8x16 = false;
      ImGui::Checkbox("8x16", &size8x16);
      ImGui::SliderInt("Width", &tw, 1, 48);
      ImGui::BeginChild("Tiles", ImVec2(0, 0), false,
                        ImGuiWindowFlags_HorizontalScrollbar);
      int th = (384 + tw - 1) / tw;
      ImDrawList* draw_list = ImGui::GetWindowDrawList();
      ImVec2 cursor = ImGui::GetCursorScreenPos();
      ImVec2 duv(8 / (f32)(tiledata_texture->width),
                 8 / (f32)(tiledata_texture->height));
      int width = TILE_DATA_TEXTURE_WIDTH / 8;
      if (size8x16) {
        th = (th + 1) & ~1;
      }
      for (int ty = 0; ty < th; ++ty) {
        for (int tx = 0; tx < tw; ++tx) {
          int i;
          if (size8x16) {
            i = (ty & ~1) * tw + (tx * 2);
            if ((ty & 1) == 1) {
              i++;
            }
          } else {
            i = ty * tw + tx;
          }
          int x = i % width;
          int y = i / width;
          ImVec2 ul_pos(cursor.x + tx * scale * 8, cursor.y + ty * scale * 8);
          ImVec2 br_pos(ul_pos.x + scale * 8, ul_pos.y + scale * 8);
          ImVec2 ul_uv(x * duv.x, y * duv.y);
          ImVec2 br_uv(ul_uv.x + duv.x, ul_uv.y + duv.y);
          draw_list->AddImage((ImTextureID)tiledata_texture->handle, ul_pos,
                              br_pos, ul_uv, br_uv);
          if (ImGui::IsMouseHoveringRect(ul_pos, br_pos)) {
            ImGui::SetTooltip("tile: %u (0x%04x)", i, 0x8000 + i * 16);
          }
        }
      }
      ImGui::Dummy(ImVec2(tw * scale * 8, th * scale * 8));
      ImGui::EndChild();
    }
    ImGui::End();
  }
}

int main(int argc, char** argv) {
  const int audio_frequency = 44100;
  const int audio_frames = 2048;

  parse_arguments(argc, argv);

  Debugger debugger;
  debugger.Init(s_rom_filename, audio_frequency, audio_frames, s_font_scale);
  debugger.Run();
  return 0;
}
