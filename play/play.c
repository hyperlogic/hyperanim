#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <raylib.h>
#include <raymath.h>

#include "arena.h"
#include "flycam.h"

#define HYA_IMPLEMENTATION
#include "hyperanim.h"
#undef HYA_IMPLEMENTATION

#include "json.h"
#include "loadjson.h"
#include "mathutil.h"

#define STB_DS_IMPLEMENTATION
#include "stb_ds.h"

static const Vector2 kMouseSens = {5.5f, -5.5f};

// Global state for the main loop (needed for emscripten callback)
static struct {
  FlyCam flycam;
  HYA_Graph *graph;
} ctx;

static void PrintUsage(const char *prog) {
  fprintf(stderr, "usage: %s -i <input.hya>\n", prog);
}

static void Mat4ToRaylib(float *m, Matrix *mm) {
  mm->m0 = m[0];
  mm->m1 = m[1];
  mm->m2 = m[2];
  mm->m3 = m[3];

  mm->m4 = m[4];
  mm->m5 = m[5];
  mm->m6 = m[6];
  mm->m7 = m[7];

  mm->m8 = m[8];
  mm->m9 = m[9];
  mm->m10 = m[10];
  mm->m11 = m[11];

  mm->m12 = m[12];
  mm->m13 = m[13];
  mm->m14 = m[14];
  mm->m15 = m[15];
}

static void DrawFloorGrid(float size, int32_t num_subdivs) {
  float d = size / num_subdivs;
  for (size_t i = 0; i < num_subdivs + 1; i++) {
    DrawLine3D((Vector3){-size / 2.0f, (-size / 2.0f) + (d * i), 0.0f},
               (Vector3){size / 2.0f, (-size / 2.0f) + (d * i), 0.0f}, BLACK);
    DrawLine3D((Vector3){(-size / 2.0f) + (d * i), -size / 2.0f, 0.0f},
               (Vector3){(-size / 2.0f) + (d * i), size / 2.0f, 0.0f}, BLACK);
  }
}

static void DrawAxes(Matrix m, float axis_len) {
  Vector3 pos = {m.m12, m.m13, m.m14};
  Vector3 x = Vector3Scale((Vector3){1.0f, 0.0f, 0.0f}, axis_len);
  Vector3 y = Vector3Scale((Vector3){0.0f, 1.0f, 0.0f}, axis_len);
  Vector3 z = Vector3Scale((Vector3){0.0f, 0.0f, 1.0f}, axis_len);
  DrawLine3D(pos, Vector3Transform(x, m), RED);
  DrawLine3D(pos, Vector3Transform(y, m), GREEN);
  DrawLine3D(pos, Vector3Transform(z, m), BLUE);
}

#define MAX_NUM_XFORMS 1024
static HYA_Xform abs_xforms[MAX_NUM_XFORMS];

static void DrawSkeleton(HYA_Skeleton *skeleton) {
  float m[16];
  Matrix mm;
  assert(skeleton->num_joints <= MAX_NUM_XFORMS);
  for (size_t i = 0; i < skeleton->num_joints; i++) {
    HYA_Xform xform = skeleton->xforms[i];
    /*
    printf("joint[%zu] %s pos = (%.5f, %.5f, %.5f)\n", i,
           ctx.graph->str_ptrs[skeleton->joint_names[i]], xform.t.x, xform.t.y,
           xform.t.z);
    */
    if (skeleton->parent_indices[i] >= 0) {
      xform = XformMul(abs_xforms[skeleton->parent_indices[i]],
                       skeleton->xforms[i]);
      DrawLine3D(*(Vector3 *)&abs_xforms[skeleton->parent_indices[i]].t,
                 *(Vector3 *)&xform.t, GRAY);
    }
    abs_xforms[i] = xform;
    Mat4Make(m, xform.t, xform.r, (HYA_Vec3){xform.s, xform.s, xform.s});
    Mat4ToRaylib(m, &mm);
    DrawAxes(mm, 1.0f);
  }
}

