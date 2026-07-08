#include "loadjson.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>

#include "gltf.h"
#include "stb_ds.h"

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
    Print_##Type(&graph->name##_nodes[i], graph);          \
  }
  HYA_NODE_TYPE_LIST
#undef X
  printf("  num_node_ptrs = %zu\n", graph->num_node_ptrs);
  printf("  num_str_ptrs = %zu\n", graph->num_str_ptrs);
  printf("  num_vars = %zu\n", graph->num_vars);
  printf("}\n");
}

void Print_HYA_Node(const HYA_Node *node, const HYA_Graph *graph) {
  printf("      id = %d\n", node->id);
  printf("      type = %zu\n", node->type);
  printf("      name = %s\n", graph->str_ptrs[node->name]);
  printf("      num_children = %zu\n", node->num_children);
  printf("      children = [");
  for (size_t i = 0; i < node->num_children; i++) {
    HYA_NODE_ID id = node->children[i];
    HYA_Node *child = graph->node_ptrs[id];
    printf("%s", graph->str_ptrs[child->name]);
    if (i != node->num_children - 1) {
      printf(", ");
    }
  }
  printf("]\n");
}

static HYA_Result BuildNodes(struct json_array_s *array, HYA_Graph *graph,
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
    res = Init_##Type(&graph->name##_nodes[graph->num_##name##_nodes++], ctx,  \
                      object);                                                 \
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

HYA_Result Init_HYA_Graph(HYA_Graph *graph, Context *ctx,
                          struct json_object_s *object) {
  HYA_Result res;
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
      graph->version = v;
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
      res = BuildNodes(array, graph, ctx);
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
      res = InitSkeletonFromGLTF(s->string, &graph->tpose, ctx);
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
  graph->root = shget(ctx->node_map, root_name);
  if (graph->root < 0) {
    printf("ERROR: BuildGraph: could not find root node %s\n", root_name);
    return HYA_ERR_FAILURE;
  }

  // create ptrs to each node with node id as in index.
  graph->num_node_ptrs = (size_t)ctx->next_node_id;
  graph->node_ptrs = (HYA_Node **)ArenaAllocFrom(
      ctx->arena, sizeof(HYA_Node *) * graph->num_node_ptrs);
  if (!graph->node_ptrs) {
    printf("ERROR: BuildGraph node_ptrs alloc failed\n");
    return HYA_ERR_OUT_OF_MEMORY;
  }

#define X(Type, name, NAME)                            \
  for (size_t i = 0; i < graph->num_##name##_nodes; i++) { \
    Type *node = &graph->name##_nodes[i]; /* NOLINT */     \
    assert(node && node->node.id >= 0);                \
    graph->node_ptrs[node->node.id] = (HYA_Node *)node;    \
  }
  HYA_NODE_TYPE_LIST
#undef X

  // create ptrs to each string in the str_map
  graph->num_str_ptrs = (size_t)ctx->next_str_id;
  char **str_ptrs =
      (char **)ArenaAllocFrom(ctx->arena, sizeof(char *) * graph->num_str_ptrs);
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
  graph->str_ptrs = (const char **)str_ptrs;

  // create vars
  graph->num_vars = (size_t)ctx->next_var_id;
  graph->vars =
      (HYA_Var *)ArenaAllocFrom(ctx->arena, sizeof(HYA_Var) * graph->num_vars);
  if (!graph->vars) {
    printf("failure allocating %zu HYA_Var*\n", graph->num_vars);
    return HYA_ERR_OUT_OF_MEMORY;
  }
  memset(graph->vars, 0, sizeof(HYA_Var) * graph->num_vars);  // NOLINT
  n = shlen(ctx->var_map);                            // number of pairs
  for (ptrdiff_t i = 0; i < n; i++) {
    char *key = ctx->var_map[i].key;
    size_t value = ctx->var_map[i].value;

    // find name from string table
    int id = shget(ctx->str_map, key);
    graph->vars[value].name = id;
  }

  return HYA_OK;
}

