/*
  Copyright (c) 2025 Anthony J. Thibault
  This software is licensed under the MIT License. See LICENSE for more
  details.
*/

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

#include "arena.h"

bool ArenaAlloc(Arena **arena, size_t capacity) {
  assert(arena);
  Arena *a = (Arena *)malloc(sizeof(Arena));
  a->base = malloc(capacity);
  if (!a->base) {
    free(a);
    return false;
  }
  a->offset = 0;
  a->capacity = capacity;
  *arena = a;
  return true;
}

void ArenaFree(Arena *a) {
  free(a->base);
  free(a);
}

// Round n up to a power-of-two alignment.
static inline uintptr_t AlignUp(uintptr_t n, size_t align) {
  assert((align & (align - 1)) == 0 && "alignment must be a power of two");
  return (n + (align - 1)) & ~(uintptr_t)(align - 1);
}

void *ArenaAllocFromAligned(Arena *a, size_t size, size_t align) {
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

void *ArenaAllocFrom(Arena *a, size_t size) {
  return ArenaAllocFromAligned(a, size, _Alignof(max_align_t));
}
