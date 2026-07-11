/*
  Copyright (c) 2025 Anthony J. Thibault
  This software is licensed under the MIT License. See LICENSE for more
  details.
*/
#include "loadgltf.h"

#include <assert.h>
#include <stdio.h>

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#define LOG_ERROR(fmt, ...) \
  fprintf(stderr, "ERROR: %s " fmt, __func__, ##__VA_ARGS__)

static void PrintNode(cgltf_node *node, int indent_level) {
  for (int i = 0; i < indent_level; i++) printf("  ");
  printf("%s\n", node->name);
  for (cgltf_size i = 0; i < node->children_count; i++) {
    PrintNode(node->children[i], indent_level + 1);
  }
}

HYA_Result InitSkeletonFromGLTF(const char *filename, HYA_Skeleton *skeleton,
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

  PrintNode(data->scene->nodes[0], 0);

  cgltf_free(data);

  return HYA_OK;
}

HYA_Result InitMotionFromGLTF(const char *filename, HYA_MotionNode *motion,
                              Context *ctx) {
  return HYA_ERR_FAILURE;  // not implemented
}
