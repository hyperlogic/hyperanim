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

void PrintUsage(const char *prog) {
  fprintf(stderr, "usage: %s -i <input.json> -o <output.hya>\n", prog);
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

int main(int argc, char **argv) {
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
    return 2;
  }

  size_t buf_size = 0;
  char *buf = ReadFile(input, &buf_size);
  if (!buf) {
    printf("ERROR: loading %s\n", input);
    return 1;
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
    return 1;
  }

  Context ctx;
  const size_t ARENA_SIZE = (size_t)10 * 1024 * 1024;
  HYA_Result res = ContextInit(&ctx, ARENA_SIZE, input);
  if (res != HYA_OK) {
    free(root);
    printf("ERROR: ContextInit failed: %d\n", res);
    return res;
  }

#define X(Name, name, NAME) shput(ctx.type_map, #name, HYA_NODE_TYPE_##NAME);
  HYA_NODE_NAME_LIST
#undef X

  HYA_Graph *graph = NULL;
  assert(ctx.arena->offset == 0);  // graph must be the first allocation
  graph = (HYA_Graph *)ArenaAllocFrom(ctx.arena, sizeof(HYA_Graph));

  // NOTE: the strings in the ctx stb_ds maps
  // point directly to data from the json root json_value_s
  // so the ctx shouldn't outlive the root.
  res = InitGraph(graph, &ctx, root);
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
  PrintGraph(graph);

  ContextDeinit(&ctx);
  free(root);

  return 0;
}
