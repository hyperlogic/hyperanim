#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arena.h"
#include "hyperanim.h"
#include "json.h"
#include "loadjson.h"

#define STB_DS_IMPLEMENTATION
#include "stb_ds.h"

void PrintUsage() {
  printf("play\n");
  printf("\n");
  printf("USAGE:\n");
  printf("  play graph.json\n");
}

char *ReadFile(const char *filename, size_t *out_size) {
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

int main(int argc, const char *argv[]) {
  if (argc != 2) {
    printf("ERROR: Expected file.json argument\n");
    PrintUsage();
    return 1;
  }

  size_t buf_size = 0;
  char *buf = ReadFile(argv[1], &buf_size);
  if (!buf) {
    printf("ERROR: loading %s\n", argv[1]);
    return 1;
  }

  struct json_parse_result_s parse_result;
  struct json_value_s *root =
      json_parse_ex((const void *)buf, buf_size, json_parse_flags_default, NULL,
                    NULL, &parse_result);
  free(buf);
  if (!root) {
    printf("ERROR: parsing %s\n", argv[1]);
    printf("JSON parse error %zu at line %zu, column %zu (byte offset %zu)\n",
           parse_result.error, parse_result.error_line_no,
           parse_result.error_row_no, parse_result.error_offset);
    return 1;
  }

  Context ctx;
  const size_t ARENA_SIZE = (size_t)10 * 1024 * 1024;
  HYA_Result res = ContextInit(&ctx, ARENA_SIZE, argv[1]);
  if (res != HYA_OK) {
    free(root);
    printf("ERROR: ContextInit failed: %d\n", res);
    return res;
  }

#define X(Type, name, NAME) shput(ctx.type_map, #name, HYA_NODE_TYPE_##NAME);
  HYA_NODE_TYPE_LIST
#undef X

  HYA_Graph *graph = NULL;
  assert(ctx.arena->offset == 0);  // graph must be the first allocation
  graph = (HYA_Graph *)ArenaAllocFrom(ctx.arena, sizeof(HYA_Graph));
  memset(graph, 0, sizeof(HYA_Graph));  // NOLINT

  struct json_object_s *object = (struct json_object_s *)root->payload;
  if (!object) {
    ContextDeinit(&ctx);
    free(root);
    printf("ERROR: expected root to be a json object\n");
    return 1;
  }

  // NOTE: the strings in the ctx stb_ds maps
  // point directly to data from the json root json_value_s
  // so the ctx shouldn't outlive the root.
  res = Init_HYA_Graph(graph, &ctx, object);
  if (res != HYA_OK) {
    printf("ERROR: BuildGraph failed: %d\n", res);
    if (res == HYA_ERR_OUT_OF_MEMORY) {
      printf("Try increasing ARENA_SIZE, currently %zu bytes\n", ARENA_SIZE);
    }
    ContextDeinit(&ctx);
    free(root);
    return res;
  }

  printf("graph using %zu bytes\n", ctx.arena->offset);
  Print_HYA_Graph(graph);

  ContextDeinit(&ctx);
  free(root);

  return 0;
}
