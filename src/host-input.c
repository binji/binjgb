#include "host-input.h"

#include <ctype.h>

static const struct {
  const char* name;
  HostInputDestination destination;
  HostKeycode key;
} s_bindings[] = {
#define V(id, name, destination, key) {name, HOST_INPUT_##destination, HOST_KEYCODE_##key},
  FOREACH_HOST_INPUT_BINDING(V)
#undef V
};

static const char* s_key_names[] = {
  "UNKNOWN",
#define V(name) #name,
  FOREACH_HOST_KEYCODE(V)
#undef V
};

static Bool name_equals(const char* a, size_t length, const char* b) {
  if (length != strlen(b)) return FALSE;
  size_t i;
  for (i = 0; i < length; ++i) {
    if (toupper((unsigned char)a[i]) != toupper((unsigned char)b[i])) return FALSE;
  }
  return TRUE;
}

static Bool parse_keys(const char* value, u8* keys, Bool allow_all) {
  memset(keys, 0, HOST_KEYCODE_COUNT);
  const char* end = value + strlen(value);
  while (isspace((unsigned char)*value)) ++value;
  while (end > value && isspace((unsigned char)end[-1])) --end;
  if (value == end || name_equals(value, end - value, "none")) return TRUE;
  if (allow_all && name_equals(value, end - value, "all")) {
    memset(keys + 1, 1, HOST_KEYCODE_COUNT - 1);
    return TRUE;
  }
  while (value < end) {
    const char* comma = memchr(value, ',', end - value);
    const char* token_end = comma ? comma : end;
    while (isspace((unsigned char)*value)) ++value;
    while (token_end > value && isspace((unsigned char)token_end[-1])) --token_end;
    HostKeycode key;
    for (key = 1; key < HOST_KEYCODE_COUNT; ++key) {
      if (name_equals(value, token_end - value, s_key_names[key])) break;
    }
    if (key == HOST_KEYCODE_COUNT) return FALSE;
    keys[key] = 1;
    if (!comma) break;
    value = comma + 1;
    if (value == end) return FALSE;
  }
  return TRUE;
}

void host_input_defaults(HostInputConfig* config) {
  ZERO_MEMORY(*config);
  size_t i;
  for (i = 0; i < ARRAY_SIZE(s_bindings); ++i) {
    config->bindings[i][s_bindings[i].key] = 1;
  }
  parse_keys("all", config->ps2_keys, TRUE);
}

int host_input_parse(HostInputConfig* config, const char* name, const char* value) {
  u8 keys[HOST_KEYCODE_COUNT];
  if (strcmp(name, "ps2-keys") == 0) {
    if (!parse_keys(value, keys, TRUE)) return -1;
    memcpy(config->ps2_keys, keys, sizeof(keys));
    return 1;
  }
  /* Convenient starting points for a typing or controller-only profile. */
  HostInputDestination group = HOST_INPUT_NONE;
  if (strcmp(name, "shortcuts") == 0) group = HOST_INPUT_SHORTCUT;
  if (strcmp(name, "joypad") == 0) group = HOST_INPUT_JOYPAD;
  if (group != HOST_INPUT_NONE) {
    Bool defaults = name_equals(value, strlen(value), "default");
    if (!defaults && !name_equals(value, strlen(value), "none")) return -1;
    size_t i;
    for (i = 0; i < ARRAY_SIZE(s_bindings); ++i) {
      if (s_bindings[i].destination != group) continue;
      memset(config->bindings[i], 0, HOST_KEYCODE_COUNT);
      if (defaults) config->bindings[i][s_bindings[i].key] = 1;
    }
    return 1;
  }
  size_t i;
  for (i = 0; i < ARRAY_SIZE(s_bindings); ++i) {
    if (strcmp(name, s_bindings[i].name) != 0) continue;
    if (!parse_keys(value, keys, FALSE)) return -1;
    memcpy(config->bindings[i], keys, sizeof(keys));
    return 1;
  }
  return 0;
}

Bool host_input_validate(const HostInputConfig* config) {
  Bool valid = TRUE;
  size_t i, j;
  HostKeycode key;
  for (i = 0; i < ARRAY_SIZE(s_bindings); ++i) {
    for (j = i + 1; j < ARRAY_SIZE(s_bindings); ++j) {
      if (s_bindings[i].destination != s_bindings[j].destination) continue;
      for (key = 1; key < HOST_KEYCODE_COUNT; ++key) {
        if (config->bindings[i][key] && config->bindings[j][key]) {
          fprintf(stderr, "conflicting input bindings: %s and %s both use %s\n",
                  s_bindings[i].name, s_bindings[j].name, s_key_names[key]);
          valid = FALSE;
        }
      }
    }
  }
  return valid;
}

HostInputRoute host_input_route(const HostInputConfig* config, HostKeycode key,
                                 Bool ps2_connected) {
  HostInputRoute route = {HOST_INPUT_NONE, HOST_KEYCODE_UNKNOWN};
  if (key <= HOST_KEYCODE_UNKNOWN || key >= HOST_KEYCODE_COUNT) return route;
  HostInputDestination destination;
  for (destination = HOST_INPUT_SHORTCUT; destination <= HOST_INPUT_JOYPAD;
       ++destination) {
    size_t i;
    for (i = 0; i < ARRAY_SIZE(s_bindings); ++i) {
      if (s_bindings[i].destination == destination && config->bindings[i][key]) {
        route.destination = destination;
        route.key = s_bindings[i].key;
        return route;
      }
    }
  }
  if (ps2_connected && config->ps2_keys[key]) {
    route.destination = HOST_INPUT_PS2;
    route.key = key;
  }
  return route;
}
