/*
 * Copyright (C) 2017 Ben Smith
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include "debugger.h"

#include <inttypes.h>

#include <algorithm>
#include <string>
#include <utility>

#include "imgui.h"
#include "imgui_dock.h"

#define SAVE_EXTENSION ".sav"
#define SAVE_STATE_EXTENSION ".state"
#define ROM_USAGE_EXTENSION ".romusage"

static const ImVec2 kTileSize(8, 8);
static const ImVec2 k8x16OBJSize(8, 16);
static const ImVec2 kScreenSize(SCREEN_WIDTH, SCREEN_HEIGHT);
static const ImVec2 kTileMapSize(TILE_MAP_WIDTH, TILE_MAP_HEIGHT);
static const ImU32 kHighlightColor(IM_COL32(0, 255, 0, 192));
static const ImVec4 kPCColor(0.2f, 1.f, 0.1f, 1.f);
static const ImVec4 kRegColor(1.f, 0.75f, 0.3f, 1.f);

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

template <typename T, size_t N>
bool Combo(const char* label, T* value, const char* (&names)[N]) {
  int int_value = static_cast<int>(*value);
  bool result = Combo(label, &int_value, names, N);
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

Debugger::Debugger() {}

Debugger::~Debugger() {
  file_data_delete(&reverse_step_save_state);
  emulator_delete(e);
  host_delete(host);
}

bool Debugger::Init(const char* filename, int audio_frequency, int audio_frames,
                    int font_scale, bool paused_at_start) {
  FileData rom;
  if (!SUCCESS(file_read(filename, &rom))) {
    return false;
  }

  run_state = paused_at_start ? Paused : Running;

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
  // TODO: make these configurable?
  host_init.rewind.frames_per_base_state = 45;
  host_init.rewind.buffer_capacity = MEGABYTES(32);
  host = host_new(&host_init, e);
  if (host == nullptr) {
    return false;
  }

  tiledata_image.Init(host);

  int rom_size = emulator_get_rom_size(e);
  // ROM size should always be a power of two.
  assert(rom_size != 0 && (rom_size & (rom_size - 1)) == 0);

  // Try to make it as square as possible, while keeping the sides
  // powers-of-two.
  rom_texture_width = rom_size;
  rom_texture_height = 1;
  while (rom_texture_width >= rom_texture_height) {
    rom_texture_width >>= 1;
    rom_texture_height <<= 1;
  }

  rom_texture = host_create_texture(host, rom_texture_width, rom_texture_height,
                                    HOST_TEXTURE_FORMAT_U8);
  emulator_clear_rom_usage(e);

  save_filename = replace_extension(filename, SAVE_EXTENSION);
  save_state_filename = replace_extension(filename, SAVE_STATE_EXTENSION);
  rom_usage_filename = replace_extension(filename, ROM_USAGE_EXTENSION);
  ImGui::GetIO().FontGlobalScale = font_scale;

  memory_editor.UserData = this;
  memory_editor.ReadFn = [](u8*, size_t offset, void* user_data) {
    return static_cast<Debugger*>(user_data)->MemoryEditorRead(offset);
  };
  memory_editor.WriteFn = [](u8*, size_t offset, u8 value, void* user_data) {
    static_cast<Debugger*>(user_data)->MemoryEditorWrite(offset, value);
  };

  emulator_init_state_file_data(&reverse_step_save_state);

  return true;
}

void Debugger::Run() {
  emulator_read_ext_ram_from_file(e, save_filename);

  f64 refresh_ms = host_get_monitor_refresh_ms(host);
  while (run_state != Exiting && host_poll_events(host)) {
    host_begin_video(host);
    switch (run_state) {
      case Running:
      case SteppingFrame:
        host_run_ms(host, refresh_ms);
        if (run_state == SteppingFrame) {
          host_reset_audio(host);
          run_state = Paused;
        }
        break;

      case SteppingInstruction:
        host_step(host);
        run_state = Paused;
        break;

      case AutoRewinding:
        AutoRewind(refresh_ms);
        break;

      case Exiting:
      case Paused:
      case Rewinding:
        break;
    }

    host_upload_texture(host, rom_texture, rom_texture_width,
                        rom_texture_height, emulator_get_rom_usage(e));
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
      RewindWindow();
      TiledataWindow();
      ObjWindow();
      MapWindow();
      ROMWindow();
      MemoryWindow();
      DisassemblyWindow();
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
    case HOST_KEYCODE_N: StepFrame(); break;
    case HOST_KEYCODE_SPACE: TogglePause(); break;
    case HOST_KEYCODE_ESCAPE: Exit(); break;
    case HOST_KEYCODE_TAB: host_config.no_sync = TRUE; break;
    case HOST_KEYCODE_MINUS: SetAudioVolume(audio_volume - 0.05f); break;
    case HOST_KEYCODE_EQUALS: SetAudioVolume(audio_volume + 0.05f); break;
    case HOST_KEYCODE_GRAVE: BeginAutoRewind(); break;
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
    case HOST_KEYCODE_GRAVE: EndAutoRewind(); break;
    default: return;
  }

  host_set_config(host, &host_config);
}

void Debugger::StepInstruction() {
  if (run_state == Running || run_state == Paused) {
    run_state = SteppingInstruction;
  } else if (run_state == Rewinding) {
    RewindTo(emulator_get_cycles(e) + 1);
  }
}

void Debugger::StepFrame() {
  if (run_state == Running || run_state == Paused) {
    run_state = SteppingFrame;
  } else if (run_state == Rewinding) {
    RewindTo(emulator_get_cycles(e) + PPU_FRAME_CYCLES);
  }
}

void Debugger::TogglePause() {
  switch (run_state) {
    case Running:
      run_state = Paused;
      break;

    case Paused:
      run_state = Running;
      break;

    case Rewinding:
      EndRewind();
      break;

    default:
      break;
  }
}

void Debugger::Pause() {
  if (run_state == Running) {
    run_state = Paused;
  }
}

void Debugger::Exit() {
  run_state = Exiting;
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
        Exit();
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
      ImGui::MenuItem("Rewind", NULL, &rewind_window_open);
      ImGui::MenuItem("ROM", NULL, &rom_window_open);
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

static std::string PrettySize(size_t size) {
  char buffer[1000];
  const char* suffix;
  float fsize;
  if (size > GIGABYTES(1)) {
    suffix = "Gib";
    fsize = size / (float)(GIGABYTES(1));
  } else if (size > MEGABYTES(1)) {
    suffix = "Mib";
    fsize = size / (float)(MEGABYTES(1));
  } else if (size > KILOBYTES(1)) {
    suffix = "Kib";
    fsize = size / (float)(KILOBYTES(1));
  } else {
    suffix = "b";
    fsize = size;
  }
  snprintf(buffer, sizeof(buffer), "%.1f%s", fsize, suffix);
  return buffer;
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
    ImGui::Combo("Palette", &palette_type, palette_names);
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
      ImGui::Combo("Color 0", &custom_palette.color[0], color_names);
      ImGui::Combo("Color 1", &custom_palette.color[1], color_names);
      ImGui::Combo("Color 2", &custom_palette.color[2], color_names);
      ImGui::Combo("Color 3", &custom_palette.color[3], color_names);
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
    ImGui::Combo("Layer", &layer_type, layer_names);
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

void Debugger::DisassemblyWindow() {
  ImGui::SetNextDock(ImGuiDockSlot_Tab);
  if (ImGui::BeginDock("Disassembly", &disassembly_window_open)) {
    static bool track_pc = true;
    static bool rom_only = true;
    static f32 last_scroll_y = 0;
    static Address scroll_addr = 0;
    // Offset to add to prevent popping when dragging the scrollbar.
    static f32 scroll_addr_offset = 0;

    Cycles now = emulator_get_cycles(e);
    u32 hr, min, sec, ms;
    emulator_cycles_to_time(now, &hr, &min, &sec, &ms);

    Registers regs = emulator_get_registers(e);
    ImGui::Text("Cycles: %" PRIu64 " Time: %u:%02u:%02u.%02u", now, hr, min,
                sec, ms / 10);
    ImGui::Separator();

    auto&& text_reg8 = [&](const char* name, u8 value) {
      ImGui::Text("%s:", name);
      ImGui::SameLine();
      ImGui::TextColored(kRegColor, "%02x", value);
      ImGui::SameLine(0, 20);
    };

    auto&& text_reg16 = [&](const char* name, u16 value) {
      ImGui::Text("%s:", name);
      ImGui::SameLine();
      ImGui::TextColored(kRegColor, "%04x", value);
      ImGui::SameLine(0, 20);
    };

    text_reg8("A", regs.A);
    text_reg8("B", regs.B);
    text_reg8("C", regs.C);
    text_reg8("D", regs.D);
    text_reg8("E", regs.E);
    text_reg8("H", regs.H);
    text_reg8("L", regs.L);
    ImGui::NewLine();

    text_reg16("BC", regs.BC);
    text_reg16("DE", regs.DE);
    text_reg16("HL", regs.HL);
    text_reg16("SP", regs.SP);
    ImGui::NewLine();

    ImGui::Text("F:");
    ImGui::SameLine();
    ImGui::TextColored(kRegColor, "%c%c%c%c", regs.F.Z ? 'Z' : '_',
                       regs.F.N ? 'N' : '_', regs.F.H ? 'H' : '_',
                       regs.F.C ? 'C' : '_');

    text_reg16("PC", regs.PC);
    ImGui::NewLine();

    ImGui::Separator();

    int scroll_delta = 0;

    ImGui::Checkbox("Track PC", &track_pc);
    ImGui::SameLine(0, 20);

    ImGui::Checkbox("ROM only", &rom_only);
    ImGui::SameLine(0, 20);

    {
      ImGui::PushItemWidth(ImGui::CalcTextSize("00000").x);
      char addr_input_buf[5] = {};
      if (ImGui::InputText("Goto", addr_input_buf, 5,
                           ImGuiInputTextFlags_CharsHexadecimal |
                               ImGuiInputTextFlags_EnterReturnsTrue)) {
        u32 addr;
        if (sscanf(addr_input_buf, "%x", &addr) == 1) {
          scroll_addr = addr;
          scroll_addr_offset = 0;
          track_pc = false;
        }
      }
      ImGui::PopItemWidth();
      ImGui::SameLine(0, 20);
    }

    ImGui::PushButtonRepeat(true);
    if (ImGui::Button("-I")) { scroll_delta = -1; track_pc = false; }
    ImGui::SameLine();
    if (ImGui::Button("+I")) { scroll_delta = 1; track_pc = false; }
    ImGui::PopButtonRepeat();

    ImGui::Separator();

    ImGui::PushButtonRepeat(true);
    if (ImGui::Button("step")) {
      StepInstruction();
    }
    ImGui::PopButtonRepeat();

    instr_count = 0;

    for (int rom_region = 0; rom_region < 2; ++rom_region) {
      int bank = emulator_get_rom_bank(e, rom_region);
      u8* rom_usage = emulator_get_rom_usage(e) + (bank << 14);

      for (Address rel_addr = 0; rel_addr < 0x4000;) {
        Address addr = rom_region * 0x4000 + rel_addr;
        u8 usage = rom_usage[rel_addr];
        bool is_data = usage == ROM_USAGE_DATA;
        int len;
        if (!is_data) {
          // Code or unknown usage, disassemble either way.
          u8 opcode = emulator_read_u8_raw(e, addr);
          len = opcode_bytes(opcode);
          assert(len <= 3);
          if (len == 0) {
            is_data = true;
          } else if (!(usage & ROM_USAGE_CODE_START)) {
            // Unknown, disassemble but be careful not to skip over a
            // ROM_USAGE_CODE_START.
            for (int i = 1; i < len; ++i) {
              if (rom_usage[rel_addr + i] & ROM_USAGE_CODE_START) {
                is_data = true;
                break;
              }
            }
          }
        }

        if (is_data) {
          rel_addr++;
        } else {
          assert(instr_count < (int)instrs.size());
          instrs[instr_count++] = addr;
          rel_addr += len;
        }
      }
    }

    if (!rom_only || regs.PC > 0x8000) {
      for (int addr = 0x8000; addr < 0x10000;) {
        u8 opcode = emulator_read_u8_raw(e, addr);
        int len = opcode_bytes(opcode);
        if (len != 0) {
          assert(instr_count < (int)instrs.size());
          instrs[instr_count++] = addr;
          addr += len;
        } else {
          addr++;
        }
      }
    }

    ImGui::BeginChild("Disassembly");
    // TODO(binji): Is there a better way to tell if the user scrolled?
    f32 scroll_y = ImGui::GetScrollY();
    bool did_mouse_scroll = scroll_y != last_scroll_y;
    last_scroll_y = scroll_y;

    f32 line_height = ImGui::GetTextLineHeightWithSpacing();
    f32 avail_y = ImGui::GetContentRegionAvail().y;

    if (!did_mouse_scroll) {
      Address want_scroll_addr = track_pc ? regs.PC : scroll_addr;

      auto addr_end = instrs.begin() + instr_count;
      auto iter = std::lower_bound(instrs.begin(), addr_end, want_scroll_addr);

      if (iter != addr_end) {
        int got_line = iter - instrs.begin();
        f32 view_min_y = scroll_y;
        f32 view_max_y = view_min_y + avail_y;
        f32 item_y = got_line * line_height + scroll_addr_offset;

        if (scroll_delta) {
          item_y += scroll_delta * line_height;
        }

        bool is_in_view =
            item_y >= view_min_y && item_y + line_height < view_max_y;
        bool should_center = !(track_pc && is_in_view);

        if (should_center) {
          last_scroll_y = std::max(
              std::min(item_y - avail_y * 0.5f, ImGui::GetScrollMaxY()), 0.f);
          ImGui::SetScrollY(last_scroll_y);

          if (track_pc) {
            scroll_addr = want_scroll_addr;
            scroll_addr_offset = 0;
          }
        }
      }
    }

    if (!track_pc) {
      f32 center = last_scroll_y + avail_y * 0.5f;
      int center_index = (int)(center / line_height);
      if (center_index < instr_count) {
        scroll_addr = instrs[center_index];
        scroll_addr_offset = center - center_index * line_height;
        track_pc = false;
      }
    }

    ImGuiListClipper clipper(instr_count, line_height);

    while (clipper.Step()) {
      for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
        Address addr = instrs[i];
        char buffer[64];
        emulator_disassemble(e, addr, buffer, sizeof(buffer));
        if (addr == regs.PC) {
          ImGui::TextColored(kPCColor, "%s", buffer);
        } else {
          ImGui::Text("%s", buffer);
        }
      }
    }

    ImGui::EndChild();
  }
  ImGui::EndDock();
}

void Debugger::MemoryWindow() {
  ImGui::SetNextDock(ImGuiDockSlot_Tab);
  if (ImGui::BeginDock("Memory", &memory_window_open)) {
    static const char* region_names[] = {
      "ALL",
      "ROM",
      "VRAM",
      "EXT RAM",
      "WRAM",
      "OAM",
      "I/O",
    };
    static int region = 0;
    ImGui::Combo("Region", &region, region_names);
    size_t size = 0x10000;
    switch (region) {
      case 0: memory_editor_base = 0;      size = 0x10000; break; /* ALL */
      case 1: memory_editor_base = 0;      size = 0x08000; break; /* ROM */
      case 2: memory_editor_base = 0x8000; size = 0x02000; break; /* VRAM */
      case 3: memory_editor_base = 0xa000; size = 0x02000; break; /* EXT RAM */
      case 4: memory_editor_base = 0xc000; size = 0x02000; break; /* WRAM */
      case 5: memory_editor_base = 0xfe00; size = 0x000a0; break; /* OAM */
      case 6: memory_editor_base = 0xff00; size = 0x00100; break; /* I/O */
    }
    memory_editor.DrawContents(nullptr, size, memory_editor_base);
  }
  ImGui::EndDock();
}