static void UpdateAndDraw(void) {
  float dt = GetFrameTime();
  BeginDrawing();
  ClearBackground(DARKGRAY);

  /*
  if (IsKeyPressed(KEY_F1)) ctx.draw_help = !ctx.draw_help;
  if (IsKeyPressed(KEY_SPACE)) ctx.motion_playing = !ctx.motion_playing;
  */

  Vector2 left_stick = {0.0f, 0.0f};
  Vector2 right_stick = {0.0f, 0.0f};
  Vector2 mouse_stick = {0.0f, 0.0f};
  float roll_amount = 0.0f;
  float up_amount = 0.0f;
  if (IsKeyDown(KEY_A)) left_stick.x -= 1.0f;
  if (IsKeyDown(KEY_D)) left_stick.x += 1.0f;
  if (IsKeyDown(KEY_W)) left_stick.y += 1.0f;
  if (IsKeyDown(KEY_S)) left_stick.y -= 1.0f;
  if (IsKeyDown(KEY_Q)) roll_amount += 1.0f;
  if (IsKeyDown(KEY_E)) roll_amount -= 1.0f;
  if (IsKeyDown(KEY_R)) up_amount += 1.0f;
  if (IsKeyDown(KEY_F)) up_amount -= 1.0f;
  if (IsKeyDown(KEY_LEFT)) right_stick.x -= 1.0f;
  if (IsKeyDown(KEY_RIGHT)) right_stick.x += 1.0f;
  if (IsKeyDown(KEY_UP)) right_stick.y += 1.0f;
  if (IsKeyDown(KEY_DOWN)) right_stick.y -= 1.0f;
  if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
    mouse_stick =
        Vector2Add(right_stick, Vector2Multiply(GetMouseDelta(), kMouseSens));
  }
  right_stick = Vector2ClampValue(right_stick, 0.0f, 1.0f);

  FlyCamProcess(&ctx.flycam, left_stick, Vector2Add(right_stick, mouse_stick),
                roll_amount, up_amount, dt);

  Camera3D camera = {ctx.flycam.position, ctx.flycam.target, ctx.flycam.up,
                     60.0f, CAMERA_PERSPECTIVE};
  BeginMode3D(camera);

  DrawFloorGrid(2000.0f, 20);
  Matrix origin = MatrixIdentity();
  origin.m14 = 0.01f;
  DrawAxes(origin, 1.0f);

  DrawSkeleton(&(ctx.graph->tpose));

  EndMode3D();

  EndDrawing();
}

int main(int argc, char **argv) {
  const int screen_width = 800;
  const int screen_height = 600;

  HYA_Result res = HYA_ERR_FAILURE;
  const char *input = NULL;
  int c;
  while ((c = getopt(argc, argv, "i:")) != -1) {
    switch (c) {
      case 'i':
        input = optarg;
        break;
      default:
        PrintUsage(argv[0]);
        return HYA_ERR_BAD_ARGS;
    }
  }

  InitWindow(screen_width, screen_height, "play");

  if (!input) {
    fprintf(stderr, "%s: -i is required\n", argv[0]);
    PrintUsage(argv[0]);
    res = HYA_ERR_BAD_ARGS;
    goto cleanup_0;
  }

  HYA_Graph *graph;
  res = HYA_GraphCreate(&graph, input);
  if (res != HYA_OK) {
    fprintf(stderr, "ERROR: failed to load graph %s, result = %d\n", input,
            res);
    goto cleanup_0;
  }
  ctx.graph = graph;

  // PrintGraph(graph);

  Vector3 target = {0.0f, 0.0f, 0.0f};
  Vector3 offset = {10.0f, -10.0f, 5.0f};
  Vector3 pos = Vector3Add(target, offset);

  ctx.flycam = (FlyCam){.lin_speed = 10.0f,
                        .rot_speed = 3.0f,
                        .up = {0.0f, 0.0f, 1.0f},
                        .position = pos,
                        .target = target,
                        .velocity = {0.0f, 0.0f, 0.0f}};

  while (!WindowShouldClose()) {
    UpdateAndDraw();
  }

  HYA_GraphFree(graph);

  res = HYA_OK;

cleanup_0:
  CloseWindow();

  return res;
}
