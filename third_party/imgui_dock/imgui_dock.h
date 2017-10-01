#ifndef IMGUI_DOCK_H_
#define IMGUI_DOCK_H_

#include "imgui.h"

enum ImGuiDockSlot {
  ImGuiDockSlot_Left,
  ImGuiDockSlot_Right,
  ImGuiDockSlot_Top,
  ImGuiDockSlot_Bottom,
  ImGuiDockSlot_Tab,
  ImGuiDockSlot_Float,
  ImGuiDockSlot_None
};

namespace ImGui {

void BeginWorkspace();
void EndWorkspace();
void ShutdownDock();
void SetNextDock(ImGuiDockSlot);
void SetNextDockParentToRoot();
void SetDockActive();
bool BeginDock(const char* label, bool* opened = nullptr,
               ImGuiWindowFlags extra_flags = 0);
void EndDock();
void SetDockActive();

}  // namespace ImGui

#endif // IMGUI_DOCK_H_
