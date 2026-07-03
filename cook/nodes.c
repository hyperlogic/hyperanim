#include "nodes.h"

#include <assert.h>
#include <stdio.h>

#include "stb_ds.h"

void Print_HYA_Node(const HYA_Node *node) {
  printf("      id = %zu\n", node->id);
  printf("      type = %zu\n", node->type);
  printf("      num_children = %zu\n", node->num_children);
  printf("      children = [");
  for (size_t i = 0; i < node->num_children; i++) {
    printf("%zu", node->children[i]);
    if (i != node->num_children - 1) {
      printf(", ");
    }
  }
  printf("]\n");
}

void Print_HYA_StateMachineNode(const HYA_StateMachineNode *node) {
  printf("    StateMachineNode {\n");
  Print_HYA_Node(&node->node);
  printf("    }\n");
}

void Print_HYA_MotionNode(const HYA_MotionNode *node) {
  printf("    MotionNode {\n");
  Print_HYA_Node(&node->node);
  printf("    }\n");
}

void Print_HYA_BlendNode(const HYA_BlendNode *node) {
  printf("    BlendNode {\n");
  Print_HYA_Node(&node->node);
  printf("    }\n");
}

void Print_HYA_Graph(const HYA_Graph *graph) {
  printf("{\n");
  printf("  version = %zu\n", graph->version);
  printf("  root = %zu\n", graph->root);
#define X(Type, name, NAME)                                \
  for (size_t i = 0; i < graph->num_##name##_nodes; i++) { \
    Print_##Type(&graph->name##_nodes[i]);                 \
  }
  HYA_NODE_TYPE_LIST
#undef X
}

void Init_HYA_Node(struct json_object_s *object, HYA_Node *node,
                   StrToIdPair *node_map, StrToIdPair *type_map, Arena *arena) {
  memset(node, 0, sizeof(HYA_Node));  // NOLINT
  struct json_object_element_s *elem = object->start;
  while (elem != NULL) {
    if (0 == strcmp(elem->name->string, "name")) {
      struct json_string_s *s = json_value_as_string(elem->value);
      assert(s);
      size_t id = shget(node_map, s->string);
      if (id == 0) {
        printf("ERROR: unknown node name %s\n", s->string);
        exit(10);
      }
      node->id = id;
    } else if (0 == strcmp(elem->name->string, "type")) {
      struct json_string_s *s = json_value_as_string(elem->value);
      assert(s);
      size_t id = shget(type_map, s->string);
      if (id == 0) {
        printf("ERROR: unknown node type %s\n", s->string);
        exit(11);
      }
      node->type = id;
    } else if (0 == strcmp(elem->name->string, "children")) {
      struct json_array_s *a = json_value_as_array(elem->value);
      assert(a);
      node->num_children = a->length;
      node->children =
          ArenaAllocFrom(arena, sizeof(HYA_NODE_ID) * node->num_children);
      struct json_array_element_s *e = a->start;
      int i = 0;
      while (e != NULL) {
        struct json_string_s *s = json_value_as_string(e->value);
        assert(s);
        size_t id = shget(node_map, s->string);
        if (id == 0) {
          printf("ERROR: unknown node name %s\n", s->string);
          exit(10);
        }
        node->children[i] = id;
        i++;
        e = e->next;
      }
    }
    elem = elem->next;
  }
}

void Init_HYA_StateMachineNode(struct json_object_s *object,
                               HYA_StateMachineNode *node,
                               StrToIdPair *node_map, StrToIdPair *type_map,
                               Arena *arena) {
  Init_HYA_Node(object, &node->node, node_map, type_map, arena);
  return;
}

void Init_HYA_MotionNode(struct json_object_s *object, HYA_MotionNode *node,
                         StrToIdPair *node_map, StrToIdPair *type_map,
                         Arena *arena) {
  Init_HYA_Node(object, &node->node, node_map, type_map, arena);
  return;
}

void Init_HYA_BlendNode(struct json_object_s *object, HYA_BlendNode *node,
                        StrToIdPair *node_map, StrToIdPair *type_map,
                        Arena *arena) {
  Init_HYA_Node(object, &node->node, node_map, type_map, arena);
  return;
}
