#ifndef CONTEXT_H
#define CONTEXT_H

#include "arena.h"
#include "hyperanim.h"

typedef struct StrToIdPair {
  char *key;
  int value;
} StrToIdPair;

#define CONTEXT_MAX_NUM_NAMES 64

typedef struct Context {
  HYA_Graph *graph;
  StrToIdPair *node_map;
  int next_node_id;
  StrToIdPair *type_map;
  StrToIdPair *var_map;
  int next_var_id;
  StrToIdPair *str_map;
  Arena *arena;
} Context;

HYA_Result ContextInit(Context *ctx, size_t arena_size);
HYA_Result ContextCreate(Context **ctx, size_t arena_size);
void ContextDeinit(Context *ctx);
void ContextDestroy(Context *ctx);

#endif  // CONTEXT_H
