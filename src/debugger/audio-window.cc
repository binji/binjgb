/*
 * Copyright (C) 2018 Ben Smith
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include "debugger.h"

#include "imgui.h"
#include "imgui-helpers.h"

// static
const char Debugger::s_audio_window_name[] = "Audio";

Debugger::AudioWindow::AudioWindow(Debugger* d) : Window(d) {}

void Debugger::AudioWindow::Tick() {
  if (!is_open) return;

  if (ImGui::Begin(Debugger::s_audio_window_name, &is_open)) {
    EmulatorConfig config = emulator_get_config(d->e);
    ImGui::Text("channel enable");
    ImGui::SameLine(0, 20);
    ImGui::CheckboxNot("1", &config.disable_sound[APU_CHANNEL1]);
    ImGui::SameLine();
    ImGui::CheckboxNot("2", &config.disable_sound[APU_CHANNEL2]);
    ImGui::SameLine();
    ImGui::CheckboxNot("3", &config.disable_sound[APU_CHANNEL3]);
    ImGui::SameLine();
    ImGui::CheckboxNot("4", &config.disable_sound[APU_CHANNEL4]);
    emulator_set_config(d->e, &config);
    if (ImGui::SliderFloat("Volume", &d->audio_volume, 0, 1)) {
      d->audio_volume = CLAMP(d->audio_volume, 0, 1);
      host_set_audio_volume(d->host, d->audio_volume);
    }

    ImGui::Spacing();
    ImGui::PlotLines("left", audio_data[0], kAudioDataSamples, 0, nullptr, 0,
                     128, ImVec2(0, 80));
    ImGui::PlotLines("right", audio_data[1], kAudioDataSamples, 0, nullptr, 0,
                     128, ImVec2(0, 80));

  }
  ImGui::End();
}
