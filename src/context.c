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
    if (out_size < 2) {
      return false;
    }
    out[0] = '.';
    out[1] = '\0';
    return true;
  }
  size_t len = (size_t)(slash - path);
  if (len == 0) len = 1; /* "/graph.json" -> "/" not "" */
  if (len + 1 > out_size) {
    return false;
  }
  memcpy(out, path, len);
  out[len] = '\0';
  return true;
}

/* Copies the filename part of `path` into `out`.
 * If `path` ends in a separator, writes "".
 * Returns false if `out_size` is too small. */
static bool basename(const char *path, char *out, size_t out_size) {
  const char *slash = strrchr(path, '/');
#ifdef _WIN32
  const char *bslash = strrchr(path, '\\');
  if (!slash || (bslash && bslash > slash)) {
    slash = bslash;
  }
#endif
  const char *base = slash ? slash + 1 : path;
  size_t len = strlen(base);
  if (len + 1 > out_size) {
    return false;
  }
  memcpy(out, base, len + 1); /* includes '\0' */
  return true;
}

HYA_Result ContextInit(Context *ctx, size_t arena_size, const char *filename) {
  memset(ctx, 0, sizeof(Context));
  sh_new_strdup(ctx->node_map);  // map will own a copy of string keys.
  sh_new_strdup(ctx->type_map);
  sh_new_strdup(ctx->var_map);
  sh_new_strdup(ctx->str_map);

  HYA_Result res = ArenaCreate(&ctx->arena, arena_size);
  if (res != HYA_OK) {
    printf("ERROR ArenaCreate failure %d\n", res);
    return res;
  }
  if (!dirname(filename, ctx->dirname, CONTEXT_PATH_SIZE)) {
    printf("ERROR: path too long\n");
    ContextDeinit(ctx);
    return HYA_ERR_FAILURE;
  }
  if (!basename(filename, ctx->basename, CONTEXT_PATH_SIZE)) {
    printf("ERROR: path too long\n");
    ContextDeinit(ctx);
    return HYA_ERR_FAILURE;
  }
  ctx->reloc_arr = NULL;
  return HYA_OK;
}

HYA_Result ContextCreate(Context **ctx, size_t arena_size,
                         const char *filename) {
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
  arrfree(ctx->reloc_arr);
}

void ContextDestroy(Context *ctx) {
  ContextDeinit(ctx);
  free(ctx);
}

HYA_STR_ID ContextInternString(Context *ctx, const char *str) {
  ptrdiff_t i = shgeti(ctx->str_map, str);
  if (i < 0) {
    shputs(ctx->str_map, (Symbol){str});
    return shlen(ctx->str_map) - 1;
  }
  return i;
}

// Specific alignment: for minimal padding
uint8_t *ContextAllocFromAligned(Context *ctx, HYA_MemCategory cat, void *addr,
                                 size_t size, size_t align) {
  uint8_t *res = ArenaAllocFromAligned(ctx->arena, size, align);
  if (res) {
    // save this allocation for later relocation during cooking.
    ptrdiff_t pp = addr ? (uint8_t *)addr - ctx->arena->base : 0;
    RelocInfo reloc = {pp, res - ctx->arena->base, size, cat};
    arrpush(ctx->reloc_arr, reloc);
  }
  return res;
}

// Default alignment: safe for any built-in type.
uint8_t *ContextAllocFrom(Context *ctx, HYA_MemCategory cat, void *addr,
                          size_t size) {
  uint8_t *res = ArenaAllocFrom(ctx->arena, size);
  if (res) {
    // save this allocation for later relocation during cooking.
    ptrdiff_t pp = addr ? (uint8_t *)addr - ctx->arena->base : 0;
    RelocInfo reloc = {pp, res - ctx->arena->base, size, cat};
    arrpush(ctx->reloc_arr, reloc);
  }
  return res;
}
