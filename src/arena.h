/*
  Copyright (c) 2025 Anthony J. Thibault
  This software is licensed under the MIT License. See LICENSE for more
  details.
*/

#ifndef ARENA_H
#define ARENA_H

#include <stdbool.h>
#include <stddef.h>

#include "hyperanim.h"

typedef struct Arena {
  unsigned char *base;  // start of the backing buffer
  size_t offset;        // bytes handed out so far
  size_t capacity;
} Arena;

HYA_Result ArenaInit(Arena *arena, size_t arena_size);
HYA_Result ArenaCreate(Arena **arena, size_t arena_size);
void ArenaDeinit(Arena *arena);
void ArenaDestroy(Arena *arena);

void *ArenaAllocFromAligned(Arena *a, size_t size, size_t align);

// Default alignment: safe for any built-in type.
void *ArenaAllocFrom(Arena *a, size_t size);

#endif  // #define ARENA_H
