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

typedef struct HYA_Vec3 {
  float x, y, z;
} HYA_Vec3;

typedef struct HYA_Quat {
  float x, y, z, w;
} HYA_Quat;

typedef size_t HYA_VAR_ID;
typedef size_t HYA_VAR_TYPE;

enum {
  HYA_VAR_TYPE_BOOL = 0,
  HYA_VAR_TYPE_FLOAT,
};

typedef struct HYA_Var {
  HYA_VAR_TYPE type;
  union {
    bool b;
    float f;
  };
} HYA_Var;

typedef struct HYA_Condition {
  HYA_VAR_ID var;
  bool negate;
} HYA_Condition;

typedef struct HYA_Transition {
  HYA_Condition condition;
  int dst_state_idx;
} HYA_Transition;

typedef struct HYA_State {
  int state_idx;
  float interp_time;
  int num_transitions;
  HYA_Transition *transitions;
} HYA_State;

typedef size_t HYA_NODE_ID;
typedef size_t HYA_NODE_TYPE;

typedef struct HYA_Node {
  HYA_NODE_ID id;
  HYA_NODE_TYPE type;
  int num_children;
  HYA_NODE_ID *children;
} HYA_Node;

typedef struct HYA_StateMachineNode {
  HYA_Node node;
  int num_states;
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
  int num_joints;
  int num_frames;
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
  int version;
  HYA_NODE_ID root;

#define X(Type, name, NAME)  \
  size_t num_##name##_nodes; \
  Type *name##_nodes;  // NOLINT
  HYA_NODE_TYPE_LIST
#undef X
} HYA_Graph;

HYA_Graph *HYA_Load(const char *filename);

#if defined(HYA_IMPLEMENTATION)

HYA_API HYA_Graph *HYA_Load(const char *filename) { return NULL; }

#endif  // defined(HYA_IMPLEMENTATION)
#endif  // HYPERANIM_H
