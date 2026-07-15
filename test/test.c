/*
  Copyright (c) 2025 Anthony J. Thibault
  This software is licensed under the MIT License. See LICENSE for more
  details.
*/
#include "utest.h"

#include <stdbool.h>
#include <math.h>

#define STB_DS_IMPLEMENTATION
#include "stb_ds.h"

#include "mathutil.h"

bool FuzzyEqual(float lhs, float rhs, float epsilon) {
  return fabs(lhs - rhs) <= epsilon;
}

// column major layout
UTEST(Mat4Decompose, ident) {
  HYA_Quat test_quats[] = {
      {0.0f, 0.0f, 0.0f, 1.0f},  // ident
      QuatFromAxisAngle((HYA_Vec3){1.0f, 0.0f, 0.0f}, M_PI / 2.0f),
      QuatFromAxisAngle((HYA_Vec3){1.0f, 0.0f, 0.0f}, M_PI),
      QuatFromAxisAngle((HYA_Vec3){1.0f, 0.0f, 0.0f}, 3.0f * M_PI / 2.0f),
      QuatFromAxisAngle((HYA_Vec3){0.0f, 1.0f, 0.0f}, M_PI / 2.0f),
      QuatFromAxisAngle((HYA_Vec3){0.0f, 1.0f, 0.0f}, M_PI),
      QuatFromAxisAngle((HYA_Vec3){0.0f, 1.0f, 0.0f}, 3.0f * M_PI / 2.0f),
      QuatFromAxisAngle((HYA_Vec3){0.0f, 0.0f, 1.0f}, M_PI / 2.0f),
      QuatFromAxisAngle((HYA_Vec3){0.0f, 0.0f, 1.0f}, M_PI),
      QuatFromAxisAngle((HYA_Vec3){0.0f, 0.0f, 1.0f}, 3.0f * M_PI / 2.0f)};
  size_t num_quats = sizeof(test_quats) / sizeof(test_quats[0]);
  HYA_Vec3 test_scales[] = {{1.0f, 1.0f, 1.0f},
                            {2.0f, 0.5f, 0.5f},
                            {0.5f, 2.0f, 0.5f},
                            {0.5f, 0.5f, 2.0f}};
  size_t num_scales = sizeof(test_scales) / sizeof(test_scales[0]);
  for (size_t i = 0; i < num_quats; i++) {
    for (size_t j = 0; j < num_scales; j++) {
      HYA_Quat test_r = test_quats[i];
      HYA_Vec3 test_s = test_scales[j];
      HYA_Vec3 x = Vec3Rotate(test_r, (HYA_Vec3){test_s.x, 0.0f, 0.0f});
      HYA_Vec3 y = Vec3Rotate(test_r, (HYA_Vec3){0.0f, test_s.y, 0.0f});
      HYA_Vec3 z = Vec3Rotate(test_r, (HYA_Vec3){0.0f, 0.0f, test_s.z});
      // column major layout
      float m[16] = {
          x.x, x.y, x.z,  0.0f, y.x,  y.y,  y.z, 0.0f, z.x,
          z.y, z.z, 0.0f, 1.0f, 2.0f, 3.0f, 1.0f  // it's not worth testing
                                                  // translations
      };
      HYA_Vec3 t, s;
      HYA_Quat r;
      Mat4Decompose(m, &t, &r, &s);
      ASSERT_TRUE(t.x == 1.0f);
      ASSERT_TRUE(t.y == 2.0f);
      ASSERT_TRUE(t.z == 3.0f);

      ASSERT_TRUE(FuzzyEqual(s.x, test_s.x, 1.0e-6));
      ASSERT_TRUE(FuzzyEqual(s.y, test_s.y, 1.0e-6));
      ASSERT_TRUE(FuzzyEqual(s.z, test_s.z, 1.0e-6));

      float dot =
          (r.x * test_r.x + r.y * test_r.y + r.z * test_r.z + r.w * test_r.w);
      if (dot < 0.0f) {
        r = (HYA_Quat){-r.x, -r.y, -r.z, -r.w};
      }

      ASSERT_TRUE(FuzzyEqual(r.x, test_r.x, 1.0e-6));
      ASSERT_TRUE(FuzzyEqual(r.y, test_r.y, 1.0e-6));
      ASSERT_TRUE(FuzzyEqual(r.z, test_r.z, 1.0e-6));
      ASSERT_TRUE(FuzzyEqual(r.w, test_r.w, 1.0e-6));
    }
  }
}

UTEST_MAIN()
