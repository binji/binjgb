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

static ImVec2 GetObjSizeVec2(ObjSize obj_size, f32 scale) {
  if (obj_size == OBJ_SIZE_8X16) {
    return ImVec2(k8x16OBJSize * scale);
  } else {
    return ImVec2(kTileSize * scale);
  }
}

Debugger::ObjWindow::ObjWindow(Debugger* d) : Window(d) {}

void Debugger::ObjWindow::Tick() {
  ImGui::SetNextDock(ImGuiDockSlot_Tab);
  if (ImGui::BeginDock("Obj", &is_open)) {
    static int scale = 4;
    static int obj_index = 0;

    ObjSize obj_size = emulator_get_obj_size(d->e);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    for (int y = 0; y < 4; ++y) {
      for (int x = 0; x < 10; ++x) {
        int button_index = y * 10 + x;
        Obj obj = emulator_get_obj(d->e, button_index);
        bool visible = static_cast<bool>(obj_is_visible(&obj));

        char label[16];
        snprintf(label, sizeof(label), "%2d", button_index);
        if (x > 0) {
          ImGui::SameLine();
        }

        ImVec2 button_size = GetObjSizeVec2(obj_size, scale);
        bool clicked;
        if (visible) {
          PaletteRGBA palette_rgba = emulator_get_palette_rgba(
              d->e, (PaletteType)(PALETTE_TYPE_OBP0 + obj.palette));

          int tile_index = d->tiledata_image.DrawOBJ(
              draw_list, obj_size, obj.tile, ImGui::GetCursorScreenPos(), scale,
              palette_rgba, obj.xflip, obj.yflip);

          if (tile_index >= 0) {
            d->highlight_tile = true;
            d->highlight_tile_index = tile_index;
          }
          clicked = ImGui::InvisibleButton(label, button_size);
        } else {
          ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32_BLACK);
          clicked = ImGui::Button(label, button_size);
          ImGui::PopStyleColor();
        }
        if (clicked) {
          d->highlight_obj_index = obj_index = button_index;
        }
        if (obj_index == button_index) {
          ImGui::GetWindowDrawList()->AddRect(
              ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32_WHITE);
        }
      }
    }

    ImGui::Checkbox("Highlight OBJ", &d->highlight_obj);
    ImGui::Separator();

    Obj obj = emulator_get_obj(d->e, obj_index);

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
