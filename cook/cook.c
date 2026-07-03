#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "json.h"
#include "hyperanim.h"

typedef struct Arena {
  unsigned char *base;  // start of the backing buffer
  size_t offset;        // bytes handed out so far
  size_t capacity;
} Arena;

bool ArenaAlloc(Arena **arena, size_t capacity) {
  assert(arena);
  Arena *a = (Arena *)malloc(sizeof(Arena));
  a->base = malloc(capacity);
  if (!a->base) {
    free(a);
    return false;
  }
  a->offset = 0;
  a->capacity = capacity;
  *arena = a;
  return true;
}

// Round n up to a power-of-two alignment.
static inline uintptr_t AlignUp(uintptr_t n, size_t align) {
  assert((align & (align - 1)) == 0 && "alignment must be a power of two");
  return (n + (align - 1)) & ~(uintptr_t)(align - 1);
}

void *ArenaAllocFromAligned(Arena *a, size_t size, size_t align) {
  uintptr_t curr = (uintptr_t)a->base + a->offset;
  uintptr_t aligned = AlignUp(curr, align);
  size_t padding = aligned - curr;

  // Overflow-safe capacity check.
  if ((padding > a->capacity - a->offset) ||
      (size > a->capacity - a->offset - padding)) {
    return NULL;  // out of space
  }

  a->offset += padding + size;
  return (void *)aligned;  // NOLINT
}

// Default alignment: safe for any built-in type.
void *ArenaAllocFrom(Arena *a, size_t size) {
  return ArenaAllocFromAligned(a, size, _Alignof(max_align_t));
}

void ArenaFree(Arena *a) {
  free(a->base);
  free(a);
}

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

void Init_HYA_StateMachineNode(struct json_object_s *object,
                               HYA_StateMachineNode *node, Arena *arena) {
  return;
}

void Init_HYA_MotionNode(struct json_object_s *object, HYA_MotionNode *node,
                         Arena *arena) {
  return;
}

void Init_HYA_BlendNode(struct json_object_s *object, HYA_BlendNode *node,
                        Arena *arena) {
  return;
}

void BuildNodes(struct json_array_s *array, HYA_Graph *graph, Arena *arena) {
  printf("%zu nodes\n", array->length);

  // first pass: determine node counts
  struct json_array_element_s *e = array->start;
  while (e != NULL) {
    struct json_object_s *object = json_value_as_object(e->value);
    struct json_object_element_s *elem = object->start;
    while (elem != NULL) {
      if (0 == strcmp(elem->name->string, "type")) {
        struct json_string_s *s = json_value_as_string(elem->value);
        printf("%s: %s\n", elem->name->string, s->string);

#define X(Type, name)                       \
  else if (0 == strcmp(s->string, #name)) { \
    graph->num_##name##_nodes++;            \
  }
        if (0) {
        }
        HYA_NODE_TYPE_LIST
#undef X
      }
      elem = elem->next;
    }
    e = e->next;
  }

  // now allocate nodes arrays, and set counts back to zero,
  // for second pass
#define X(Type, name)                                     \
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
        printf("%s: %s\n", elem->name->string, s->string);

#define X(Type, name)                                                      \
  if (0 == strcmp(s->string, #name)) {                                     \
    Init_##Type(object, &graph->name##_nodes[graph->num_##name##_nodes++], \
                arena);                                                    \
  }
        HYA_NODE_TYPE_LIST
#undef X
      }
      elem = elem->next;
    }
    e = e->next;
  }
}

bool BuildGraph(struct json_value_s *root, HYA_Graph **graph, Arena *arena) {
  HYA_Graph *g = (HYA_Graph *)ArenaAllocFrom(arena, sizeof(HYA_Graph));
  memset(g, 0, sizeof(HYA_Graph));  // NOLINT

  struct json_object_s *object = (struct json_object_s *)root->payload;
  struct json_object_element_s *elem = object->start;
  const char *root_name = NULL;
  while (elem != NULL) {
    if (0 == strcmp(elem->name->string, "version")) {
      long v = JSON_NumberToLong(json_value_as_number(elem->value));
      printf("%s: %ld\n", elem->name->string, v);
      g->version = (int)v;
    } else if (0 == strcmp(elem->name->string, "root")) {
      struct json_string_s *s = json_value_as_string(elem->value);
      root_name = s->string;
      printf("%s: %s\n", elem->name->string, root_name);
      /// TODO: intern this string
    } else if (0 == strcmp(elem->name->string, "nodes")) {
      struct json_array_s *array = json_value_as_array(elem->value);
      BuildNodes(array, g, arena);
    } else {
      printf("WARNING: Unknown key %s, skipping\n", elem->name->string);
    }
    elem = elem->next;
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

  HYA_Graph *graph;
  if (!BuildGraph(root, &graph, arena)) {
    printf("ERROR: traversal failed\n");
  }

  ArenaFree(arena);
  free(root);
}
