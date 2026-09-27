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

typedef struct StrToIntPair {
  const char *key;
  int value;
} StrToIntPair;

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

static cgltf_node *FindNode(cgltf_node *node, const char *name) {
  if (0 == strcmp(node->name, name)) {
    return node;
  }
  for (cgltf_size i = 0; i < node->children_count; i++) {
    cgltf_node *result = FindNode(node->children[i], name);
    if (result) {
      return result;
    }
  }
  return NULL;
}

// traverse gltf_node hierarchy recursively pushing each node
// in pre-order, such that parent's are aways before children.
static void BuildNodeArr(cgltf_node *node, cgltf_node ***node_arr) {
  arrpush(*node_arr, node);
  for (cgltf_size i = 0; i < node->children_count; i++) {
    BuildNodeArr(node->children[i], node_arr);
  }
}

static void GetRelTransform(const cgltf_node *node, HYA_Vec3 *t, HYA_Quat *r,
                            HYA_Vec3 *s) {
  if (node->has_matrix) {
    Mat4Decompose(node->matrix, t, r, s);
  } else {
    if (node->has_translation) {
      *t = (HYA_Vec3){node->translation[0], node->translation[1],
                      node->translation[2]};
    } else {
      *t = (HYA_Vec3){0};
    }
    if (node->has_scale) {
      *s = (HYA_Vec3){node->scale[0], node->scale[1], node->scale[2]};
    } else {
      *s = (HYA_Vec3){1.0f, 1.0f, 1.0f};
    }
    if (node->has_rotation) {
      *r = (HYA_Quat){node->rotation[0], node->rotation[1], node->rotation[2],
                      node->rotation[3]};
    } else {
      *r = (HYA_Quat){0.0f, 0.0f, 0.0f, 1.0f};
    }
  }
}

static void GetAbsTransform(const cgltf_node *node, HYA_Vec3 *t, HYA_Quat *r,
                            HYA_Vec3 *s) {
  float m[16];
  float tmp_m[16];
  Mat4Ident(m);
  HYA_Vec3 tmp_t, tmp_s;
  HYA_Quat tmp_r;
  const cgltf_node *n = node;
  while (n) {
    GetRelTransform(n, &tmp_t, &tmp_r, &tmp_s);
    Mat4Make(tmp_m, tmp_t, tmp_r, tmp_s);
    Mat4Mul(m, tmp_m, m);
    n = n->parent;
  }
  Mat4Decompose(m, t, r, s);
}

static bool IsSkeletonSame(cgltf_node **node_arr, const HYA_Skeleton *skeleton,
                           Context *ctx) {
  if (skeleton->num_joints != arrlen(node_arr)) {
    printf("count mismatch %d, %td\n", skeleton->num_joints, arrlen(node_arr));
    return false;
  }
  for (ptrdiff_t i = 0; i < arrlen(node_arr); i++) {
    if (0 !=
        strcmp(ctx->str_map[skeleton->joint_names[i]].key, node_arr[i]->name)) {
      printf("%td idx=%d, %s != %s\n", i, skeleton->joint_names[i],
             ctx->str_map[skeleton->joint_names[i]].key, node_arr[i]->name);
      return false;
    }
  }
  return true;
}

static void PrintAccessor(const cgltf_accessor *acc) {
  printf("AJT:            name = %s\n", acc->name);
  printf("AJT:            component_type = %d\n", acc->component_type);
  printf("AJT:            normalized = %d\n", acc->normalized);
  printf("AJT:            type = %d\n", acc->type);
  printf("AJT:            offset = %zu\n", acc->offset);
  printf("AJT:            count = %zu\n", acc->count);
  printf("AJT:            stride = %zu\n", acc->stride);
}

static void PrintChannel(const cgltf_animation_channel *channel) {
  printf("AJT:        sampler = %p\n", channel->sampler);
  printf("AJT:        target_node = %s\n", channel->target_node->name);
  const char *str = NULL;
  switch (channel->target_path) {
    default:
    case cgltf_animation_path_type_invalid:
      str = "invalid";
      break;
    case cgltf_animation_path_type_translation:
      str = "translation";
      break;
    case cgltf_animation_path_type_rotation:
      str = "rotation";
      break;
    case cgltf_animation_path_type_scale:
      str = "scale";
      break;
    case cgltf_animation_path_type_weights:
      str = "weights";
      break;
  }
  printf("AJT:        target_path = %s\n", str);
}