void Debugger::RewindWindow() {
  ImGui::SetNextDock(ImGuiDockSlot_Tab);
  if (ImGui::BeginDock("Rewind", &rewind_window_open)) {
    bool rewinding = host_is_rewinding(host);
    if (ImGui::Checkbox("Rewind", &rewinding)) {
      if (rewinding) {
        BeginRewind();
      } else {
        EndRewind();
      }
    }

    if (rewinding) {
      Cycles cur_cy = emulator_get_cycles(e);
      Cycles oldest_cy = host_get_rewind_oldest_cycles(host);
      Cycles rel_cur_cy = cur_cy - oldest_cy;
      u32 range_fr = (host_newest_cycles(host) - oldest_cy) / PPU_FRAME_CYCLES;

      // Frames.
      int frame = rel_cur_cy / PPU_FRAME_CYCLES;

      ImGui::PushButtonRepeat(true);
      if (ImGui::Button("-1")) { --frame; }
      ImGui::SameLine();
      if (ImGui::Button("+1")) { ++frame; }
      ImGui::PopButtonRepeat();
      ImGui::SameLine();
      ImGui::SliderInt("Frames", &frame, 0, range_fr);

      frame = CLAMP(frame, 0, static_cast<int>(range_fr));

      // Cycles.
      int offset_cy = rel_cur_cy % PPU_FRAME_CYCLES;
      bool reverse_step = false;

      ImGui::PushButtonRepeat(true);
      if (ImGui::Button("-I")) { offset_cy -= 28; reverse_step = true; }
      ImGui::SameLine();
      if (ImGui::Button("+I")) { offset_cy += 1; }
      ImGui::PopButtonRepeat();
      ImGui::SameLine();
      ImGui::SliderInt("Cycle Offset", &offset_cy, 0, PPU_FRAME_CYCLES - 1);

      Cycles rel_seek_cy = (Cycles)frame * PPU_FRAME_CYCLES + offset_cy;

      if (rel_cur_cy != rel_seek_cy) {
        RewindTo(oldest_cy + rel_seek_cy);

        // Reverse stepping is tricky because we don't know how long the
        // previous instruction took. We can rewind by 28 cycles (longer than
        // any instruction or interrupt dispatch) and step forward until just
        // before the current cycle. But since we don't know how long a step
        // will take, it's easier to just save state, step forward one
        // instruction too far, then load state and step just before it.
        if (reverse_step) {
          emulator_write_state(e, &reverse_step_save_state);
          int count = 0;
          for (; emulator_get_cycles(e) < cur_cy; ++count) {
            emulator_step(e);
          }

          emulator_read_state(e, &reverse_step_save_state);
          for (int i = 0; i < count - 1; ++i) {
            emulator_step(e);
          }
        }
      }
    }

    ImGui::Separator();
    JoypadStats joyp_stats = host_get_joypad_stats(host);
    RewindStats rw_stats = host_get_rewind_stats(host);
    size_t base = rw_stats.base_bytes;
    size_t diff = rw_stats.diff_bytes;
    size_t total = base + diff;
    size_t uncompressed = rw_stats.uncompressed_bytes;
    size_t used = rw_stats.used_bytes;
    size_t capacity = rw_stats.capacity_bytes;
    Cycles total_cycles = host_newest_cycles(host) - host_oldest_cycles(host);
    f64 sec = (f64)total_cycles / CPU_CYCLES_PER_SECOND;

    ImGui::Text("joypad used/capacity: %s/%s",
                PrettySize(joyp_stats.used_bytes).c_str(),
                PrettySize(joyp_stats.capacity_bytes).c_str());

    ImGui::Text("rewind base/diff/total: %s/%s/%s (%.0f%%)",
                PrettySize(base).c_str(), PrettySize(diff).c_str(),
                PrettySize(total).c_str(), (f64)(total)*100 / uncompressed);
    ImGui::Text("rewind uncomp: %s", PrettySize(uncompressed).c_str());
    ImGui::Text("rewind used: %s/%s (%.0f%%)", PrettySize(used).c_str(),
                PrettySize(capacity).c_str(), (f64)used * 100 / capacity);
    ImGui::Text("rate: %s/sec %s/min %s/hr", PrettySize(total / sec).c_str(),
                PrettySize(total / sec * 60).c_str(),
                PrettySize(total / sec * 60 * 60).c_str());

    Cycles oldest = host_get_rewind_oldest_cycles(host);
    Cycles newest = host_get_rewind_newest_cycles(host);
    f64 range = (f64)(newest - oldest) / CPU_CYCLES_PER_SECOND;
    ImGui::Text("range: [%" PRIu64 "..%" PRIu64 "] (%.0f sec)", oldest, newest,
                range);

    ImVec2 cursor = ImGui::GetCursorScreenPos();
    ImVec2 avail_size = ImGui::GetContentRegionAvail();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    f32 w = avail_size.x, h = 64;
    ImVec2 ul_pos = cursor;
    ImVec2 br_pos = ul_pos + ImVec2(w, h);
    ImVec2 margin(4, 4);
    draw_list->AddRectFilled(ul_pos, br_pos, IM_COL32_BLACK);
    draw_list->AddRectFilled(ul_pos + margin, br_pos - margin, IM_COL32_WHITE);

    auto xoffset = [&](size_t x) -> f32 {
      return (f32)x * (w - margin.x * 2) / (f32)capacity;
    };

    auto draw_bar = [&](size_t l, size_t r, ImU32 col) {
      ImVec2 ul = ul_pos + margin + ImVec2(xoffset(l), 0);
      ImVec2 br = ul_pos + margin + ImVec2(xoffset(r), h - margin.y * 2);
      draw_list->AddRectFilled(ul, br, col);
    };

    draw_bar(rw_stats.data_ranges[0], rw_stats.data_ranges[1], 0xfff38bff);
    draw_bar(rw_stats.data_ranges[2], rw_stats.data_ranges[3], 0xffac5eb5);
    draw_bar(rw_stats.info_ranges[0], rw_stats.info_ranges[1], 0xff64ea54);
    draw_bar(rw_stats.info_ranges[2], rw_stats.info_ranges[3], 0xff3eab32);
    ImGui::Dummy(ImVec2(w, h));
  }
  ImGui::EndDock();
}

