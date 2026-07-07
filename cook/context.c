#include "context.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "stb_ds.h"

/* Copies the directory part of `path` into `out` (no trailing slash).
 * If `path` has no directory component, writes ".".
 * Returns false if `out_size` is too small. */
static bool dirname(const char *path, char *out, size_t out_size) {
  const char *slash = strrchr(path, '/');
#ifdef _WIN32
  const char *bslash = strrchr(path, '\\');
  if (!slash || (bslash && bslash > slash)) {
    slash = bslash;
  }
#endif
  if (!slash) {
    if (out_size < 2) return false;
    out[0] = '.';
    out[1] = '\0';
    return true;
  }

  size_t len = (size_t)(slash - path);
  if (len == 0) len = 1;              /* "/graph.json" -> "/" not "" */
  if (len + 1 > out_size) return false;

  memcpy(out, path, len);
  out[len] = '\0';
  return true;
}

HYA_Result ContextInit(Context *ctx, size_t arena_size, const char* filename) {
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
  if (!dirname(filename, ctx->path, CONTEXT_PATH_SIZE)) {
    printf("ERROR: path too long\n");
    ContextDeinit(ctx);
    return HYA_ERR_FAILURE;
  }
  return HYA_OK;
}

HYA_Result ContextCreate(Context **ctx, size_t arena_size, const char* filename) {
  *ctx = (Context *)malloc(sizeof(Context));
  if (!*ctx) {
    return HYA_ERR_OUT_OF_MEMORY;
  }
  return ContextInit(*ctx, arena_size, filename);
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
