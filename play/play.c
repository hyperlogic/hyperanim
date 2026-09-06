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
  fprintf(stderr, "usage: %s -i <input.hya>\n", prog);
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

static HYA_Result HYA_GraphCreate(HYA_Graph **graph, const char *filename) {
  HYA_Result res = HYA_ERR_FAILURE;
  size_t buf_size = 0;
  uint8_t *buf = ReadFile(filename, &buf_size);
  if (!buf) {
    printf("ERROR: loading %s\n", filename);
    res = HYA_ERR_FILE;
    goto cleanup_0;
  }

  if (buf[0] != 'H' || buf[1] != 'Y' || buf[2] != 'A' || buf[3] != 'G' ||
      buf[4] != 'R' || buf[5] != 'A' || buf[6] != 'P' || buf[7] != 'H') {
    res = HYA_ERR_BAD_MAGIC;
    goto cleanup_1;
  }

  uint8_t *p = buf + 8;
  size_t num_offsets = *(size_t *)p;
  uint8_t *base = p + sizeof(size_t) * (num_offsets + 2);
  p += sizeof(size_t);
  for (size_t i = 0; i < num_offsets; i++) {
    size_t offset = *(size_t *)p;
    printf("AJT: offset[%zu] = %zu\n", i, offset);
    p += sizeof(size_t);
    unsigned long *ptr = (unsigned long *)(base + offset);
    *ptr = (unsigned long)(base + *ptr);
  }

  *graph = (HYA_Graph *)base;

  // on success: don't free buf
  return HYA_OK;

cleanup_1:
  free(buf);
cleanup_0:

  return res;
}

void HYA_GraphFree(HYA_Graph *graph) {
  size_t *p = (size_t *)graph;
  size_t num_offsets = *(p - 1);
  uint8_t *buf = (uint8_t *)(p - (num_offsets + 3));
  assert(buf[0] == 'H' && buf[1] == 'Y' && buf[2] == 'A' && buf[3] == 'G');
  free(buf);
}

int main(int argc, char **argv) {
  HYA_Result res = HYA_ERR_FAILURE;
  const char *input = NULL;
  int c;
  while ((c = getopt(argc, argv, "i:")) != -1) {
    switch (c) {
      case 'i':
        input = optarg;
        break;
      default:
        PrintUsage(argv[0]);
        return HYA_ERR_BAD_ARGS;
    }
  }

  if (!input) {
    fprintf(stderr, "%s: -i is required\n", argv[0]);
    PrintUsage(argv[0]);
    res = HYA_ERR_BAD_ARGS;
    goto cleanup_0;
  }

  HYA_Graph *graph;
  res = HYA_GraphCreate(&graph, input);
  if (res != HYA_OK) {
    fprintf(stderr, "ERROR: failed to load graph %s, result = %d\n", input,
            res);
    goto cleanup_0;
  }

  PrintGraph(graph);
  HYA_GraphFree(graph);

  res = HYA_OK;

cleanup_0:

  return res;
}
