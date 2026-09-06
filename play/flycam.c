/*
  Copyright (c) 2026 Anthony J. Thibault
  This software is licensed under the MIT License. See LICENSE for more
  details.
*/

#include "flycam.h"

#include <raymath.h>

static const float kRiseTime = 0.2f;  // time to reach ~95% of terminal speed

void FlyCamProcess(FlyCam *flycam, Vector2 left_stick, Vector2 right_stick,
                   float roll_amount, float up_amount, float dt) {
  const float kK = 3.0f / kRiseTime;
  const float kExp = expf(-kK * dt);

  left_stick = Vector2ClampValue(left_stick, 0.0f, 1.0f);
  // right_stick = Vector2ClampValue(right_stick, 0.0f, 1.0f);

  Quaternion rot = QuaternionInvert(QuaternionFromMatrix(
      MatrixLookAt(flycam->position, flycam->target, flycam->up)));
  Vector3 target_v = Vector3Scale(
      (Vector3){left_stick.x, up_amount, -left_stick.y}, flycam->lin_speed);
  target_v = Vector3RotateByQuaternion(target_v, rot);
  Vector3 v = Vector3Add(Vector3Scale(flycam->velocity, kExp),
                         Vector3Scale(target_v, 1.0f - kExp));
  Vector3 pos_delta =
      Vector3Add(Vector3Scale(Vector3Subtract(flycam->velocity, target_v),
                              (1.0f - kExp) / kK),
                 Vector3Scale(target_v, dt));
  Vector3 p = Vector3Add(flycam->position, pos_delta);
  Vector3 right = Vector3RotateByQuaternion((Vector3){1.0f, 0.0f, 0.0f}, rot);
  Vector3 forward =
      Vector3RotateByQuaternion((Vector3){0.0f, 0.0f, -1.0f}, rot);
  Quaternion yaw = QuaternionFromAxisAngle(
      flycam->up, flycam->rot_speed * dt * -right_stick.x);
  Quaternion pitch =
      QuaternionFromAxisAngle(right, flycam->rot_speed * dt * right_stick.y);
  rot = QuaternionMultiply(QuaternionMultiply(yaw, pitch), rot);

  Quaternion roll = (Quaternion){0.0f, 0.0f, 0.0f, 1.0f};
  if (fabs(roll_amount) > 0.1f) {
    roll =
        QuaternionFromAxisAngle(forward, flycam->rot_speed * dt * roll_amount);
  }

  flycam->target = Vector3Add(
      p, Vector3RotateByQuaternion((Vector3){0.0f, 0.0f, -1.0f}, rot));
  flycam->up = Vector3RotateByQuaternion(flycam->up, roll);
  flycam->position = p;
  flycam->velocity = v;
}
