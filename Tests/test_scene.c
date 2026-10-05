#include "soft3d_scene.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static Soft3D_Context ctx;
static uint16_t pixels[280U*240U];
static float depth[280U*240U];

static uint32_t render_hash(Soft3D_Scene scene, Soft3D_Mode mode, float time, float distance)
{
    const Soft3D_Camera camera = {1.0471975512f, 0.35f, 20.0f};
    const uint32_t triangle_counts[] = {12U, 192U, 252U};
    Soft3D_SceneView view;
    uint32_t i, coverage = 0U, hash = 2166136261U;
    view.scene = scene;
    view.mode = mode;
    view.orientation = Soft3D_SceneAutoOrientation(time);
    view.distance = distance;
    view.animation_seconds = time;
    soft3d_begin_frame(&ctx, &camera, 0x1082U);
    assert(Soft3D_SceneSubmit(&ctx, &view));
    assert(ctx.stats.submitted_triangles == triangle_counts[(unsigned)scene]);
    assert(ctx.stats.dropped_triangles == 0U);
    assert(soft3d_render_band(&ctx, 0U, 240U, pixels, depth));
    for (i = 0U; i < 280U*240U; ++i) {
        if (depth[i] > 0.0f) ++coverage;
        hash = (hash ^ pixels[i])*16777619U;
    }
    assert(coverage > 100U);
    return hash;
}

static void test_recenter(void)
{
    Soft3D_Quaternion reference = Soft3D_SceneAutoOrientation(4.0f);
    Soft3D_Quaternion centered = Soft3D_SceneRelativeOrientation(reference, reference);
    Soft3D_Quaternion expected = Soft3D_SceneAutoOrientation(0.0f);
    assert(fabsf(centered.x - expected.x) < 0.000001f);
    assert(fabsf(centered.y - expected.y) < 0.000001f);
    assert(fabsf(centered.z - expected.z) < 0.000001f);
    assert(fabsf(centered.w - expected.w) < 0.000001f);
}

int main(void)
{
    unsigned scene, mode, zoom;
    const float distances[] = {3.6f, 2.6f, 4.8f};
    Soft3D_SceneView invalid = {SOFT3D_SCENE_COUNT, SOFT3D_TEXTURED,
                               {0.0f, 0.0f, 0.0f, 1.0f}, 3.6f, 0.0f};
    soft3d_init(&ctx, 280U, 240U);
    test_recenter();
    for (scene = 0U; scene < SOFT3D_SCENE_COUNT; ++scene) {
        for (mode = 0U; mode <= SOFT3D_WIREFRAME; ++mode) {
            for (zoom = 0U; zoom < 3U; ++zoom) {
                uint32_t first = render_hash((Soft3D_Scene)scene, (Soft3D_Mode)mode, 0.0f, distances[zoom]);
                uint32_t rotated = render_hash((Soft3D_Scene)scene, (Soft3D_Mode)mode, 1.0f, distances[zoom]);
                assert(first != rotated);
            }
        }
    }
    assert(!Soft3D_SceneSubmit(&ctx, &invalid));
    invalid.scene = SOFT3D_SCENE_ORBIT;
    invalid.orientation.w = 0.0f;
    assert(!Soft3D_SceneSubmit(&ctx, &invalid));
    invalid.orientation.w = 1.0f;
    invalid.animation_seconds = NAN;
    assert(!Soft3D_SceneSubmit(&ctx, &invalid));
    assert(strcmp(Soft3D_SceneName(SOFT3D_SCENE_ORBIT), "ORBIT") == 0);
    puts("Scene tests passed: 54 nonblank renders, 3 scenes, 3 modes, 3 distances, recenter.");
    return 0;
}
