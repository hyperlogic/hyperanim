/*
  Copyright (c) 2025 Anthony J. Thibault
  This software is licensed under the MIT License. See LICENSE for more
  details.
*/

#ifndef ARENA_H
#define ARENA_H

#include <stdbool.h>
#include <stddef.h>

typedef struct Arena {
  unsigned char *base;  // start of the backing buffer
  size_t offset;        // bytes handed out so far
  size_t capacity;
} Arena;

bool ArenaAlloc(Arena **arena, size_t capacity);
void ArenaFree(Arena *a);
void *ArenaAllocFromAligned(Arena *a, size_t size, size_t align);

// Default alignment: safe for any built-in type.
void *ArenaAllocFrom(Arena *a, size_t size);

#endif  // #define ARENA_H
