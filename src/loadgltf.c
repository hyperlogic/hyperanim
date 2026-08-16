/*
  Copyright (c) 2025 Anthony J. Thibault
  This software is licensed under the MIT License. See LICENSE for more
  details.
*/
#include "loadgltf.h"

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"
#include "stb_ds.h"

#include "mathutil.h"

#define LOG_ERROR(fmt, ...) \
  fprintf(stderr, "ERROR: %s " fmt, __func__, ##__VA_ARGS__)

#define LOG_WARNING(fmt, ...) \
  fprintf(stderr, "WARNING: %s " fmt, __func__, ##__VA_ARGS__)

#define kScaleEpsilon 0.001f

static void PrintNode(cgltf_node *node, int indent_level) {
  for (int i = 0; i < indent_level; i++) printf("  ");
  printf("%s\n", node->name);
  for (cgltf_size i = 0; i < node->children_count; i++) {
    PrintNode(node->children[i], indent_level + 1);
  }
}

static void TraverseNodes(cgltf_node *node, const char *root_joint,
                          cgltf_node ***joints, bool *push) {
  bool is_root = 0 == strcmp(node->name, root_joint);
  if (is_root) {
    *push = true;
  }
  if (*push) {
    arrpush(*joints, node);
  }
  for (cgltf_size i = 0; i < node->children_count; i++) {
    TraverseNodes(node->children[i], root_joint, joints, push);
  }
  if (is_root) {
    *push = false;
  }
}

HYA_Result InitSkeletonFromGLTF(const char *filename, const char *root_joint,
                                HYA_Skeleton *skeleton, Context *ctx) {
  cgltf_options options = {0};
  cgltf_data *data = NULL;

  char full[1024];
  int n = snprintf(full, sizeof full, "%s/%s", ctx->dirname, filename);
  if (n < 0 || (size_t)n >= sizeof full) {
    /* truncated (or encoding error) — don't call Load with a mangled path */
    LOG_ERROR("path too long: %s/%s\n", ctx->dirname, filename);
    return HYA_ERR_FAILURE;
  }

  cgltf_result result = cgltf_parse_file(&options, full, &data);
  if (result != cgltf_result_success) {
    LOG_ERROR("gltf_parse_file failed!\n");
    return HYA_ERR_FAILURE;
  }

  cgltf_node **joints = NULL;
  bool push = false;
  TraverseNodes(data->scene->nodes[0], root_joint, &joints, &push);
  ptrdiff_t num_joints = arrlen(joints);
  printf("num_joints = %td\n", num_joints);

  skeleton->num_joints = num_joints;
  skeleton->joint_names = (HYA_STR_ID *)ArenaAllocFromAligned(
      ctx->arena, sizeof(HYA_STR_ID) * num_joints, sizeof(HYA_STR_ID));
  skeleton->parent_indices = (int *)ArenaAllocFromAligned(
      ctx->arena, sizeof(int) * num_joints, sizeof(int));
  skeleton->xforms = (HYA_Xform *)ArenaAllocFromAligned(
      ctx->arena, sizeof(HYA_Xform) * num_joints, sizeof(float));

  StrToIdPair *joint_map = NULL;
  shdefault(joint_map, -1);
  for (ptrdiff_t i = 0; i < num_joints; i++) {
    const cgltf_node *joint = joints[i];
    skeleton->joint_names[i] = ContextInternString(ctx, joint->name);
    int id = shget(joint_map, joint->name);
    if (id >= 0) {
      LOG_ERROR("duplicate node name %s\n", joint->name);
      return HYA_ERR_FAILURE;
    }
    shput(joint_map, joint->name, i);
    if (joint->has_matrix) {
      HYA_Vec3 scale;
      Mat4Decompose(joint->matrix, &skeleton->xforms[i].t,
                    &skeleton->xforms[i].r, &scale);
      if (fabs(scale.x - scale.y) > kScaleEpsilon ||
          fabs(scale.x - scale.z) > kScaleEpsilon) {
        LOG_WARNING("joint[%td] matrix scale is not uniform\n", i);
      }
      skeleton->xforms[i].s = scale.x;
    } else {
      if (joint->has_translation) {
        skeleton->xforms[i].t =
            (HYA_Vec3){joint->translation[0], joint->translation[1],
                       joint->translation[2]};
      } else {
        skeleton->xforms[i].t = (HYA_Vec3){0};
      }
      if (joint->has_scale) {
        if (fabs(joint->scale[0] - joint->scale[1]) > kScaleEpsilon ||
            fabs(joint->scale[0] - joint->scale[2]) > kScaleEpsilon) {
          LOG_WARNING("joint[%td] scale is not uniform\n", i);
        }
        skeleton->xforms[i].s = joint->scale[0];
      } else {
        skeleton->xforms[i].s = 1.0f;
      }
      if (joint->has_rotation) {
        skeleton->xforms[i].r =
            (HYA_Quat){joint->rotation[0], joint->rotation[1],
                       joint->rotation[2], joint->rotation[3]};
      } else {
        skeleton->xforms[i].r = (HYA_Quat){0.0f, 0.0f, 0.0f, 1.0f};
      }
    }
  }

  for (ptrdiff_t i = 0; i < num_joints; i++) {
    if (joints[i]->parent) {
      int id = shget(joint_map, joints[i]->parent->name);
      skeleton->parent_indices[i] = id;
    } else {
      skeleton->parent_indices[i] = -1;
    }
  }

  shfree(joint_map);
  arrfree(joints);
  cgltf_free(data);

  return HYA_OK;
}

HYA_Result InitMotionFromGLTF(const char *filename, HYA_Skeleton *skeleton,
                              HYA_Motion *motion, float sample_rate, bool loop,
                              Context *ctx) {
  cgltf_options options = {0};
  cgltf_data *data = NULL;

  char full[1024];
  int n = snprintf(full, sizeof full, "%s/%s", ctx->dirname, filename);
  if (n < 0 || (size_t)n >= sizeof full) {
    /* truncated (or encoding error) — don't call Load with a mangled path */
    LOG_ERROR("path too long: %s/%s\n", ctx->dirname, filename);
    return HYA_ERR_FAILURE;
  }

  cgltf_result result = cgltf_parse_file(&options, full, &data);
  if (result != cgltf_result_success) {
    LOG_ERROR("gltf_parse_file failed!\n");
    return HYA_ERR_FAILURE;
  }

  cgltf_node **joints = NULL;
  bool push = false;
  if (skeleton->num_joints == 0) {
    LOG_ERROR("skeleton has zero joints\n");
    return HYA_ERR_FAILURE;
  }
  const char *root_joint = ctx->str_arr[skeleton->joint_names[0]];
  TraverseNodes(data->scene->nodes[0], root_joint, &joints, &push);
  ptrdiff_t num_joints = arrlen(joints);
  printf("num_joints = %td\n", num_joints);

  return HYA_ERR_NOT_IMPLEMENTED;
}
