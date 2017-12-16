/*
 * Copyright (C) 2017 Ben Smith
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include <assert.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>

#include <algorithm>
#include <utility>

#include "emulator-debug.h"
#include "options.h"
#include "host.h"

#include "imgui.h"
#include "imgui_dock.h"
#include "imgui_memory_editor.h"

#define SAVE_EXTENSION ".sav"
#define SAVE_STATE_EXTENSION ".state"

static const char* s_rom_filename;
static f32 s_font_scale = 1.0f;
static bool s_paused_at_start;

static void usage(int argc, char** argv) {
  PRINT_ERROR(
      "usage: %s [options] <in.gb>\n"
      "  -h,--help          help\n"
      "  -t,--trace         trace each instruction\n"
      "  -f,--font-scale=F  set the global font scale factor to F\n"
      "  -l,--log S=N       set log level for system S to N\n\n"
      "  -p,--pause         pause at start\n",
      argv[0]);

  emulator_print_log_systems();
}

void parse_arguments(int argc, char** argv) {
  static const Option options[] = {
    {'h', "help", 0},
    {'t', "trace", 0},
    {'f', "font-scale", 1},
    {'l', "log", 1},
    {'p', "pause", 0},
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

          case 'l':
            switch (emulator_set_log_level_from_string(result.value)) {
              case SET_LOG_LEVEL_ERROR_NONE:
                break;

              case SET_LOG_LEVEL_ERROR_INVALID_FORMAT:
                PRINT_ERROR("invalid log level format, should be S=N\n");
                break;

              case SET_LOG_LEVEL_ERROR_UNKNOWN_LOG_SYSTEM: {
                const char* equals = strchr(result.value, '=');
                PRINT_ERROR("unknown log system: %.*s\n",
                            (int)(equals - result.value), result.value);
                emulator_print_log_systems();
                break;
              }
            }
            break;

          case 'p':
            s_paused_at_start = true;
            break;

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
static const ImU32 kHighlightColor(IM_COL32(0, 255, 0, 192));
static const ImVec4 kPCColor(0, 255, 0, 192);

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

void SetPaletteAndEnable(Host* host, ImDrawList* draw_list,
                         const PaletteRGBA& palette) {
  using Context = std::pair<Host*, PaletteRGBA>;
  auto func = [](const ImDrawList*, const ImDrawCmd* cmd) {
    Context* ctx = static_cast<Context*>(cmd->UserCallbackData);
    Host* host = ctx->first;
    host_set_palette(host, ctx->second.color);
    host_enable_palette(host, TRUE);
    delete ctx;
  };

  draw_list->AddCallback(func, new Context(host, palette));
}

void DisablePalette(Host* host, ImDrawList* draw_list) {
  auto func = [](const ImDrawList*, const ImDrawCmd* cmd) {
    Host* host = static_cast<Host*>(cmd->UserCallbackData);
    host_enable_palette(host, FALSE);
  };

  draw_list->AddCallback(func, host);
}

class TileImage {
 public:
  TileImage();

  void Init(Host* host);
  void Upload(Emulator*);
  // Return true if hovering on the tile.
  bool DrawTile(ImDrawList* draw_list, int index, const ImVec2& ul_pos,
                f32 scale, PaletteRGBA palette, bool xflip = false,
                bool yflip = false);
  // Return -1 if not hovering, or tile index if hovering.
  int DrawOBJ(ImDrawList* draw_list, ObjSize obj_size, int index,
              const ImVec2& ul_pos, f32 scale, PaletteRGBA palette, bool xflip,
              bool yflip);

 private:
  Host* host;
  TileData tile_data;
  HostTexture* texture;
};

TileImage::TileImage() : host(nullptr), texture(nullptr) {}

void TileImage::Init(Host* h) {
  host = h;
  texture =
      host_create_texture(host, TILE_DATA_TEXTURE_WIDTH,
                          TILE_DATA_TEXTURE_HEIGHT, HOST_TEXTURE_FORMAT_U8);
}

void TileImage::Upload(Emulator* e) {
  emulator_get_tile_data(e, tile_data);
  host_upload_texture(host, texture, TILE_DATA_TEXTURE_WIDTH,
                      TILE_DATA_TEXTURE_HEIGHT, tile_data);
}

bool TileImage::DrawTile(ImDrawList* draw_list, int index, const ImVec2& ul_pos,
                         f32 scale, PaletteRGBA palette, bool xflip,
                         bool yflip) {
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
  SetPaletteAndEnable(host, draw_list, palette);
  draw_list->AddImage((ImTextureID)texture->handle, ul_pos, br_pos, ul_uv,
                      br_uv);
  DisablePalette(host, draw_list);
  return ImGui::IsMouseHoveringRect(ul_pos, br_pos);
}

int TileImage::DrawOBJ(ImDrawList* draw_list, ObjSize obj_size, int tile,
                       const ImVec2& ul_pos, f32 scale, PaletteRGBA palette,
                       bool xflip, bool yflip) {
  const ImVec2 kScaledTileSize = kTileSize * scale;
  int result = -1;
  if (obj_size == OBJ_SIZE_8X16) {
    int tile_top = tile & ~1;
    int tile_bottom = tile | 1;
    if (yflip) {
      std::swap(tile_top, tile_bottom);
    }

    if (DrawTile(draw_list, tile_top, ul_pos, scale, palette, xflip, yflip)) {
      result = tile_top;
    }

    if (DrawTile(draw_list, tile_bottom, ul_pos + ImVec2(0, kScaledTileSize.y),
                 scale, palette, xflip, yflip)) {
      result = tile_bottom;
    }
  } else {
    if (DrawTile(draw_list, tile, ul_pos, scale, palette, xflip, yflip)) {
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
  void OnAudioBufferFull();
  void OnKeyDown(HostKeycode);
  void OnKeyUp(HostKeycode);

  void WriteStateToFile();
  void ReadStateFromFile();

  void SetAudioVolume(f32 volume);

  void MainMenuBar();
  void EmulatorWindow();
  void AudioWindow();
  void TiledataWindow();
  void ObjWindow();
  void MapWindow();
  void DisassemblyWindow();
  void MemoryWindow();

  u8 MemoryEditorRead(Address addr);
  void MemoryEditorWrite(Address addr, u8 value);

  EmulatorInit emulator_init;
  HostInit host_init;
  Emulator* e = nullptr;
  Host* host = nullptr;
  const char* save_filename = nullptr;
  const char* save_state_filename = nullptr;
  bool running = true;
  bool paused = false;
  bool step_frame = false;

  TileImage tiledata_image;

  static const int kAudioDataSamples = 1000;
  f32 audio_data[2][kAudioDataSamples];
  f32 audio_volume = 0.5f;

  bool highlight_obj = false;
  int highlight_obj_index = 0;
  bool highlight_tile = false;
  int highlight_tile_index = 0;

  MemoryEditor memory_editor;

  bool emulator_window_open = true;
  bool audio_window_open = true;
  bool tiledata_window_open = true;
  bool obj_window_open = true;
  bool map_window_open = true;
  bool disassembly_window_open = true;
  bool memory_window_open = true;
};

Debugger::Debugger() {
  ZERO_MEMORY(audio_data);
  paused = s_paused_at_start;
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
  host_init.audio_volume = audio_volume;
  host_init.hooks.user_data = this;
  host_init.hooks.audio_buffer_full = [](HostHookContext* ctx) {
    static_cast<Debugger*>(ctx->user_data)->OnAudioBufferFull();
  };
  host_init.hooks.key_down = [](HostHookContext* ctx, HostKeycode code) {
    static_cast<Debugger*>(ctx->user_data)->OnKeyDown(code);
  };
  host_init.hooks.key_up = [](HostHookContext* ctx, HostKeycode code) {
    static_cast<Debugger*>(ctx->user_data)->OnKeyUp(code);
  };
  host = host_new(&host_init, e);
  if (host == nullptr) {
    return false;
  }

  tiledata_image.Init(host);

  save_filename = replace_extension(filename, SAVE_EXTENSION);
  save_state_filename = replace_extension(filename, SAVE_STATE_EXTENSION);
  ImGui::GetIO().FontGlobalScale = s_font_scale;

  memory_editor.UserData = this;
  memory_editor.ReadFn = [](u8*, size_t offset, void* user_data) {
    return static_cast<Debugger*>(user_data)->MemoryEditorRead(offset);
  };
  memory_editor.WriteFn = [](u8*, size_t offset, u8 value, void* user_data) {
    static_cast<Debugger*>(user_data)->MemoryEditorWrite(offset, value);
  };

  return true;
}

void Debugger::Run() {
  emulator_read_ext_ram_from_file(e, save_filename);

  f64 refresh_ms = host_get_monitor_refresh_ms(host);
  while (running && host_poll_events(host)) {
    host_begin_video(host);
    if (!paused) {
      host_run_ms(host, refresh_ms);
      if (step_frame) {
        host_reset_audio(host);
        step_frame = false;
        paused = true;
      }
    }

    tiledata_image.Upload(e);

    // Create a frameless top-level window to hold the workspace.
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar;

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    if (ImGui::Begin("##root", nullptr, flags)) {
      MainMenuBar();
      ImGui::BeginWorkspace();
      EmulatorWindow();
      AudioWindow();
      TiledataWindow();
      ObjWindow();
      MapWindow();
      DisassemblyWindow();
      MemoryWindow();
      ImGui::EndWorkspace();
    }

    ImGui::End();

    host_end_video(host);
  }

  emulator_write_ext_ram_to_file(e, save_filename);
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

static void Toggle(Bool& value) { value = static_cast<Bool>(!value); }
static void Toggle(bool& value) { value = !value; }

void Debugger::OnKeyDown(HostKeycode code) {
  EmulatorConfig emu_config = emulator_get_config(e);
  HostConfig host_config = host_get_config(host);

  switch (code) {
    case HOST_KEYCODE_1: Toggle(emu_config.disable_sound[APU_CHANNEL1]); break;
    case HOST_KEYCODE_2: Toggle(emu_config.disable_sound[APU_CHANNEL2]); break;
    case HOST_KEYCODE_3: Toggle(emu_config.disable_sound[APU_CHANNEL3]); break;
    case HOST_KEYCODE_4: Toggle(emu_config.disable_sound[APU_CHANNEL4]); break;
    case HOST_KEYCODE_B: Toggle(emu_config.disable_bg); break;
    case HOST_KEYCODE_W: Toggle(emu_config.disable_window); break;
    case HOST_KEYCODE_O: Toggle(emu_config.disable_obj); break;
    case HOST_KEYCODE_F6: WriteStateToFile(); break;
    case HOST_KEYCODE_F9: ReadStateFromFile(); break;
    case HOST_KEYCODE_N: step_frame = true; paused = false; break;
    case HOST_KEYCODE_SPACE: Toggle(paused); break;
    case HOST_KEYCODE_ESCAPE: running = false; break;
    case HOST_KEYCODE_TAB: host_config.no_sync = TRUE; break;
    case HOST_KEYCODE_MINUS: SetAudioVolume(audio_volume - 0.05f); break;
    case HOST_KEYCODE_EQUALS: SetAudioVolume(audio_volume + 0.05f); break;
    default: return;
  }

  emulator_set_config(e, &emu_config);
  host_set_config(host, &host_config);
}

void Debugger::OnKeyUp(HostKeycode code) {
  HostConfig host_config = host_get_config(host);

  switch (code) {
    case HOST_KEYCODE_TAB: host_config.no_sync = FALSE; break;
    case HOST_KEYCODE_F11: Toggle(host_config.fullscreen); break;
    default: return;
  }

  host_set_config(host, &host_config);
}

void Debugger::WriteStateToFile() {
  emulator_write_state_to_file(e, save_state_filename);
}

void Debugger::ReadStateFromFile() {
  emulator_read_state_from_file(e, save_state_filename);
}

void Debugger::SetAudioVolume(f32 volume) {
  audio_volume = CLAMP(volume, 0, 1);
  host_set_audio_volume(host, audio_volume);
}

void Debugger::MainMenuBar() {
  if (ImGui::BeginMenuBar()) {
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
      ImGui::MenuItem("Memory", NULL, &memory_window_open);
      ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
  }
}

void Debugger::EmulatorWindow() {
  ImGui::SetNextDock(ImGuiDockSlot_Tab);
  if (ImGui::BeginDock("Binjgb", &emulator_window_open)) {
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
  ImGui::EndDock();
}

void Debugger::AudioWindow() {
  ImGui::SetNextDock(ImGuiDockSlot_Bottom);
  if (ImGui::BeginDock("Audio", &audio_window_open)) {
    EmulatorConfig config = emulator_get_config(e);
    ImGui::Text("channel enable");
    ImGui::SameLine(0, 20);
    ImGui::CheckboxNot("1", &config.disable_sound[APU_CHANNEL1]);
    ImGui::SameLine();
    ImGui::CheckboxNot("2", &config.disable_sound[APU_CHANNEL2]);
    ImGui::SameLine();
    ImGui::CheckboxNot("3", &config.disable_sound[APU_CHANNEL3]);
    ImGui::SameLine();
    ImGui::CheckboxNot("4", &config.disable_sound[APU_CHANNEL4]);
    emulator_set_config(e, &config);
    if (ImGui::SliderFloat("Volume", &audio_volume, 0, 1)) {
      audio_volume = CLAMP(audio_volume, 0, 1);
      host_set_audio_volume(host, audio_volume);
    }

    ImGui::Spacing();
    ImGui::PlotLines("left", audio_data[0], kAudioDataSamples, 0, nullptr, 0,
                     128, ImVec2(0, 80));
    ImGui::PlotLines("right", audio_data[1], kAudioDataSamples, 0, nullptr, 0,
                     128, ImVec2(0, 80));
  }
  ImGui::EndDock();
}

void Debugger::TiledataWindow() {
  ImGui::SetNextDockParentToRoot();
  ImGui::SetNextDock(ImGuiDockSlot_Right);
  if (ImGui::BeginDock("TileData", &tiledata_window_open)) {
    static const int kPaletteCustom = 3;
    static const char* palette_names[] = {
        "BGP",
        "OBP0",
        "OBP1",
        "Custom",
    };
    static int scale = 3;
    static int palette_type = PALETTE_TYPE_BGP;

    ImGui::SliderInt("Scale", &scale, 1, 5);
    ImGui::Combo("Palette", &palette_type, palette_names, 4);
    PaletteRGBA palette_rgba;

    if (palette_type == kPaletteCustom) {
      static Palette custom_palette = {
          {COLOR_WHITE, COLOR_LIGHT_GRAY, COLOR_DARK_GRAY, COLOR_BLACK}};

      for (int i = 0; i < 3; ++i) {
        char label[16];
        snprintf(label, sizeof(label), "Copy from %s", palette_names[i]);
        if (ImGui::Button(label)) {
          custom_palette = emulator_get_palette(e, static_cast<PaletteType>(i));
        }
      }

      static const char* color_names[] = {
          "White",
          "Light Gray",
          "Dark Gray",
          "Black",
      };
      ImGui::Combo("Color 0", &custom_palette.color[0], color_names, 4);
      ImGui::Combo("Color 1", &custom_palette.color[1], color_names, 4);
      ImGui::Combo("Color 2", &custom_palette.color[2], color_names, 4);
      ImGui::Combo("Color 3", &custom_palette.color[3], color_names, 4);
      palette_rgba = palette_to_palette_rgba(custom_palette);
    } else {
      palette_rgba = emulator_get_palette_rgba(e, (PaletteType)palette_type);
    }

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
        bool is_hovering = tiledata_image.DrawTile(draw_list, tile_index,
                                                   ul_pos, scale, palette_rgba);
        if (highlight_tile && highlight_tile_index == tile_index) {
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
  ImGui::EndDock();
}

void Debugger::ObjWindow() {
  ImGui::SetNextDock(ImGuiDockSlot_Tab);
  if (ImGui::BeginDock("Obj", &obj_window_open)) {
    static int scale = 4;
    static int obj_index = 0;

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
          PaletteRGBA palette_rgba = emulator_get_palette_rgba(
              e, (PaletteType)(PALETTE_TYPE_OBP0 + obj.palette));

          int tile_index = tiledata_image.DrawOBJ(
              draw_list, obj_size, obj.tile, ImGui::GetCursorScreenPos(), scale,
              palette_rgba, obj.xflip, obj.yflip);

          if (tile_index >= 0) {
            highlight_tile = true;
            highlight_tile_index = tile_index;
          }
          clicked = ImGui::InvisibleButton(label, button_size);
        } else {
          ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32_BLACK);
          clicked = ImGui::Button(label, button_size);
          ImGui::PopStyleColor();
        }
        if (clicked) {
          highlight_obj_index = obj_index = button_index;
        }
        if (obj_index == button_index) {
          ImGui::GetWindowDrawList()->AddRect(
              ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32_WHITE);
        }
      }
    }

    ImGui::Checkbox("Highlight OBJ", &highlight_obj);
    ImGui::Separator();

    Obj obj = emulator_get_obj(e, obj_index);

    ImGui::LabelText("Index", "%d", obj_index);
    ImGui::LabelText("Tile", "%d", obj.tile);
    ImGui::LabelText("Pos", "%d, %d", obj.x, obj.y);
    ImGui::LabelText(
        "Priority", "%s",
        obj.priority == OBJ_PRIORITY_ABOVE_BG ? "Above BG" : "Behind BG");
    ImGui::LabelText("Flip", "%c%c", obj.xflip ? 'X' : '_',
                     obj.yflip ? 'Y' : '_');
    ImGui::LabelText("Palette", "OBP%d", obj.palette);
  }
  ImGui::EndDock();
}

void Debugger::MapWindow() {
  ImGui::SetNextDock(ImGuiDockSlot_Tab);
  if (ImGui::BeginDock("Map", &map_window_open)) {
    static int scale = 3;
    static const char* layer_names[] = {
        "BG",
        "Window",
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
    PaletteRGBA palette_rgba = emulator_get_palette_rgba(e, PALETTE_TYPE_BGP);

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
        if (tiledata_image.DrawTile(draw_list, tile_index, ul_pos, scale,
                                    palette_rgba)) {
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
  ImGui::EndDock();
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
  ImGui::SetNextDock(ImGuiDockSlot_Right);
  if (ImGui::BeginDock("Disassembly", &disassembly_window_open)) {
    static bool track_pc = true;
    static Address start_addr = 0;

    Registers regs = emulator_get_registers(e);
    ImGui::Text("Cycles: %" PRIu64 "", emulator_get_cycles(e));
    ImGui::Text("A: %02X", regs.A);
    ImGui::Text("B: %02X C: %02X BC: %04X", regs.B, regs.C, regs.BC);
    ImGui::Text("D: %02X E: %02X DE: %04X", regs.D, regs.E, regs.DE);
    ImGui::Text("H: %02X L: %02X HL: %04X", regs.H, regs.L, regs.HL);
    ImGui::Text("SP: %04X", regs.SP);
    ImGui::Text("PC: %04X", regs.PC);
    ImGui::Text("F: %c%c%c%c", regs.F.Z ? 'Z' : '_', regs.F.N ? 'N' : '_',
                regs.F.H ? 'H' : '_', regs.F.C ? 'C' : '_');
    ImGui::Separator();

    ImGui::PushButtonRepeat(true);
    if (ImGui::Button("-1")) {
      start_addr = std::max(start_addr - 1, 0);
      track_pc = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("+1")) {
      start_addr = std::min(start_addr + 1, 0xffff);
      track_pc = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("-I")) {
      start_addr = step_backward_by_instruction(e, start_addr);
      track_pc = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("+I")) {
      start_addr = step_forward_by_instruction(e, start_addr);
      track_pc = false;
    }
    ImGui::PopButtonRepeat();
    ImGui::SameLine();
    ImGui::Checkbox("Track PC", &track_pc);
    ImGui::Separator();

    ImGui::PushButtonRepeat(true);
    if (ImGui::Button("step")) {
      host_step(host);
      paused = true;
    }
    ImGui::PopButtonRepeat();

    f32 height = ImGui::GetTextLineHeightWithSpacing();
    int lines = static_cast<int>(ImGui::GetContentRegionAvail().y / height);

    // When tracking the PC, determine whether PC is in currently visible
    // range; if it's not, adjust the view so it is.
    if (track_pc) {
      Address addr = start_addr;
      for (int i = 0; i < lines; ++i) {
        addr += emulator_opcode_bytes(e, addr);
      }
      if (regs.PC < start_addr || regs.PC > addr) {
        start_addr = regs.PC;
        // Step backward by half the height to center the instruction at PC.
        for (int j = 0; j < lines / 2 - 1; ++j) {
          start_addr = step_backward_by_instruction(e, start_addr);
        }
      }
    }

    Address addr = start_addr;
    for (int i = 0; i < lines; ++i) {
      char buffer[64];
      bool is_pc = addr == regs.PC;
      addr += emulator_disassemble(e, addr, buffer, sizeof(buffer));
      if (is_pc) {
        ImGui::TextColored(kPCColor, "%s", buffer);
      } else {
        ImGui::Text("%s", buffer);
      }
    }
  }
  ImGui::EndDock();
}

void Debugger::MemoryWindow() {
  ImGui::SetNextDock(ImGuiDockSlot_Tab);
  if (ImGui::BeginDock("Memory", &memory_window_open)) {
    memory_editor.DrawContents(nullptr, 0x10000);
  }
  ImGui::EndDock();
}

u8 Debugger::MemoryEditorRead(Address addr) {
  return emulator_read_u8_raw(e, addr);
}

void Debugger::MemoryEditorWrite(Address addr, u8 value) {
  return emulator_write_u8_raw(e, addr, value);
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
