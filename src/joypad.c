/*
 * Copyright (C) 2018 Ben Smith
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include "joypad.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include "emulator.h"

#define DEBUG_JOYPAD_BUTTONS 0

#define JOYPAD_CHUNK_DEFAULT_CAPACITY 4096

#define GET_TICKS(x) ((x).ticks)
#define CMP_LT(x, y) ((x) < (y))

JoypadBuffer* joypad_new(void) {
  JoypadBuffer* buffer = xmalloc(sizeof(JoypadBuffer));
  ZERO_MEMORY(*buffer);
  buffer->sentinel.next = buffer->sentinel.prev = &buffer->sentinel;
  joypad_append(buffer, &buffer->last_buttons, 0);
  return buffer;
}

void joypad_delete(JoypadBuffer* buffer) {
  if (!buffer) {
    return;
  }
  JoypadChunk* current = buffer->sentinel.next;
  while (current != &buffer->sentinel) {
    JoypadChunk* next = current->next;
    xfree(current->data);
    xfree(current);
    current = next;
  }
  xfree(buffer);
}

static JoypadChunk* alloc_joypad_chunk(size_t capacity) {
  JoypadChunk* chunk = xcalloc(1, sizeof(JoypadChunk));
  chunk->data = xcalloc(1, capacity * sizeof(JoypadState));
  chunk->capacity = capacity;
  return chunk;
}

static JoypadState* alloc_joypad_state(JoypadBuffer* buffer) {
  JoypadChunk* tail = buffer->sentinel.prev;
  if (tail->size >= tail->capacity) {
    JoypadChunk* new_chunk = alloc_joypad_chunk(JOYPAD_CHUNK_DEFAULT_CAPACITY);
    new_chunk->next = &buffer->sentinel;
    new_chunk->prev = tail;
    buffer->sentinel.prev = tail->next = new_chunk;
    tail = new_chunk;
  }
  return &tail->data[tail->size++];
}

void joypad_append(JoypadBuffer* buffer, JoypadButtons* buttons, Ticks ticks) {
  JoypadState* state = alloc_joypad_state(buffer);
  state->ticks = ticks;
  state->buttons = joypad_pack_buttons(buttons);
  buffer->last_buttons = *buttons;
}

static Bool buttons_are_equal(JoypadButtons* lhs, JoypadButtons* rhs) {
  return lhs->down == rhs->down && lhs->up == rhs->up &&
         lhs->left == rhs->left && lhs->right == rhs->right &&
         lhs->start == rhs->start && lhs->select == rhs->select &&
         lhs->B == rhs->B && lhs->A == rhs->A;
}

static void print_joypad_buttons(Ticks ticks, JoypadButtons buttons) {
  printf("joyp: %" PRIu64 " %c%c%c%c %c%c%c%c\n", ticks,
         buttons.down ? 'D' : '_', buttons.up ? 'U' : '_',
         buttons.left ? 'L' : '_', buttons.right ? 'R' : '_',
         buttons.start ? 'S' : '_', buttons.select ? 's' : '_',
         buttons.B ? 'B' : '_', buttons.A ? 'A' : '_');
}

void joypad_append_if_new(JoypadBuffer* buffer, JoypadButtons* buttons,
                          Ticks ticks) {
  if (!buttons_are_equal(buttons, &buffer->last_buttons)) {
    joypad_append(buffer, buttons, ticks);
#if DEBUG_JOYPAD_BUTTONS
    print_joypad_buttons(ticks, *buttons);
#endif
  }
}

JoypadStateIter joypad_find_state(JoypadBuffer* buffer, Ticks ticks) {
  /* TODO(binji): Use a skip list if this is too slow? */
  JoypadStateIter result;
  JoypadChunk* first_chunk = buffer->sentinel.next;
  JoypadChunk* last_chunk = buffer->sentinel.prev;
  assert(first_chunk->size != 0 && last_chunk->size != 0);
  Ticks first_ticks = first_chunk->data[0].ticks;
  Ticks last_ticks = last_chunk->data[last_chunk->size - 1].ticks;
  if (ticks <= first_ticks) {
    /* At or past the beginning. */
    result.chunk = first_chunk;
    result.state = &first_chunk->data[0];
    return result;
  } else if (ticks >= last_ticks) {
    /* At or past the end. */
    result.chunk = last_chunk;
    result.state = &last_chunk->data[last_chunk->size - 1];
    return result;
  } else if (ticks - first_ticks < last_ticks - ticks) {
    /* Closer to the beginning. */
    JoypadChunk* chunk = first_chunk;
    while (ticks >= chunk->data[chunk->size - 1].ticks) {
      chunk = chunk->next;
    }
    result.chunk = chunk;
  } else {
    /* Closer to the end. */
    JoypadChunk* chunk = last_chunk;
    while (ticks < chunk->data[0].ticks) {
      chunk = chunk->prev;
    }
    result.chunk = chunk;
  }

  JoypadState* begin = result.chunk->data;
  JoypadState* end = begin + result.chunk->size;
  LOWER_BOUND(JoypadState, lower_bound, begin, end, ticks, GET_TICKS, CMP_LT);
  assert(lower_bound != NULL); /* The chunk should not be empty. */

  result.state = lower_bound;
  assert(result.state->ticks <= ticks);
  return result;
}

