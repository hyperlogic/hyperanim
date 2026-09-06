#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "arena.h"
#include "hyperanim.h"
#include "json.h"
#include "loadjson.h"
#include "util.h"

#define STB_DS_IMPLEMENTATION
#include "stb_ds.h"

static void PrintUsage(const char *prog) {
  fprintf(stderr, "usage: %s -i <input.json> -o <output.hya>\n", prog);
}

/*
HYAGRAPH
num_offsets  size_t
offset 0
offset 1
...
offset n-1
num_offsets  size_t
HYA_Graph
*/

static HYA_Result CookGraph(Context *ctx, HYA_Graph *graph,
                            const char *filename) {
  FILE *fp = fopen(filename, "wb");
  fwrite("HYAGRAPH", 8, 1, fp);

  // skip the first offset, because it's of the graph itself.
  size_t num_offsets = arrlen(ctx->reloc_arr) - 1;
  fwrite(&num_offsets, sizeof(size_t), 1, fp);

  const char *categories[HYA_MEM_COUNT] = {"Node", "String", "Var", "Skeleton",
                                           "Motion"};
  // munge all the ptrs to be relative to the graph base addr.
  for (ptrdiff_t i = 1; i < arrlen(ctx->reloc_arr); i++) {
    const RelocInfo *r = ctx->reloc_arr + i;
    printf("reloc[%td] %s: pp = %td, p = %td, size = %zu\n", i,
           categories[r->cat], r->pp, r->p, r->size);
    size_t offset = (size_t)r->pp;
    fwrite(&offset, sizeof(size_t), 1, fp);
    *(uintptr_t *)(ctx->arena->base + r->pp) = (uintptr_t)r->p;
  }
  fwrite(&num_offsets, sizeof(size_t), 1, fp);
  fwrite(graph, ctx->arena->offset, 1, fp);
  fclose(fp);

  // un munge the ptrs
  for (ptrdiff_t i = 1; i < arrlen(ctx->reloc_arr); i++) {
    const RelocInfo *r = ctx->reloc_arr + i;
    *(uint8_t **)(ctx->arena->base + r->pp) = ctx->arena->base + r->p;
  }

  return HYA_OK;
}

int main(int argc, char **argv) {
  HYA_Result res = HYA_OK;
  const char *input = NULL;
  const char *output = NULL;
  int c;
  while ((c = getopt(argc, argv, "i:o:")) != -1) {
    switch (c) {
      case 'i':
        input = optarg;
        break;
      case 'o':
        output = optarg;
        break;
      default:
        PrintUsage(argv[0]);
        return HYA_ERR_BAD_ARGS;
    }
  }

  if (!input || !output) {
    fprintf(stderr, "%s: both -i and -o are required\n", argv[0]);
    PrintUsage(argv[0]);
    res = HYA_ERR_BAD_ARGS;
    goto cleanup_0;
  }

  size_t buf_size = 0;
  uint8_t *buf = ReadFile(input, &buf_size);
  if (!buf) {
    printf("ERROR: loading %s\n", input);
    res = HYA_ERR_FILE;
    goto cleanup_0;
  }

  struct json_parse_result_s parse_result;
  struct json_value_s *json_root = json_parse_ex(
      (const void *)buf, buf_size, json_parse_flags_allow_location_information,
      NULL, NULL, &parse_result);
  free(buf);
  if (!json_root) {
    printf("ERROR: parsing %s\n", input);
    printf("JSON parse error %zu at line %zu, column %zu (byte offset %zu)\n",
           parse_result.error, parse_result.error_line_no,
           parse_result.error_row_no, parse_result.error_offset);
    res = HYA_ERR_JSON_PARSE;
    goto cleanup_0;
  }

  Context ctx;
  const size_t ARENA_SIZE = (size_t)10 * 1024 * 1024;
  res = ContextInit(&ctx, ARENA_SIZE, input);
  if (res != HYA_OK) {
    printf("ERROR: ContextInit failed: %d\n", res);
    goto cleanup_1;
  }

#define X(Name, name, NAME)                            \
  assert(shlen(ctx.type_map) == HYA_NODE_TYPE_##NAME); \
  shputs(ctx.type_map, (Symbol) { #name });

  HYA_NODE_NAME_LIST
#undef X

  HYA_Graph *graph = NULL;
  assert(ctx.arena->offset == 0);  // graph must be the first allocation
  graph = (HYA_Graph *)ContextAllocFrom(&ctx, HYA_MEM_NODE, NULL,
                                        sizeof(HYA_Graph));
  if (!graph) {
    printf("ERROR: Could not allocate graph of size %zu\n", sizeof(HYA_Graph));
    res = HYA_ERR_OUT_OF_MEMORY;
    goto cleanup_2;
  }

  res = InitGraph(graph, &ctx, json_root);
  if (res != HYA_OK) {
    printf("ERROR: BuildGraph failed: %d\n", res);
    if (res == HYA_ERR_OUT_OF_MEMORY) {
      printf("Try increasing ARENA_SIZE, currently %zu bytes\n", ARENA_SIZE);
    }
    goto cleanup_2;
  }

  printf("graph using %zu bytes\n", ctx.arena->offset);
  printf("graph mem by catagory:\n");
  size_t counts[HYA_MEM_COUNT] = {0};
  size_t total = 0;
  for (ptrdiff_t i = 0; i < arrlen(ctx.reloc_arr); i++) {
    size_t size = ctx.reloc_arr[i].size;
    total += size;
    counts[ctx.reloc_arr[i].cat] += size;
  }
  const char *categories[HYA_MEM_COUNT] = {"Node", "String", "Var", "Skeleton",
                                           "Motion"};
  for (int i = 0; i < HYA_MEM_COUNT; i++) {
    printf("    %s: %zu bytes\n", categories[i], counts[i]);
  }
  printf("    padding: %zu bytes\n", ctx.arena->offset - total);
  PrintGraph(graph);

  // NOTE: this invalidates the graph
  res = CookGraph(&ctx, graph, output);
  if (res != HYA_OK) {
    printf("ERROR: CookGraph failure: %d\n", res);
    goto cleanup_2;
  }

cleanup_2:
  ContextDeinit(&ctx);
cleanup_1:
  free(json_root);
cleanup_0:

  return res;
}
