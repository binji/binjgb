/*
 * Copyright (C) 2017 Ben Smith
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>

#include <algorithm>

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

static const ImVec2 kTileSize(8, 8);
static const ImVec2 k8x16OBJSize(8, 16);
static const ImVec2 kScreenSize(SCREEN_WIDTH, SCREEN_HEIGHT);
static const ImVec2 kTileMapSize(TILE_MAP_WIDTH, TILE_MAP_HEIGHT);
static const ImColor kHighlightColor(IM_COL32(0, 255, 0, 192));
static const ImColor kPCColor(IM_COL32(0, 255, 0, 192));

ImVec2 operator +(const ImVec2& lhs, const ImVec2& rhs) {
  return ImVec2(lhs.x + rhs.x, lhs.y + rhs.y);
}

ImVec2 operator -(const ImVec2& lhs, const ImVec2& rhs) {
  return ImVec2(lhs.x - rhs.x, lhs.y - rhs.y);
}

ImVec2 operator *(const ImVec2& lhs, f32 s) {
  return ImVec2(lhs.x * s, lhs.y * s);
}

ImVec2 operator *(const ImVec2& lhs, const ImVec2& rhs) {
  return ImVec2(lhs.x * rhs.x, lhs.y * rhs.y);
}

static f32 dist_squared(const ImVec2& v1, const ImVec2& v2) {
  return (v1.x - v2.x) * (v1.x - v2.x) + (v1.y - v2.y) * (v1.y - v2.y);
}

ImVec2 get_obj_size_vec2(ObjSize obj_size, f32 scale) {
  if (obj_size == OBJ_SIZE_8X16) {
    return ImVec2(k8x16OBJSize * scale);
  } else {
    return ImVec2(kTileSize * scale);
  }
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

}  // namespace ImGui

class TileImage {
 public:
  TileImage();

  void Init(Host* host);
  void Upload(Emulator*, const Palette&);
  // Return true if hovering on the tile.
  bool DrawTile(ImDrawList* draw_list, int index, const ImVec2& ul_pos,
                f32 scale, bool xflip, bool yflip);
  // Return -1 if not hovering, or tile index if hovering.
  int DrawOBJ(ImDrawList* draw_list, ObjSize obj_size, int index,
              const ImVec2& ul_pos, f32 scale, bool xflip, bool yflip);

 private:
  Host* host;
  TileData tile_data;
  HostTexture* texture;
};

TileImage::TileImage() : host(nullptr), texture(nullptr) {}

void TileImage::Init(Host* host) {
  texture = host_create_texture(host, TILE_DATA_TEXTURE_WIDTH,
                                TILE_DATA_TEXTURE_HEIGHT);
}

void TileImage::Upload(Emulator* e, const Palette& palette) {
  emulator_get_tile_data(e, palette, tile_data);
  host_upload_texture(host, texture, TILE_DATA_TEXTURE_WIDTH,
                      TILE_DATA_TEXTURE_HEIGHT, tile_data);
}

bool TileImage::DrawTile(ImDrawList* draw_list, int index, const ImVec2& ul_pos,
                         f32 scale, bool xflip = false, bool yflip = false) {
  const int width = TILE_DATA_TEXTURE_WIDTH / 8;
  ImVec2 src(index % width, index / width);
  ImVec2 duv =
      kTileSize * ImVec2(1.0f / texture->width, 1.0f / texture->height);
  ImVec2 br_pos = ul_pos + kTileSize * scale;
  ImVec2 ul_uv = src * duv;
  ImVec2 br_uv = ul_uv + duv;
  if (xflip) {
    std::swap(ul_uv.x, br_uv.x);
  }
  if (yflip) {
    std::swap(ul_uv.y, br_uv.y);
  }
  draw_list->AddImage((ImTextureID)texture->handle, ul_pos, br_pos, ul_uv,
                      br_uv);
  return ImGui::IsMouseHoveringRect(ul_pos, br_pos);
}

int TileImage::DrawOBJ(ImDrawList* draw_list, ObjSize obj_size, int tile,
                       const ImVec2& ul_pos, f32 scale, bool xflip,
                       bool yflip) {
  const ImVec2 kScaledTileSize = kTileSize * scale;
  int result = -1;
  if (obj_size == OBJ_SIZE_8X16) {
    int tile_top = tile & ~1;
    int tile_bottom = tile | 1;
    if (yflip) {
      std::swap(tile_top, tile_bottom);
    }

    if (DrawTile(draw_list, tile_top, ul_pos, scale, xflip, yflip)) {
      result = tile_top;
    }

    if (DrawTile(draw_list, tile_bottom, ul_pos + ImVec2(0, kScaledTileSize.y),
                 scale, xflip, yflip)) {
      result = tile_bottom;
    }
  } else {
    if (DrawTile(draw_list, tile, ul_pos, scale, xflip, yflip)) {
      result = tile;
    }
  }
  return result;
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
  void ObjWindow();
  void MapWindow();
  void DisassemblyWindow();

  void DrawTile(ImDrawList* draw_list, int index, const ImVec2& pos, int scale,
                bool xflip = false, bool yflip = false);

  EmulatorInit emulator_init;
  HostInit host_init;
  Emulator* e;
  Host* host;
  const char* save_filename;
  bool running;

  TileImage tiledata_image;
  TileImage obj_image[2];  // Palette 0 or 1.
  TileImage map_image;

  static const int kAudioDataSamples = 1000;
  f32 audio_data[2][kAudioDataSamples];

  bool highlight_obj;
  int highlight_obj_index;
  bool highlight_tile;
  int highlight_tile_index;

  bool emulator_window_open;
  bool audio_window_open;
  bool tiledata_window_open;
  bool obj_window_open;
  bool map_window_open;
  bool disassembly_window_open;
};

Debugger::Debugger()
    : e(nullptr),
      host(nullptr),
      save_filename(nullptr),
      running(true),
      highlight_obj(false),
      highlight_obj_index(0),
      highlight_tile(false),
      highlight_tile_index(0),
      emulator_window_open(true),
      audio_window_open(true),
      tiledata_window_open(true),
      obj_window_open(true),
      map_window_open(true),
      disassembly_window_open(true) {
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

  tiledata_image.Init(host);
  obj_image[0].Init(host);
  obj_image[1].Init(host);
  map_image.Init(host);

  save_filename = replace_extension(filename, SAVE_EXTENSION);
  ImGui::GetIO().FontGlobalScale = s_font_scale;
  return true;
}

void Debugger::Run() {
  emulator_read_ext_ram_from_file(e, save_filename);

  f64 refresh_ms = host_get_monitor_refresh_ms(host);
  while (running && host_poll_events(host)) {
    host_begin_video(host);
    host_run_ms(host, refresh_ms);

    MainMenuBar();
    EmulatorWindow();
    AudioWindow();
    TiledataWindow();
    ObjWindow();
    MapWindow();
    DisassemblyWindow();

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
    if (ImGui::BeginMenu("File")) {
      if (ImGui::MenuItem("Exit")) {
        running = false;
      }
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Window")) {
      for (int scale = 1; scale <= 4; ++scale) {
        char label[3];
        snprintf(label, sizeof(label), "%dx", scale);
        if (ImGui::MenuItem(label)) {
          // This is pretty cheesy, seems like there must be a better way.
          ImGuiStyle& style = ImGui::GetStyle();
          ImVec2 size = kScreenSize * scale + style.WindowPadding * 2;
          size.y += ImGui::GetFontSize() + style.FramePadding.y * 2;
          ImGui::SetWindowSize("Binjgb", size);
        }
      }
      ImGui::Separator();
      ImGui::MenuItem("Binjgb", NULL, &emulator_window_open);
      ImGui::MenuItem("Audio", NULL, &audio_window_open);
      ImGui::MenuItem("TileData", NULL, &tiledata_window_open);
      ImGui::MenuItem("Obj", NULL, &obj_window_open);
      ImGui::MenuItem("Map", NULL, &map_window_open);
      ImGui::MenuItem("Disassembly", NULL, &disassembly_window_open);
      ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
  }
}

void Debugger::EmulatorWindow() {
  ImGui::SetNextWindowPos(ImVec2(16, 48), ImGuiSetCond_FirstUseEver);
  ImGui::SetNextWindowSize(kScreenSize * host_init.render_scale,
                           ImGuiSetCond_FirstUseEver);
  if (emulator_window_open) {
    if (ImGui::Begin("Binjgb", &emulator_window_open)) {
      ImVec2 cursor = ImGui::GetCursorScreenPos();
      HostTexture* fb_texture = host_get_frame_buffer_texture(host);
      ImVec2 avail_size = ImGui::GetContentRegionAvail();
      f32 w = avail_size.x, h = avail_size.y;
      f32 aspect = w / h;
      f32 want_aspect = (f32)SCREEN_WIDTH / SCREEN_HEIGHT;
      ImVec2 image_size(aspect < want_aspect ? w : h * want_aspect,
                        aspect < want_aspect ? w / want_aspect : h);

      ImDrawList* draw_list = ImGui::GetWindowDrawList();
      ImVec2 image_ul = cursor + (avail_size - image_size) * 0.5f;
      ImVec2 image_br = image_ul + image_size;
      draw_list->PushClipRect(image_ul, image_br);

      ImVec2 ul_uv(0, 0);
      ImVec2 br_uv((f32)SCREEN_WIDTH / fb_texture->width,
                   (f32)SCREEN_HEIGHT / fb_texture->height);
      draw_list->AddImage((ImTextureID)fb_texture->handle, image_ul, image_br,
                          ul_uv, br_uv);

      if (highlight_obj) {
        f32 scale = image_size.x / SCREEN_WIDTH;
        ObjSize obj_size = emulator_get_obj_size(e);
        Obj obj = emulator_get_obj(e, highlight_obj_index);

        // The OBJ position is already offset so it draws from the top-left,
        // but this means that the coordinates are sometimes positive when they
        // should be negative (e.g. 255 should be drawn as -1). This code adds
        // the offset back in, wrapped to 255, and draws from the bottom-right
        // instead.
        ImVec2 obj_pos(static_cast<u8>(obj.x + OBJ_X_OFFSET),
                       static_cast<u8>(obj.y + OBJ_Y_OFFSET));
        ImVec2 br_pos = image_ul + obj_pos * scale;
        ImVec2 ul_pos = br_pos - k8x16OBJSize * scale;
        if (obj_size == OBJ_SIZE_8X8) {
          br_pos.y -= kTileSize.y * scale;
        }
        draw_list->AddRectFilled(ul_pos, br_pos, kHighlightColor);
      }

      draw_list->PopClipRect();
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

      tiledata_image.Upload(e, palette);

      static int tw = 16;
      static bool size8x16 = false;
      ImGui::Checkbox("8x16", &size8x16);
      ImGui::SliderInt("Width", &tw, 1, 48);
      ImGui::BeginChild("Tiles", ImVec2(0, 0), false,
                        ImGuiWindowFlags_HorizontalScrollbar);
      int th = (384 + tw - 1) / tw;
      ImDrawList* draw_list = ImGui::GetWindowDrawList();
      ImVec2 cursor = ImGui::GetCursorScreenPos();
      int width = TILE_DATA_TEXTURE_WIDTH / kTileSize.x;
      if (size8x16) {
        th = (th + 1) & ~1;
      }
      ImVec2 scaled_tile_size = kTileSize * scale;
      for (int ty = 0; ty < th; ++ty) {
        for (int tx = 0; tx < tw; ++tx) {
          int tile_index;
          if (size8x16) {
            tile_index = (ty & ~1) * tw + (tx * 2);
            if ((ty & 1) == 1) {
              tile_index++;
            }
          } else {
            tile_index = ty * tw + tx;
          }
          ImVec2 ul_pos = cursor + ImVec2(tx, ty) * scaled_tile_size;
          ImVec2 br_pos = ul_pos + scaled_tile_size;
          bool is_hovering =
              tiledata_image.DrawTile(draw_list, tile_index, ul_pos, scale);
          if (highlight_tile && highlight_tile_index == tile_index) {
            ImGui::Text("highlight: %d\n", highlight_tile_index);
            draw_list->AddRectFilled(ul_pos, br_pos, kHighlightColor);
          }
          if (is_hovering) {
            ImGui::SetTooltip("tile: %u (0x%04x)", tile_index,
                              0x8000 + tile_index * 16);
          }
        }
      }
      highlight_tile = false;
      ImGui::Dummy(ImVec2(tw, th) * scaled_tile_size);
      ImGui::EndChild();
    }
    ImGui::End();
  }
}

void Debugger::ObjWindow() {
  if (obj_window_open) {
    ImGui::SetNextWindowPos(ImVec2(660, 860), ImGuiSetCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(440, 516), ImGuiSetCond_FirstUseEver);
    if (ImGui::Begin("Obj", &obj_window_open)) {
      static int scale = 4;
      static int obj_index = 0;

      obj_image[0].Upload(e, emulator_get_palette(e, PALETTE_TYPE_OBP0));
      obj_image[1].Upload(e, emulator_get_palette(e, PALETTE_TYPE_OBP1));

      ObjSize obj_size = emulator_get_obj_size(e);
      ImDrawList* draw_list = ImGui::GetWindowDrawList();

      for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 10; ++x) {
          int button_index = y * 10 + x;
          Obj obj = emulator_get_obj(e, button_index);
          bool visible = static_cast<bool>(obj_is_visible(&obj));

          char label[16];
          snprintf(label, sizeof(label), "%2d", button_index);
          if (x > 0) {
            ImGui::SameLine();
          }

          ImVec2 button_size = get_obj_size_vec2(obj_size, scale);
          bool clicked;
          if (visible) {
            int tile_index = obj_image[obj.palette].DrawOBJ(
                draw_list, obj_size, obj.tile, ImGui::GetCursorScreenPos(),
                scale, obj.xflip, obj.yflip);
            if (tile_index >= 0) {
              highlight_tile = true;
              highlight_tile_index = tile_index;
            }
            clicked = ImGui::InvisibleButton(label, button_size);
          } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImColor(0, 0, 0));
            clicked = ImGui::Button(label, button_size);
            ImGui::PopStyleColor();
          }
          if (clicked) {
            highlight_obj_index = obj_index = button_index;
          }
          if (obj_index == button_index) {
            ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(),
                                                ImGui::GetItemRectMax(),
                                                IM_COL32_WHITE);
          }
        }
      }

      ImGui::Checkbox("Highlight OBJ", &highlight_obj);
      ImGui::Separator();

      Obj obj = emulator_get_obj(e, obj_index);

      ImGui::LabelText("Index", "%d", obj_index);
      ImGui::LabelText("Tile", "%d", obj.tile);
      ImGui::LabelText("Pos", "%d, %d", obj.x, obj.y);
      ImGui::LabelText("Priority", "%s", obj.priority == OBJ_PRIORITY_ABOVE_BG
                                             ? "Above BG"
                                             : "Behind BG");
      ImGui::LabelText("Flip", "%c%c", obj.xflip ? 'X' : '_',
                       obj.yflip ? 'Y' : '_');
      ImGui::LabelText("Palette", "OBP%d", obj.palette);

    }
    ImGui::End();
  }
}

void Debugger::MapWindow() {
  if (map_window_open) {
    ImGui::SetNextWindowPos(ImVec2(1719, 50), ImGuiSetCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(800, 1014), ImGuiSetCond_FirstUseEver);
    if (ImGui::Begin("Map", &map_window_open)) {
      static int scale = 3;
      static const char* layer_names[] = {
          "BG", "Window",
      };
      static LayerType layer_type = LAYER_TYPE_BG;
      static bool highlight = true;

      ImGui::SliderInt("Scale", &scale, 1, 5);
      ImGui::Combo("Layer", &layer_type, layer_names, 2);
      ImGui::Checkbox("Highlight", &highlight);
      ImGui::Separator();

      bool display = false;
      u8 scroll_x, scroll_y;
      switch (layer_type) {
        case LAYER_TYPE_BG:
          display = emulator_get_bg_display(e);
          emulator_get_bg_scroll(e, &scroll_x, &scroll_y);
          break;
        case LAYER_TYPE_WINDOW:
          display = emulator_get_window_display(e);
          emulator_get_window_scroll(e, &scroll_x, &scroll_y);
          break;
      }

      ImGui::LabelText("Display", "%s", display ? "On" : "Off");
      ImGui::LabelText("Scroll", "%d, %d", scroll_x, scroll_y);

      TileMapSelect map_select = emulator_get_tile_map_select(e, layer_type);
      TileDataSelect data_select = emulator_get_tile_data_select(e);
      TileMap tile_map;
      emulator_get_tile_map(e, map_select, tile_map);

      map_image.Upload(e, emulator_get_palette(e, PALETTE_TYPE_BGP));

      const ImVec2 scaled_tile_size = kTileSize * scale;
      const ImVec2 scaled_tile_map_size = kTileMapSize * scaled_tile_size;
      ImGui::BeginChild("Tiles", ImVec2(0, 0), false,
                        ImGuiWindowFlags_HorizontalScrollbar);
      ImDrawList* draw_list = ImGui::GetWindowDrawList();
      ImVec2 cursor = ImGui::GetCursorScreenPos();
      ImGui::PushClipRect(cursor, cursor + scaled_tile_map_size, true);
      for (int ty = 0; ty < TILE_MAP_HEIGHT; ++ty) {
        for (int tx = 0; tx < TILE_MAP_WIDTH; ++tx) {
          ImVec2 ul_pos = cursor + ImVec2(tx, ty) * scaled_tile_size;
          int tile_index = tile_map[ty * TILE_MAP_WIDTH + tx];
          if (data_select == TILE_DATA_8800_97FF) {
            tile_index = 256 + (s8)tile_index;
          }
          if (map_image.DrawTile(draw_list, tile_index, ul_pos, scale)) {
            ImGui::SetTooltip("tile: %u (0x%04x)", tile_index,
                              0x8000 + tile_index * 16);
            highlight_tile = true;
            highlight_tile_index = tile_index;
          }
        }
      }

      if (display && highlight) {
        // The BG layer wraps, but the window layer doesn't. Also, the window
        // layer always displays the lower-right corner of its map.
        switch (layer_type) {
          case LAYER_TYPE_BG: {
            ImVec2 ul_pos = cursor + ImVec2(scroll_x, scroll_y) * scale;
            ImVec2 br_pos = ul_pos + kScreenSize * scale;
            for (int y = -1; y <= 0; ++y) {
              for (int x = -1; x <= 0; ++x) {
                ImVec2 offset = ImVec2(x, y) * scaled_tile_map_size;
                draw_list->AddRect(ul_pos + offset, br_pos + offset,
                                   kHighlightColor, 0, ~0, 4.0f);
              }
            }
            break;
          }
          case LAYER_TYPE_WINDOW: {
            ImVec2 ul_pos = cursor;
            ImVec2 br_pos =
                ul_pos + (kScreenSize - ImVec2(scroll_x, scroll_y)) * scale;
            draw_list->AddRect(ul_pos, br_pos, kHighlightColor, 0, ~0, 4.0f);
            break;
          }
        }
      }

      ImGui::PopClipRect();
      ImGui::Dummy(scaled_tile_map_size);
      ImGui::EndChild();
    }
    ImGui::End();
  }
}

static Address step_forward_by_instruction(Emulator* e, Address from_addr) {
  return std::min(from_addr + emulator_opcode_bytes(e, from_addr), 0xffff);
}

static Address step_backward_by_instruction(Emulator* e, Address from_addr) {
  // Iterate from |from_addr| - MAX to |from_addr| - 1, adding up the number of
  // paths that lead to each instruction. Finally, choose the instruction whose
  // next instruction is |from_addr| and has the most paths leading up to it.
  // Since instructions are 1, 2, or 3 bytes, we only have to inspect counts
  // 1..3.
  const int MAX = 16;
  int count[MAX];
  ZERO_MEMORY(count);
  for (int i = std::min<int>(from_addr, MAX) - 1; i > 0; --i) {
    Address next = step_forward_by_instruction(e, from_addr - i);
    if (next <= from_addr) {
      // Give a "bonus" to instructions whose next is |from_addr|.
      count[i] += (next == from_addr ? MAX : 1);
      count[from_addr - next] += count[i];
    }
  }
  int* best = std::max_element(count + 1, count + 4);
  return std::max<int>(from_addr - (best - count), 0);
}

void Debugger::DisassemblyWindow() {
  if (disassembly_window_open) {
    ImGui::SetNextWindowPos(ImVec2(1113, 51), ImGuiSetCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(590, 1326), ImGuiSetCond_FirstUseEver);
    if (ImGui::Begin("Disassembly", &disassembly_window_open)) {
      static bool track_pc = false;
      static Address addr = 0;

      Registers regs = emulator_get_registers(e);
      ImGui::Text("A: %02X", regs.A);
      ImGui::Text("B: %02X C: %02X BC: %04X", regs.B, regs.C, regs.BC);
      ImGui::Text("D: %02X E: %02X DE: %04X", regs.D, regs.E, regs.DE);
      ImGui::Text("H: %02X L: %02X HL: %04X", regs.H, regs.L, regs.HL);
      ImGui::Text("SP: %04X", regs.SP);
      ImGui::Text("PC: %04X", regs.PC);
      ImGui::Text("%c%c%c%c", regs.F.Z ? 'Z' : '_', regs.F.N ? 'N' : '_',
                  regs.F.H ? 'H' : '_', regs.F.C ? 'C' : '_');
      ImGui::Separator();

      ImGui::PushButtonRepeat(true);
      if (ImGui::Button("-1")) {
        addr = std::max(addr - 1, 0);
        track_pc = false;
      }
      ImGui::SameLine();
      if (ImGui::Button("+1")) {
        addr = std::min(addr + 1, 0xffff);
        track_pc = false;
      }
      ImGui::SameLine();
      if (ImGui::Button("-I")) {
        addr = step_backward_by_instruction(e, addr);
        track_pc = false;
      }
      ImGui::SameLine();
      if (ImGui::Button("+I")) {
        addr = step_forward_by_instruction(e, addr);
        track_pc = false;
      }
      ImGui::PopButtonRepeat();
      ImGui::SameLine();
      ImGui::Checkbox("Track PC", &track_pc);
      ImGui::Separator();

      if (track_pc) {
        addr = regs.PC;
      }

      float height = ImGui::GetTextLineHeightWithSpacing();
      Address i = addr;
      while (ImGui::GetContentRegionAvail().y >= height) {
        char buffer[64];
        bool is_pc = i == regs.PC;
        i += emulator_disassemble(e, i, buffer, sizeof(buffer));
        if (is_pc) {
          ImGui::TextColored(kPCColor, buffer);
        } else {
          ImGui::Text(buffer);
        }
      }
    }
    ImGui::End();
  }
}

int main(int argc, char** argv) {
  const int audio_frequency = 44100;
  const int audio_frames = 2048;

  parse_arguments(argc, argv);

  Debugger debugger;
  if (!debugger.Init(s_rom_filename, audio_frequency, audio_frames,
                     s_font_scale)) {
    return 1;
  }
  debugger.Run();
  return 0;
}