void Debugger::ROMWindow() {
  ImGui::SetNextDock(ImGuiDockSlot_Right);
  if (ImGui::BeginDock("ROM", &rom_window_open)) {
    static int scale = 1;

    PaletteRGBA palette = {
        {0xff202020u, 0xff00ff00u, 0xffff0000u, 0xffff00ffu}};

    size_t rom_size = emulator_get_rom_size(e);
    u8* rom_usage = emulator_get_rom_usage(e);

    if (ImGui::Button("Dump")) {
      FileData file_data;
      file_data.data = rom_usage;
      file_data.size = rom_size;
      file_write(rom_usage_filename, &file_data);
    }
    ImGui::SliderInt("Scale", &scale, 1, 16);

    static int counter = 60;
    static size_t usage_bytes[4];

    if (--counter <= 0) {
      counter = 60;
      ZERO_MEMORY(usage_bytes);
      for (size_t i = 0; i < rom_size; ++i) {
        usage_bytes[rom_usage[i]]++;
      }
    }

    ImGui::Text("Unknown: %s (%.0f%%)", PrettySize(usage_bytes[0]).c_str(),
                (f64)usage_bytes[0] * 100 / rom_size);
    ImGui::Text("Data: %s (%.0f%%)", PrettySize(usage_bytes[2]).c_str(),
                (f64)usage_bytes[2] * 100 / rom_size);
    ImGui::Text("Code: %s (%.0f%%)", PrettySize(usage_bytes[3]).c_str(),
                (f64)usage_bytes[3] * 100 / rom_size);

    ImGui::Separator();

    ImVec2 avail_size = ImGui::GetContentRegionAvail();
    s32 avail_x = (s32)(avail_size.x - ImGui::GetStyle().ScrollbarSize);
    avail_x -= avail_x % scale;
    ImVec2 child_size(avail_x, rom_texture_width * scale * rom_texture_height *
                                       scale / avail_x +
                                   scale);

    ImGui::BeginChild("Data");
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 cursor = ImGui::GetCursorScreenPos();

    SetPaletteAndEnable(host, draw_list, palette);
    ImTextureID texture_id = (ImTextureID)rom_texture->handle;
    draw_list->PushTextureID(texture_id);
    draw_list->PushClipRect(cursor, cursor + child_size, true);

    f32 scroll_y = ImGui::GetScrollY();
    f32 inv_scale = 1.f / scale;
    s32 min_y = (s32)(scroll_y * inv_scale);
    s32 max_y = (s32)(
        std::min((scroll_y + avail_size.y + scale), child_size.y) * inv_scale);
    s32 unscaled_w = avail_x / scale;

    s32 x = 0;
    s32 y = min_y;
    s32 tx = (y * unscaled_w + x) % rom_texture_width;
    s32 ty = (y * unscaled_w + x) / rom_texture_width;

    ImVec2 inv_tex_size(1.f / rom_texture_width, 1.f / rom_texture_height);

    while (y < max_y && ty < rom_texture_height) {
      ImVec2 ul_pos = cursor + ImVec2(x, y) * scale;
      ImVec2 ul_uv = ImVec2(tx, ty) * inv_tex_size;

      s32 strip_w = std::min(unscaled_w - x, rom_texture_width - tx);

      ImVec2 br_pos = cursor + ImVec2(x + strip_w, y + 1) * scale;
      ImVec2 br_uv = ImVec2(tx + strip_w, ty + 1) * inv_tex_size;

      x += strip_w;
      if (x >= unscaled_w) {
        x -= unscaled_w;
        y += 1;
      }

      tx += strip_w;
      if (tx >= rom_texture_width) {
        tx -= rom_texture_width;
        ty += 1;
      }

      draw_list->AddImage(texture_id, ul_pos, br_pos, ul_uv, br_uv);
    }

    draw_list->PopTextureID();
    DisablePalette(host, draw_list);

    ImGui::PopClipRect();
    ImGui::Dummy(child_size);
    if (ImGui::IsItemHovered()) {
      ImVec2 mouse_pos = (ImGui::GetMousePos() - cursor) * inv_scale;
      s32 rom_loc = (s32)mouse_pos.y * unscaled_w + (s32)mouse_pos.x;

      if (rom_loc < rom_texture_width * rom_texture_height) {
        u32 bank = rom_loc >> 14;
        u32 addr = (rom_loc & 0x3fff) + (bank == 0 ? 0 : 0x4000);
        ImGui::SetTooltip("%02x:%04x", bank, addr);
      }
    }
    ImGui::EndChild();
  }
  ImGui::EndDock();
}

