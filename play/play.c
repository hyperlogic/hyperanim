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

#define STB_DS_IMPLEMENTATION
#include "stb_ds.h"

static const Vector2 kMouseSens = {5.5f, -5.5f};

// Global state for the main loop (needed for emscripten callback)
static struct {
  FlyCam flycam;
} ctx;

static void PrintUsage(const char *prog) {
  fprintf(stderr, "usage: %s -i <input.hya>\n", prog);
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
  origin.m14 = 0.1f;
  DrawAxes(origin, 100.0f);

  /*
  MotionRelXformsAtFrame(ctx.rel_xforms, ctx.motion, motion_frame);
  SkeletonAbsXformsFromRelXforms(ctx.abs_xforms, ctx.motion->skeleton,
  ctx.rel_xforms); DrawSkeletonWithAbsXforms(ctx.motion->skeleton,
  ctx.abs_xforms);
  */

  EndMode3D();

  /*
  const int kFontSize = 30;
  const int kPadding = 10;
  if (ctx.draw_help) {
    DrawText("a, s, d, f - move\narrows, r-mouse-drag - look\nspace -
  play/stop\nn, p - step", kPadding, kPadding, kFontSize, RAYWHITE);
  }
  const char* text = TextFormat("%3d", motion_frame);
  int width = MeasureText(text, kFontSize);
  DrawText(text, GetScreenWidth() - width - kPadding, kPadding, kFontSize,
  RAYWHITE); int scrubber_frame = (int)motion_frame;
  DrawScrubber(&scrubber_frame, ctx.motion->num_frames);
  if ((uint32_t)scrubber_frame != motion_frame) {
    motion_frame = (uint32_t)scrubber_frame;
    ctx.motion_t = (float)motion_frame / ctx.motion->sample_rate;
  }
  */

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

  // PrintGraph(graph);

  Vector3 target = {0.0f, 0.0f, 0.0f};
  Vector3 offset = {100.0f, -100.0f, 50.0f};
  Vector3 pos = Vector3Add(target, offset);

  ctx.flycam = (FlyCam){.lin_speed = 100.0f,
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
