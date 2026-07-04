#include "context.h"

#include <stdio.h>
#include <stdlib.h>

#include "stb_ds.h"

HYA_Result ContextInit(Context *ctx, size_t arena_size) {
  memset(ctx, 0, sizeof(Context));
  shdefault(ctx->node_map, -1);
  shdefault(ctx->type_map, -1);
  shdefault(ctx->var_map, -1);
  shdefault(ctx->str_map, -1);
  HYA_Result res = ArenaCreate(&ctx->arena, arena_size);
  if (res != HYA_OK) {
    printf("ERROR ArenaCreate failure %d\n", res);
    return res;
  }
  return HYA_OK;
}

HYA_Result ContextCreate(Context **ctx, size_t arena_size) {
  *ctx = (Context *)malloc(sizeof(Context));
  if (!*ctx) {
    return HYA_ERR_OUT_OF_MEMORY;
  }
  return ContextInit(*ctx, arena_size);
}

void ContextDeinit(Context *ctx) {
  ArenaDestroy(ctx->arena);
  shfree(ctx->node_map);
  shfree(ctx->type_map);
  shfree(ctx->var_map);
  shfree(ctx->str_map);
}

void ContextDestroy(Context *ctx) {
  ContextDeinit(ctx);
  free(ctx);
}

HYA_STR_ID ContextAddString(Context *ctx, const char* str) {
  int str_id = shget(ctx->str_map, str);
  if (str_id < 0) {
    str_id = ctx->next_str_id++;
    shput(ctx->str_map, str, str_id);
  }
  return str_id;
}
