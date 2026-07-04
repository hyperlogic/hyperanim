#include "node_handlers.h"

#include <assert.h>
#include <stdio.h>

#include "stb_ds.h"

static double JSON_NumberToDouble(struct json_number_s *n) {
  char *end;
  double d = strtod(n->number, &end);
  if (end == n->number || *end != '\0') {
    // didn't consume the whole thing — malformed (shouldn't happen
    // on library output, but worth guarding if you ever feed it elsewhere)
    return 0.0f;
  }
  return d;
}

void Print_HYA_Graph(const HYA_Graph *graph) {
  printf("{\n");
  printf("  version = %zu\n", graph->version);
  printf("  root = %d\n", graph->root);
#define X(Type, name, NAME)                                \
  for (size_t i = 0; i < graph->num_##name##_nodes; i++) { \
    Print_##Type(&graph->name##_nodes[i]);                 \
  }
  HYA_NODE_TYPE_LIST
#undef X
  printf("}\n");
}

void Print_HYA_Node(const HYA_Node *node) {
  printf("      id = %d\n", node->id);
  printf("      type = %zu\n", node->type);
  printf("      num_children = %zu\n", node->num_children);
  printf("      children = [");
  for (size_t i = 0; i < node->num_children; i++) {
    printf("%d", node->children[i]);
    if (i != node->num_children - 1) {
      printf(", ");
    }
  }
  printf("]\n");
}

HYA_Result Init_HYA_Node(struct json_object_s *object, HYA_Node *node,
                         Context *ctx) {
  struct json_object_element_s *elem = object->start;
  while (elem != NULL) {
    if (0 == strcmp(elem->name->string, "name")) {
      struct json_string_s *s = json_value_as_string(elem->value);
      if (!s) {
        printf("ERROR: Init_HYA_Node name value is not a string\n");
        return HYA_ERR_FAILURE;
      }
      int id = shget(ctx->node_map, s->string);
      if (id < 0) {
        printf("ERROR: unknown node name %s\n", s->string);
        return HYA_ERR_FAILURE;
      }
      node->id = id;
    } else if (0 == strcmp(elem->name->string, "type")) {
      struct json_string_s *s = json_value_as_string(elem->value);
      if (!s) {
        printf("ERROR: Init_HYA_Node type value is not a string\n");
        return HYA_ERR_FAILURE;
      }
      int id = shget(ctx->type_map, s->string);
      if (id < 0) {
        printf("ERROR: unknown node type %s\n", s->string);
        return HYA_ERR_FAILURE;
      }
      node->type = id;
    } else if (0 == strcmp(elem->name->string, "children")) {
      struct json_array_s *a = json_value_as_array(elem->value);
      if (!a) {
        printf("ERROR: Init_HYA_Node children value is not an array\n");
        return HYA_ERR_FAILURE;
      }
      node->num_children = a->length;
      node->children =
          ArenaAllocFrom(ctx->arena, sizeof(HYA_NODE_ID) * node->num_children);
      if (!node->children) {
        printf("ERROR: Init_HYA_Node children allocation failed\n");
        return HYA_ERR_OUT_OF_MEMORY;
      }
      struct json_array_element_s *e = a->start;
      int i = 0;
      while (e != NULL) {
        struct json_string_s *s = json_value_as_string(e->value);
        if (!s) {
          printf("ERROR: Init_HYA_Node child value is not a string\n");
          return HYA_ERR_FAILURE;
        }
        int id = shget(ctx->node_map, s->string);
        if (id < 0) {
          printf("ERROR: unknown node name %s\n", s->string);
          return HYA_ERR_FAILURE;
        }
        node->children[i] = id;
        i++;
        e = e->next;
      }
    }
    elem = elem->next;
  }
  return HYA_OK;
}

static void Print_HYA_State(const HYA_State *state) {
  printf("        {\n");
  printf("          state_idx = %d\n", state->state_idx);
  printf("          interp_time = %.3f\n", state->interp_time);
  printf("          transitions = [\n");
  for (size_t i = 0; i < state->num_transitions; i++) {
    printf("            { var_id = %d, dst_state_idx = %d }\n",
           state->transitions[i].var_id, state->transitions[i].dst_state_idx);
  }
  printf("          ]\n");
  printf("        }\n");
}

