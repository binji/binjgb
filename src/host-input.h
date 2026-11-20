/* Native keyboard bindings and routing. */
#ifndef BINJGB_HOST_INPUT_H_
#define BINJGB_HOST_INPUT_H_

#include "host.h"

typedef enum HostInputDestination {
  HOST_INPUT_NONE,
  HOST_INPUT_SHORTCUT,
  HOST_INPUT_JOYPAD,
  HOST_INPUT_PS2,
  HOST_INPUT_RAW,
} HostInputDestination;

/* The key is the logical key expected by existing joypad/shortcut handlers. */
#define FOREACH_HOST_INPUT_BINDING(_) \
  _(UP, "joypad-up", JOYPAD, UP) \
  _(DOWN, "joypad-down", JOYPAD, DOWN) \
  _(LEFT, "joypad-left", JOYPAD, LEFT) \
  _(RIGHT, "joypad-right", JOYPAD, RIGHT) \
  _(B, "joypad-b", JOYPAD, Z) \
  _(A, "joypad-a", JOYPAD, X) \
  _(START, "joypad-start", JOYPAD, RETURN) \
  _(SELECT, "joypad-select", JOYPAD, TAB) \
  _(QUIT, "shortcut-quit", SHORTCUT, ESCAPE) \
  _(SAVE, "shortcut-save-state", SHORTCUT, F6) \
  _(LOAD, "shortcut-load-state", SHORTCUT, F9) \
  _(FULLSCREEN, "shortcut-fullscreen", SHORTCUT, F11) \
  _(AUDIO1, "shortcut-audio-1", SHORTCUT, 1) \
  _(AUDIO2, "shortcut-audio-2", SHORTCUT, 2) \
  _(AUDIO3, "shortcut-audio-3", SHORTCUT, 3) \
  _(AUDIO4, "shortcut-audio-4", SHORTCUT, 4) \
  _(BG, "shortcut-bg", SHORTCUT, B) \
  _(WINDOW, "shortcut-window", SHORTCUT, W) \
  _(OBJ, "shortcut-obj", SHORTCUT, O) \
  _(FAST_FORWARD, "shortcut-fast-forward", SHORTCUT, LSHIFT) \
  _(REWIND, "shortcut-rewind", SHORTCUT, BACKSPACE) \
  _(PAUSE, "shortcut-pause", SHORTCUT, SPACE) \
  _(STEP, "shortcut-step", SHORTCUT, N) \
  _(VOLUME_DOWN, "shortcut-volume-down", SHORTCUT, MINUS) \
  _(VOLUME_UP, "shortcut-volume-up", SHORTCUT, EQUALS) \
  _(PALETTE_PREV, "shortcut-palette-prev", SHORTCUT, LEFTBRACKET) \
  _(PALETTE_NEXT, "shortcut-palette-next", SHORTCUT, RIGHTBRACKET)

typedef enum HostInputBinding {
#define V(id, name, destination, key) HOST_INPUT_BINDING_##id,
  FOREACH_HOST_INPUT_BINDING(V)
#undef V
  HOST_INPUT_BINDING_COUNT,
} HostInputBinding;

typedef struct HostInputConfig {
  u8 bindings[HOST_INPUT_BINDING_COUNT][HOST_KEYCODE_COUNT];
  u8 ps2_keys[HOST_KEYCODE_COUNT];
} HostInputConfig;

typedef struct HostInputRoute {
  HostInputDestination destination;
  HostKeycode key;
} HostInputRoute;

void host_input_defaults(HostInputConfig*);
/* Returns 1 if handled, 0 if unknown, -1 if invalid (config unchanged). */
int host_input_parse(HostInputConfig*, const char* name, const char* value);
/* Reports overlapping bindings within a destination. */
Bool host_input_validate(const HostInputConfig*);
HostInputRoute host_input_route(const HostInputConfig*, HostKeycode,
                                 Bool ps2_connected);

#endif /* BINJGB_HOST_INPUT_H_ */
