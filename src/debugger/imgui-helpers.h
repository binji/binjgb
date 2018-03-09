/*
 * Copyright (C) 2017 Ben Smith
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include "imgui.h"

#include "common.h"

namespace ImGui {

template <typename T, size_t N>
inline bool Combo(const char* label, T* value, const char* (&names)[N]) {
  int int_value = static_cast<int>(*value);
  bool result = Combo(label, &int_value, names, N);
  *value = static_cast<T>(int_value);
  return result;
}

inline bool CheckboxNot(const char* label, Bool* v) {
  bool bv = !*v;
  bool result = Checkbox(label, &bv);
  *v = static_cast<Bool>(!bv);
  return result;
}

}  // namespace ImGui

inline ImVec2 operator +(const ImVec2& lhs, const ImVec2& rhs) {
  return ImVec2(lhs.x + rhs.x, lhs.y + rhs.y);
}

inline ImVec2 operator -(const ImVec2& lhs, const ImVec2& rhs) {
  return ImVec2(lhs.x - rhs.x, lhs.y - rhs.y);
}

inline ImVec2 operator *(const ImVec2& lhs, f32 s) {
  return ImVec2(lhs.x * s, lhs.y * s);
}

inline ImVec2 operator *(const ImVec2& lhs, const ImVec2& rhs) {
  return ImVec2(lhs.x * rhs.x, lhs.y * rhs.y);
}