HYA_Result Init_HYA_Node(HYA_Node *node, Context *ctx,
                         struct json_object_s *object) {
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
      node->name = ContextAddString(ctx, s->string);
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

static void Print_HYA_State(const HYA_State *state, const HYA_Graph *graph) {
  printf("        {\n");
  printf("          state_idx = %d\n", state->state_idx);
  printf("          name = %s\n", graph->str_ptrs[state->name]);
  printf("          interp_time = %.3f\n", state->interp_time);
  printf("          transitions = [\n");
  for (size_t i = 0; i < state->num_transitions; i++) {
    printf("            { var_id = %d, dst_state_idx = %d }\n",
           state->transitions[i].var_id, state->transitions[i].dst_state_idx);
  }
  printf("          ]\n");
  printf("        }\n");
}

void Print_HYA_StateMachineNode(const HYA_StateMachineNode *node,
                                const HYA_Graph *graph) {
  printf("    StateMachineNode {\n");
  Print_HYA_Node(&node->node, graph);
  printf("      states = [\n");
  for (size_t i = 0; i < node->num_states; i++) {
    Print_HYA_State(&node->states[i], graph);
  }
  printf("      ]\n");
  printf("    }\n");
}

static HYA_Result Init_HYA_Transition(HYA_Transition *transition,
                                      Context *ctx,
                                      struct json_object_s *object,
                                      int transition_idx,
                                      StrToIdPair **state_map) {
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
      ContextAddString(ctx, s->string);
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

static HYA_Result Init_HYA_State(HYA_State *state,
                                 Context *ctx,
                                 struct json_object_s *object,
                                 int state_idx, StrToIdPair **state_map) {
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
        return HYA_ERR_FAILURE;
      }
      state->state_idx = id;
      state->name = ContextAddString(ctx, s->string);
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
            Init_HYA_Transition(&state->transitions[j], ctx, obj, j, state_map);
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

HYA_Result Init_HYA_StateMachineNode(HYA_StateMachineNode *node, Context *ctx,
                                     struct json_object_s *object) {
  HYA_Result res;
  memset(node, 0, sizeof(HYA_StateMachineNode));  // NOLINT
  Init_HYA_Node(&node->node, ctx, object);
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
        if (!obj) {
          printf("ERROR: Init_HYA_StateMachineNode state is not an object\n");
          return HYA_ERR_FAILURE;
        }
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
        res = Init_HYA_State(&node->states[i], ctx, obj, i, &state_map);
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

void Print_HYA_MotionNode(const HYA_MotionNode *node, const HYA_Graph *graph) {
  printf("    MotionNode {\n");
  Print_HYA_Node(&node->node, graph);
  printf("    }\n");
}

HYA_Result Init_HYA_MotionNode(HYA_MotionNode *node, Context *ctx,
                               struct json_object_s *object) {
  HYA_Result res;
  memset(node, 0, sizeof(HYA_MotionNode));  // NOLINT
  res = Init_HYA_Node(&node->node, ctx, object);
  if (res != HYA_OK) {
    printf("ERROR: Init_HYA_MotionNode Init_HYA_Node failed %d\n", res);
    return res;
  }
  return HYA_OK;
}

void Print_HYA_BlendNode(const HYA_BlendNode *node, const HYA_Graph *graph) {
  printf("    BlendNode {\n");
  Print_HYA_Node(&node->node, graph);
  printf("    }\n");
}

HYA_Result Init_HYA_BlendNode(HYA_BlendNode *node, Context *ctx,
                              struct json_object_s *object) {
  HYA_Result res;
  memset(node, 0, sizeof(HYA_BlendNode));  // NOLINT
  res = Init_HYA_Node(&node->node, ctx, object);
  if (res != HYA_OK) {
    printf("ERROR: Init_HYA_BlkendNode Init_HYA_Node failed %d\n", res);
    return res;
  }
  return HYA_OK;
}
