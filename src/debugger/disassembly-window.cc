/*
 * Copyright (C) 2018 Ben Smith
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include "debugger.h"

#include <inttypes.h>

#include "imgui.h"
#include "imgui-helpers.h"

// static
const char Debugger::s_disassembly_window_name[] = "Disassembly";

Debugger::DisassemblyWindow::DisassemblyWindow(Debugger* d) : Window(d) {}

void Debugger::DisassemblyWindow::Tick() {
  const ImVec4 kPCColor(0.2f, 1.f, 0.1f, 1.f);
  const ImVec4 kRegColor(1.f, 0.75f, 0.3f, 1.f);
  const ImU32 kBreakpointColor = IM_COL32(192, 0, 0, 255);

  if (!is_open) return;

  if (ImGui::Begin(Debugger::s_disassembly_window_name, &is_open)) {
    Ticks now = emulator_get_ticks(d->e);
    u32 day, hr, min, sec, ms;
    emulator_ticks_to_time(now, &day, &hr, &min, &sec, &ms);

    Registers regs = emulator_get_registers(d->e);
    ImGui::Text("Ticks: %" PRIu64 " Time: %u:%02u:%02u.%02u", now,
                day * 24 + hr, min, sec, ms / 10);
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

    {
      bool trace = d->trace();
      ImGui::Checkbox("Trace", &trace);
      d->SetTrace(trace);
      ImGui::SameLine(0, 20);
    }

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
      d->StepInstruction();
    }
    ImGui::PopButtonRepeat();

    instr_count = 0;

    for (int rom_region = 0; rom_region < 2; ++rom_region) {
      Address region_addr = rom_region << 14;
      int bank = emulator_get_rom_bank(d->e, region_addr);
      u8* rom_usage = emulator_get_rom_usage() + (bank << 14);

      for (Address rel_addr = 0; rel_addr < 0x4000;) {
        Address addr = region_addr + rel_addr;
        u8 usage = rom_usage[rel_addr];
        bool is_data = usage == ROM_USAGE_DATA;
        int len;
        if (!is_data) {
          // Code or unknown usage, disassemble either way.
          u8 opcode = emulator_read_u8_raw(d->e, addr);
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
        u8 opcode = emulator_read_u8_raw(d->e, addr);
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

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImGuiListClipper clipper(instr_count, line_height);

    while (clipper.Step()) {
      for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
        Address addr = instrs[i];
        Breakpoint bp = emulator_get_breakpoint_by_address(d->e, addr);
        ImGui::PushID(i);

        const ImVec2 bp_size = ImVec2(line_height, line_height);
        const float bp_radius = bp_size.x * 0.4f;
        if (ImGui::InvisibleButton("##bp", bp_size)) {
          if (bp.valid) {
            if (bp.enabled) {
              emulator_enable_breakpoint(bp.id, FALSE);
            } else {
              emulator_remove_breakpoint(bp.id);
            }
          } else {
            emulator_add_breakpoint(d->e, addr, TRUE);
          }
        }
        if (bp.valid && ImGui::IsItemHovered()) {
          ImGui::SetTooltip("breakpoint %d: $%04x [%s]", bp.id, bp.addr,
                            bp.enabled ? "enabled" : "disabled");
        }

        ImVec2 rect_min = ImGui::GetItemRectMin();
        ImVec2 rect_max = ImGui::GetItemRectMax();
        ImVec2 center = (rect_max + rect_min) * 0.5f;
        if (bp.valid) {
          if (bp.enabled) {
            draw_list->AddCircleFilled(center, bp_radius, kBreakpointColor);
          } else {
            draw_list->AddCircle(center, bp_radius, kBreakpointColor);
          }
        }

        ImGui::SameLine();
        ImGui::PopID();

        char buffer[64];
        emulator_disassemble(d->e, addr, buffer, sizeof(buffer));
        if (addr == regs.PC) {
          ImGui::TextColored(kPCColor, "%s", buffer);
        } else {
          ImGui::Text("%s", buffer);
        }
      }
    }

    ImGui::EndChild();
  }
  ImGui::End();
}

void Debugger::StepInstruction() {
  if (run_state == Running || run_state == Paused) {
    run_state = SteppingInstruction;
  } else if (run_state == Rewinding) {
    RewindTo(emulator_get_ticks(e) + 1);
  }
}
