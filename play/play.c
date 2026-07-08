#include <assert.h>
#include <errno.h>
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
#include "gltf.h"

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

HYA_Result BuildNodes(struct json_array_s *array, HYA_Graph *graph,
                      Context *ctx) {
  HYA_Result res;
  // first pass: determine node counts
  struct json_array_element_s *e = array->start;
  while (e != NULL) {
    struct json_object_s *object = json_value_as_object(e->value);
    if (!object) {
      printf("ERROR: BuildNodes node is not an object\n");
      return HYA_ERR_FAILURE;
    }
    struct json_object_element_s *elem = object->start;
    bool found_name = false;
    bool found_type = false;
    while (elem != NULL) {
      if (0 == strcmp(elem->name->string, "type")) {
        struct json_string_s *s = json_value_as_string(elem->value);
        if (!s) {
          printf("ERROR: BuildNodes type value is not a string\n");
          return HYA_ERR_FAILURE;
        }

#define X(Type, name, NAME)                 \
  else if (0 == strcmp(s->string, #name)) { \
    graph->num_##name##_nodes++;            \
  }
        if (0) {
        }
        HYA_NODE_TYPE_LIST
#undef X
        else {
          printf("ERROR: unknown node type %s\n", s->string);
          return HYA_ERR_FAILURE;
        }
        found_type = true;
      }
      if (0 == strcmp(elem->name->string, "name")) {
        struct json_string_s *s = json_value_as_string(elem->value);
        if (!s) {
          printf("ERROR: BuildNodes name value is not a string\n");
          return HYA_ERR_FAILURE;
        }
        int id = shget(ctx->node_map, s->string);
        if (id >= 0) {
          printf("ERROR: BuildNodes duplicate node name %s\n", s->string);
          return HYA_ERR_FAILURE;
        }
        shput(ctx->node_map, s->string, ctx->next_node_id++);
        found_name = true;
      }
      elem = elem->next;
    }
    if (!found_name) {
      printf("ERROR: missing name\n");
      return HYA_ERR_FAILURE;
    }
    if (!found_type) {
      printf("ERROR: missing type\n");
      return HYA_ERR_FAILURE;
    }
    e = e->next;
  }

  // now allocate nodes arrays, and set counts back to zero,
  // for second pass
#define X(Type, name, NAME)                                             \
  if (graph->num_##name##_nodes > 0) {                                  \
    graph->name##_nodes = (Type *)ArenaAllocFrom(                       \
        ctx->arena, sizeof(Type) * graph->num_##name##_nodes);          \
    if (!graph->name##_nodes) {                                         \
      printf("ERROR: BuildNodes %s allocate failed\n", #name "_nodes"); \
      return HYA_ERR_OUT_OF_MEMORY;                                     \
    }                                                                   \
    graph->num_##name##_nodes = 0;                                      \
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
        if (!s) {
          printf("ERROR: BuildNodes type value is not a string\n");
          return HYA_ERR_FAILURE;
        }

#define X(Type, name, NAME)                                                    \
  if (0 == strcmp(s->string, #name)) {                                         \
    res = Init_##Type(object,                                                  \
                      &graph->name##_nodes[graph->num_##name##_nodes++], ctx); \
    if (res != HYA_OK) {                                                       \
      printf("ERROR: BuildNodes Init_##Type failed: %d\n", res);               \
      return res;                                                              \
    }                                                                          \
  }
        HYA_NODE_TYPE_LIST
#undef X
      }
      elem = elem->next;
    }
    e = e->next;
  }
  return HYA_OK;
}

HYA_Result BuildGraph(struct json_value_s *root, HYA_Graph **graph,
                      Context *ctx) {
  HYA_Result res;
  assert(ctx->arena->offset == 0);  // graph must be the first allocation
  HYA_Graph *g = (HYA_Graph *)ArenaAllocFrom(ctx->arena, sizeof(HYA_Graph));
  memset(g, 0, sizeof(HYA_Graph));  // NOLINT

  struct json_object_s *object = (struct json_object_s *)root->payload;
  struct json_object_element_s *elem = object->start;
  const char *root_name = NULL;
  while (elem != NULL) {
    if (0 == strcmp(elem->name->string, "version")) {
      struct json_number_s *n = json_value_as_number(elem->value);
      if (!n) {
        printf("ERROR: BuildGraph version value is not a number\n");
        return HYA_ERR_FAILURE;
      }
      long v = JSON_NumberToLong(n);
      g->version = v;
    } else if (0 == strcmp(elem->name->string, "root")) {
      struct json_string_s *s = json_value_as_string(elem->value);
      if (!s) {
        printf("ERROR: BuildGraph root value is not a string\n");
        return HYA_ERR_FAILURE;
      }
      root_name = s->string;
    } else if (0 == strcmp(elem->name->string, "nodes")) {
      struct json_array_s *array = json_value_as_array(elem->value);
      if (!array) {
        printf("ERROR: BuildGraph nodes value is not an array\n");
        return HYA_ERR_FAILURE;
      }
      res = BuildNodes(array, g, ctx);
      if (res != HYA_OK) {
        printf("ERROR: BuildGraph: BuildNodes failed: %d\n", res);
        return res;
      }
    } else if (0 == strcmp(elem->name->string, "tpose")) {
      struct json_string_s *s = json_value_as_string(elem->value);
      if (!s) {
        printf("ERROR: BuildGraph root value is not a string\n");
        return HYA_ERR_FAILURE;
      }
      // load the tpose
      res = InitSkeletonFromGLTF(s->string, &g->tpose, ctx);
      if (res != HYA_OK) {
        printf("ERROR: BuildGraph InitSkeletonFromGLTF failed: %d\n", res);
        return res;
      }
    } else {
      printf("WARNING: BuildGraph: Unknown key %s, skipping\n",
             elem->name->string);
    }
    elem = elem->next;
  }
  if (!root_name) {
    printf("ERROR: BuildGraph: could not no root_node\n");
    return HYA_ERR_FAILURE;
  }
  g->root = shget(ctx->node_map, root_name);
  if (g->root < 0) {
    printf("ERROR: BuildGraph: could not find root node %s\n", root_name);
    return HYA_ERR_FAILURE;
  }

  // create ptrs to each node with node id as in index.
  g->num_node_ptrs = (size_t)ctx->next_node_id;
  g->node_ptrs = (HYA_Node **)ArenaAllocFrom(
      ctx->arena, sizeof(HYA_Node *) * g->num_node_ptrs);
  if (!g->node_ptrs) {
    printf("ERROR: BuildGraph node_ptrs alloc failed\n");
    return HYA_ERR_OUT_OF_MEMORY;
  }

#define X(Type, name, NAME)                            \
  for (size_t i = 0; i < g->num_##name##_nodes; i++) { \
    Type *node = &g->name##_nodes[i]; /* NOLINT */     \
    assert(node && node->node.id >= 0);                \
    g->node_ptrs[node->node.id] = (HYA_Node *)node;    \
  }
  HYA_NODE_TYPE_LIST
#undef X

  // create ptrs to each string in the str_map
  g->num_str_ptrs = (size_t)ctx->next_str_id;
  char **str_ptrs =
      (char **)ArenaAllocFrom(ctx->arena, sizeof(char *) * g->num_str_ptrs);
  if (!str_ptrs) {
    printf("ERROR: BuildGraph str_ptrs alloc failed\n");
    return HYA_ERR_OUT_OF_MEMORY;
  }
  ptrdiff_t n = shlen(ctx->str_map);  // number of pairs
  for (ptrdiff_t i = 0; i < n; i++) {
    char *key = ctx->str_map[i].key;
    size_t value = ctx->str_map[i].value;

    // copy key from map into ctx->arena
    size_t len = strlen(key);  // NOLINT
    char *arena_str = (char *)ArenaAllocFrom(ctx->arena, len + 1);
    if (!arena_str) {
      printf("failure allocating string of %zu bytes\n", len + 1);
      return HYA_ERR_OUT_OF_MEMORY;
    }
    strcpy(arena_str, key);  // NOLINT
    arena_str[len] = 0;

    str_ptrs[value] = arena_str;

    // printf("str_ptrs[%zu] = %s\n", value, arena_str);
  }
  g->str_ptrs = (const char **)str_ptrs;

  // create vars
  g->num_vars = (size_t)ctx->next_var_id;
  g->vars =
      (HYA_Var *)ArenaAllocFrom(ctx->arena, sizeof(HYA_Var) * g->num_vars);
  if (!g->vars) {
    printf("failure allocating %zu HYA_Var*\n", g->num_vars);
    return HYA_ERR_OUT_OF_MEMORY;
  }
  memset(g->vars, 0, sizeof(HYA_Var) * g->num_vars);  // NOLINT
  n = shlen(ctx->var_map);                            // number of pairs
  for (ptrdiff_t i = 0; i < n; i++) {
    char *key = ctx->var_map[i].key;
    size_t value = ctx->var_map[i].value;

    // find name from string table
    int id = shget(ctx->str_map, key);
    g->vars[value].name = id;
  }

  *graph = g;

  return HYA_OK;
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

  if (root->type != json_type_object) {  // NOLINT
    free(root);
    printf("ERROR: expected root to a json object\n");
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
  // NOTE: the strings in the ctx stb_ds maps
  // point directly to data from the json root json_value_s
  // so the ctx shouldn't outlive the root.
  res = BuildGraph(root, &graph, &ctx);
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
