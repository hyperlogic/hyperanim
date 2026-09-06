/*
  Copyright (c) 2026 Anthony J. Thibault
  This software is licensed under the MIT License. See LICENSE for more
  details.
*/
#include "mathutil.h"

#include <assert.h>
#include <math.h>
#include <string.h>

HYA_Quat QuatMul(HYA_Quat q1, HYA_Quat q2) {
  return (HYA_Quat){q1.x * q2.w + q1.y * q2.z - q1.z * q2.y + q1.w * q2.x,
                    -q1.x * q2.z + q1.y * q2.w + q1.z * q2.x + q1.w * q2.y,
                    q1.x * q2.y - q1.y * q2.x + q1.z * q2.w + q1.w * q2.z,
                    -q1.x * q2.x - q1.y * q2.y - q1.z * q2.z + q1.w * q2.w};
}

HYA_Quat QuatInv(HYA_Quat q) { return (HYA_Quat){-q.x, -q.y, -q.z, q.w}; }

HYA_Vec3 Vec3Scale(float s, HYA_Vec3 v) {
  return (HYA_Vec3){s * v.x, s * v.y, s * v.z};
}

HYA_Vec3 Vec3Add(HYA_Vec3 lhs, HYA_Vec3 rhs) {
  return (HYA_Vec3){lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
}

HYA_Vec3 Vec3Rotate(HYA_Quat q, HYA_Vec3 v) {
  HYA_Quat tmp =
      QuatMul(QuatMul(q, (HYA_Quat){v.x, v.y, v.z, 0.0f}), QuatInv(q));
  return (HYA_Vec3){tmp.x, tmp.y, tmp.z};
}

float Vec3Norm(HYA_Vec3 v) { return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z); }

HYA_Quat QuatFromAxisAngle(HYA_Vec3 axis, float angle) {
  float norm = Vec3Norm(axis);
  float s = sinf(angle / 2.0f);
  float nx = (axis.x / norm) * s;
  float ny = (axis.y / norm) * s;
  float nz = (axis.z / norm) * s;
  return (HYA_Quat){nx, ny, nz, cosf(angle / 2.0f)};
}

// https://github.com/g-truc/glm/blob/master/glm/gtc/quaternion.inl#L81
// https://arc.aiaa.org/doi/10.2514/3.55767b
// assumes m is a 4x4 matrix with column major layout.
void Mat4Decompose(const float m[16], HYA_Vec3 *t, HYA_Quat *r, HYA_Vec3 *s) {
  // put the 3x3 part of m into local vars
  float a = m[0], b = m[4], c = m[8];
  float d = m[1], e = m[5], f = m[9];
  float g = m[2], h = m[6], i = m[10];
  // compute det of 3x3 part
  float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
  // extract scale of each column.
  s->x = sqrtf(a * a + d * d + g * g);
  if (det < 0.0f) {
    // left handed matrix, flip sign
    s->x = -s->x;
  }
  s->y = sqrtf(b * b + e * e + h * h);
  s->z = sqrtf(c * c + f * f + i * i);

  // cancel out scale before extracting quaternion
  float inv_sx = 1.0f / s->x;
  float inv_sy = 1.0f / s->y;
  float inv_sz = 1.0f / s->z;
  a *= inv_sx;
  d *= inv_sx;
  g *= inv_sx;
  b *= inv_sy;
  e *= inv_sy;
  h *= inv_sy;
  c *= inv_sz;
  f *= inv_sz;
  i *= inv_sz;

  // shepards method
  float fourxsquaredminus1 = a - e - i;
  float fourysquaredminus1 = e - a - i;
  float fourzsquaredminus1 = i - a - e;
  float fourwsquaredminus1 = a + e + i;
  int biggestindex = 0;
  float fourbiggestsquaredminus1 = fourwsquaredminus1;
  if (fourxsquaredminus1 > fourbiggestsquaredminus1) {
    fourbiggestsquaredminus1 = fourxsquaredminus1;
    biggestindex = 1;
  }
  if (fourysquaredminus1 > fourbiggestsquaredminus1) {
    fourbiggestsquaredminus1 = fourysquaredminus1;
    biggestindex = 2;
  }
  if (fourzsquaredminus1 > fourbiggestsquaredminus1) {
    fourbiggestsquaredminus1 = fourzsquaredminus1;
    biggestindex = 3;
  }
  float biggestval = sqrtf(fourbiggestsquaredminus1 + 1.0f) * 0.5f;
  float mult = 0.25f / biggestval;

  switch (biggestindex) {
    case 0:
      *r = (HYA_Quat){(h - f) * mult, (c - g) * mult, (d - b) * mult,
                      biggestval};
      break;
    case 1:
      *r = (HYA_Quat){biggestval, (b + d) * mult, (g + c) * mult,
                      (h - f) * mult};
      break;
    case 2:
      *r = (HYA_Quat){(b + d) * mult, biggestval, (f + h) * mult,
                      (c - g) * mult};
      break;
    case 3:
      *r = (HYA_Quat){(g + c) * mult, (f + h) * mult, biggestval,
                      (d - b) * mult};
      break;
    default:
      assert(false);
      *r = (HYA_Quat){0.0f, 0.0f, 0.0f, 1.0f};
  }
  t->x = m[12];
  t->y = m[13];
  t->z = m[14];
}

