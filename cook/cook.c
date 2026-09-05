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

#define STB_DS_IMPLEMENTATION
#include "stb_ds.h"

static void PrintUsage(const char *prog) {
  fprintf(stderr, "usage: %s -i <input.json> -o <output.hya>\n", prog);
}

static char *ReadFile(const char *filename, size_t *out_size) {
  FILE *fp = fopen(filename, "rb");
  if (!fp) {
    return NULL;
  }
  if (fseek(fp, 0, SEEK_END) != 0) {
    fclose(fp);
    return NULL;
  }
  long size = ftell(fp);
  if (size < 0) {
    fclose(fp);
    return NULL;
  }

  if (fseek(fp, 0, SEEK_SET) != 0) {
    fclose(fp);
    return NULL;
  }

  char *buf = malloc(size + 1);  // +1 for optional NUL terminator
  if (!buf) {
    fclose(fp);
    return NULL;
  }

  size_t nread = fread(buf, 1, size, fp);
  if (nread != (size_t)size) {  // short read = error
    free(buf);
    fclose(fp);
    return NULL;
  }

  buf[size] = '\0';  // makes it safe to treat as a C string
  fclose(fp);

  if (out_size) {
    *out_size = nread;
  }
  return buf;
}

/*
HYAC
num_offsets  size_t
offset 0
offset 1
...
offset n-1
HYA_Graph
*/

static HYA_Result CookGraph(Context *ctx, HYA_Graph *graph,
                            const char *filename) {
  const char *categories[HYA_MEM_COUNT] = {"Node", "String", "Var", "Skeleton",
                                           "Motion"};

  // update all the ptrs to be relative to the graph base addr.
  for (ptrdiff_t i = 0; i < arrlen(ctx->reloc_arr); i++) {
    const RelocInfo *r = ctx->reloc_arr + i;
    printf("reloc[%td] %s: addr = %p, ptr = %p, offset = %td, size = %zu\n", i,
           categories[r->cat], r->addr, r->ptr, r->offset, r->size);
  }
  return HYA_ERR_FAILURE;
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
        return 2;
    }
  }

  if (!input || !output) {
    fprintf(stderr, "%s: both -i and -o are required\n", argv[0]);
    PrintUsage(argv[0]);
    res = HYA_ERR_BAD_ARGS;
    goto cleanup_0;
  }

  size_t buf_size = 0;
  char *buf = ReadFile(input, &buf_size);
  if (!buf) {
    printf("ERROR: loading %s\n", input);
    res = HYA_ERR_FILE;
    goto cleanup_0;
  }

  struct json_parse_result_s parse_result;
  struct json_value_s *root = json_parse_ex(
      (const void *)buf, buf_size, json_parse_flags_allow_location_information,
      NULL, NULL, &parse_result);
  free(buf);
  if (!root) {
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

  res = InitGraph(graph, &ctx, root);
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

  res = CookGraph(&ctx, graph, output);
  if (res != HYA_OK) {
    printf("ERROR: CookGraph failure: %d\n", res);
    goto cleanup_2;
  }

cleanup_2:
  ContextDeinit(&ctx);
cleanup_1:
  free(root);
cleanup_0:

  return res;
}
