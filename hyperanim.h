/*
    Copyright (c) 2025 Anthony J. Thibault
    This software is licensed under the MIT License. See LICENSE for more details.
*/

#ifndef HYPERANIM_H
#define HYPERANIM_H

#if defined(HYA_IMPLEMENTATION)
#define HYA_API extern inline // Provide external definition
#endif

typedef struct HYA_Vec3 {
  float x, y, z;
} HYA_Vec3;

typedef struct HYA_Quat {
  float x, y, z, w;
} HYA_Quat;

typedef int HYA_VAR_ID;
typedef int HYA_VAR_TYPE;

enum {
  HYA_VAR_TYPE_BOOL = 0,
  HYA_VAR_TYPE_FLOAT,
};

typedef struct HYA_Var {
  HYA_VAR_TYPE type;
  union {
    bool b;
    float f;
  } data;
};

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
  HYA_Transition* transitions;
} HYA_State;

typedef int HYA_NODE_ID;
typedef int HYA_NODE_TYPE;

enum {
  HYA_NODE_TYPE_STATE_MACHINE = 0,
  HYA_NODE_TYPE_MOTION,
  HYA_NODE_TYPE_BLEND,
};

typedef struct HYA_Node {
  HYA_NODE_ID id;
  HYA_NODE_TYPE type;
  int num_children;
  HYA_NODE_ID* children;
} HYA_Node;


typedef struct HYA_StateMachineNode {
  HYA_Node node;
  int num_states;
  HYA_State* states;
} HYA_StateMachine;

enum {
  HYA_MOTION_LOOP = (1 << 0),
};

typedef struct HYA_MotionNode {
  HYA_Node node;
  HYA_Vec3* translations;  // [num_frames, num_joints]
  HYA_Quat* rotations;  // [num_frames, num_joints]
  float sample_rate;
  int num_joints;
  int num_frames;
  unsigned int flags;
} HYA_MotionNode;

typedef struct HYA_BlendNode {
  HYA_Node node;
  HYA_VAR_ID alpha_var;
} HYA_BlendNode;
