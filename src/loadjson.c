#include "loadjson.h"

#include <assert.h>
#include <errno.h>
#include <stdalign.h>
#include <stdio.h>

#include "loadgltf.h"
#include "stb_ds.h"

#define LOG_ERROR(fmt, ...) \
  fprintf(stderr, "ERROR: %s " fmt, __func__, ##__VA_ARGS__)

/* Requires the parser to have been called with
   json_parse_flags_allow_location_information; otherwise the cast reads
   uninitialized memory. Relies on `ctx` being in scope. */
#define LOG_JSON_VAL_ERR(v, fmt, ...)                          \
  fprintf(stderr, "%s:%zu:%zu: ERROR: %s " fmt, ctx->basename, \
          ((struct json_value_ex_s *)(v))->line_no,            \
          ((struct json_value_ex_s *)(v))->row_no, __func__, ##__VA_ARGS__)

#define JSON_VAL_TO_ARR(v, a)                       \
  struct json_array_s *a = json_value_as_array(v);  \
  if (!a) {                                         \
    LOG_JSON_VAL_ERR(v, "value is not an array\n"); \
    return HYA_ERR_JSON_SCHEMA;                     \
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

#define JSON_VAL_TO_BOOL(v, b)                    \
  bool b = json_value_is_true(v);                 \
  if (!b && !json_value_is_false(v)) {            \
    LOG_JSON_VAL_ERR(v, "value is not a bool\n"); \
    return HYA_ERR_JSON_SCHEMA;                   \
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

void PrintGraph(const HYA_Graph *graph) {
  printf("{\n");
  printf("  version = %zu\n", graph->version);
  printf("  root = %d\n", graph->root);
#define X(Name, name, NAME)                                \
  for (size_t i = 0; i < graph->num_##name##_nodes; i++) { \
    Print##Name##Node(&graph->name##_nodes[i], graph);     \
  }
  HYA_NODE_NAME_LIST
#undef X
  printf("  num_node_ptrs = %zu\n", graph->num_node_ptrs);
  printf("  num_str_ptrs = %zu\n", graph->num_str_ptrs);
  printf("  num_vars = %zu\n", graph->num_vars);
  printf("}\n");
}

void PrintNode(const HYA_Node *node, const HYA_Graph *graph) {
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

#define X(Name, name, NAME)                 \
  else if (0 == strcmp(s->string, #name)) { \
    graph->num_##name##_nodes++;            \
  }
        if (0) {
        }
        HYA_NODE_NAME_LIST
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
        // node names must be unique.
        int id = shgeti(ctx->node_map, s->string);
        if (id >= 0) {
          LOG_JSON_VAL_ERR(obj_elem->value, "duplicate node name %s\n",
                           s->string);
          return HYA_ERR_JSON_SCHEMA;
        }
        shputs(ctx->node_map, (Symbol){s->string});
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

  // now allocate arrays for each node type and set counts back to zero
  // for second pass
#define X(Name, name, NAME)                                            \
  if (graph->num_##name##_nodes > 0) {                                 \
    graph->name##_nodes = (HYA_##Name##Node *)ContextAllocFromAligned( \
        ctx, HYA_MEM_NODE, &graph->name##_nodes,                       \
        sizeof(HYA_##Name##Node) * graph->num_##name##_nodes,          \
        _Alignof(HYA_##Name##Node));                                   \
    if (!graph->name##_nodes) {                                        \
      LOG_ERROR("%s allocate failed\n", #name "_nodes");               \
      return HYA_ERR_OUT_OF_MEMORY;                                    \
    }                                                                  \
    graph->num_##name##_nodes = 0;                                     \
  }
  HYA_NODE_NAME_LIST
#undef X

  // second pass: init each node
  JSON_ARR_FOR_EACH(array, arr_elem) {
    JSON_VAL_TO_OBJ(arr_elem->value, object);
    JSON_OBJ_FOR_EACH(object, obj_elem) {
      if (0 == strcmp(obj_elem->name->string, "type")) {
        JSON_VAL_TO_STR(obj_elem->value, s);

#define X(Name, name, NAME)                                                   \
  if (0 == strcmp(s->string, #name)) {                                        \
    res = Init##Name##Node(&graph->name##_nodes[graph->num_##name##_nodes++], \
                           ctx, arr_elem->value);                             \
    if (res != HYA_OK) {                                                      \
      LOG_ERROR("Init" #Name "Node failed: %d\n", res);                       \
      return res;                                                             \
    }                                                                         \
  }
        HYA_NODE_NAME_LIST
#undef X
      }
    }
  }
  return HYA_OK;
}

HYA_Result InitGraph(HYA_Graph *graph, Context *ctx,
                     struct json_value_s *value) {
  HYA_Result res;
  memset(graph, 0, sizeof(HYA_Graph));  // NOLINT
  const char *root_node = NULL;
  const char *tpose_src = NULL;
  const char *root_joint = NULL;
  JSON_VAL_TO_OBJ(value, object);
  JSON_OBJ_FOR_EACH(object, obj_elem) {
    if (0 == strcmp(obj_elem->name->string, "version")) {
      JSON_VAL_TO_NUM(obj_elem->value, n);
      long v = JSON_NumberToLong(n);
      graph->version = v;
    } else if (0 == strcmp(obj_elem->name->string, "root_node")) {
      JSON_VAL_TO_STR(obj_elem->value, s);
      root_node = s->string;
    } else if (0 == strcmp(obj_elem->name->string, "nodes")) {
      res = BuildNodes(obj_elem->value, graph, ctx);
      if (res != HYA_OK) {
        LOG_ERROR("BuildNodes failed: %d\n", res);
        return res;
      }
    } else if (0 == strcmp(obj_elem->name->string, "tpose_src")) {
      JSON_VAL_TO_STR(obj_elem->value, s);
      tpose_src = s->string;
    } else if (0 == strcmp(obj_elem->name->string, "root_joint")) {
      JSON_VAL_TO_STR(obj_elem->value, s);
      root_joint = s->string;
    } else {
      fprintf(stderr, "WARNING: BuildGraph: Unknown key %s, skipping\n",
              obj_elem->name->string);
    }
  }

  if (!root_joint) {
    LOG_JSON_VAL_ERR(value, "missing root_joint key\n");
    return HYA_ERR_JSON_SCHEMA;
  }
  graph->root_joint = ContextInternString(ctx, root_joint);

  if (!tpose_src) {
    LOG_JSON_VAL_ERR(value, "missing tpose_src key\n");
    return HYA_ERR_JSON_SCHEMA;
  }

  if (!root_node) {
    LOG_JSON_VAL_ERR(value, "missing root_node key\n");
    return HYA_ERR_JSON_SCHEMA;
  }
  graph->root = shgeti(ctx->node_map, root_node);
  if (graph->root < 0) {
    LOG_ERROR("could not find root node %s\n", root_node);
    return HYA_ERR_JSON_SCHEMA;
  }

  // create ptrs to each node with node id as in index.
  graph->num_node_ptrs = shlen(ctx->node_map);
  graph->node_ptrs = (HYA_Node **)ContextAllocFromAligned(
      ctx, HYA_MEM_NODE, &graph->node_ptrs,
      sizeof(HYA_Node *) * graph->num_node_ptrs, _Alignof(HYA_Node *));
  if (!graph->node_ptrs) {
    LOG_ERROR("node_ptrs alloc failed\n");
    return HYA_ERR_OUT_OF_MEMORY;
  }

#define X(Name, name, NAME)                                         \
  for (size_t i = 0; i < graph->num_##name##_nodes; i++) {          \
    HYA_##Name##Node *node = &graph->name##_nodes[i]; /* NOLINT */  \
    assert(node && node->node.id >= 0);                             \
    graph->node_ptrs[node->node.id] = (HYA_Node *)node;             \
    ContextRelocAlias(ctx, &graph->node_ptrs[node->node.id], node); \
  }
  HYA_NODE_NAME_LIST
#undef X

  // load the tpose
  res = InitSkeletonFromGLTF(tpose_src, root_joint, &graph->tpose, ctx);
  if (res != HYA_OK) {
    LOG_ERROR("InitSkeletonFromGLTF failed: %d\n", res);
    return res;
  }

  // now load each gltf node.
  for (size_t i = 0; i < graph->num_motion_nodes; i++) {
    HYA_MotionNode *node = &graph->motion_nodes[i];
    const char *src = ctx->str_map[node->src].key;
    res = InitMotionFromGLTF(src, &graph->tpose, &node->motion,
                             node->sample_rate, node->loop, ctx);
    if (HYA_OK != res) {
      LOG_ERROR("InitMotionFromGLTF for %s failed, %d\n", src, res);
      return res;
    }
  }

  // create ptrs to each string in the str_map
  ptrdiff_t n = shlen(ctx->str_map);
  graph->num_str_ptrs = n;
  graph->str_ptrs = (const char **)ContextAllocFromAligned(
      ctx, HYA_MEM_STRING, &graph->str_ptrs, sizeof(char *) * n,
      _Alignof(char *));
  if (!graph->str_ptrs) {
    LOG_ERROR("str_ptrs alloc failed\n");
    return HYA_ERR_OUT_OF_MEMORY;
  }
  for (ptrdiff_t i = 0; i < n; i++) {
    graph->str_ptrs[i] = ContextAllocFromAligned(
        ctx, HYA_MEM_STRING, &graph->str_ptrs[i],
        strlen(ctx->str_map[i].key) + 1, _Alignof(char));
    if (!graph->str_ptrs[i]) {
      LOG_ERROR("str_ptr alloc %td failed\n", i);
      return HYA_ERR_OUT_OF_MEMORY;
    }
    strcpy((char *)graph->str_ptrs[i], ctx->str_map[i].key);
  }

  // create vars
  graph->num_vars = shlen(ctx->var_map);
  graph->vars = (HYA_Var *)ContextAllocFromAligned(
      ctx, HYA_MEM_VAR, &graph->vars, sizeof(HYA_Var) * graph->num_vars,
      _Alignof(HYA_Var));
  if (!graph->vars) {
    fprintf(stderr, "failure allocating %zu HYA_Var*\n", graph->num_vars);
    return HYA_ERR_OUT_OF_MEMORY;
  }
  memset(graph->vars, 0, sizeof(HYA_Var) * graph->num_vars);  // NOLINT
  n = shlen(ctx->var_map);                                    // number of pairs
  for (ptrdiff_t i = 0; i < n; i++) {
    const char *key = ctx->var_map[i].key;

    // find name from string table
    int str_id = shgeti(ctx->str_map, key);
    graph->vars[i].name = str_id;
  }

  return HYA_OK;
}

HYA_Result InitNode(HYA_Node *node, Context *ctx, struct json_value_s *value) {
  JSON_VAL_TO_OBJ(value, object);
  JSON_OBJ_FOR_EACH(object, obj_elem) {
    if (0 == strcmp(obj_elem->name->string, "name")) {
      JSON_VAL_TO_STR(obj_elem->value, s);
      int id = shgeti(ctx->node_map, s->string);
      if (id < 0) {
        LOG_ERROR("unknown node name %s\n", s->string);
        return HYA_ERR_JSON_SCHEMA;
      }
      node->id = id;
      node->name = ContextInternString(ctx, s->string);
    } else if (0 == strcmp(obj_elem->name->string, "type")) {
      JSON_VAL_TO_STR(obj_elem->value, s);
      int id = shgeti(ctx->type_map, s->string);
      if (id < 0) {
        LOG_ERROR("unknown node type %s\n", s->string);
        return HYA_ERR_JSON_SCHEMA;
      }
      node->type = id;
    } else if (0 == strcmp(obj_elem->name->string, "children")) {
      JSON_VAL_TO_ARR(obj_elem->value, a);
      node->num_children = a->length;
      node->children = (HYA_NODE_ID *)ContextAllocFromAligned(
          ctx, HYA_MEM_NODE, &node->children,
          sizeof(HYA_NODE_ID) * node->num_children, _Alignof(HYA_NODE_ID));
      if (!node->children) {
        LOG_ERROR("children allocation failed\n");
        return HYA_ERR_OUT_OF_MEMORY;
      }
      int i = 0;
      JSON_ARR_FOR_EACH(a, arr_elem) {
        JSON_VAL_TO_STR(arr_elem->value, s);
        int id = shgeti(ctx->node_map, s->string);
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

static void PrintState(const HYA_State *state, const HYA_Graph *graph) {
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

void PrintStateMachineNode(const HYA_StateMachineNode *node,
                           const HYA_Graph *graph) {
  printf("    StateMachineNode {\n");
  PrintNode(&node->node, graph);
  printf("      states = [\n");
  for (size_t i = 0; i < node->num_states; i++) {
    PrintState(&node->states[i], graph);
  }
  printf("      ]\n");
  printf("    }\n");
}

static HYA_Result InitTransition(HYA_Transition *transition, Context *ctx,
                                 struct json_value_s *value, int transition_idx,
                                 StrToIdPair **state_map) {
  JSON_VAL_TO_OBJ(value, object);
  JSON_OBJ_FOR_EACH(object, obj_elem) {
    if (0 == strcmp(obj_elem->name->string, "var")) {
      JSON_VAL_TO_STR(obj_elem->value, s);
      int var_id = shgeti(ctx->var_map, s->string);
      if (var_id < 0) {
        shputs(ctx->var_map, (Symbol){s->string});
        var_id = shlen(ctx->var_map) - 1;
      }
      transition->var_id = var_id;
      ContextInternString(ctx, s->string);
    } else if (0 == strcmp(obj_elem->name->string, "dst_state")) {
      JSON_VAL_TO_STR(obj_elem->value, s);
      int state_id = shget(*state_map, s->string);
      if (state_id < 0) {
        LOG_ERROR("could not find dst_state %s\n", s->string);
        return HYA_ERR_JSON_SCHEMA;
      }
      transition->dst_state_idx = state_id;
    } else {
      LOG_JSON_VAL_ERR(obj_elem, "unexpected key %s\n", obj_elem->name->string);
    }
  }
  return HYA_OK;
}

static HYA_Result InitState(HYA_State *state, Context *ctx,
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
      state->name = ContextInternString(ctx, s->string);
    } else if (0 == strcmp(obj_elem->name->string, "interp_time")) {
      JSON_VAL_TO_NUM(obj_elem->value, n);
      double d = JSON_NumberToDouble(n);
      state->interp_time = (float)d;
    } else if (0 == strcmp(obj_elem->name->string, "transitions")) {
      JSON_VAL_TO_ARR(obj_elem->value, a)
      state->num_transitions = a->length;
      state->transitions = (HYA_Transition *)ContextAllocFromAligned(
          ctx, HYA_MEM_NODE, &state->transitions,
          sizeof(HYA_Transition) * a->length, _Alignof(HYA_Transition));
      if (!state->transitions) {
        LOG_ERROR("state->transitions alloc failed\n");
        return HYA_ERR_OUT_OF_MEMORY;
      }
      size_t j = 0;
      JSON_ARR_FOR_EACH(a, arr_elem) {
        res = InitTransition(&state->transitions[j], ctx, arr_elem->value, j,
                             state_map);
        if (res != HYA_OK) {
          LOG_ERROR("InitTransition failed: %d\n", res);
          return HYA_ERR_JSON_SCHEMA;
        }
        j++;
      }
    } else {
      LOG_JSON_VAL_ERR(obj_elem, "InitState unexpected key %s\n",
                       obj_elem->name->string);
    }
  }
  return HYA_OK;
}

HYA_Result InitStateMachineNode(HYA_StateMachineNode *node, Context *ctx,
                                struct json_value_s *value) {
  HYA_Result res;
  memset(node, 0, sizeof(HYA_StateMachineNode));  // NOLINT
  InitNode(&node->node, ctx, value);
  StrToIdPair *state_map = NULL;
  shdefault(state_map, -1);
  JSON_VAL_TO_OBJ(value, object);
  JSON_OBJ_FOR_EACH(object, obj_elem) {
    if (0 == strcmp(obj_elem->name->string, "states")) {
      JSON_VAL_TO_ARR(obj_elem->value, a);
      node->num_states = a->length;
      node->states = (HYA_State *)ContextAllocFromAligned(
          ctx, HYA_MEM_NODE, &node->states, sizeof(HYA_State) * a->length,
          _Alignof(HYA_State));
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
        res = InitState(&node->states[i], ctx, arr_elem->value, i, &state_map);
        if (res != HYA_OK) {
          LOG_ERROR("InitState failed\n");
          return res;
        }
        i++;
      }
    }
  }
  shfree(state_map);
  return HYA_OK;
}

void PrintMotionNode(const HYA_MotionNode *node, const HYA_Graph *graph) {
  printf("    MotionNode {\n");
  PrintNode(&node->node, graph);
  printf("    }\n");
}

HYA_Result InitMotionNode(HYA_MotionNode *node, Context *ctx,
                          struct json_value_s *value) {
  HYA_Result res;
  memset(node, 0, sizeof(HYA_MotionNode));  // NOLINT
  res = InitNode(&node->node, ctx, value);
  if (res != HYA_OK) {
    LOG_ERROR("InitNode failed %d\n", res);
    return res;
  }
  const char *src = NULL;
  bool loop = false;
  float sample_rate = 30.0f;
  JSON_VAL_TO_OBJ(value, object);
  JSON_OBJ_FOR_EACH(object, obj_elem) {
    if (0 == strcmp(obj_elem->name->string, "src")) {
      JSON_VAL_TO_STR(obj_elem->value, s);
      src = s->string;
    }
    if (0 == strcmp(obj_elem->name->string, "loop")) {
      JSON_VAL_TO_BOOL(obj_elem->value, b);
      loop = b;
    }
    if (0 == strcmp(obj_elem->name->string, "sample_rate")) {
      JSON_VAL_TO_NUM(obj_elem->value, n);
      double d = JSON_NumberToDouble(n);
      sample_rate = (float)d;
    }
  }
  if (NULL == src) {
    LOG_ERROR("missing src field\n");
    return HYA_ERR_JSON_SCHEMA;
  }
  node->src = ContextInternString(ctx, src);
  node->sample_rate = sample_rate;
  node->loop = loop;
  return HYA_OK;
}

void PrintBlendNode(const HYA_BlendNode *node, const HYA_Graph *graph) {
  printf("    BlendNode {\n");
  PrintNode(&node->node, graph);
  printf("    }\n");
}

HYA_Result InitBlendNode(HYA_BlendNode *node, Context *ctx,
                         struct json_value_s *value) {
  HYA_Result res;
  memset(node, 0, sizeof(HYA_BlendNode));  // NOLINT
  res = InitNode(&node->node, ctx, value);
  if (res != HYA_OK) {
    LOG_ERROR("InitNode failed %d\n", res);
    return res;
  }
  return HYA_OK;
}
