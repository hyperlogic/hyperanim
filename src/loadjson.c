#include "loadjson.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>

#include "loadgltf.h"
#include "stb_ds.h"

#define LOG_ERROR(fmt, ...) \
  fprintf(stderr, "ERROR: %s " fmt, __func__, ##__VA_ARGS__)

/* Requires the parser to have been called with
   json_parse_flags_allow_location_information; otherwise the cast reads
   uninitialized memory. Relies on `ctx` being in scope. */
#define LOG_JSON_VAL_ERR(v, fmt, ...)                              \
  fprintf(stderr, "%s:%zu:%zu: ERROR: %s " fmt, ctx->basename,     \
          ((struct json_value_ex_s *)(v))->line_no,                \
          ((struct json_value_ex_s *)(v))->row_no, __func__,       \
          ##__VA_ARGS__)

#define JSON_VAL_TO_ARR(v, a)                        \
  struct json_array_s *a = json_value_as_array(v);   \
  if (!a) {                                          \
    LOG_JSON_VAL_ERR(v, "value is not an array\n");  \
    return HYA_ERR_JSON_SCHEMA;                      \
  }

#define JSON_VAL_TO_STR(v, s)                        \
  struct json_string_s *s = json_value_as_string(v); \
  if (!s) {                                          \
    LOG_JSON_VAL_ERR(v, "value is not a string\n");  \
    return HYA_ERR_JSON_SCHEMA;                      \
  }

#define JSON_VAL_TO_NUM(v, n)                        \
  struct json_number_s *n = json_value_as_number(v); \
  if (!n) {                                          \
    LOG_JSON_VAL_ERR(v, "value is not a number\n");  \
    return HYA_ERR_JSON_SCHEMA;                      \
  }

#define JSON_VAL_TO_OBJ(v, o)                        \
  struct json_object_s *o = json_value_as_object(v); \
  if (!o) {                                          \
    LOG_JSON_VAL_ERR(v, "value is not an object\n"); \
    return HYA_ERR_JSON_SCHEMA;                      \
  }

#define JSON_ARR_FOR_EACH(a, e) \
  for (struct json_array_element_s *e = a->start; e; e = e->next)

#define JSON_OBJ_FOR_EACH(o, e) \
  for (struct json_object_element_s *e = o->start; e; e = e->next)

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

static HYA_Result BuildNodes(struct json_value_s *value, HYA_Graph *graph,
                             Context *ctx) {
  HYA_Result res;

  // first pass: determine node counts
  JSON_VAL_TO_ARR(value, array);
  JSON_ARR_FOR_EACH(array, arr_elem) {
    bool found_name = false;
    bool found_type = false;
    JSON_VAL_TO_OBJ(arr_elem->value, object);
    JSON_OBJ_FOR_EACH(object, obj_elem) {
      if (0 == strcmp(obj_elem->name->string, "type")) {
        JSON_VAL_TO_STR(obj_elem->value, s);

#define X(Type, name, NAME)                 \
  else if (0 == strcmp(s->string, #name)) { \
    graph->num_##name##_nodes++;            \
  }
        if (0) {
        }
        HYA_NODE_TYPE_LIST
#undef X
        else {
          LOG_JSON_VAL_ERR(obj_elem->value, "unknown node type %s\n",
                           s->string);
          return HYA_ERR_JSON_SCHEMA;
        }
        found_type = true;
      }
      if (0 == strcmp(obj_elem->name->string, "name")) {
        JSON_VAL_TO_STR(obj_elem->value, s);
        int id = shget(ctx->node_map, s->string);
        if (id >= 0) {
          LOG_JSON_VAL_ERR(obj_elem->value, "duplicate node name %s\n",
                           s->string);
          return HYA_ERR_JSON_SCHEMA;
        }
        shput(ctx->node_map, s->string, ctx->next_node_id++);
        found_name = true;
      }
    }
    if (!found_name) {
      LOG_JSON_VAL_ERR(arr_elem->value, "missing name\n");
      return HYA_ERR_JSON_SCHEMA;
    }
    if (!found_type) {
      LOG_JSON_VAL_ERR(arr_elem->value, "missing type\n");
      return HYA_ERR_JSON_SCHEMA;
    }
  }

  // now allocate nodes arrays, and set counts back to zero,
  // for second pass
#define X(Type, name, NAME)                                     \
  if (graph->num_##name##_nodes > 0) {                          \
    graph->name##_nodes = (Type *)ArenaAllocFrom(               \
        ctx->arena, sizeof(Type) * graph->num_##name##_nodes);  \
    if (!graph->name##_nodes) {                                 \
      LOG_ERROR("%s allocate failed\n", #name "_nodes");        \
      return HYA_ERR_OUT_OF_MEMORY;                             \
    }                                                           \
    graph->num_##name##_nodes = 0;                              \
  }
  HYA_NODE_TYPE_LIST
#undef X

  // second pass: init each node
  JSON_ARR_FOR_EACH(array, arr_elem) {
    JSON_VAL_TO_OBJ(arr_elem->value, object);
    JSON_OBJ_FOR_EACH(object, obj_elem) {
      if (0 == strcmp(obj_elem->name->string, "type")) {
        JSON_VAL_TO_STR(obj_elem->value, s);

#define X(Type, name, NAME)                                                   \
  if (0 == strcmp(s->string, #name)) {                                        \
    res = Init_##Type(&graph->name##_nodes[graph->num_##name##_nodes++], ctx, \
                      arr_elem->value);                                       \
    if (res != HYA_OK) {                                                      \
      LOG_ERROR("Init_" #Type " failed: %d\n", res);                          \
      return res;                                                             \
    }                                                                         \
  }
        HYA_NODE_TYPE_LIST
#undef X
      }
    }
  }
  return HYA_OK;
}

HYA_Result Init_HYA_Graph(HYA_Graph *graph, Context *ctx,
                          struct json_value_s *value) {
  HYA_Result res;

  const char *root_name = NULL;
  JSON_VAL_TO_OBJ(value, object);
  JSON_OBJ_FOR_EACH(object, obj_elem) {
    if (0 == strcmp(obj_elem->name->string, "version")) {
      JSON_VAL_TO_NUM(obj_elem->value, n);
      long v = JSON_NumberToLong(n);
      graph->version = v;
    } else if (0 == strcmp(obj_elem->name->string, "root")) {
      JSON_VAL_TO_STR(obj_elem->value, s);
      root_name = s->string;
    } else if (0 == strcmp(obj_elem->name->string, "nodes")) {
      res = BuildNodes(obj_elem->value, graph, ctx);
      if (res != HYA_OK) {
        LOG_ERROR("BuildNodes failed: %d\n", res);
        return res;
      }
    } else if (0 == strcmp(obj_elem->name->string, "tpose")) {
      JSON_VAL_TO_STR(obj_elem->value, s);
      // load the tpose
      res = InitSkeletonFromGLTF(s->string, &graph->tpose, ctx);
      if (res != HYA_OK) {
        LOG_ERROR("InitSkeletonFromGLTF failed: %d\n", res);
        return res;
      }
    } else {
      fprintf(stderr, "WARNING: BuildGraph: Unknown key %s, skipping\n",
              obj_elem->name->string);
    }
  }
  if (!root_name) {
    LOG_JSON_VAL_ERR(value, "could not find root_node key\n");
    return HYA_ERR_JSON_SCHEMA;
  }
  graph->root = shget(ctx->node_map, root_name);
  if (graph->root < 0) {
    LOG_ERROR("could not find root node %s\n", root_name);
    return HYA_ERR_JSON_SCHEMA;
  }

  // create ptrs to each node with node id as in index.
  graph->num_node_ptrs = (size_t)ctx->next_node_id;
  graph->node_ptrs = (HYA_Node **)ArenaAllocFrom(
      ctx->arena, sizeof(HYA_Node *) * graph->num_node_ptrs);
  if (!graph->node_ptrs) {
    LOG_ERROR("node_ptrs alloc failed\n");
    return HYA_ERR_OUT_OF_MEMORY;
  }

#define X(Type, name, NAME)                                \
  for (size_t i = 0; i < graph->num_##name##_nodes; i++) { \
    Type *node = &graph->name##_nodes[i]; /* NOLINT */     \
    assert(node && node->node.id >= 0);                    \
    graph->node_ptrs[node->node.id] = (HYA_Node *)node;    \
  }
  HYA_NODE_TYPE_LIST
#undef X

  // create ptrs to each string in the str_map
  graph->num_str_ptrs = (size_t)ctx->next_str_id;
  char **str_ptrs =
      (char **)ArenaAllocFrom(ctx->arena, sizeof(char *) * graph->num_str_ptrs);
  if (!str_ptrs) {
    LOG_ERROR("str_ptrs alloc failed\n");
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
      LOG_ERROR("failure allocating string of %zu bytes\n", len + 1);
      return HYA_ERR_OUT_OF_MEMORY;
    }
    strcpy(arena_str, key);  // NOLINT
    arena_str[len] = 0;
    str_ptrs[value] = arena_str;
  }
  graph->str_ptrs = (const char **)str_ptrs;

  // create vars
  graph->num_vars = (size_t)ctx->next_var_id;
  graph->vars =
      (HYA_Var *)ArenaAllocFrom(ctx->arena, sizeof(HYA_Var) * graph->num_vars);
  if (!graph->vars) {
    fprintf(stderr, "failure allocating %zu HYA_Var*\n", graph->num_vars);
    return HYA_ERR_OUT_OF_MEMORY;
  }
  memset(graph->vars, 0, sizeof(HYA_Var) * graph->num_vars);  // NOLINT
  n = shlen(ctx->var_map);                                    // number of pairs
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
                         struct json_value_s *value) {
  JSON_VAL_TO_OBJ(value, object);
  JSON_OBJ_FOR_EACH(object, obj_elem) {
    if (0 == strcmp(obj_elem->name->string, "name")) {
      JSON_VAL_TO_STR(obj_elem->value, s);
      int id = shget(ctx->node_map, s->string);
      if (id < 0) {
        LOG_ERROR("unknown node name %s\n", s->string);
        return HYA_ERR_JSON_SCHEMA;
      }
      node->id = id;
      node->name = ContextAddString(ctx, s->string);
    } else if (0 == strcmp(obj_elem->name->string, "type")) {
      JSON_VAL_TO_STR(obj_elem->value, s);
      int id = shget(ctx->type_map, s->string);
      if (id < 0) {
        LOG_ERROR("unknown node type %s\n", s->string);
        return HYA_ERR_JSON_SCHEMA;
      }
      node->type = id;
    } else if (0 == strcmp(obj_elem->name->string, "children")) {
      JSON_VAL_TO_ARR(obj_elem->value, a);
      node->num_children = a->length;
      node->children =
          ArenaAllocFrom(ctx->arena, sizeof(HYA_NODE_ID) * node->num_children);
      if (!node->children) {
        LOG_ERROR("children allocation failed\n");
        return HYA_ERR_OUT_OF_MEMORY;
      }
      int i = 0;
      JSON_ARR_FOR_EACH(a, arr_elem) {
        JSON_VAL_TO_STR(arr_elem->value, s);
        int id = shget(ctx->node_map, s->string);
        if (id < 0) {
          LOG_ERROR("unknown node name %s\n", s->string);
          return HYA_ERR_JSON_SCHEMA;
        }
        node->children[i] = id;
        i++;
      }
    }
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

static HYA_Result Init_HYA_Transition(HYA_Transition *transition, Context *ctx,
                                      struct json_value_s *value,
                                      int transition_idx,
                                      StrToIdPair **state_map) {
  JSON_VAL_TO_OBJ(value, object);
  JSON_OBJ_FOR_EACH(object, obj_elem) {
    if (0 == strcmp(obj_elem->name->string, "var")) {
      JSON_VAL_TO_STR(obj_elem->value, s);
      int var_id = shget(ctx->var_map, s->string);
      if (var_id < 0) {
        var_id = ctx->next_var_id++;
        shput(ctx->var_map, s->string, var_id);
      }
      transition->var_id = var_id;
      ContextAddString(ctx, s->string);
    } else if (0 == strcmp(obj_elem->name->string, "dst_state")) {
      JSON_VAL_TO_STR(obj_elem->value, s);
      int state_id = shget(*state_map, s->string);
      if (state_id < 0) {
        LOG_ERROR("could not find dst_state %s\n", s->string);
        return HYA_ERR_JSON_SCHEMA;
      }
      transition->dst_state_idx = state_id;
    } else {
      fprintf(stderr, "WARNING: Init_HYA_Transition unexpected key %s\n",
              obj_elem->name->string);
    }
  }
  return HYA_OK;
}

static HYA_Result Init_HYA_State(HYA_State *state, Context *ctx,
                                 struct json_value_s *value, int state_idx,
                                 StrToIdPair **state_map) {
  HYA_Result res;
  JSON_VAL_TO_OBJ(value, object);
  JSON_OBJ_FOR_EACH(object, obj_elem) {
    if (0 == strcmp(obj_elem->name->string, "name")) {
      JSON_VAL_TO_STR(obj_elem->value, s);
      int id = shget(*state_map, s->string);
      if (id < 0) {
        LOG_ERROR("could not find state name %s in state_map\n", s->string);
        return HYA_ERR_JSON_SCHEMA;
      }
      state->state_idx = id;
      state->name = ContextAddString(ctx, s->string);
    } else if (0 == strcmp(obj_elem->name->string, "interp_time")) {
      JSON_VAL_TO_NUM(obj_elem->value, n);
      double d = JSON_NumberToDouble(n);
      state->interp_time = (float)d;
    } else if (0 == strcmp(obj_elem->name->string, "transitions")) {
      JSON_VAL_TO_ARR(obj_elem->value, a)
      state->num_transitions = a->length;
      state->transitions = (HYA_Transition *)ArenaAllocFrom(
          ctx->arena, sizeof(HYA_Transition) * a->length);
      if (!state->transitions) {
        LOG_ERROR("state->transitions alloc failed\n");
        return HYA_ERR_OUT_OF_MEMORY;
      }
      size_t j = 0;
      JSON_ARR_FOR_EACH(a, arr_elem) {
        res = Init_HYA_Transition(&state->transitions[j], ctx, arr_elem->value,
                                  j, state_map);
        if (res != HYA_OK) {
          LOG_ERROR("Init_HYA_Transition failed: %d\n", res);
          return HYA_ERR_JSON_SCHEMA;
        }
        j++;
      }
    } else {
      fprintf(stderr, "WARNING: Init_HYA_State unexpected key %s\n",
              obj_elem->name->string);
    }
  }
  return HYA_OK;
}

HYA_Result Init_HYA_StateMachineNode(HYA_StateMachineNode *node, Context *ctx,
                                     struct json_value_s *value) {
  HYA_Result res;
  memset(node, 0, sizeof(HYA_StateMachineNode));  // NOLINT
  Init_HYA_Node(&node->node, ctx, value);
  StrToIdPair *state_map = NULL;
  shdefault(state_map, -1);
  JSON_VAL_TO_OBJ(value, object);
  JSON_OBJ_FOR_EACH(object, obj_elem) {
    if (0 == strcmp(obj_elem->name->string, "states")) {
      JSON_VAL_TO_ARR(obj_elem->value, a);
      node->num_states = a->length;
      node->states = (HYA_State *)ArenaAllocFrom(ctx->arena,
                                                 sizeof(HYA_State) * a->length);
      if (!node->states) {
        LOG_ERROR("states alloc failed\n");
        return HYA_ERR_OUT_OF_MEMORY;
      }

      // first pass: fill the state_map with all the names.
      size_t i = 0;
      JSON_ARR_FOR_EACH(a, arr_elem) {
        JSON_VAL_TO_OBJ(arr_elem->value, obj);
        JSON_OBJ_FOR_EACH(obj, oe) {
          if (0 == strcmp(oe->name->string, "name")) {
            JSON_VAL_TO_STR(oe->value, s);
            int id = shget(state_map, s->string);
            if (id >= 0) {
              LOG_ERROR("duplicate state name %s\n", s->string);
              return HYA_ERR_JSON_SCHEMA;
            }
            shput(state_map, s->string, i);
          }
        }
        i++;
      }

      // second pass: init node->states[i]
      i = 0;
      JSON_ARR_FOR_EACH(a, arr_elem) {
        res = Init_HYA_State(&node->states[i], ctx, arr_elem->value, i,
                             &state_map);
        if (res != HYA_OK) {
          LOG_ERROR("Init_HYA_State failed\n");
          return res;
        }
        i++;
      }
    }
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
                               struct json_value_s *value) {
  HYA_Result res;
  memset(node, 0, sizeof(HYA_MotionNode));  // NOLINT
  res = Init_HYA_Node(&node->node, ctx, value);
  if (res != HYA_OK) {
    LOG_ERROR("Init_HYA_Node failed %d\n", res);
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
                              struct json_value_s *value) {
  HYA_Result res;
  memset(node, 0, sizeof(HYA_BlendNode));  // NOLINT
  res = Init_HYA_Node(&node->node, ctx, value);
  if (res != HYA_OK) {
    LOG_ERROR("Init_HYA_Node failed %d\n", res);
    return res;
  }
  return HYA_OK;
}
