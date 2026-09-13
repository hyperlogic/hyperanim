#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <raylib.h>
#include <rlgl.h>
#include <raymath.h>

#include "arena.h"
#include "flycam.h"

#define HYA_IMPLEMENTATION
#include "hyperanim.h"
#undef HYA_IMPLEMENTATION

#include "mathutil.h"

#define STB_DS_IMPLEMENTATION
#include "stb_ds.h"

#define STR_MAX 1024

static const Vector2 kMouseSens = {5.5f, -5.5f};

// Global state for the main loop (needed for emscripten callback)
static struct {
  FlyCam flycam;
  HYA_Graph *graph;
  HYA_GraphState *graph_state;
  Model model;
  int *model_to_graph_idx;
} ctx;

/* Copies the directory part of `path` into `out` (no trailing slash).
 * If `path` has no directory component, writes ".".
 * Returns false if `out_size` is too small. */
static bool dirname(const char *path, char *out, size_t out_size) {
  const char *slash = strrchr(path, '/');
#ifdef _WIN32
  const char *bslash = strrchr(path, '\\');
  if (!slash || (bslash && bslash > slash)) {
    slash = bslash;
  }
#endif
  if (!slash) {
    if (out_size < 2) {
      return false;
    }
    out[0] = '.';
    out[1] = '\0';
    return true;
  }
  size_t len = (size_t)(slash - path);
  if (len == 0) len = 1; /* "/graph.json" -> "/" not "" */
  if (len + 1 > out_size) {
    return false;
  }
  memcpy(out, path, len);
  out[len] = '\0';
  return true;
}

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
    DrawLine3D((Vector3){-size / 2.0f, 0.0f, (-size / 2.0f) + (d * i)},
               (Vector3){size / 2.0f, 0.0f, (-size / 2.0f) + (d * i)}, BLACK);
    DrawLine3D((Vector3){(-size / 2.0f) + (d * i), 0.0f, -size / 2.0f},
               (Vector3){(-size / 2.0f) + (d * i), 0.0f, size / 2.0f}, BLACK);
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

// raylib's glTF loader bakes each mesh node's world transform (A) into the
// mesh vertices, so a stored vertex is A * v. For a skinned mesh that causes
// problems if A contains rotation or scale.
static void FixRaylibBindPose(Model *model, HYA_Xform root_xform) {
  for (int i = 0; i < model->skeleton.boneCount; i++) {
    Transform *b = &model->skeleton.bindPose[i];
    HYA_Xform bind = {
        {b->translation.x, b->translation.y, b->translation.z},
        b->scale.x,
        {b->rotation.x, b->rotation.y, b->rotation.z, b->rotation.w}};
    bind = XformMul(root_xform, bind);
    b->translation = (Vector3){bind.t.x, bind.t.y, bind.t.z};
    b->rotation = (Quaternion){bind.r.x, bind.r.y, bind.r.z, bind.r.w};
    b->scale = (Vector3){bind.s, bind.s, bind.s};
  }
}

#define MAX_NUM_XFORMS 1024
static HYA_Xform g_abs_xforms[MAX_NUM_XFORMS];

static HYA_Xform *CalcAbsXforms(const HYA_Skeleton *skeleton) {
  assert(skeleton->num_joints <= MAX_NUM_XFORMS);
  HYA_Xform root_xform = skeleton->root_xform;
  for (size_t i = 0; i < skeleton->num_joints; i++) {
    HYA_Xform xform = skeleton->xforms[i];
    if (skeleton->parent_indices[i] >= 0) {
      xform = XformMul(g_abs_xforms[skeleton->parent_indices[i]],
                       skeleton->xforms[i]);
    } else {
      xform = XformMul(root_xform, xform);
    }
    g_abs_xforms[i] = xform;
  }
  return g_abs_xforms;
}

