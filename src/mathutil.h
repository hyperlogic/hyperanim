/*
  Copyright (c) 2026 Anthony J. Thibault
  This software is licensed under the MIT License. See LICENSE for more
  details.
*/

#ifndef MATHUTIL_H
#define MATHUTIL_H

#include "hyperanim.h"

HYA_Quat QuatMul(HYA_Quat q1, HYA_Quat q2);
HYA_Quat QuatInv(HYA_Quat q);
float Vec3Norm(HYA_Vec3 v);
HYA_Quat QuatFromAxisAngle(HYA_Vec3 axis, float angle);
HYA_Vec3 Vec3Rotate(HYA_Quat q, HYA_Vec3 v);
void Mat4Decompose(const float m[16], HYA_Vec3 *t, HYA_Quat *r, HYA_Vec3 *s);

#endif  // MATH_H
