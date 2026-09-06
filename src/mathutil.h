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
HYA_Vec3 Vec3Scale(float s, HYA_Vec3 v);
HYA_Vec3 Vec3Add(HYA_Vec3 lhs, HYA_Vec3 rhs);
float Vec3Norm(HYA_Vec3 v);
HYA_Quat QuatFromAxisAngle(HYA_Vec3 axis, float angle);
HYA_Vec3 Vec3Rotate(HYA_Quat q, HYA_Vec3 v);
void Mat4Decompose(const float m[16], HYA_Vec3 *t, HYA_Quat *r, HYA_Vec3 *s);
void Mat4Make(float m[16], HYA_Vec3 t, HYA_Quat r, HYA_Vec3 s);
HYA_Xform XformIdent();
HYA_Xform XformMul(HYA_Xform lhs, HYA_Xform rhs);
#endif  // MATH_H
