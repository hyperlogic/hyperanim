#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "arena.h"
#include "hyperanim.h"
#include "json.h"
#include "node_handlers.h"

#define STB_DS_IMPLEMENTATION
#include "stb_ds.h"

void PrintUsage() {
  printf("cook\n");
  printf("\n");
  printf("USAGE:\n");
  printf("  cook file.json\n");
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

static long JSON_NumberToLong(struct json_number_s *n) {
  char *end;
  errno = 0;
  long v = strtol(n->number, &end, 10);
  if (end == n->number || *end != '\0') {
    // no digits consumed / trailing junk
    return 0;
  }
  if (errno == ERANGE) {
    // overflowed long
    return 0;
  }
  return v;
}

void BuildNodes(struct json_array_s *array, HYA_Graph *graph, Context *ctx) {
  // first pass: determine node counts
  struct json_array_element_s *e = array->start;
  while (e != NULL) {
    struct json_object_s *object = json_value_as_object(e->value);
    struct json_object_element_s *elem = object->start;
    while (elem != NULL) {
      if (0 == strcmp(elem->name->string, "type")) {
        struct json_string_s *s = json_value_as_string(elem->value);
        assert(s);

#define X(Type, name, NAME)                 \
  else if (0 == strcmp(s->string, #name)) { \
    graph->num_##name##_nodes++;            \
  }
        if (0) {
        }
        HYA_NODE_TYPE_LIST
#undef X
      }
      if (0 == strcmp(elem->name->string, "name")) {
        struct json_string_s *s = json_value_as_string(elem->value);
        assert(s);
        int id = shget(ctx->node_map, s->string);
        if (id >= 0) {
          printf("ERROR: duplicate node name %s\n", s->string);
          exit(9);
        }
        shput(ctx->node_map, s->string, ctx->next_node_id++);
      }
      elem = elem->next;
    }
    e = e->next;
  }

  // now allocate nodes arrays, and set counts back to zero,
  // for second pass
#define X(Type, name, NAME)                                    \
  if (graph->num_##name##_nodes > 0) {                         \
    graph->name##_nodes = (Type *)ArenaAllocFrom(              \
        ctx->arena, sizeof(Type) * graph->num_##name##_nodes); \
    graph->num_##name##_nodes = 0;                             \
  }
  HYA_NODE_TYPE_LIST
#undef X

  // second pass: init each node
  e = array->start;
  while (e != NULL) {
    struct json_object_s *object = json_value_as_object(e->value);
    struct json_object_element_s *elem = object->start;
    while (elem != NULL) {
      if (0 == strcmp(elem->name->string, "type")) {
        struct json_string_s *s = json_value_as_string(elem->value);
        assert(s);

#define X(Type, name, NAME)                                                \
  if (0 == strcmp(s->string, #name)) {                                     \
    Init_##Type(object, &graph->name##_nodes[graph->num_##name##_nodes++], \
                ctx);                                                      \
  }
        HYA_NODE_TYPE_LIST
#undef X
      }
      elem = elem->next;
    }
    e = e->next;
  }
}

HYA_Result BuildGraph(struct json_value_s *root, HYA_Graph **graph,
                      Context *ctx) {
  HYA_Graph *g = (HYA_Graph *)ArenaAllocFrom(ctx->arena, sizeof(HYA_Graph));
  memset(g, 0, sizeof(HYA_Graph));  // NOLINT

  struct json_object_s *object = (struct json_object_s *)root->payload;
  struct json_object_element_s *elem = object->start;
  const char *root_name = NULL;
  while (elem != NULL) {
    if (0 == strcmp(elem->name->string, "version")) {
      long v = JSON_NumberToLong(json_value_as_number(elem->value));
      g->version = v;
    } else if (0 == strcmp(elem->name->string, "root")) {
      struct json_string_s *s = json_value_as_string(elem->value);
      assert(s);
      root_name = s->string;
    } else if (0 == strcmp(elem->name->string, "nodes")) {
      struct json_array_s *array = json_value_as_array(elem->value);
      assert(array);
      BuildNodes(array, g, ctx);
    } else {
      printf("WARNING: Unknown key %s, skipping\n", elem->name->string);
    }
    elem = elem->next;
  }
  if (!root_name) {
    printf("ERROR: could not no root_node\n");
    return false;
  }
  g->root = shget(ctx->node_map, root_name);
  if (g->root < 0) {
    printf("ERROR: could not find root node %s\n", root_name);
    return false;
  }

  *graph = g;

  return true;
}

int main(int argc, const char *argv[]) {
  if (argc != 2) {
    printf("ERROR: Expected file.json argument\n");
    PrintUsage();
    return 1;
  }

  size_t buf_size = 0;
  const char *buf = ReadFile(argv[1], &buf_size);
  if (!buf) {
    printf("ERROR: loading %s\n", argv[1]);
    return 2;
  }

  struct json_parse_result_s parse_result;
  struct json_value_s *root =
      json_parse_ex((const void *)buf, buf_size, json_parse_flags_default, NULL,
                    NULL, &parse_result);
  if (!root) {
    printf("ERROR: parsing %s\n", argv[1]);
    printf("JSON parse error %zu at line %zu, column %zu (byte offset %zu)\n",
           parse_result.error, parse_result.error_line_no,
           parse_result.error_row_no, parse_result.error_offset);
    return 3;
  }

  if (root->type != json_type_object) {  // NOLINT
    free(root);
    printf("ERROR: expected root to a json object\n");
    return 4;
  }

  Context ctx;
  const size_t ARENA_SIZE = (size_t)10 * 1024 * 1024;
  HYA_Result res = ContextInit(&ctx, ARENA_SIZE);
  if (res != HYA_OK) {
    free(root);
    printf("ERROR: ContextInit failed: %d\n", res);
    return 5;
  }

#define X(Type, name, NAME) shput(ctx.type_map, #name, HYA_NODE_TYPE_##NAME);
  HYA_NODE_TYPE_LIST
#undef X

  HYA_Graph *graph = NULL;
  if (!BuildGraph(root, &graph, &ctx)) {
    ContextDeinit(&ctx);
    free(root);
    printf("ERROR: BuildGraph failed\n");
    return 6;
  }

  printf("graph using %zu bytes\n", ctx.arena->offset);
  Print_HYA_Graph(graph);

  ContextDeinit(&ctx);
  free(root);
}
