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
#include "imgui-helpers.h"

#define SAVE_EXTENSION ".sav"
#define SAVE_STATE_EXTENSION ".state"
#define ROM_USAGE_EXTENSION ".romusage"

void Debugger::SetPaletteAndEnable(ImDrawList* draw_list,
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

void Debugger::DisablePalette(ImDrawList* draw_list) {
  auto func = [](const ImDrawList*, const ImDrawCmd* cmd) {
    Host* host = static_cast<Host*>(cmd->UserCallbackData);
    host_enable_palette(host, FALSE);
  };

  draw_list->AddCallback(func, host);
}

bool Debugger::DrawTile(ImDrawList* draw_list, int index, const ImVec2& ul_pos,
                        f32 scale, PaletteRGBA palette, bool xflip,
                        bool yflip) {
  const int width = TILE_DATA_TEXTURE_WIDTH / 8;
  ImVec2 src(index % width, index / width);
  ImVec2 duv = kTileSize * ImVec2(1.0f / tile_data_texture->width,
                                  1.0f / tile_data_texture->height);
  ImVec2 br_pos = ul_pos + kTileSize * scale;
  ImVec2 ul_uv = src * duv;
  ImVec2 br_uv = ul_uv + duv;
  if (xflip) {
    std::swap(ul_uv.x, br_uv.x);
  }
  if (yflip) {
    std::swap(ul_uv.y, br_uv.y);
  }
  SetPaletteAndEnable(draw_list, palette);
  draw_list->AddImage((ImTextureID)tile_data_texture->handle, ul_pos, br_pos,
                      ul_uv, br_uv);
  DisablePalette(draw_list);
  return ImGui::IsMouseHoveringRect(ul_pos, br_pos);
}

int Debugger::DrawOBJ(ImDrawList* draw_list, ObjSize obj_size, int tile,
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

Debugger::Debugger()
    : audio_window(this),
      disassembly_window(this),
      emulator_window(this),
      map_window(this),
      memory_window(this),
      obj_window(this),
      rewind_window(this),
      rom_window(this),
      tiledata_window(this) {}

Debugger::~Debugger() {
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

  tile_data_texture =
      host_create_texture(host, TILE_DATA_TEXTURE_WIDTH,
                          TILE_DATA_TEXTURE_HEIGHT, HOST_TEXTURE_FORMAT_U8);
  rom_window.Init();

  save_filename = replace_extension(filename, SAVE_EXTENSION);
  save_state_filename = replace_extension(filename, SAVE_STATE_EXTENSION);
  rom_usage_filename = replace_extension(filename, ROM_USAGE_EXTENSION);
  ImGui::GetIO().FontGlobalScale = font_scale;

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

    emulator_get_tile_data(e, tile_data);
    host_upload_texture(host, tile_data_texture, TILE_DATA_TEXTURE_WIDTH,
                        TILE_DATA_TEXTURE_HEIGHT, tile_data);

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
      emulator_window.Tick();
      audio_window.Tick();
      rewind_window.Tick();
      tiledata_window.Tick();
      obj_window.Tick();
      map_window.Tick();
      rom_window.Tick();
      memory_window.Tick();
      disassembly_window.Tick();
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

  for (int i = 0; i < AudioWindow::kAudioDataSamples; ++i) {
    int index = (i * size / AudioWindow::kAudioDataSamples) & ~1;
    audio_window.audio_data[0][i] = audio_buffer->data[index];
    audio_window.audio_data[1][i] = audio_buffer->data[index + 1];
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
      ImGui::MenuItem("Binjgb", NULL, &emulator_window.is_open);
      ImGui::MenuItem("Audio", NULL, &audio_window.is_open);
      ImGui::MenuItem("TileData", NULL, &tiledata_window.is_open);
      ImGui::MenuItem("Obj", NULL, &obj_window.is_open);
      ImGui::MenuItem("Map", NULL, &map_window.is_open);
      ImGui::MenuItem("Disassembly", NULL, &disassembly_window.is_open);
      ImGui::MenuItem("Memory", NULL, &memory_window.is_open);
      ImGui::MenuItem("Rewind", NULL, &rewind_window.is_open);
      ImGui::MenuItem("ROM", NULL, &rom_window.is_open);
      ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
  }
}

// static
std::string Debugger::PrettySize(size_t size) {
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
