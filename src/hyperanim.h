/*
  Copyright (c) 2026 Anthony J. Thibault
  This software is licensed under the MIT License. See LICENSE for more
  details.
*/

#ifndef HYPERANIM_H
#define HYPERANIM_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

typedef enum HYA_Result {
  HYA_OK = 0,  // success is always zero
  HYA_ERR_FAILURE,
  HYA_ERR_OUT_OF_MEMORY,
  HYA_ERR_JSON_SCHEMA,
  HYA_ERR_UNSUPPORTED,
  HYA_ERR_NOT_FOUND,
  HYA_ERR_NOT_IMPLEMENTED,
  HYA_ERR_SKELETON_MISMATCH,
  HYA_ERR_BAD_ARGS,
  HYA_ERR_FILE,
  HYA_ERR_JSON_PARSE,
  HYA_ERR_BAD_MAGIC,
} HYA_Result;

typedef struct HYA_Vec3 {
  float x, y, z;
} HYA_Vec3;

typedef struct HYA_Quat {
  float x, y, z, w;
} HYA_Quat;

typedef struct HYA_Xform {
  HYA_Vec3 t;  // translation
  float s;     // scale
  HYA_Quat r;  // rotation
} HYA_Xform;

typedef struct HYA_Vec3Key {
  float time;
  HYA_Vec3 v;
} HYA_Vec3Key;

typedef struct HYA_QuatKey {
  float time;
  HYA_Quat q;
} HYA_QuatKey;

typedef struct HYA_FloatKey {
  float time;
  float f;
} HYA_FloatKey;

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

typedef struct HYA_Channel {
  int32_t sampler_idx;
  int32_t joint_idx;
  uint8_t path;  // 1 = translation, 2 = rotation, 3 = scale
} HYA_Channel;

typedef struct HYA_Sampler {
  uint32_t time_idx;
  uint32_t value_idx;
  uint32_t num_keys;
  uint16_t type;   // 1 = scalar, 2 = vec2, 3 = vec3, 4 = vec4
  uint8_t interp;  // 0 = step, 1 = linear, 2 = cubic_spline
} HYA_Sampler;

typedef struct HYA_Motion {
  float *times;
  float *values;
  HYA_Sampler *samplers;
  HYA_Channel *channels;
  size_t num_samplers;
  size_t num_channels;
  unsigned int flags;
} HYA_Motion;

typedef struct HYA_MotionNode {
  HYA_Node node;
  HYA_STR_ID src;
  float sample_rate;
  bool loop;
  HYA_Motion motion;
} HYA_MotionNode;

typedef struct HYA_BlendNode {
  HYA_Node node;
  HYA_VAR_ID alpha_var;
} HYA_BlendNode;

// X(CamelCaseName, snake_case_name, SCREAMING_SNAKE_CASE_NAME)
#define HYA_NODE_NAME_LIST                      \
  X(StateMachine, state_machine, STATE_MACHINE) \
  X(Motion, motion, MOTION)                     \
  X(Blend, blend, BLEND)

enum {
#define X(Name, name, NAME) HYA_NODE_TYPE_##NAME,
  HYA_NODE_NAME_LIST
#undef X
};

typedef struct HYA_Skeleton {
  HYA_STR_ID *joint_names;
  int *parent_indices;
  HYA_Xform *xforms;
  size_t num_joints;
} HYA_Skeleton;

typedef struct HYA_Graph {
  size_t version;
  HYA_NODE_ID root;

  // use HYA_NODE_ID to index into this array
  size_t num_node_ptrs;
  HYA_Node **node_ptrs;

  // use HYA_STR_ID to index into this array
  size_t num_str_ptrs;
  const char **str_ptrs;

  // use HYA_VAR_ID to index into this array
  size_t num_vars;
  HYA_Var *vars;

  HYA_Skeleton tpose;
  HYA_STR_ID root_joint;

} HYA_Graph;

#endif  // HYPERANIM_H

#ifdef HYA_IMPLEMENTATION

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

static uint8_t *ReadFile(const char *filename, size_t *out_size) {
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

  uint8_t *buf = malloc(size + 1);  // +1 for optional NUL terminator
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

/*
HYAGRAPH
num_offsets  size_t
offset 0
offset 1
...
offset n-1
num_offsets  size_t
HYA_Graph
*/
HYA_Result HYA_GraphCreate(HYA_Graph **graph, const char *filename) {
  HYA_Result res = HYA_ERR_FAILURE;
  size_t buf_size = 0;
  uint8_t *buf = ReadFile(filename, &buf_size);
  if (!buf) {
    printf("ERROR: loading %s\n", filename);
    res = HYA_ERR_FILE;
    goto cleanup_0;
  }

  if (buf[0] != 'H' || buf[1] != 'Y' || buf[2] != 'A' || buf[3] != 'G' ||
      buf[4] != 'R' || buf[5] != 'A' || buf[6] != 'P' || buf[7] != 'H') {
    res = HYA_ERR_BAD_MAGIC;
    goto cleanup_1;
  }

  uint8_t *p = buf + 8;
  size_t num_offsets = *(size_t *)p;
  uint8_t *base = p + sizeof(size_t) * (num_offsets + 2);
  p += sizeof(size_t);
  for (size_t i = 0; i < num_offsets; i++) {
    size_t offset = *(size_t *)p;
    printf("AJT: offset[%zu] = %zu\n", i, offset);
    p += sizeof(size_t);
    unsigned long *ptr = (unsigned long *)(base + offset);
    *ptr = (unsigned long)(base + *ptr);
  }

  *graph = (HYA_Graph *)base;

  // on success: don't free buf
  return HYA_OK;

cleanup_1:
  free(buf);
cleanup_0:

  return res;
}

void HYA_GraphFree(HYA_Graph *graph) {
  size_t *p = (size_t *)graph;
  size_t num_offsets = *(p - 1);
  uint8_t *buf = (uint8_t *)(p - (num_offsets + 3));
  assert(buf[0] == 'H' && buf[1] == 'Y' && buf[2] == 'A' && buf[3] == 'G');
  free(buf);
}

#endif  // HYA_IMPLEMENTATION
