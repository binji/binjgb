/*
 * Copyright (C) 2018 Ben Smith
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#ifndef BINJGB_DEBUGGER_H__
#define BINJGB_DEBUGGER_H__

#include <array>
#include <string>

#include "imgui.h"
#include "imgui_memory_editor.h"

#include "emulator-debug.h"
#include "host.h"

const ImVec2 kTileSize(8, 8);
const ImVec2 k8x16OBJSize(8, 16);
const ImVec2 kScreenSize(SCREEN_WIDTH, SCREEN_HEIGHT);
const ImU32 kHighlightColor(IM_COL32(0, 255, 0, 192));

class Debugger {
 public:
  Debugger();
  Debugger(const Debugger&) = delete;
  Debugger& operator=(const Debugger&) = delete;

  ~Debugger();

  bool Init(const char* filename, int audio_frequency, int audio_frames,
            int font_scale, bool paused_at_start, u32 random_seed,
            u32 builtin_palette, bool force_dmg, bool use_sgb_border,
            CgbColorCurve cgb_color_curve);
  void Run();

 private:
  static std::string PrettySize(size_t size);

  void OnAudioBufferFull();
  void OnKeyDown(HostKeycode);
  void OnKeyUp(HostKeycode);

  void StepInstruction();
  void StepFrame();
  void TogglePause();
  void Exit();

  void WriteStateToFile();
  void ReadStateFromFile();

  void SetAudioVolume(f32 volume);

  void ToggleTrace();
  void SetTrace(bool);
  bool trace() { return !!emulator_get_trace(); }

  void MainMenuBar();

  void BeginRewind();
  void EndRewind();
  void BeginAutoRewind();
  void EndAutoRewind();
  void AutoRewind(f64 ms);
  void RewindTo(Ticks ticks);

  // Return true if hovering on the tile.
  bool DrawTile(ImDrawList* draw_list, int index, const ImVec2& ul_pos,
                f32 scale, PaletteRGBA palette, bool xflip = false,
                bool yflip = false);
  // Return -1 if not hovering, or tile index if hovering.
  int DrawOBJ(ImDrawList* draw_list, ObjSize obj_size, int index,
              const ImVec2& ul_pos, f32 scale, PaletteRGBA palette, bool xflip,
              bool yflip);

  void SetPaletteAndEnable(ImDrawList* draw_list, const PaletteRGBA& palette);
  void DisablePalette(ImDrawList* draw_list);

  static int GetTileIndexInBank(int tile_index) {
    return tile_index >= 384 ? tile_index - 384 : tile_index;
  }
  static int GetByteTileIndex(int tile_index) {
    return GetTileIndexInBank(tile_index) & 255;
  }
  static int GetTileBank(int tile_index) { return tile_index > 384 ? 1 : 0; }
  static Address GetTileAddr(int tile_index) {
    return 0x8000 + GetTileIndexInBank(tile_index) * 16;
  }

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

  TileData tile_data;
  HostTexture* tile_data_texture;

  bool is_cgb = false;
  bool is_sgb = false;

  f32 audio_volume = 0.5f;

  bool highlight_obj = false;
  int highlight_obj_index = 0;
  bool highlight_tile = false;
  int highlight_tile_index = 0;

  struct Window {
    explicit Window(Debugger* d) : d(d) {}
    Debugger* d;
    bool is_open = true;
  };

  struct AudioWindow : Window {
    explicit AudioWindow(Debugger*);
    void Tick();

    static const int kAudioDataSamples = 1000;
    f32 audio_data[2][kAudioDataSamples] = {};
  };

  struct DisassemblyWindow : Window {
    explicit DisassemblyWindow(Debugger*);
    void Tick();

    bool track_pc = true;
    bool rom_only = true;
    f32 last_scroll_y = 0;
    Address scroll_addr = 0;
    // Offset to add to prevent popping when dragging the scrollbar.
    f32 scroll_addr_offset = 0;

    // Used to collect disassembled instructions.
    std::array<Address, 65536> instrs;
    int instr_count = 0;
  };

  struct EmulatorWindow : Window {
    explicit EmulatorWindow(Debugger*);
    void Tick();
  };

  struct IOWindow : Window {
    explicit IOWindow(Debugger*);
    void Tick();
  };

  struct MapWindow : Window {
    explicit MapWindow(Debugger*);
    void Tick();

    int scale = 3;
    LayerType layer_type = LAYER_TYPE_BG;
    bool highlight = true;
    int hovering_map_index = 0;
  };

  struct MemoryWindow : Window {
    explicit MemoryWindow(Debugger*);
    void Tick();

    int region = 0;
    MemoryEditor memory_editor;
    Address memory_editor_base = 0;
  };

  struct ObjWindow : Window {
    explicit ObjWindow(Debugger*);
    void Tick();
    int GetObjTile(Obj);

    int scale = 4;
    int obj_index = 0;
  };

  struct RewindWindow : Window {
    explicit RewindWindow(Debugger*);
    ~RewindWindow();
    void Tick();

    FileData reverse_step_save_state;
  };

  struct ROMWindow : Window {
    explicit ROMWindow(Debugger*);
    void Init();
    void Tick();

    HostTexture* rom_texture = nullptr;
    int rom_texture_width = 0;
    int rom_texture_height = 0;

    int scale = 1;
    int counter = 60;
    size_t usage_bytes[4];
  };

  struct TiledataWindow : Window {
    explicit TiledataWindow(Debugger*);
    void Tick();
    void CalculateAutoPaletteColors();
    void CalculateAutoTilePaletteIndex(LayerType);
    void CalculateAutoObjPaletteIndex();

    bool color_auto = true;
    int scale = 3;
    int palette_type = PALETTE_TYPE_BGP;
    int color_set = PALETTE_TYPE_BGP;
    Palette custom_palette = {
        {COLOR_WHITE, COLOR_LIGHT_GRAY, COLOR_DARK_GRAY, COLOR_BLACK}};
    int cgb_palette_type = CGB_PALETTE_TYPE_BGCP;
    int cgb_palette_index = 0;
    int hovering_tile_index = 0;

    int wrap_width = 16;
    bool size8x16 = false;

    static const u32 kMaxTileCount = 384 * 2;
    PaletteRGBA palette_rgba[17]; // 1 default color + 8 bg + 8 obj
    u8 tile_palette_index[kMaxTileCount];
  };

  ImGuiID dockspace_id;
  AudioWindow audio_window;
  DisassemblyWindow disassembly_window;
  EmulatorWindow emulator_window;
  IOWindow io_window;
  MapWindow map_window;
  MemoryWindow memory_window;
  ObjWindow obj_window;
  RewindWindow rewind_window;
  ROMWindow rom_window;
  TiledataWindow tiledata_window;

  static const char s_audio_window_name[];
  static const char s_disassembly_window_name[];
  static const char s_emulator_window_name[];
  static const char s_io_window_name[];
  static const char s_map_window_name[];
  static const char s_memory_window_name[];
  static const char s_obj_window_name[];
  static const char s_rewind_window_name[];
  static const char s_rom_window_name[];
  static const char s_tiledata_window_name[];
};

#endif  // #define BINJGB_DEBUGGER_H__