void joypad_truncate_to(JoypadBuffer* buffer, JoypadStateIter iter) {
  size_t index = iter.state - iter.chunk->data;
  iter.chunk->size = index + 1;
  JoypadChunk* chunk = iter.chunk->next;
  JoypadChunk* sentinel = &buffer->sentinel;
  while (chunk != sentinel) {
    JoypadChunk* temp = chunk->next;
    xfree(chunk->data);
    xfree(chunk);
    chunk = temp;
  }
  iter.chunk->next = sentinel;
  sentinel->prev = iter.chunk;
  buffer->last_buttons = joypad_unpack_buttons(iter.state->buttons);
}

JoypadStateIter joypad_get_next_state(JoypadStateIter iter) {
  size_t index = iter.state - iter.chunk->data;
  if (index + 1 < iter.chunk->size) {
    ++iter.state;
    return iter;
  }

  iter.chunk = iter.chunk->next;
  iter.state = iter.chunk->size != 0 ? iter.chunk->data : NULL;
  return iter;
}

u8 joypad_pack_buttons(JoypadButtons* buttons) {
  return (buttons->down << 7) | (buttons->up << 6) | (buttons->left << 5) |
         (buttons->right << 4) | (buttons->start << 3) |
         (buttons->select << 2) | (buttons->B << 1) | (buttons->A << 0);
}

JoypadButtons joypad_unpack_buttons(u8 packed) {
  JoypadButtons buttons;
  buttons.A = packed & 1;
  buttons.B = (packed >> 1) & 1;
  buttons.select = (packed >> 2) & 1;
  buttons.start = (packed >> 3) & 1;
  buttons.right = (packed >> 4) & 1;
  buttons.left = (packed >> 5) & 1;
  buttons.up = (packed >> 6) & 1;
  buttons.down = (packed >> 7) & 1;
  return buttons;
}

JoypadStats joypad_get_stats(JoypadBuffer* buffer) {
  JoypadStats stats;
  ZERO_MEMORY(stats);
  JoypadChunk* sentinel = &buffer->sentinel;
  JoypadChunk* cur = sentinel->next;
  while (cur != sentinel) {
    size_t overhead = sizeof(*cur);
    stats.used_bytes += cur->size * sizeof(JoypadState) + overhead;
    stats.capacity_bytes += cur->capacity * sizeof(JoypadState) + overhead;
    cur = cur->next;
  }
  return stats;
}

static size_t joypad_file_size(JoypadBuffer* buffer) {
  size_t size = 0;
  JoypadChunk* chunk = buffer->sentinel.next;
  while (chunk != &buffer->sentinel) {
    size += chunk->size * sizeof(JoypadState);
    chunk = chunk->next;
  }
  return size;
}

