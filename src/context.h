#ifndef CONTEXT_H
#define CONTEXT_H

#include "arena.h"
#include "hyperanim.h"

typedef struct Symbol {
  const char *key;
} Symbol;

// TODO: REPLACE with Symbol.
typedef struct StrToIdPair {
  const char *key;
  int value;
} StrToIdPair;

typedef enum HYA_MemCategory {
  HYA_MEM_NODE = 0,
  HYA_MEM_STRING,
  HYA_MEM_VAR,
  HYA_MEM_SKELETON,
  HYA_MEM_MOTION,
  HYA_MEM_COUNT
} HYA_MemCategory;

typedef struct RelocInfo {
  void *addr;
  void *ptr;
  ptrdiff_t offset;
  size_t size;
  HYA_MemCategory cat;
} RelocInfo;

#define CONTEXT_MAX_NUM_NAMES 64
#define CONTEXT_PATH_SIZE 1024

typedef struct Context {
  char dirname[CONTEXT_PATH_SIZE];
  char basename[CONTEXT_PATH_SIZE];
  HYA_Graph *graph;
  Symbol *node_map;
  Symbol *type_map;
  Symbol *var_map;
  Symbol *str_map;
  Arena *arena;
  RelocInfo *reloc_arr;
} Context;

HYA_Result ContextInit(Context *ctx, size_t arena_size, const char *filename);
HYA_Result ContextCreate(Context **ctx, size_t arena_size,
                         const char *filename);
void ContextDeinit(Context *ctx);
void ContextDestroy(Context *ctx);

HYA_STR_ID ContextInternString(Context *ctx, const char *str);

// Specific alignment: for minimal padding
uint8_t *ContextAllocFromAligned(Context *ctx, HYA_MemCategory cat, void *addr,
                                 size_t size, size_t align);

// Default alignment: safe for any built-in type.
uint8_t *ContextAllocFrom(Context *ctx, HYA_MemCategory cat, void *addr,
                          size_t size);
void ContextRelocAlias(Context *ctx, void *addr, void *ptr);

#endif  // CONTEXT_H