void Print_HYA_StateMachineNode(const HYA_StateMachineNode *node) {
  printf("    StateMachineNode {\n");
  Print_HYA_Node(&node->node);
  printf("      states = [\n");
  for (size_t i = 0; i < node->num_states; i++) {
    Print_HYA_State(&node->states[i]);
  }
  printf("      ]\n");
  printf("    }\n");
}

static HYA_Result Init_HYA_Transition(struct json_object_s *object,
                                      HYA_Transition *transition,
                                      int transition_idx,
                                      StrToIdPair **state_map, Context *ctx) {
  struct json_object_element_s *elem = object->start;
  while (elem != NULL) {
    if (0 == strcmp(elem->name->string, "var")) {
      struct json_string_s *s = json_value_as_string(elem->value);
      if (!s) {
        printf("ERROR: Init_HYA_Transition var value is not a string\n");
        return HYA_ERR_FAILURE;
      }
      int var_id = shget(ctx->var_map, s->string);
      if (var_id < 0) {
        var_id = ctx->next_var_id++;
        shput(ctx->var_map, s->string, var_id);
      }
      transition->var_id = var_id;
    } else if (0 == strcmp(elem->name->string, "dst_state")) {
      struct json_string_s *s = json_value_as_string(elem->value);
      if (!s) {
        printf("ERROR: Init_HYA_Transition dst_state value is not a string\n");
        return HYA_ERR_FAILURE;
      }
      int state_id = shget(*state_map, s->string);
      if (state_id < 0) {
        printf("ERROR: Init_HYA_Transition could not find dst_state %s\n",
               s->string);
        return HYA_ERR_FAILURE;
      }
      transition->dst_state_idx = state_id;
    } else {
      printf("WARNING: Init_HYA_Transition unexpected key %s\n",
             elem->name->string);
    }
    elem = elem->next;
  }
  return HYA_OK;
}

static HYA_Result Init_HYA_State(struct json_object_s *object, HYA_State *state,
                                 int state_idx, StrToIdPair **state_map,
                                 Context *ctx) {
  HYA_Result res;
  struct json_object_element_s *elem = object->start;
  while (elem != NULL) {
    if (0 == strcmp(elem->name->string, "name")) {
      struct json_string_s *s = json_value_as_string(elem->value);
      if (!s) {
        printf("ERROR: Init_HYA_State name value is not a string\n");
        return HYA_ERR_FAILURE;
      }
      int id = shget(*state_map, s->string);
      if (id < 0) {
        printf(
            "ERROR: Init_HYA_State could not find state name %s in state_map\n",
            s->string);
        exit(13);
      }
      state->state_idx = id;
    } else if (0 == strcmp(elem->name->string, "interp_time")) {
      struct json_number_s *n = json_value_as_number(elem->value);
      if (!n) {
        printf("ERROR: Init_HYA_State interp_time value is not a number\n");
        return HYA_ERR_FAILURE;
      }
      double d = JSON_NumberToDouble(n);
      state->interp_time = (float)d;
    } else if (0 == strcmp(elem->name->string, "transitions")) {
      struct json_array_s *array = json_value_as_array(elem->value);
      if (!array) {
        printf("ERROR: Init_HYA_State transitions value is not an array\n");
        return HYA_ERR_FAILURE;
      }
      state->num_transitions = array->length;
      state->transitions = (HYA_Transition *)ArenaAllocFrom(
          ctx->arena, sizeof(HYA_Transition) * array->length);
      if (!state->transitions) {
        printf("ERROR: Init_HYA_State state->transitions alloc failed\n");
        return HYA_ERR_OUT_OF_MEMORY;
      }
      struct json_array_element_s *el = array->start;
      int j = 0;
      while (el != NULL) {
        struct json_object_s *obj = json_value_as_object(el->value);
        if (!obj) {
          printf("ERROR: Init_HYA_State transition element is not an object\n");
          return HYA_ERR_FAILURE;
        }
        res =
            Init_HYA_Transition(obj, &state->transitions[j], j, state_map, ctx);
        if (res != HYA_OK) {
          printf("ERROR: Init_HYA_State Init_HYA_Transition failed: %d\n", res);
          return HYA_ERR_FAILURE;
        }
        el = el->next;
        j++;
      }
    } else {
      printf("WARNING: Init_HYA_State unexpected key %s\n", elem->name->string);
    }
    elem = elem->next;
  }
  return HYA_OK;
}