void Mat4Make(float m[16], HYA_Vec3 t, HYA_Quat r, HYA_Vec3 s) {
  HYA_Vec3 x = Vec3Rotate(r, (HYA_Vec3){s.x, 0.0f, 0.0f});
  HYA_Vec3 y = Vec3Rotate(r, (HYA_Vec3){0.0f, s.y, 0.0f});
  HYA_Vec3 z = Vec3Rotate(r, (HYA_Vec3){0.0f, 0.0f, s.z});

  m[0] = x.x;
  m[1] = x.y;
  m[2] = x.z;
  m[3] = 0.0f;

  m[4] = y.x;
  m[5] = y.y;
  m[6] = y.z;
  m[7] = 0.0f;

  m[8] = z.x;
  m[9] = z.y;
  m[10] = z.z;
  m[11] = 0.0f;

  m[12] = t.x;
  m[13] = t.y;
  m[14] = t.z;
  m[15] = 1.0f;
}

void Mat4Ident(float m[16]) {
  m[0] = 1.0f;
  m[1] = 0.0f;
  m[2] = 0.0f;
  m[3] = 0.0f;

  m[4] = 0.0f;
  m[5] = 1.0f;
  m[6] = 0.0f;
  m[7] = 0.0f;

  m[8] = 0.0f;
  m[9] = 0.0f;
  m[10] = 1.0f;
  m[11] = 0.0f;

  m[12] = 0.0f;
  m[13] = 0.0f;
  m[14] = 0.0f;
  m[15] = 1.0f;
}

void Mat4Mul(float result[16], const float lhs[16], const float rhs[16]) {
  float tmp[16];
  for (int c = 0; c < 4; ++c) {
    for (int r = 0; r < 4; ++r) {
      tmp[c * 4 + r] =
          lhs[0 * 4 + r] * rhs[c * 4 + 0] + lhs[1 * 4 + r] * rhs[c * 4 + 1] +
          lhs[2 * 4 + r] * rhs[c * 4 + 2] + lhs[3 * 4 + r] * rhs[c * 4 + 3];
    }
  }
  memcpy(result, tmp, sizeof tmp);
}

HYA_Xform XformIdent() {
  return (HYA_Xform){(HYA_Vec3){0.0f, 0.0f, 0.0f}, 1.0f,
                     (HYA_Quat){0.0f, 0.0f, 0.0f, 1.0f}};
}

HYA_Xform XformMul(HYA_Xform lhs, HYA_Xform rhs) {
  HYA_Xform result;
  result.t = Vec3Add(lhs.t, Vec3Rotate(lhs.r, Vec3Scale(lhs.s, rhs.t)));
  result.r = QuatMul(lhs.r, rhs.r);
  result.s = lhs.s * rhs.s;
  return result;
}
