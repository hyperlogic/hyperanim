/*
  Copyright (c) 2026 Anthony J. Thibault
  This software is licensed under the MIT License. See LICENSE for more
  details.
*/

#ifndef FLYCAM_H
#define FLYCAM_H

#include <raylib.h>

typedef struct FlyCam {
  float lin_speed;
  float rot_speed;
  Vector3 up;
  Vector3 position;
  Vector3 target;
  Vector3 velocity;
} FlyCam;

void FlyCamProcess(FlyCam* flycam, Vector2 left_stick, Vector2 right_stick,
                   float roll_amount, float up_amount, float dt);

#endif  // FLYCAM_H
