/*
 * Copyright (C) 2018 Ben Smith
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#ifndef BINJGB_DEBUGGER_H__
#define BINJGB_DEBUGGER_H__

#include <array>

#include "emulator-debug.h"
#include "host.h"

#include "imgui.h"
#include "imgui_memory_editor.h"

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

class Debugger {
 public:
  Debugger();
  Debugger(const Debugger&) = delete;
  Debugger& operator=(const Debugger&) = delete;

  ~Debugger();

  bool Init(const char* filename, int audio_frequency, int audio_frames,
            int font_scale, bool paused_at_start);
  void Run();

 private:
  void OnAudioBufferFull();
  void OnKeyDown(HostKeycode);
  void OnKeyUp(HostKeycode);

  void StepInstruction();
  void StepFrame();
  void TogglePause();
  void Pause();
  void Exit();

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
  void RewindWindow();
  void ROMWindow();

  void BeginRewind();
  void EndRewind();
  void BeginAutoRewind();
  void EndAutoRewind();
  void AutoRewind(f64 ms);
  void RewindTo(Cycles cycles);

  u8 MemoryEditorRead(Address addr);
  void MemoryEditorWrite(Address addr, u8 value);

  EmulatorInit emulator_init;
  HostInit host_init;
  Emulator* e = nullptr;
  Host* host = nullptr;
  const char* save_filename = nullptr;
  const char* save_state_filename = nullptr;
  const char* rom_usage_filename = nullptr;

  enum RunState {
    Exiting,
    Running,
    Paused,
    SteppingFrame,
    SteppingInstruction,
    Rewinding,
    AutoRewinding,
  };
  RunState run_state = Running;

  TileImage tiledata_image;
  HostTexture* rom_texture = nullptr;
  int rom_texture_width = 0;
  int rom_texture_height = 0;

  static const int kAudioDataSamples = 1000;
  f32 audio_data[2][kAudioDataSamples] = {};
  f32 audio_volume = 0.5f;

  bool highlight_obj = false;
  int highlight_obj_index = 0;
  bool highlight_tile = false;
  int highlight_tile_index = 0;

  MemoryEditor memory_editor;
  Address memory_editor_base = 0;

  bool emulator_window_open = true;
  bool audio_window_open = true;
  bool tiledata_window_open = true;
  bool obj_window_open = true;
  bool map_window_open = true;
  bool disassembly_window_open = true;
  bool memory_window_open = true;
  bool rewind_window_open = true;
  bool rom_window_open = true;

  FileData reverse_step_save_state;

  // Used to collect disassembled instructions.
  std::array<Address, 65536> instrs;
  int instr_count = 0;
};

#endif  // #define BINJGB_DEBUGGER_H__
