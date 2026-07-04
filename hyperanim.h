/*
  Copyright (c) 2025 Anthony J. Thibault
  This software is licensed under the MIT License. See LICENSE for more
  details.
*/

#ifndef HYPERANIM_H
#define HYPERANIM_H

#include <stdbool.h>
#include <stdlib.h>

#if defined(HYA_IMPLEMENTATION)
#define HYA_API extern inline  // Provide external definition
#endif

typedef enum HYA_Result {
  HYA_OK = 0,  // success is always zero
  HYA_ERR_FAILURE,
  HYA_ERR_OUT_OF_MEMORY,
  HYA_ERR_INVALID_ARG,
  HYA_ERR_NOT_FOUND,
} HYA_Result;

typedef struct HYA_Vec3 {
  float x, y, z;
} HYA_Vec3;

typedef struct HYA_Quat {
  float x, y, z, w;
} HYA_Quat;

typedef int HYA_STR_ID;

typedef int HYA_VAR_ID;
typedef int HYA_VAR_TYPE;

enum {
  HYA_VAR_TYPE_BOOL = 0,
  HYA_VAR_TYPE_FLOAT,
};

typedef struct HYA_Var {
  HYA_STR_ID name;
  HYA_VAR_TYPE type;
  union {
    bool b;
    float f;
  };
} HYA_Var;

typedef struct HYA_Transition {
  HYA_VAR_ID var_id;
  int dst_state_idx;
} HYA_Transition;

typedef struct HYA_State {
  int state_idx;
  HYA_STR_ID name;
  float interp_time;
  size_t num_transitions;
  HYA_Transition *transitions;
} HYA_State;

typedef int HYA_NODE_ID;
typedef size_t HYA_NODE_TYPE;

typedef struct HYA_Node {
  HYA_NODE_ID id;
  HYA_NODE_TYPE type;
  HYA_STR_ID name;
  size_t num_children;
  HYA_NODE_ID *children;
} HYA_Node;

typedef struct HYA_StateMachineNode {
  HYA_Node node;
  size_t num_states;
  HYA_State *states;
} HYA_StateMachineNode;

enum {
  HYA_MOTION_LOOP = (1 << 0),
};

typedef struct HYA_MotionNode {
  HYA_Node node;
  HYA_Vec3 *translations;  // [num_frames, num_joints]
  HYA_Quat *rotations;     // [num_frames, num_joints]
  float sample_rate;
  size_t num_joints;
  size_t num_frames;
  unsigned int flags;
} HYA_MotionNode;

typedef struct HYA_BlendNode {
  HYA_Node node;
  HYA_VAR_ID alpha_var;
} HYA_BlendNode;

// X(TypeName, field_prefix)
#define HYA_NODE_TYPE_LIST                              \
  X(HYA_StateMachineNode, state_machine, STATE_MACHINE) \
  X(HYA_MotionNode, motion, MOTION)                     \
  X(HYA_BlendNode, blend, BLEND)

enum {
#define X(Type, name, NAME) HYA_NODE_TYPE_##NAME,
  HYA_NODE_TYPE_LIST
#undef X
};

typedef struct HYA_Graph {
  size_t version;
  HYA_NODE_ID root;

#define X(Type, name, NAME)  \
  size_t num_##name##_nodes; \
  Type *name##_nodes;  // NOLINT
  HYA_NODE_TYPE_LIST
#undef X

  // use HYA_NODE_ID to index into this array
  size_t num_node_ptrs;
  HYA_Node **node_ptrs;

  // use HYA_STR_ID to index into this array
  size_t num_str_ptrs;
  const char **str_ptrs;

  // use HYA_VAR_ID to index into this array
  size_t num_vars;
  HYA_Var *vars;

} HYA_Graph;

HYA_Graph *HYA_Load(const char *filename);

#if defined(HYA_IMPLEMENTATION)

HYA_API HYA_Graph *HYA_Load(const char *filename) { return NULL; }

#endif  // defined(HYA_IMPLEMENTATION)
#endif  // HYPERANIM_H