HYA_Result InitSkeletonFromGLTF(const char *filename, const char *root_joint,
                                HYA_Skeleton *skeleton, Context *ctx) {
  HYA_Result res = HYA_ERR_FAILURE;
  cgltf_options options = {0};
  cgltf_data *data = NULL;

  // full = ctx->dirname / filename
  char full[1024];
  int n = snprintf(full, sizeof full, "%s/%s", ctx->dirname, filename);
  if (n < 0 || (size_t)n >= sizeof full) {
    LOG_ERROR("path too long: %s/%s\n", ctx->dirname, filename);
    goto cleanup_0;
  }

  // load the actual gltf
  cgltf_result result = cgltf_parse_file(&options, full, &data);
  if (result != cgltf_result_success) {
    LOG_ERROR("gltf_parse_file failed!\n");
    goto cleanup_0;
  }
  if (!data->nodes || data->nodes_count == 0) {
    LOG_ERROR("gltf has no nodes!\n");
    goto cleanup_1;
  }
  if (!data->scene) {
    LOG_ERROR("gltf has no scene!\n");
    goto cleanup_1;
  }
  if (data->scene->nodes_count == 0) {
    LOG_ERROR("gltf scene has no nodes!\n");
    goto cleanup_1;
  }

  // build node_arr from root_joint
  cgltf_node **node_arr = NULL;
  cgltf_node *root_node = FindNode(data->scene->nodes[0], root_joint);
  if (!root_node) {
    LOG_ERROR("could not find root_joint %s in scene\n", root_joint);
    goto cleanup_1;
  }
  BuildNodeArr(root_node, &node_arr);
  ptrdiff_t num_nodes = arrlen(node_arr);

  // allocate skeleton arrays
  skeleton->num_joints = num_nodes;
  skeleton->joint_names = (HYA_STR_ID *)ContextAllocFromAligned(
      ctx, HYA_MEM_SKELETON, &skeleton->joint_names,
      sizeof(HYA_STR_ID) * num_nodes, _Alignof(HYA_STR_ID));
  if (!skeleton->joint_names) {
    LOG_ERROR("out of memory! when allocating joint_names, %zu bytes!\n",
              sizeof(HYA_STR_ID) * num_nodes);
    goto cleanup_2;
  }
  skeleton->parent_indices = (int *)ContextAllocFromAligned(
      ctx, HYA_MEM_SKELETON, &skeleton->parent_indices, sizeof(int) * num_nodes,
      _Alignof(int));
  if (!skeleton->parent_indices) {
    LOG_ERROR("out of memory! when allocating parent_indices, %zu bytes!\n",
              sizeof(int) * num_nodes);
    goto cleanup_2;
  }
  skeleton->xforms = (HYA_Xform *)ContextAllocFromAligned(
      ctx, HYA_MEM_SKELETON, &skeleton->xforms, sizeof(HYA_Xform) * num_nodes,
      _Alignof(HYA_Xform));
  if (!skeleton->xforms) {
    LOG_ERROR("out of memory! when allocating xforms, %zu bytes!\n",
              sizeof(HYA_Xform) * num_nodes);
    goto cleanup_2;
  }
  // joint_map will be used to determine parent id.
  StrToIntPair *joint_map = NULL;
  shdefault(joint_map, -1);

  // iterate over node_arr and init joint_names & xforms
  for (ptrdiff_t i = 0; i < num_nodes; i++) {
    const cgltf_node *node = node_arr[i];
    skeleton->joint_names[i] = ContextInternString(ctx, node->name);
    int id = shget(joint_map, node->name);
    if (id >= 0) {
      LOG_ERROR("duplicate node name %s\n", node->name);
      goto cleanup_3;
    }
    shput(joint_map, node->name, i);

    HYA_Vec3 t, s;
    HYA_Quat r;
    if (node == root_node) {
      GetAbsTransform(node->parent, &t, &r, &s);
      skeleton->root_xform.t = t;
      skeleton->root_xform.r = r;
      skeleton->root_xform.s = s.x;
      GetRelTransform(node, &t, &r, &s);
    } else {
      GetRelTransform(node, &t, &r, &s);
    }

    skeleton->xforms[i].t = t;
    skeleton->xforms[i].r = r;
    if (fabs(s.x - s.y) > kScaleEpsilon || fabs(s.x - s.z) > kScaleEpsilon) {
      LOG_WARNING("joint[%td] matrix scale is not uniform\n", i);
    }
    skeleton->xforms[i].s = s.x;
  }

  // use joint_map to init parent_indices
  for (ptrdiff_t i = 0; i < num_nodes; i++) {
    if (node_arr[i]->parent) {
      int id = shget(joint_map, node_arr[i]->parent->name);
      skeleton->parent_indices[i] = id;
    } else {
      skeleton->parent_indices[i] = -1;
    }
  }
  res = HYA_OK;

  // cleanup
cleanup_3:
  shfree(joint_map);
cleanup_2:
  arrfree(node_arr);
cleanup_1:
  cgltf_free(data);
cleanup_0:

  return res;
}

