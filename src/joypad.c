/*
 * Copyright (C) 2018 Ben Smith
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include "joypad.h"

#include <assert.h>
#include <stdlib.h>

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
  JoypadChunk* chunk = xmalloc(sizeof(JoypadChunk));
  ZERO_MEMORY(*chunk);
  chunk->data = xmalloc(capacity * sizeof(JoypadState));
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

void joypad_append_if_new(JoypadBuffer* buffer, JoypadButtons* buttons,
                          Ticks ticks) {
  if (!buttons_are_equal(buttons, &buffer->last_buttons)) {
    joypad_append(buffer, buttons, ticks);
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
}

JoypadStateIter joypad_get_next_state(JoypadStateIter iter) {
  size_t index = iter.state - iter.chunk->data;
  if (index < iter.chunk->size) {
    ++iter.state;
    return iter;
  }

  iter.chunk = iter.chunk->next;
  iter.state = iter.chunk ? iter.chunk->data : NULL;
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