void joypad_init_file_data(JoypadBuffer* buffer, FileData* file_data) {
  file_data->size = joypad_file_size(buffer);
  file_data->data = xmalloc(file_data->size);
}

Result joypad_write(JoypadBuffer* buffer, FileData* file_data) {
  JoypadChunk* chunk = buffer->sentinel.next;
  u8* p = file_data->data;
  while (chunk != &buffer->sentinel) {
    size_t chunk_size = chunk->size * sizeof(JoypadState);
    memcpy(p, chunk->data, chunk_size);
    p += chunk_size;
    chunk = chunk->next;
  }
  return OK;
}

Result joypad_read(const FileData* file_data, JoypadBuffer** out_buffer) {
  CHECK_MSG(file_data->size % sizeof(JoypadState) == 0,
            "Expected joypad file size to be multiple of %zu\n",
            sizeof(JoypadState));
  size_t size = file_data->size / sizeof(JoypadState);
  size_t i;
  Ticks last_ticks = 0;
  for (i = 0; i < size; ++i) {
    JoypadState* state = (JoypadState*)file_data->data + i;
    Ticks ticks = state->ticks;
    CHECK_MSG(ticks >= last_ticks,
              "Expected ticks to be sorted, got %" PRIu64 " then %" PRIu64 "\n",
              last_ticks, ticks);
    size_t j;
    for (j = 0; j < ARRAY_SIZE(state->padding); ++j) {
      CHECK_MSG(state->padding[j] == 0, "Expected padding to be zero, got %u\n",
                state->padding[i]);
    }
    last_ticks = ticks;
  }

  JoypadBuffer* buffer = xcalloc(1, sizeof(JoypadBuffer));
  JoypadChunk* new_chunk = alloc_joypad_chunk(size);
  memcpy(new_chunk->data, file_data->data, file_data->size);
  new_chunk->size = size;
  new_chunk->prev = new_chunk->next = &buffer->sentinel;
  buffer->sentinel.prev = buffer->sentinel.next = new_chunk;
  *out_buffer = buffer;
  return OK;
  ON_ERROR_RETURN;
}

static void joypad_playback_callback(struct JoypadButtons* joyp,
                                     void* user_data) {
  Bool changed = FALSE;
  JoypadPlayback* playback = user_data;
  Ticks ticks = emulator_get_ticks(playback->e);
  if (ticks < playback->current.state->ticks) {
    playback->current = joypad_find_state(playback->buffer, ticks);
    playback->next = joypad_get_next_state(playback->current);
    changed = TRUE;
  }

  assert(ticks >= playback->current.state->ticks);

  while (playback->next.state && playback->next.state->ticks <= ticks) {
    assert(playback->next.state->ticks >= playback->current.state->ticks);
    playback->current = playback->next;
    playback->next = joypad_get_next_state(playback->next);
    changed = TRUE;
  }

#if DEBUG_JOYPAD_BUTTONS
  if (changed) {
    print_joypad_buttons(
        playback->current.state->ticks,
        joypad_unpack_buttons(playback->current.state->buttons));
  }
#else
  (void)changed;
#endif

  *joyp = joypad_unpack_buttons(playback->current.state->buttons);
}

static void init_joypad_playback_state(JoypadPlayback* playback,
                                       JoypadBuffer* buffer,
                                       struct Emulator* e) {
  playback->e = e;
  playback->buffer = buffer;
  playback->current =
      joypad_find_state(playback->buffer, emulator_get_ticks(e));
  playback->next = joypad_get_next_state(playback->current);
}

void emulator_set_joypad_playback_callback(struct Emulator* e,
                                           JoypadBuffer* buffer,
                                           JoypadPlayback* playback) {
  init_joypad_playback_state(playback, buffer, e);
  emulator_set_joypad_callback(e, joypad_playback_callback, playback);
}
