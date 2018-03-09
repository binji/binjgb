/*
 * Copyright (C) 2018 Ben Smith
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include "debugger.h"

#include "imgui.h"
#include "imgui_dock.h"
#include "imgui-helpers.h"

Debugger::MemoryWindow::MemoryWindow(Debugger* d) : Window(d) {
  memory_editor.UserData = this;
  memory_editor.ReadFn = [](u8*, size_t addr, void* user_data) {
    MemoryWindow* this_ = static_cast<MemoryWindow*>(user_data);
    return emulator_read_u8_raw(this_->d->e, this_->memory_editor_base + addr);
  };
  memory_editor.WriteFn = [](u8*, size_t addr, u8 value, void* user_data) {
    MemoryWindow* this_ = static_cast<MemoryWindow*>(user_data);
    emulator_write_u8_raw(this_->d->e, this_->memory_editor_base + addr, value);
  };
}

void Debugger::MemoryWindow::Tick() {
  ImGui::SetNextDock(ImGuiDockSlot_Tab);
  if (ImGui::BeginDock("Memory", &is_open)) {
    static const char* region_names[] = {
        "ALL", "ROM", "VRAM", "EXT RAM", "WRAM", "OAM", "I/O",
    };
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