HYA_Result Init_HYA_StateMachineNode(struct json_object_s *object,
                                     HYA_StateMachineNode *node, Context *ctx) {
  HYA_Result res;
  memset(node, 0, sizeof(HYA_StateMachineNode));  // NOLINT
  Init_HYA_Node(object, &node->node, ctx);
  struct json_object_element_s *element = object->start;
  StrToIdPair *state_map = NULL;
  shdefault(state_map, -1);
  while (element != NULL) {
    if (0 == strcmp(element->name->string, "states")) {
      struct json_array_s *array = json_value_as_array(element->value);
      if (!array) {
        printf(
            "ERROR: Init_HYA_StateMachineNode states value is not an array\n");
        return HYA_ERR_FAILURE;
      }
      node->num_states = array->length;
      node->states = (HYA_State *)ArenaAllocFrom(
          ctx->arena, sizeof(HYA_State) * array->length);
      if (!node->states) {
        printf("ERROR: Init_HYA_StateMachineNode states alloc failed\n");
        return HYA_ERR_OUT_OF_MEMORY;
      }

      // first pass: fill the state_map with all the names.
      struct json_array_element_s *elem = array->start;
      int i = 0;
      while (elem != NULL) {
        struct json_object_s *obj = json_value_as_object(elem->value);
        struct json_object_element_s *e = obj->start;
        while (e != NULL) {
          if (0 == strcmp(e->name->string, "name")) {
            struct json_string_s *s = json_value_as_string(e->value);
            if (!s) {
              printf(
                  "ERROR: Init_HYA_StateMachineNode name value is not a "
                  "string\n");
              return HYA_ERR_FAILURE;
            }
            int id = shget(state_map, s->string);
            if (id >= 0) {
              printf("ERROR: duplicate state name %s\n", s->string);
              return HYA_ERR_FAILURE;
            }
            shput(state_map, s->string, i);
          }
          e = e->next;
        }
        elem = elem->next;
        i++;
      }

      // second pass: init node->states[i]
      elem = array->start;
      i = 0;
      while (elem != NULL) {
        struct json_object_s *obj = json_value_as_object(elem->value);
        if (!obj) {
          printf(
              "ERROR: Init_HYA_StateMachineNode state value is not an "
              "object\n");
          return HYA_ERR_FAILURE;
        }
        res = Init_HYA_State(obj, &node->states[i], i, &state_map, ctx);
        if (res != HYA_OK) {
          printf("ERROR: Init_HYA_StateMachineNode Init_HYA_State failed\n");
          return res;
        }
        elem = elem->next;
        i++;
      }
    }
    element = element->next;
  }
  shfree(state_map);
  return HYA_OK;
}

void Print_HYA_MotionNode(const HYA_MotionNode *node) {
  printf("    MotionNode {\n");
  Print_HYA_Node(&node->node);
  printf("    }\n");
}

HYA_Result Init_HYA_MotionNode(struct json_object_s *object,
                               HYA_MotionNode *node, Context *ctx) {
  HYA_Result res;
  memset(node, 0, sizeof(HYA_MotionNode));  // NOLINT
  res = Init_HYA_Node(object, &node->node, ctx);
  if (res != HYA_OK) {
    printf("ERROR: Init_HYA_MotionNode Init_HYA_Node failed %d\n", res);
    return res;
  }
  return HYA_OK;
}

void Print_HYA_BlendNode(const HYA_BlendNode *node) {
  printf("    BlendNode {\n");
  Print_HYA_Node(&node->node);
  printf("    }\n");
}

HYA_Result Init_HYA_BlendNode(struct json_object_s *object, HYA_BlendNode *node,
                              Context *ctx) {
  HYA_Result res;
  memset(node, 0, sizeof(HYA_BlendNode));  // NOLINT
  res = Init_HYA_Node(object, &node->node, ctx);
  if (res != HYA_OK) {
    printf("ERROR: Init_HYA_BlkendNode Init_HYA_Node failed %d\n", res);
    return res;
  }
  return HYA_OK;
}
