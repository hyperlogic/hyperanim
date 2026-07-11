/*
  Copyright (c) 2025 Anthony J. Thibault
  This software is licensed under the MIT License. See LICENSE for more
  details.
*/

#ifndef GLTF_H
#define GLTF_H

#include "context.h"
#include "hyperanim.h"

HYA_Result InitSkeletonFromGLTF(const char *filename, HYA_STR_ID root_joint_id,
                                HYA_Skeleton *skeleton, Context *ctx);
HYA_Result InitMotionFromGLTF(const char *filename, HYA_MotionNode *motion,
                              Context *ctx);

#endif  // #define GLTF_H
