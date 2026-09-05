#ifndef CONTEXT_H
#define CONTEXT_H

#include "arena.h"
#include "hyperanim.h"

typedef struct StrToIdPair {
  char *key;
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
  uint8_t *ptr;
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
  StrToIdPair *node_map;
  int next_node_id;
  StrToIdPair *type_map;
  StrToIdPair *var_map;
  int next_var_id;
  StrToIdPair *str_map;
  const char **str_arr;
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
uint8_t *ContextAllocFromAligned(Context *ctx, HYA_MemCategory cat, size_t size,
                                 size_t align);

// Default alignment: safe for any built-in type.
uint8_t *ContextAllocFrom(Context *ctx, HYA_MemCategory cat, size_t size);

#endif  // CONTEXT_H