static void DrawSkeleton(HYA_Skeleton *skeleton) {
  float m[16];
  Matrix mm;
  HYA_Vec3 a, b;
  HYA_Xform *abs_xforms = CalcAbsXforms(skeleton);
  for (size_t i = 0; i < skeleton->num_joints; i++) {
    HYA_Xform xform = abs_xforms[i];
    Mat4Make(m, xform.t, xform.r, (HYA_Vec3){xform.s, xform.s, xform.s});
    Mat4ToRaylib(m, &mm);
    DrawAxes(mm, 10.0f);
    if (skeleton->parent_indices[i] >= 0) {
      a = abs_xforms[skeleton->parent_indices[i]].t;
      b = abs_xforms[i].t;
      DrawLine3D(*(Vector3 *)&a, *(Vector3 *)&b, GRAY);
    }
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

  HYA_Result res = HYA_GraphAnimate(ctx.graph, dt, ctx.graph_state);
  if (res != HYA_OK) {
    fprintf(stderr, "ERROR: HYA_GraphAnimate() failed, result = %d\n", res);
  }

  // flatten transforms.
  HYA_Xform *abs_xforms = CalcAbsXforms(&ctx.graph_state->skeleton);

  // build a one frame animation.
  ModelAnimation anim = {0};
  anim.boneCount = ctx.model.skeleton.boneCount;
  anim.keyframeCount = 1;
  Transform xforms[ctx.model.skeleton.boneCount];
  Transform *frames[1] = {xforms};
  anim.keyframePoses = frames;
  for (size_t i = 0; i < ctx.model.skeleton.boneCount; i++) {
    // NOTE: These poses are in abs object space.
    int graph_idx = ctx.model_to_graph_idx[i];
    if (graph_idx >= 0) {
      HYA_Xform xform = abs_xforms[graph_idx];
      anim.keyframePoses[0][i].scale = (Vector3){xform.s, xform.s, xform.s};
      HYA_Quat q = QuatNormalize(xform.r);
      anim.keyframePoses[0][i].rotation = (Quaternion){q.x, q.y, q.z, q.w};
      anim.keyframePoses[0][i].translation =
          (Vector3){xform.t.x, xform.t.y, xform.t.z};
    }
  }

  // apply the one frame animation to the model.
  UpdateModelAnimation(ctx.model, anim, 0);

  // render the model
  DrawModel(ctx.model, (Vector3){0.0f, 0.0f, 0.0f}, 1.0f, WHITE);

  DrawFloorGrid(20.0f, 20);

  Matrix origin = MatrixIdentity();
  origin.m14 = 0.01f;  // offset a bit to reduce z-fighting with the grid.
  DrawAxes(origin, 1.0f);

  rlDrawRenderBatchActive();
  rlDisableDepthTest();
  // DrawSkeleton(&(ctx.graph->tpose));
  DrawSkeleton(&(ctx.graph_state->skeleton));
  rlDrawRenderBatchActive();
  rlEnableDepthTest();

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

  SetConfigFlags(FLAG_WINDOW_RESIZABLE);
  InitWindow(screen_width, screen_height, "play");

  if (!input) {
    fprintf(stderr, "%s: -i is required\n", argv[0]);
    PrintUsage(argv[0]);
    res = HYA_ERR_BAD_ARGS;
    goto cleanup_0;
  }

  HYA_Graph *graph;
  res = HYA_GraphNew(&graph, input);
  if (res != HYA_OK) {
    fprintf(stderr, "ERROR: HYA_GraphNew(%s) failed, result = %d\n", input,
            res);
    goto cleanup_0;
  }
  ctx.graph = graph;

  HYA_GraphState *graph_state;
  res = HYA_GraphStateNew(&graph_state, graph);
  if (res != HYA_OK) {
    fprintf(stderr, "ERROR: HYA_GraphStateNew() failed, result = %d\n", res);
    goto cleanup_1;
  }
  ctx.graph_state = graph_state;

  // PrintGraph(graph);

  // Load model
  char dir[STR_MAX];
  if (!dirname(input, dir, STR_MAX)) {
    fprintf(stderr, "ERROR: dirname failed = %s\n", input);
    res = HYA_ERR_FAILURE;
    goto cleanup_2;
  }
  const char *model_filename = "/anim/ybot.glb";
  if (strlen(dir) + strlen(model_filename) > STR_MAX - 1) {
    fprintf(stderr, "ERROR: %s/%s too large\n", dir, model_filename);
  }
  char full_model_filename[STR_MAX];
  strcpy(full_model_filename, dir);
  strcat(full_model_filename, model_filename);

  ctx.model = LoadModel(full_model_filename);
  FixRaylibBindPose(&ctx.model, ctx.graph_state->skeleton.root_xform);

  // build a map from model joint to graph_state.skeleton
  ctx.model_to_graph_idx =
      (int *)malloc(sizeof(int) * ctx.model.skeleton.boneCount);
  // O(N^2) but we only do it once...
  for (size_t i = 0; i < ctx.model.skeleton.boneCount; i++) {
    const char *name = ctx.model.skeleton.bones[i].name;
    bool found = false;
    for (size_t j = 0; j < ctx.graph_state->skeleton.num_joints; j++) {
      if (strncmp(name,
                  ctx.graph->str_ptrs[ctx.graph_state->skeleton.joint_names[j]],
                  32) == 0) {
        ctx.model_to_graph_idx[i] = j;
        found = true;
        break;
      }
    }
    if (!found) {
      fprintf(stderr, "WARNING: could not find joint '%s' in graph skeleton\n",
              name);
      ctx.model_to_graph_idx[i] = -1;
    }
  }

  Vector3 position = {0.0f, 0.0f, 0.0f};  // Set model world position
  Vector3 target = {0.0f, 1.0f, 0.0f};
  Vector3 offset = {2.0f, 0.0f, 2.0f};
  Vector3 pos = Vector3Add(target, offset);

  ctx.flycam = (FlyCam){.lin_speed = 10.0f,
                        .rot_speed = 3.0f,
                        .up = {0.0f, 1.0f, 0.0f},
                        .position = pos,
                        .target = target,
                        .velocity = {0.0f, 0.0f, 0.0f}};

  while (!WindowShouldClose()) {
    UpdateAndDraw();
  }

  res = HYA_OK;

cleanup_2:
  HYA_GraphStateDelete(graph_state);
cleanup_1:
  HYA_GraphDelete(graph);
cleanup_0:
  CloseWindow();

  return res;
}
