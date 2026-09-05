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

static bool IsSkeletonSame(cgltf_node **node_arr, const HYA_Skeleton *skeleton,
                           Context *ctx) {
  if (skeleton->num_joints != arrlen(node_arr)) {
    printf("count mismatch %zu, %td\n", skeleton->num_joints, arrlen(node_arr));
    return false;
  }
  for (ptrdiff_t i = 0; i < arrlen(node_arr); i++) {
    if (0 !=
        strcmp(ctx->str_arr[skeleton->joint_names[i]], node_arr[i]->name)) {
      printf("%td idx=%d, %s != %s\n", i, skeleton->joint_names[i],
             ctx->str_arr[skeleton->joint_names[i]], node_arr[i]->name);
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
      ctx, HYA_MEM_SKELETON, sizeof(HYA_STR_ID) * num_nodes,
      sizeof(HYA_STR_ID));
  if (!skeleton->joint_names) {
    LOG_ERROR("out of memory! when allocating joint_names, %zu bytes!\n",
              sizeof(HYA_STR_ID) * num_nodes);
    goto cleanup_2;
  }
  skeleton->parent_indices = (int *)ContextAllocFromAligned(
      ctx, HYA_MEM_SKELETON, sizeof(int) * num_nodes, sizeof(int));
  if (!skeleton->parent_indices) {
    LOG_ERROR("out of memory! when allocating parent_indices, %zu bytes!\n",
              sizeof(int) * num_nodes);
    goto cleanup_2;
  }
  skeleton->xforms = (HYA_Xform *)ContextAllocFromAligned(
      ctx, HYA_MEM_SKELETON, sizeof(HYA_Xform) * num_nodes, sizeof(float));
  if (!skeleton->xforms) {
    LOG_ERROR("out of memory! when allocating xforms, %zu bytes!\n",
              sizeof(HYA_Xform) * num_nodes);
    goto cleanup_2;
  }
  // joint_map will be used to determine parent id.
  StrToIdPair *joint_map = NULL;
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
    if (node->has_matrix) {
      HYA_Vec3 scale;
      Mat4Decompose(node->matrix, &skeleton->xforms[i].t,
                    &skeleton->xforms[i].r, &scale);
      if (fabs(scale.x - scale.y) > kScaleEpsilon ||
          fabs(scale.x - scale.z) > kScaleEpsilon) {
        LOG_WARNING("joint[%td] matrix scale is not uniform\n", i);
      }
      skeleton->xforms[i].s = scale.x;
    } else {
      if (node->has_translation) {
        skeleton->xforms[i].t = (HYA_Vec3){
            node->translation[0], node->translation[1], node->translation[2]};
      } else {
        skeleton->xforms[i].t = (HYA_Vec3){0};
      }
      if (node->has_scale) {
        if (fabs(node->scale[0] - node->scale[1]) > kScaleEpsilon ||
            fabs(node->scale[0] - node->scale[2]) > kScaleEpsilon) {
          LOG_WARNING("node[%td] scale is not uniform\n", i);
        }
        skeleton->xforms[i].s = node->scale[0];
      } else {
        skeleton->xforms[i].s = 1.0f;
      }
      if (node->has_rotation) {
        skeleton->xforms[i].r =
            (HYA_Quat){node->rotation[0], node->rotation[1], node->rotation[2],
                       node->rotation[3]};
      } else {
        skeleton->xforms[i].r = (HYA_Quat){0.0f, 0.0f, 0.0f, 1.0f};
      }
    }
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
  const char *root_joint = ctx->str_arr[skeleton->joint_names[0]];
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

  /*
  printf("AJT: name = %s\n", anim->name);
  printf("AJT: samplers_count = %zu\n", anim->samplers_count);
  for (size_t i = 0; i < anim->samplers_count; i++) {
    printf("AJT:    sampler[%zu]\n", i);
    printf("AJT:        input = %p\n", anim->samplers[i].input);
    PrintAccessor(anim->samplers[i].input);
    printf("AJT:        output = %p\n", anim->samplers[i].output);
    PrintAccessor(anim->samplers[i].output);
    printf("AJT:        interp = %s\n",
           anim->samplers[i].interpolation == 0
               ? "linear"
               : (anim->samplers[i].interpolation == 1 ? "step" : "cubic"));
  }
  printf("AJT: channels_count = %zu\n", anim->channels_count);
  for (size_t i = 0; i < anim->channels_count; i++) {
    printf("AJT:    channel[%zu]\n", i);
    PrintChannel(&anim->channels[i]);
  }
  printf("AJT: extensions_count = %zu\n", anim->extensions_count);
  */

  // alloc samplers & channels
  motion->num_samplers = anim->samplers_count;
  motion->samplers = (HYA_Sampler *)ContextAllocFrom(
      ctx, HYA_MEM_MOTION, sizeof(HYA_Sampler) * motion->num_samplers);
  if (!motion->samplers) {
    LOG_ERROR("Out of memory! allocating samplers, size %zu bytes\n",
              sizeof(HYA_Sampler) * motion->num_samplers);
    res = HYA_ERR_OUT_OF_MEMORY;
    goto cleanup_3;
  }
  motion->num_channels = anim->channels_count;
  motion->channels = (HYA_Channel *)ContextAllocFrom(
      ctx, HYA_MEM_MOTION, sizeof(HYA_Channel) * motion->num_channels);
  if (!motion->channels) {
    LOG_ERROR("Out of memory! allocating channels, size %zu bytes\n",
              sizeof(HYA_Channel) * motion->num_channels);
    res = HYA_ERR_OUT_OF_MEMORY;
    goto cleanup_3;
  }

  // first pass: figure out how many times and values to allocate.
  size_t num_times = 0;
  size_t num_values = 0;
  for (size_t i = 0; i < anim->samplers_count; i++) {
    const cgltf_accessor *in_acc = anim->samplers[i].input;
    if (in_acc->type != cgltf_type_scalar) {
      LOG_ERROR("non scalar input type!\n");
      res = HYA_ERR_UNSUPPORTED;
      goto cleanup_3;
    }
    num_times += cgltf_accessor_unpack_floats(in_acc, NULL, 0);
    const cgltf_accessor *out_acc = anim->samplers[i].output;
    if (out_acc->type != cgltf_type_scalar &&
        out_acc->type != cgltf_type_vec3 && out_acc->type != cgltf_type_vec4) {
      LOG_ERROR("unsupported out type! %d\n", (int)out_acc->type);
      res = HYA_ERR_UNSUPPORTED;
      goto cleanup_3;
    }
    num_values += cgltf_accessor_unpack_floats(out_acc, NULL, 0);
  }

  motion->times =
      (float *)ContextAllocFrom(ctx, HYA_MEM_MOTION, sizeof(float) * num_times);
  if (!motion->times) {
    LOG_ERROR("Out of memory! allocating times size %zu bytes\n",
              sizeof(float) * num_times);
    res = HYA_ERR_OUT_OF_MEMORY;
    goto cleanup_3;
  }
  motion->values = (float *)ContextAllocFrom(ctx, HYA_MEM_MOTION,
                                             sizeof(float) * num_values);
  if (!motion->values) {
    LOG_ERROR("Out of memory! allocating values size %zu bytes\n",
              sizeof(float) * num_values);
    res = HYA_ERR_OUT_OF_MEMORY;
    goto cleanup_3;
  }

  // second pass: copy/unpack times and values.
  size_t times_offset = 0;
  size_t values_offset = 0;
  for (size_t i = 0; i < anim->samplers_count; i++) {
    const cgltf_accessor *in_acc = anim->samplers[i].input;
    size_t times_count = cgltf_accessor_unpack_floats(
        in_acc, motion->times + times_offset, num_times - times_offset);
    const cgltf_accessor *out_acc = anim->samplers[i].output;
    size_t values_count = cgltf_accessor_unpack_floats(
        out_acc, motion->values + values_offset, num_values - values_offset);
    motion->samplers[i].time_idx = times_offset;
    motion->samplers[i].value_idx = values_offset;
    motion->samplers[i].num_keys = times_count;
    motion->samplers[i].type = out_acc->type;
    motion->samplers[i].interp = anim->samplers[i].interpolation;

    times_offset += times_count;
    values_offset += values_count;
  }

  for (size_t i = 0; i < anim->channels_count; i++) {
    motion->channels[i].sampler_idx =
        hmgeti(sampler_to_idx_map, anim->channels[i].sampler);
    if (motion->channels[i].sampler_idx < 0) {
      LOG_ERROR("could not find sampler_idx! in channel %zu\n", i);
      goto cleanup_3;
    }
    motion->channels[i].joint_idx =
        hmgeti(node_to_idx_map, anim->channels[i].target_node);

    if (motion->channels[i].joint_idx < 0) {
      LOG_ERROR("could not find joint_idx! in channel %zu\n", i);
      goto cleanup_3;
    }
    motion->channels[i].path = anim->channels[i].target_path;
  }
  res = HYA_OK;

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
