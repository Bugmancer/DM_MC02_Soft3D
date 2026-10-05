#include "soft3d_damage.h"
#include "soft3d_scene.h"
#include "soft3d_ui.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define WIDTH 280U
#define HEIGHT 240U
#define BANDS 15U

static Soft3D_Context renderer;
static uint16_t retained[WIDTH * HEIGHT];
static uint16_t expected[WIDTH * HEIGHT];
static uint16_t pixels[WIDTH * SOFT3D_BAND_ROWS];
static float depth[WIDTH * SOFT3D_BAND_ROWS];
static uint16_t previous_geometry;
static unsigned previous_paused;
static unsigned frames;
static unsigned bands_sent;
static unsigned bands_saved;

static void draw_bands(uint16_t *target, uint16_t bands, const char *title,
                       const char *mode, const char *metrics, unsigned paused)
{
    unsigned band;
    for (band = 0U; band < BANDS; ++band) {
        unsigned y = band * SOFT3D_BAND_ROWS;
        if ((bands & (1U << band)) == 0U) continue;
        assert(soft3d_render_band(&renderer, (uint16_t)y, SOFT3D_BAND_ROWS, pixels, depth));
        Soft3D_UI_Band(pixels, WIDTH, HEIGHT, (uint16_t)y, SOFT3D_BAND_ROWS,
                       title, mode, metrics, (uint8_t)paused);
        memcpy(&target[y * WIDTH], pixels, sizeof(pixels));
    }
}

static void frame(Soft3D_Scene scene, Soft3D_Mode mode, float seconds, float distance,
                   unsigned paused, int empty, int full_refresh, uint16_t background)
{
    static const char *const modes[] = {"TEXTURE", "LIGHT", "WIRE"};
    const Soft3D_Camera camera = {1.0471975512f, 0.35f, 20.0f};
    Soft3D_SceneView view;
    char metrics[40];
    uint16_t geometry, damage;
    unsigned band;
    view.scene = scene;
    view.mode = mode;
    view.orientation = Soft3D_SceneAutoOrientation(seconds);
    view.distance = distance;
    view.animation_seconds = seconds;
    soft3d_begin_frame(&renderer, &camera, background);
    if (!empty) assert(Soft3D_SceneSubmit(&renderer, &view));
    assert(renderer.stats.dropped_triangles == 0U);
    geometry = Soft3D_GeometryBands(&renderer);
    damage = Soft3D_DamageBands(previous_geometry, geometry, full_refresh != 0,
                                paused != 0U || previous_paused != 0U);
    (void)snprintf(metrics, sizeof(metrics), "FRAME %u TRI %u", frames, renderer.triangle_count);
    draw_bands(expected, SOFT3D_ALL_BANDS, Soft3D_SceneName(scene), modes[mode], metrics, paused);
    draw_bands(retained, damage, Soft3D_SceneName(scene), modes[mode], metrics, paused);
    assert(memcmp(expected, retained, sizeof(expected)) == 0);
    for (band = 0U; band < BANDS; ++band) {
        if ((damage & (1U << band)) != 0U) ++bands_sent;
        else ++bands_saved;
    }
    previous_geometry = geometry;
    previous_paused = paused;
    ++frames;
}

static void test_bounds(void)
{
    const Soft3D_Camera camera = {1.0471975512f, 0.35f, 20.0f};
    assert(Soft3D_GeometryBands(NULL) == SOFT3D_ALL_BANDS);
    soft3d_init(&renderer, WIDTH, HEIGHT);
    assert(Soft3D_GeometryBands(&renderer) == SOFT3D_ALL_BANDS);
    soft3d_begin_frame(&renderer, &camera, 0x1082U);
    assert(Soft3D_GeometryBands(&renderer) == 0U);
    renderer.triangle_count = 1U;
    renderer.triangles[0].min_y = 15U;
    renderer.triangles[0].max_y = 16U;
    assert(Soft3D_GeometryBands(&renderer) == 3U);
    renderer.triangles[0].min_y = 239U;
    renderer.triangles[0].max_y = 239U;
    assert(Soft3D_GeometryBands(&renderer) == 0x4000U);
    renderer.triangles[0].min_y = 0U;
    renderer.triangles[0].max_y = 239U;
    assert(Soft3D_GeometryBands(&renderer) == SOFT3D_ALL_BANDS);
    renderer.triangles[0].max_y = 240U;
    assert(Soft3D_GeometryBands(&renderer) == SOFT3D_ALL_BANDS);
    assert(Soft3D_DamageBands(0U, 0U, false, false) == 0x4001U);
    assert(Soft3D_DamageBands(0U, 0U, false, true) == 0x4003U);
    assert(Soft3D_DamageBands(0U, 0U, true, false) == SOFT3D_ALL_BANDS);
    assert(Soft3D_DamageBands(0xFFFFU, 0xFFFFU, false, false) == SOFT3D_ALL_BANDS);
}

int main(void)
{
    static const float distances[] = {3.6f, 2.6f, 4.8f, 0.8f};
    unsigned scene, mode, zoom, pose;
    test_bounds();
    soft3d_init(&renderer, WIDTH, HEIGHT);
    memset(retained, 0xA5, sizeof(retained));
    frame(SOFT3D_SCENE_CUBE, SOFT3D_TEXTURED, 0.0f, 3.6f, 0U, 0, 1, 0x1082U);
    for (scene = 0U; scene < SOFT3D_SCENE_COUNT; ++scene) {
        for (mode = 0U; mode < 3U; ++mode) {
            for (zoom = 0U; zoom < sizeof(distances)/sizeof(distances[0]); ++zoom) {
                for (pose = 0U; pose < 4U; ++pose) {
                    frame((Soft3D_Scene)scene, (Soft3D_Mode)mode, 0.71f*(float)pose,
                           distances[zoom], pose & 1U, 0, 0, 0x1082U);
                }
            }
        }
    }
    /* Erase old geometry, then toggle pause where no geometry can hide stale UI. */
    frame(SOFT3D_SCENE_CUBE, SOFT3D_TEXTURED, 0.0f, 3.6f, 0U, 1, 0, 0x1082U);
    frame(SOFT3D_SCENE_CUBE, SOFT3D_TEXTURED, 0.0f, 3.6f, 1U, 1, 0, 0x1082U);
    frame(SOFT3D_SCENE_CUBE, SOFT3D_TEXTURED, 0.0f, 3.6f, 0U, 1, 0, 0x1082U);
    /* Recovery must overwrite every retained pixel, including untouched bands. */
    memset(retained, 0x5A, sizeof(retained));
    frame(SOFT3D_SCENE_TORUS, SOFT3D_LIT, 1.0f, 4.8f, 0U, 0, 1, 0x0841U);
    assert(bands_saved > 0U);
    printf("soft3d_damage: %u retained-screen frames match full redraw; sent=%u saved=%u bands\n",
            frames, bands_sent, bands_saved);
    return 0;
}
