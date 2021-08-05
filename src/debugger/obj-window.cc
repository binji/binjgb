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
const char Debugger::s_obj_window_name[] = "Obj";

static ImVec2 GetObjSizeVec2(ObjSize obj_size, f32 scale) {
  if (obj_size == OBJ_SIZE_8X16) {
    return ImVec2(k8x16OBJSize * scale);
  } else {
    return ImVec2(kTileSize * scale);
  }
}

Debugger::ObjWindow::ObjWindow(Debugger* d) : Window(d) {}

void Debugger::ObjWindow::Tick() {
  if (!is_open) return;

  if (ImGui::Begin(Debugger::s_obj_window_name, &is_open)) {
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
        if (visible) {
          PaletteRGBA palette_rgba;
          if (d->is_cgb) {
            palette_rgba = emulator_get_cgb_palette_rgba(
                d->e, CGB_PALETTE_TYPE_OBCP, obj.cgb_palette);
          } else {
            palette_rgba = emulator_get_palette_rgba(
                d->e, (PaletteType)(PALETTE_TYPE_OBP0 + obj.palette));
          }

          int tile_index = d->DrawOBJ(draw_list, obj_size, GetObjTile(obj),
                                      ImGui::GetCursorScreenPos(), scale,
                                      palette_rgba, obj.xflip, obj.yflip);

          if (tile_index >= 0) {
            d->highlight_tile = true;
            d->highlight_tile_index = tile_index;
            d->highlight_obj_index = obj_index = button_index;
          }
          ImGui::InvisibleButton(label, button_size);
        } else {
          ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32_BLACK);
          ImGui::Button(label, button_size);
          ImGui::PopStyleColor();
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

    int tile_index = GetObjTile(obj);
    ImGui::LabelText("Obj Index", "%d", obj_index);
    ImGui::LabelText("OAM Address", "%04x", 0xfe00 + obj_index * 4);
    ImGui::LabelText("Tile Index", "%02x", d->GetByteTileIndex(tile_index));
    ImGui::LabelText("Tile Address", "%d:%04x", d->GetTileBank(tile_index),
                     d->GetTileAddr(tile_index));
    ImGui::LabelText("Pos", "%d, %d", obj.x, obj.y);
    ImGui::LabelText(
        "Priority", "%s",
        obj.priority == OBJ_PRIORITY_ABOVE_BG ? "Above BG" : "Behind BG");
    ImGui::LabelText("Flip", "%c%c", obj.xflip ? 'X' : '_',
                     obj.yflip ? 'Y' : '_');
    if (d->is_cgb) {
      ImGui::LabelText("Bank", "%d", obj.bank);
      ImGui::LabelText("Palette", "OBCP%d", obj.cgb_palette);
    } else {
      ImGui::LabelText("Palette", "OBP%d", obj.palette);
    }
  }
  ImGui::End();
}

int Debugger::ObjWindow::GetObjTile(Obj obj) {
  if (d->is_cgb && obj.bank) {
    return obj.tile + 0x180;
  } else{
    return obj.tile;
  }
}
