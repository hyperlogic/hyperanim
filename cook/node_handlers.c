#include "node_handlers.h"

#include <assert.h>
#include <stdio.h>

#include "stb_ds.h"

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

void Init_HYA_Node(struct json_object_s *object, HYA_Node *node, Context *ctx) {
  memset(node, 0, sizeof(HYA_Node));  // NOLINT
  struct json_object_element_s *elem = object->start;
  while (elem != NULL) {
    if (0 == strcmp(elem->name->string, "name")) {
      struct json_string_s *s = json_value_as_string(elem->value);
      assert(s);
      size_t id = shget(ctx->node_map, s->string);
      if (id == 0) {
        printf("ERROR: unknown node name %s\n", s->string);
        exit(10);
      }
      node->id = id;
    } else if (0 == strcmp(elem->name->string, "type")) {
      struct json_string_s *s = json_value_as_string(elem->value);
      assert(s);
      size_t id = shget(ctx->type_map, s->string);
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
          ArenaAllocFrom(ctx->arena, sizeof(HYA_NODE_ID) * node->num_children);
      struct json_array_element_s *e = a->start;
      int i = 0;
      while (e != NULL) {
        struct json_string_s *s = json_value_as_string(e->value);
        assert(s);
        size_t id = shget(ctx->node_map, s->string);
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

void Print_HYA_StateMachineNode(const HYA_StateMachineNode *node) {
  printf("    StateMachineNode {\n");
  Print_HYA_Node(&node->node);
  printf("    }\n");
}

void Init_HYA_StateMachineNode(struct json_object_s *object,
                               HYA_StateMachineNode *node, Context *ctx) {
  Init_HYA_Node(object, &node->node, ctx);
  struct json_object_element_s *element = object->start;
  while (element != NULL) {
    if (0 == strcmp(element->name->string, "states")) {
      struct json_array_s *array = json_value_as_array(element->value);
      assert(array);
      struct json_array_element_s *elem = array->start;
      while (elem != NULL) {
        struct json_object_s *obj = json_value_as_object(elem->value);
        struct json_object_element_s *el = obj->start;
        while (el != NULL) {
          if (0 == strcmp(el->name->string, "name")) {
          } else if (0 == strcmp(el->name->string, "interp_time")) {
          } else if (0 == strcmp(el->name->string, "transltions")) {
            struct json_array_s *a = json_value_as_array(el->value);
            struct json_array_element_s *e = a->start;
            while (e != NULL) {
              struct json_string_s *s = json_value_as_string(e->value);
              if (0 == strcmp(s->string, "condition")) {
              } else if (0 == strcmp(s->string, "dst_state")) {
              }
              e = e->next;
            }
          }
          el = el->next;
        }
        elem = elem->next;
      }
      break;
    }
    element = element->next;
  }
  return;
}

void Print_HYA_MotionNode(const HYA_MotionNode *node) {
  printf("    MotionNode {\n");
  Print_HYA_Node(&node->node);
  printf("    }\n");
}

void Init_HYA_MotionNode(struct json_object_s *object, HYA_MotionNode *node,
                         Context *ctx) {
  Init_HYA_Node(object, &node->node, ctx);
  return;
}

void Print_HYA_BlendNode(const HYA_BlendNode *node) {
  printf("    BlendNode {\n");
  Print_HYA_Node(&node->node);
  printf("    }\n");
}

void Init_HYA_BlendNode(struct json_object_s *object, HYA_BlendNode *node,
                        Context *ctx) {
  Init_HYA_Node(object, &node->node, ctx);
  return;
}
