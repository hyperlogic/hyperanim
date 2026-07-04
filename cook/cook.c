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

size_t g_next_node_id = 1;

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

long JSON_NumberToLong(struct json_number_s *n) {
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

double JSON_NumberToDouble(struct json_number_s *n) {
  char *end;
  errno = 0;
  double d = strtod(n->number, &end);
  if (end == n->number || *end != '\0') {
    // didn't consume the whole thing — malformed (shouldn't happen
    // on library output, but worth guarding if you ever feed it elsewhere)
    return 0.0f;
  }
  return d;
}

void BuildNodes(struct json_array_s *array, HYA_Graph *graph,
                StrToIdPair **node_map, StrToIdPair *type_map, Arena *arena) {
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
        size_t id = shget(*node_map, s->string);
        if (id != 0) {
          printf("ERROR: duplicate node name %s\n", s->string);
          exit(9);
        }
        shput(*node_map, s->string, g_next_node_id++);
      }
      elem = elem->next;
    }
    e = e->next;
  }

  // now allocate nodes arrays, and set counts back to zero,
  // for second pass
#define X(Type, name, NAME)                               \
  if (graph->num_##name##_nodes > 0) {                    \
    graph->name##_nodes = (Type *)ArenaAllocFrom(         \
        arena, sizeof(Type) * graph->num_##name##_nodes); \
    graph->num_##name##_nodes = 0;                        \
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
                *node_map, type_map, arena);                               \
  }
        HYA_NODE_TYPE_LIST
#undef X
      }
      elem = elem->next;
    }
    e = e->next;
  }
}

bool BuildGraph(struct json_value_s *root, HYA_Graph **graph,
                StrToIdPair **node_map, StrToIdPair *type_map, Arena *arena) {
  HYA_Graph *g = (HYA_Graph *)ArenaAllocFrom(arena, sizeof(HYA_Graph));
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
      BuildNodes(array, g, node_map, type_map, arena);
    } else {
      printf("WARNING: Unknown key %s, skipping\n", elem->name->string);
    }
    elem = elem->next;
  }
  g->root = shget(*node_map, root_name);
  if (g->root == 0) {
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

  struct json_value_s *root = json_parse((const void *)buf, buf_size);
  if (!root) {
    printf("ERROR: parsing %s\n", argv[1]);
    return 3;
  }

  if (root->type != json_type_object) {  // NOLINT
    free(root);
    printf("ERROR: expected root to a json object\n");
    return 4;
  }

  Arena *arena;
  const size_t ARENA_SIZE = (size_t)10 * 1024 * 1024;
  if (!ArenaAlloc(&arena, ARENA_SIZE)) {
    free(root);
    printf("ERROR allocating arena\n");
    return 5;
  }

  StrToIdPair *type_map = NULL;
  sh_new_arena(type_map);
#define X(Type, name, NAME) shput(type_map, #name, HYA_NODE_TYPE_##NAME + 1);
  HYA_NODE_TYPE_LIST
#undef X

  StrToIdPair *node_map = NULL;
  sh_new_arena(node_map);

  HYA_Graph *graph;
  if (!BuildGraph(root, &graph, &node_map, type_map, arena)) {
    shfree(node_map);
    shfree(type_map);
    ArenaFree(arena);
    free(root);

    printf("ERROR: BuildGraph failed\n");
    return 6;
  }

  printf("graph using %zu bytes\n", arena->offset);
  Print_HYA_Graph(graph);

  shfree(node_map);
  shfree(type_map);
  ArenaFree(arena);
  free(root);
}