void Debugger::BeginRewind() {
  if (run_state == Running || run_state == Paused) {
    host_begin_rewind(host);
    run_state = Rewinding;
  }
}

void Debugger::EndRewind() {
  if (run_state == Rewinding) {
    host_end_rewind(host);
    run_state = Running;
  }
}

void Debugger::BeginAutoRewind() {
  if (run_state == Running || run_state == Paused) {
    host_begin_rewind(host);
    run_state = AutoRewinding;
  }
}

void Debugger::EndAutoRewind() {
  if (run_state == AutoRewinding) {
    host_end_rewind(host);
    run_state = Running;
  }
}

void Debugger::AutoRewind(f64 delta_ms) {
  assert(run_state == AutoRewinding);
  Cycles delta_cycles = (Cycles)(delta_ms * CPU_CYCLES_PER_SECOND / 1000);
  Cycles now = emulator_get_cycles(e);
  Cycles then = now >= delta_cycles ? now - delta_cycles : 0;
  RewindTo(then);
}

void Debugger::RewindTo(Cycles cycles) {
  host_rewind_to_cycles(host, cycles);
  host_reset_audio(host);
}

u8 Debugger::MemoryEditorRead(Address addr) {
  return emulator_read_u8_raw(e, memory_editor_base + addr);
}

void Debugger::MemoryEditorWrite(Address addr, u8 value) {
  return emulator_write_u8_raw(e, memory_editor_base + addr, value);
}
