/*
 * Copyright (C) 2019 Ben Smith
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include "debugger.h"

#include "imgui.h"

// static
const char Debugger::s_io_window_name[] = "IO";

Debugger::IOWindow::IOWindow(Debugger* d) : Window(d) {}

namespace {

void TextRegBits(Emulator* e, u8 v) {}

struct BitArg {
  u8 mask;
  const char* true_text;
  const char* false_text;
  const char* tooltip;
  bool invert;
};

BitArg Bit(u8 mask, const char* true_text, const char* false_text,
           const char* tooltip = 0, bool invert = false) {
  return BitArg{mask, true_text, false_text, tooltip, invert};
}

BitArg Bit0(u8 mask, const char* true_text, const char* tooltip = 0) {
  return Bit(mask, true_text, "", tooltip);
}

BitArg Bit1(u8 mask, const char* true_text, const char* tooltip = 0) {
  return Bit(mask, true_text, "_", tooltip);
}

BitArg Bit2(u8 mask, const char* true_text, const char* tooltip = 0) {
  return Bit(mask, true_text, "__", tooltip);
}

BitArg InvBit(u8 mask, const char* true_text, const char* tooltip = 0) {
  return Bit(mask, true_text, "_", tooltip, true);
}

template <typename... Args>
void TextRegBits(Emulator* e, u8 v, BitArg arg, Args... args) {
  u8 masked = v & arg.mask;
  bool is_set = arg.invert ? !masked : masked;
  const char* text = is_set ? arg.true_text : arg.false_text;
  if (strlen(text) != 0) {
    ImGui::SameLine();
    ImGui::Text("%s", text);
    if (arg.tooltip && ImGui::IsItemHovered()) {
      ImGui::SetTooltip(arg.tooltip);
    }
  }
  TextRegBits(e, v, args...);
}

struct IntArg {
  const char* text;
  u8 mask;
  int shift;
};

IntArg Int(const char* text, u8 mask, int shift = 0) {
  return IntArg{text, mask, shift};
}

template <typename... Args>
void TextRegBits(Emulator* e, u8 v, IntArg arg, Args... args) {
  ImGui::SameLine();
  ImGui::Text("%s:%d ", arg.text, (v & arg.mask) >> arg.shift);
  TextRegBits(e, v, args...);
}

template <typename T>
struct EnumArg {
  u8 mask;
};

template <typename T>
EnumArg<T> Enum(u8 mask) {
  return EnumArg<T>{mask};
}

const char* EnumToString(TimerClock clock) {
  switch (clock) {
    case TIMER_CLOCK_4096_HZ: return "4096 Hz";
    case TIMER_CLOCK_262144_HZ: return "262144 Hz";
    case TIMER_CLOCK_65536_HZ: return "65536 Hz";
    case TIMER_CLOCK_16384_HZ: return "16384 Hz";
    default: return "";
  }
}

template <typename T, typename... Args>
void TextRegBits(Emulator* e, u8 v, EnumArg<T> arg, Args... args) {
  ImGui::SameLine();
  ImGui::Text("%s ", EnumToString(static_cast<T>(v & arg.mask)));
  TextRegBits(e, v, args...);
}

struct DmgSwatchArg {
  PaletteType pal;
  int color_index;
};

struct SgbSwatchArg {
  int palette_index;
  int color_index;
};

struct CgbSwatchArg {
  CgbPaletteType type;
  int palette_index;
  int color_index;
};

DmgSwatchArg Swatch(PaletteType pal, int index) {
  return DmgSwatchArg{pal, index};
}

SgbSwatchArg SgbSwatch(int palette_index, int color_index) {
  return SgbSwatchArg{palette_index, color_index};
}

CgbSwatchArg CgbSwatch(CgbPaletteType type, int palette_index, int color_index) {
  return CgbSwatchArg{type, palette_index, color_index};
}

template <typename... Args>
void ColorSwatchTextRegBits(Emulator* e, u8 v, PaletteRGBA pal_rgba,
                            int color_index, Args... args) {
  ImGui::SameLine();
  ImGui::Text("%d:", color_index);
  u8 color = (v >> (color_index * 2)) & 3;
  RGBA color_rgba = pal_rgba.color[color];
  float sz = ImGui::GetTextLineHeight();
  ImGui::SameLine();
  ImVec2 p = ImGui::GetCursorScreenPos();
  ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + sz, p.y + sz),
                                            color_rgba);
  ImGui::Dummy(ImVec2(sz, sz));
  TextRegBits(e, v, args...);
}

template <typename... Args>
void TextRegBits(Emulator* e, u8 v, DmgSwatchArg arg, Args... args) {
  ColorSwatchTextRegBits(e, v, emulator_get_palette_rgba(e, arg.pal),
                         arg.color_index, args...);
}

template <typename... Args>
void TextRegBits(Emulator* e, u8 v, SgbSwatchArg arg, Args... args) {
  ColorSwatchTextRegBits(e, v,
                         emulator_get_sgb_palette_rgba(e, arg.palette_index),
                         arg.color_index, args...);
}

template <typename... Args>
void TextRegBits(Emulator* e, u8 v, CgbSwatchArg arg, Args... args) {
  ColorSwatchTextRegBits(
      e, v, emulator_get_cgb_palette_rgba(e, arg.type, arg.palette_index),
      arg.color_index, args...);
}

template <typename... Args>
void TextReg(Emulator* e, Address addr, const char* name, Args... args) {
  const ImVec4 kRegColor(1.f, 0.75f, 0.3f, 1.f);

  char buf[10];
  snprintf(buf, sizeof(buf), "[%s]", name);
  u8 v = emulator_read_u8_raw(e, addr);
  ImGui::Text("0x%04X%8s:", addr, buf);
  ImGui::SameLine();
  ImGui::TextColored(kRegColor, "%02X ", v);
  TextRegBits(e, v, args...);
}

template <typename... Args>
void TextSgbPal(Emulator* e, const char* name, Args... args) {
  const ImVec4 kRegColor(1.f, 0.75f, 0.3f, 1.f);

  char buf[15];
  u8 v = emulator_read_u8_raw(e, 0xff47);
  snprintf(buf, sizeof(buf), "[%s]", name);
  ImGui::Text("   %8s:   ", buf);
  ImGui::SameLine();
  TextRegBits(e, v, args...);
}

template <typename... Args>
void TextCgbPal(Emulator* e, const char* name, Args... args) {
  const ImVec4 kRegColor(1.f, 0.75f, 0.3f, 1.f);

  char buf[15];
  u8 v = 0xE4; // Always use normal order.
  snprintf(buf, sizeof(buf), "[%s]", name);
  ImGui::Text("   %8s:   ", buf);
  ImGui::SameLine();
  TextRegBits(e, v, args...);
}


}  // namespace

void Debugger::IOWindow::Tick() {
  if (!is_open) return;

  if (ImGui::Begin(Debugger::s_io_window_name, &is_open)) {
    u8 v;
    TextReg(d->e, 0xff00, "JOYP", InvBit(0x18, "D"), InvBit(0x14, "U"),
            InvBit(0x12, "L"), InvBit(0x11, "R"), InvBit(0x28, "+"),
            InvBit(0x24, "-"), InvBit(0x22, "B"), InvBit(0x21, "A"));

    TextReg(d->e, 0xff01, "SB");
    TextReg(d->e, 0xff02, "SC");
    TextReg(d->e, 0xff04, "DIV");
    TextReg(d->e, 0xff05, "TIMA");
    TextReg(d->e, 0xff06, "TMA");
    TextReg(d->e, 0xff07, "TAC", Bit(0x4, "on", "off"), Enum<TimerClock>(0x3));

    TextReg(d->e, 0xff0f, "IF", Bit0(0x10, "JOYP "), Bit0(0x8, "SERIAL "),
            Bit0(0x4, "TIMER "), Bit0(0x2, "STAT "), Bit0(0x1, "VBLANK "));

    TextReg(d->e, 0xff40, "LCDC", Bit2(0x80, "D ", "Display"),
            Bit2(0x40, "WM", "Window tile map select"),
            Bit2(0x20, "Wd", "Window display"),
            Bit2(0x10, "BD", "BG tile data select"),
            Bit2(0x08, "BM", "BG tile map select"),
            Bit2(0x04, "Os", "Obj size"), Bit2(0x02, "Od", "Obj display"),
            Bit2(0x01, "Bd", "BG display"));

    TextReg(d->e, 0xff41, "STAT", Bit2(0x40, "Yi", "Y compare interrupt"),
            Bit2(0x20, "2i", "Mode 2 interrupt"),
            Bit2(0x10, "Vi", "Vblank interrupt"),
            Bit2(0x08, "Hi", "Hblank interrupt"),
            Bit2(0x04, "Y=", "Y compare set"), Int("Mode", 0x03));

    TextReg(d->e, 0xff42, "SCY");
    TextReg(d->e, 0xff43, "SCX");
    TextReg(d->e, 0xff44, "LY");
    TextReg(d->e, 0xff45, "LYC");
    TextReg(d->e, 0xff47, "BGP", Swatch(PALETTE_TYPE_BGP, 0),
            Swatch(PALETTE_TYPE_BGP, 1), Swatch(PALETTE_TYPE_BGP, 2),
            Swatch(PALETTE_TYPE_BGP, 3));
    TextReg(d->e, 0xff48, "OPB0", Swatch(PALETTE_TYPE_OBP0, 0),
            Swatch(PALETTE_TYPE_OBP0, 1), Swatch(PALETTE_TYPE_OBP0, 2),
            Swatch(PALETTE_TYPE_OBP0, 3));
    TextReg(d->e, 0xff49, "OPB1", Swatch(PALETTE_TYPE_OBP1, 0),
            Swatch(PALETTE_TYPE_OBP1, 1), Swatch(PALETTE_TYPE_OBP1, 2),
            Swatch(PALETTE_TYPE_OBP1, 3));
    TextReg(d->e, 0xff4A, "WY");
    TextReg(d->e, 0xff4B, "WX");

    if (d->is_cgb) {
      TextReg(d->e, 0xff4d, "KEY1", Bit1(0x80, "Sp", "Current speed"),
              Bit1(0x1, "Sw", "Speed switch"));
      TextReg(d->e, 0xff4f, "VBK", Int("Bank", 0x1));
      TextReg(d->e, 0xff55, "HDMA5", Int("Mode", 0x80, 7), Int("Blocks", 0x7f));
      TextReg(d->e, 0xff56, "RP", Int("Enable", 0xc0, 6),
              Bit1(0x2, "R", "Read"), Bit1(0x01, "W", "Write"));

      TextReg(d->e, 0xff68, "BCPS", Bit1(0x80, "+", "Auto-increment"),
              Int("Index", 0x3f));
      TextReg(d->e, 0xff69, "BCPD");
      TextReg(d->e, 0xff6a, "OCPS", Bit1(0x80, "+", "Auto-increment"),
              Int("Index", 0x3f));
      TextReg(d->e, 0xff6b, "OCPD");
      TextReg(d->e, 0xff70, "SVBK", Int("Bank", 0x7));
    }

    TextReg(d->e, 0xffff, "IE", Bit0(0x10, "JOYP "), Bit0(0x8, "SERIAL "),
            Bit0(0x4, "TIMER "), Bit0(0x2, "STAT "), Bit0(0x1, "VBLANK "));

    if (d->is_cgb) {
      ImGui::NewLine();
      TextCgbPal(d->e, "CGB BG Pal 0", CgbSwatch(CGB_PALETTE_TYPE_BGCP, 0, 0),
                 CgbSwatch(CGB_PALETTE_TYPE_BGCP, 0, 1),
                 CgbSwatch(CGB_PALETTE_TYPE_BGCP, 0, 2),
                 CgbSwatch(CGB_PALETTE_TYPE_BGCP, 0, 3));
      TextCgbPal(d->e, "CGB BG Pal 1", CgbSwatch(CGB_PALETTE_TYPE_BGCP, 1, 0),
                 CgbSwatch(CGB_PALETTE_TYPE_BGCP, 1, 1),
                 CgbSwatch(CGB_PALETTE_TYPE_BGCP, 1, 2),
                 CgbSwatch(CGB_PALETTE_TYPE_BGCP, 1, 3));
      TextCgbPal(d->e, "CGB BG Pal 2", CgbSwatch(CGB_PALETTE_TYPE_BGCP, 2, 0),
                 CgbSwatch(CGB_PALETTE_TYPE_BGCP, 2, 1),
                 CgbSwatch(CGB_PALETTE_TYPE_BGCP, 2, 2),
                 CgbSwatch(CGB_PALETTE_TYPE_BGCP, 2, 3));
      TextCgbPal(d->e, "CGB BG Pal 3", CgbSwatch(CGB_PALETTE_TYPE_BGCP, 3, 0),
                 CgbSwatch(CGB_PALETTE_TYPE_BGCP, 3, 1),
                 CgbSwatch(CGB_PALETTE_TYPE_BGCP, 3, 2),
                 CgbSwatch(CGB_PALETTE_TYPE_BGCP, 3, 3));
      TextCgbPal(d->e, "CGB BG Pal 4", CgbSwatch(CGB_PALETTE_TYPE_BGCP, 4, 0),
                 CgbSwatch(CGB_PALETTE_TYPE_BGCP, 4, 1),
                 CgbSwatch(CGB_PALETTE_TYPE_BGCP, 4, 2),
                 CgbSwatch(CGB_PALETTE_TYPE_BGCP, 4, 3));
      TextCgbPal(d->e, "CGB BG Pal 5", CgbSwatch(CGB_PALETTE_TYPE_BGCP, 5, 0),
                 CgbSwatch(CGB_PALETTE_TYPE_BGCP, 5, 1),
                 CgbSwatch(CGB_PALETTE_TYPE_BGCP, 5, 2),
                 CgbSwatch(CGB_PALETTE_TYPE_BGCP, 5, 3));
      TextCgbPal(d->e, "CGB BG Pal 6", CgbSwatch(CGB_PALETTE_TYPE_BGCP, 6, 0),
                 CgbSwatch(CGB_PALETTE_TYPE_BGCP, 6, 1),
                 CgbSwatch(CGB_PALETTE_TYPE_BGCP, 6, 2),
                 CgbSwatch(CGB_PALETTE_TYPE_BGCP, 6, 3));
      TextCgbPal(d->e, "CGB BG Pal 7", CgbSwatch(CGB_PALETTE_TYPE_BGCP, 7, 0),
                 CgbSwatch(CGB_PALETTE_TYPE_BGCP, 7, 1),
                 CgbSwatch(CGB_PALETTE_TYPE_BGCP, 7, 2),
                 CgbSwatch(CGB_PALETTE_TYPE_BGCP, 7, 3));
      TextCgbPal(d->e, "CGB OBJ Pal 0", CgbSwatch(CGB_PALETTE_TYPE_OBCP, 0, 0),
                 CgbSwatch(CGB_PALETTE_TYPE_OBCP, 0, 1),
                 CgbSwatch(CGB_PALETTE_TYPE_OBCP, 0, 2),
                 CgbSwatch(CGB_PALETTE_TYPE_OBCP, 0, 3));
      TextCgbPal(d->e, "CGB OBJ Pal 1", CgbSwatch(CGB_PALETTE_TYPE_OBCP, 1, 0),
                 CgbSwatch(CGB_PALETTE_TYPE_OBCP, 1, 1),
                 CgbSwatch(CGB_PALETTE_TYPE_OBCP, 1, 2),
                 CgbSwatch(CGB_PALETTE_TYPE_OBCP, 1, 3));
      TextCgbPal(d->e, "CGB OBJ Pal 2", CgbSwatch(CGB_PALETTE_TYPE_OBCP, 2, 0),
                 CgbSwatch(CGB_PALETTE_TYPE_OBCP, 2, 1),
                 CgbSwatch(CGB_PALETTE_TYPE_OBCP, 2, 2),
                 CgbSwatch(CGB_PALETTE_TYPE_OBCP, 2, 3));
      TextCgbPal(d->e, "CGB OBJ Pal 3", CgbSwatch(CGB_PALETTE_TYPE_OBCP, 3, 0),
                 CgbSwatch(CGB_PALETTE_TYPE_OBCP, 3, 1),
                 CgbSwatch(CGB_PALETTE_TYPE_OBCP, 3, 2),
                 CgbSwatch(CGB_PALETTE_TYPE_OBCP, 3, 3));
      TextCgbPal(d->e, "CGB OBJ Pal 4", CgbSwatch(CGB_PALETTE_TYPE_OBCP, 4, 0),
                 CgbSwatch(CGB_PALETTE_TYPE_OBCP, 4, 1),
                 CgbSwatch(CGB_PALETTE_TYPE_OBCP, 4, 2),
                 CgbSwatch(CGB_PALETTE_TYPE_OBCP, 4, 3));
      TextCgbPal(d->e, "CGB OBJ Pal 5", CgbSwatch(CGB_PALETTE_TYPE_OBCP, 5, 0),
                 CgbSwatch(CGB_PALETTE_TYPE_OBCP, 5, 1),
                 CgbSwatch(CGB_PALETTE_TYPE_OBCP, 5, 2),
                 CgbSwatch(CGB_PALETTE_TYPE_OBCP, 5, 3));
      TextCgbPal(d->e, "CGB OBJ Pal 6", CgbSwatch(CGB_PALETTE_TYPE_OBCP, 6, 0),
                 CgbSwatch(CGB_PALETTE_TYPE_OBCP, 6, 1),
                 CgbSwatch(CGB_PALETTE_TYPE_OBCP, 6, 2),
                 CgbSwatch(CGB_PALETTE_TYPE_OBCP, 6, 3));
      TextCgbPal(d->e, "CGB OBJ Pal 7", CgbSwatch(CGB_PALETTE_TYPE_OBCP, 7, 0),
                 CgbSwatch(CGB_PALETTE_TYPE_OBCP, 7, 1),
                 CgbSwatch(CGB_PALETTE_TYPE_OBCP, 7, 2),
                 CgbSwatch(CGB_PALETTE_TYPE_OBCP, 7, 3));
    } else if (d->is_sgb) {
      ImGui::NewLine();
      TextSgbPal(d->e, "SGB Pal 0", SgbSwatch(0, 0), SgbSwatch(0, 1),
                 SgbSwatch(0, 2), SgbSwatch(0, 3));
      TextSgbPal(d->e, "SGB Pal 1", SgbSwatch(1, 0), SgbSwatch(1, 1),
                 SgbSwatch(1, 2), SgbSwatch(1, 3));
      TextSgbPal(d->e, "SGB Pal 2", SgbSwatch(2, 0), SgbSwatch(2, 1),
                 SgbSwatch(2, 2), SgbSwatch(2, 3));
      TextSgbPal(d->e, "SGB Pal 3", SgbSwatch(3, 0), SgbSwatch(3, 1),
                 SgbSwatch(3, 2), SgbSwatch(3, 3));
    }
  }
  ImGui::End();
}
