/*
 * Copyright (C) 2018 Ben Smith
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include "debugger.h"

#include <inttypes.h>

#include "imgui.h"
#include "imgui_dock.h"
#include "imgui-helpers.h"

Debugger::RewindWindow::RewindWindow(Debugger* d) : Window(d) {
  emulator_init_state_file_data(&reverse_step_save_state);
}

Debugger::RewindWindow::~RewindWindow() {
  file_data_delete(&reverse_step_save_state);
}

void Debugger::RewindWindow::Tick() {
  if (ImGui::BeginDock("Rewind", &is_open)) {
    bool rewinding = host_is_rewinding(d->host);
    if (ImGui::Checkbox("Rewind", &rewinding)) {
      if (rewinding) {
        d->BeginRewind();
      } else {
        d->EndRewind();
      }
    }

    if (rewinding) {
      Cycles cur_cy = emulator_get_cycles(d->e);
      Cycles oldest_cy = host_get_rewind_oldest_cycles(d->host);
      Cycles rel_cur_cy = cur_cy - oldest_cy;
      u32 range_fr =
          (host_newest_cycles(d->host) - oldest_cy) / PPU_FRAME_CYCLES;

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
        d->RewindTo(oldest_cy + rel_seek_cy);

        // Reverse stepping is tricky because we don't know how long the
        // previous instruction took. We can rewind by 28 cycles (longer than
        // any instruction or interrupt dispatch) and step forward until just
        // before the current cycle. But since we don't know how long a step
        // will take, it's easier to just save state, step forward one
        // instruction too far, then load state and step just before it.
        if (reverse_step) {
          emulator_write_state(d->e, &reverse_step_save_state);
          int count = 0;
          for (; emulator_get_cycles(d->e) < cur_cy; ++count) {
            emulator_step(d->e);
          }

          emulator_read_state(d->e, &reverse_step_save_state);
          for (int i = 0; i < count - 1; ++i) {
            emulator_step(d->e);
          }
        }
      }
    }

    ImGui::Separator();
    JoypadStats joyp_stats = host_get_joypad_stats(d->host);
    RewindStats rw_stats = host_get_rewind_stats(d->host);
    size_t base = rw_stats.base_bytes;
    size_t diff = rw_stats.diff_bytes;
    size_t total = base + diff;
    size_t uncompressed = rw_stats.uncompressed_bytes;
    size_t used = rw_stats.used_bytes;
    size_t capacity = rw_stats.capacity_bytes;
    Cycles total_cycles =
        host_newest_cycles(d->host) - host_oldest_cycles(d->host);
    f64 sec = (f64)total_cycles / CPU_CYCLES_PER_SECOND;

    ImGui::Text("joypad used/capacity: %s/%s",
                d->PrettySize(joyp_stats.used_bytes).c_str(),
                d->PrettySize(joyp_stats.capacity_bytes).c_str());

    ImGui::Text("rewind base/diff/total: %s/%s/%s (%.0f%%)",
                d->PrettySize(base).c_str(), d->PrettySize(diff).c_str(),
                d->PrettySize(total).c_str(), (f64)(total)*100 / uncompressed);
    ImGui::Text("rewind uncomp: %s", d->PrettySize(uncompressed).c_str());
    ImGui::Text("rewind used: %s/%s (%.0f%%)", d->PrettySize(used).c_str(),
                d->PrettySize(capacity).c_str(), (f64)used * 100 / capacity);
    ImGui::Text("rate: %s/sec %s/min %s/hr", d->PrettySize(total / sec).c_str(),
                d->PrettySize(total / sec * 60).c_str(),
                d->PrettySize(total / sec * 60 * 60).c_str());

    Cycles oldest = host_get_rewind_oldest_cycles(d->host);
    Cycles newest = host_get_rewind_newest_cycles(d->host);
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

void Debugger::BeginRewind() {
  if (run_state == Running || run_state == Paused) {
    emulator_push_trace(FALSE);
    host_begin_rewind(host);
    run_state = Rewinding;
  }
}

void Debugger::EndRewind() {
  if (run_state == Rewinding) {
    host_end_rewind(host);
    run_state = Running;
    emulator_pop_trace();
  }
}
