/*
  Copyright (c) 2025 Anthony J. Thibault
  This software is licensed under the MIT License. See LICENSE for more
  details.
*/

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

#include "arena.h"

HYA_Result ArenaInit(Arena *arena, size_t arena_size) {
  assert(arena);
  arena->base = malloc(arena_size);
  if (!arena->base) {
    return HYA_ERR_OUT_OF_MEMORY;
  }
  arena->offset = 0;
  arena->capacity = arena_size;
  return HYA_OK;
}

HYA_Result ArenaCreate(Arena **arena, size_t arena_size) {
  assert(arena);
  *arena = (Arena *)malloc(sizeof(Arena));
  if (!*arena) {
    return HYA_ERR_OUT_OF_MEMORY;
  }
  return ArenaInit(*arena, arena_size);
}

void ArenaDeinit(Arena *arena) {
  assert(arena);
  free(arena->base);
}

void ArenaDestroy(Arena *arena) {
  ArenaDeinit(arena);
  free(arena);
}

// Round n up to a power-of-two alignment.
static inline uintptr_t AlignUp(uintptr_t n, size_t align) {
  assert((align & (align - 1)) == 0 && "alignment must be a power of two");
  return (n + (align - 1)) & ~(uintptr_t)(align - 1);
}

uint8_t *ArenaAllocFromAligned(Arena *a, size_t size, size_t align) {
  uintptr_t curr = (uintptr_t)a->base + a->offset;
  uintptr_t aligned = AlignUp(curr, align);
  size_t padding = aligned - curr;

  // Overflow-safe capacity check.
  if ((padding > a->capacity - a->offset) ||
      (size > a->capacity - a->offset - padding)) {
    return NULL;  // out of space
  }

  a->offset += padding + size;
  return (void *)aligned;  // NOLINT
}

uint8_t *ArenaAllocFrom(Arena *a, size_t size) {
  return ArenaAllocFromAligned(a, size, _Alignof(max_align_t));
}
