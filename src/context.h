#ifndef CONTEXT_H
#define CONTEXT_H

#include "arena.h"
#include "hyperanim.h"

typedef struct StrToIdPair {
  char *key;
  int value;
} StrToIdPair;

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
} Context;

HYA_Result ContextInit(Context *ctx, size_t arena_size, const char *filename);
HYA_Result ContextCreate(Context **ctx, size_t arena_size,
                         const char *filename);
void ContextDeinit(Context *ctx);
void ContextDestroy(Context *ctx);

HYA_STR_ID ContextInternString(Context *ctx, const char *str);

#endif  // CONTEXT_H