HYA_Result InitMotionFromGLTF(const char *filename, HYA_Skeleton *skeleton,
                              HYA_Motion *motion, float sample_rate, bool loop,
                              Context *ctx) {
  cgltf_options options = {0};
  cgltf_data *data = NULL;
  HYA_Result res = HYA_ERR_FAILURE;

  bool debug = 0 == strcmp(filename, "anim/run.glb");

  char full[1024];
  int n = snprintf(full, sizeof full, "%s/%s", ctx->dirname, filename);
  if (n < 0 || (size_t)n >= sizeof full) {
    /* truncated (or encoding error) — don't call Load with a mangled path */
    LOG_ERROR("path too long: %s/%s\n", ctx->dirname, filename);
    goto cleanup_0;
  }

  // load the gltf
  cgltf_result result = cgltf_parse_file(&options, full, &data);
  if (result != cgltf_result_success) {
    LOG_ERROR("gltf_parse_file failed!\n");
    goto cleanup_0;
  }

  // load all the buffers which command
  result = cgltf_load_buffers(&options, data, full);
  if (result != cgltf_result_success) {
    LOG_ERROR("gltf_load_buffers failed!\n");
    goto cleanup_1;
  }

  // check animation count
  if (data->animations_count == 0) {
    LOG_ERROR("no animations found in gltf %s\n", full);
    goto cleanup_1;
  }
  if (data->animations_count != 1) {
    LOG_WARNING("more then one animaiton found in gltf %s, using the first\n",
                full);
  }
  if (skeleton->num_joints == 0) {
    LOG_ERROR("skeleton has zero joints\n");
    goto cleanup_1;
  }
  if (!data->nodes || data->nodes_count == 0) {
    LOG_ERROR("gltf has no nodes!\n");
    goto cleanup_1;
  }
  if (!data->scene) {
    LOG_ERROR("gltf has no scene!\n");
    goto cleanup_1;
  }
  if (data->scene->nodes_count == 0) {
    LOG_ERROR("gltf scene has no nodes!\n");
    goto cleanup_1;
  }

  // build node_arr from root_joint
  const char *root_joint = ctx->str_map[skeleton->joint_names[0]].key;
  cgltf_node **node_arr = NULL;
  cgltf_node *root_node = FindNode(data->scene->nodes[0], root_joint);
  if (!root_node) {
    LOG_ERROR("could not find root_joint %s in scene\n", root_joint);
    goto cleanup_1;
  }
  BuildNodeArr(root_node, &node_arr);
  ptrdiff_t num_nodes = arrlen(node_arr);

  if (!IsSkeletonSame(node_arr, skeleton, ctx)) {
    res = HYA_ERR_SKELETON_MISMATCH;
    goto cleanup_2;
  }

  // build a map from cgltf_node* to an index.
  typedef struct NodePair {
    cgltf_node *key;
    int32_t value;
  } NodePair;
  NodePair *node_to_idx_map = NULL;
  for (ptrdiff_t i = 0; i < arrlen(node_arr); i++) {
    hmput(node_to_idx_map, node_arr[i], i);
  }

  // pick the first animation
  const cgltf_animation *anim = &data->animations[0];
  assert(anim);

  // build a map from cgltf_sampler* to an index.
  typedef struct SamplerPair {
    cgltf_animation_sampler *key;
    int32_t value;
  } SamplerPair;
  SamplerPair *sampler_to_idx_map = NULL;
  for (size_t i = 0; i < anim->samplers_count; i++) {
    hmput(sampler_to_idx_map, anim->samplers + i, i);
  }

  // first pass: figure out how many times and values to allocate.
  size_t max_num_times = 0;
  size_t max_num_values = 0;
  for (size_t i = 0; i < anim->samplers_count; i++) {
    const cgltf_accessor *in_acc = anim->samplers[i].input;
    if (in_acc->type != cgltf_type_scalar) {
      LOG_ERROR("non scalar input type!\n");
      res = HYA_ERR_UNSUPPORTED;
      goto cleanup_3;
    }
    size_t num_times = cgltf_accessor_unpack_floats(in_acc, NULL, 0);
    if (num_times > max_num_times) {
      max_num_times = num_times;
    }

    const cgltf_accessor *out_acc = anim->samplers[i].output;
    if (out_acc->type != cgltf_type_scalar &&
        out_acc->type != cgltf_type_vec3 && out_acc->type != cgltf_type_vec4) {
      LOG_ERROR("unsupported out type! %d\n", (int)out_acc->type);
      res = HYA_ERR_UNSUPPORTED;
      goto cleanup_3;
    }
    size_t num_values = cgltf_accessor_unpack_floats(out_acc, NULL, 0);
    if (num_values > max_num_values) {
      max_num_values = num_values;
    }
  }

  float *times = (float *)malloc(sizeof(float) * max_num_times);
  if (!times) {
    LOG_ERROR("failed to allocate %zu times\n", max_num_times);
    res = HYA_ERR_OUT_OF_MEMORY;
    goto cleanup_4;
  }

  float *values = (float *)malloc(sizeof(float) * max_num_values);
  if (!values) {
    LOG_ERROR("failed to allocate %zu values\n", max_num_values);
    res = HYA_ERR_OUT_OF_MEMORY;
    goto cleanup_4;
  }

  // second pass: figure out min_time & max_time
  float min_time = FLT_MAX;
  float max_time = -FLT_MAX;
  for (size_t i = 0; i < anim->samplers_count; i++) {
    const cgltf_accessor *in_acc = anim->samplers[i].input;
    if (in_acc->type != cgltf_type_scalar) {
      LOG_ERROR("non scalar input type!\n");
      res = HYA_ERR_UNSUPPORTED;
      goto cleanup_4;
    }
    if (in_acc->has_min && in_acc->has_max) {
      if (in_acc->min[0] < min_time) {
        min_time = in_acc->min[0];
      }
      if (in_acc->max[0] > max_time) {
        max_time = in_acc->max[0];
      }
    } else {
      // iterate over every time.
      size_t times_count =
          cgltf_accessor_unpack_floats(in_acc, times, max_num_times);
      for (size_t j = 0; j < times_count; j++) {
        if (times[j] < min_time) {
          min_time = times[j];
        }
        if (times[j] > max_time) {
          max_time = times[j];
        }
      }
    }
  }

  size_t num_keys = ((max_time - min_time) * sample_rate) + 1;

  motion->num_keys = num_keys;
  motion->num_joints = num_nodes;
  motion->sample_rate = sample_rate;

  if (num_keys == 0) {
    motion->t_keys = NULL;
    motion->r_keys = NULL;
    motion->s_keys = NULL;
    res = HYA_OK;
    goto cleanup_4;
  }

  // allocate keys
  motion->t_keys = (HYA_Vec3 *)ContextAllocFromAligned(
      ctx, HYA_MEM_MOTION, &motion->t_keys,
      sizeof(HYA_Vec3) * num_keys * num_nodes, _Alignof(HYA_Vec3));
  motion->r_keys = (HYA_Quat *)ContextAllocFromAligned(
      ctx, HYA_MEM_MOTION, &motion->r_keys,
      sizeof(HYA_Quat) * num_keys * num_nodes, _Alignof(HYA_Quat));
  motion->s_keys = (float *)ContextAllocFromAligned(
      ctx, HYA_MEM_MOTION, &motion->s_keys,
      sizeof(float) * num_keys * num_nodes, _Alignof(float));

  // initialize keys with tpose.
  for (size_t i = 0; i < motion->num_keys; i++) {
    for (size_t j = 0; j < motion->num_joints; j++) {
      size_t ii = i * motion->num_joints + j;
      motion->t_keys[ii] = skeleton->xforms[j].t;
      motion->r_keys[ii] = skeleton->xforms[j].r;
      motion->s_keys[ii] = skeleton->xforms[j].s;
    }
  }

  // fill in the keys.
  for (size_t i = 0; i < anim->channels_count; i++) {
    const cgltf_animation_channel *channel = anim->channels + i;
    const cgltf_accessor *in_acc = channel->sampler->input;
    const cgltf_accessor *out_acc = channel->sampler->output;
    if (!channel->target_node) {
      LOG_WARNING("channel %zu, has no target node, skipping\n", i);
      continue;
    }
    int joint_idx = hmgeti(node_to_idx_map, channel->target_node);
    if (joint_idx < 0) {
      LOG_WARNING("could not find idx for node %s, skipping!\n",
                  channel->target_node->name);
      continue;
    }
    // validate out_acc type
    switch (channel->target_path) {
      case cgltf_animation_path_type_translation:
        if (out_acc->type != cgltf_type_vec3) {
          LOG_WARNING(
              "unsupported accessor type for translation! %d, skipping!\n",
              (int)out_acc->type);
          continue;
        }
        break;
      case cgltf_animation_path_type_rotation:
        if (out_acc->type != cgltf_type_vec4) {
          LOG_WARNING("unsupported accessor type for rotation! %d, skipping!\n",
                      (int)out_acc->type);
          continue;
        }
        break;
      case cgltf_animation_path_type_scale:
        if (out_acc->type != cgltf_type_scalar &&
            out_acc->type != cgltf_type_vec3) {
          LOG_WARNING("unsupported accessor type for scale! %d, skipping!\n",
                      (int)out_acc->type);
          continue;
        }
        break;
      default:
        // just skip this channel
        LOG_WARNING("unsupported out target_path! %d, skipping!\n",
                    (int)channel->target_path);
        continue;
        break;
    }
    size_t times_count =
        cgltf_accessor_unpack_floats(in_acc, times, max_num_times);
    size_t values_count =
        cgltf_accessor_unpack_floats(out_acc, values, max_num_values);
    size_t k = 0;
    for (size_t j = 0; j < num_keys; j++) {
      size_t jj = j * num_nodes + joint_idx;
      float t = (j / sample_rate) + min_time;
      while (times[k] <= t && k < times_count - 1) {
        k++;
      }
      size_t curr = k - 1 >= 0 ? k - 1 : k;
      size_t next = k;
      float alpha;
      if (k == times_count - 1) {
        alpha = 0.0f;
      } else {
        alpha = (t - times[curr]) / (times[next] - times[curr]);
      }
      switch (channel->target_path) {
        case cgltf_animation_path_type_translation:
          motion->t_keys[jj] = Vec3Lerp(*(((HYA_Vec3 *)values) + curr),
                                        *(((HYA_Vec3 *)values) + next), alpha);
          break;
        case cgltf_animation_path_type_rotation:
          motion->r_keys[jj] = QuatLerp(*(((HYA_Quat *)values) + curr),
                                        *(((HYA_Quat *)values) + next), alpha);
          break;
        case cgltf_animation_path_type_scale:
          if (out_acc->type != cgltf_type_scalar) {
            motion->s_keys[jj] = FloatLerp(values[curr], values[next], alpha);
          } else {
            motion->s_keys[jj] =
                FloatLerp(values[curr * 3], values[next * 3], alpha);
          }
          break;
        default:
          break;
      }
    }
  }
  res = HYA_OK;

cleanup_4:
  free(values);
  free(times);
cleanup_3:
  hmfree(node_to_idx_map);
  hmfree(sampler_to_idx_map);
cleanup_2:
  arrfree(node_arr);
cleanup_1:
  cgltf_free(data);
cleanup_0:
  return res;
}
