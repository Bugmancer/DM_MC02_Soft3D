#include "soft3d_maze_scene.h"
#include "soft3d_scene.h"
#include "soft3d_ui.h"
#include "soft3d_profile.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define WIDTH 280U
#define HEIGHT 240U
#define ROWS 16U

static Soft3D_Context renderer;
static uint16_t pixels[WIDTH * ROWS];
static float depth[WIDTH * ROWS];

int main(int argc, char **argv)
{
    const unsigned frame = 16U;
    const Soft3D_Camera camera = Soft3D_MazeSceneCamera();
    char title[24], *end;
    long workload;
    unsigned y, coverage = 0U, left = WIDTH, right = 0U, top = HEIGHT, bottom = 0U;
    FILE *output;
    if (argc != 3) return 1;
    workload = strtol(argv[2], &end, 10);
    if (*argv[2] == '\0' || *end != '\0' || workload < 0 || workload > 3) return 1;
    soft3d_init(&renderer, WIDTH, HEIGHT);
    soft3d_set_band_index_enabled(&renderer, 1);
    soft3d_begin_frame(&renderer, &camera, SOFT3D_MAZE_CLEAR_COLOR);
    if (workload == 0) {
        Soft3D_MazeState maze;
        float phase = (float)frame * 6.2831853072f / (float)SOFT3D_PROFILE_WINDOW_SIZE;
        Soft3D_MazeInit(&maze);
        maze.tilt_x = sinf(phase) * 0.3f;
        maze.tilt_y = cosf(phase) * 0.3f;
        maze.ball_orientation[0] = cosf(phase * 0.5f);
        maze.ball_orientation[2] = sinf(phase * 0.5f);
        if (!Soft3D_MazeSceneSubmit(&renderer, &maze)) return 1;
        (void)snprintf(title, sizeof(title), "LAB MAZE");
    } else {
        Soft3D_SceneView view;
        view.scene = (Soft3D_Scene)(workload - 1);
        view.mode = SOFT3D_TEXTURED;
        view.orientation = Soft3D_SceneAutoOrientation((float)frame * 0.05f);
        view.distance = 4.8f;
        view.animation_seconds = (float)frame * 0.05f;
        if (!Soft3D_SceneSubmit(&renderer, &view)) return 1;
        (void)snprintf(title, sizeof(title), "LAB %s", Soft3D_SceneName(view.scene));
    }
    if (renderer.stats.dropped_triangles != 0U) return 1;
    output = fopen(argv[1], "wb");
    if (output == NULL) return 1;
    if (fprintf(output, "P6\n%u %u\n255\n", WIDTH, HEIGHT) < 0) {
        fclose(output);
        return 1;
    }
    for (y = 0U; y < HEIGHT; y += ROWS) {
        unsigned i;
        if (!soft3d_render_band(&renderer, (uint16_t)y, ROWS, pixels, depth)) {
            fclose(output);
            return 1;
        }
        for (i = 0U; i < WIDTH * ROWS; ++i) {
            if (pixels[i] != SOFT3D_MAZE_CLEAR_COLOR) {
                unsigned x = i % WIDTH, screen_y = y + i / WIDTH;
                ++coverage;
                if (x < left) left = x;
                if (x > right) right = x;
                if (screen_y < top) top = screen_y;
                if (screen_y > bottom) bottom = screen_y;
            }
        }
        Soft3D_UI_ProfileBand(pixels, WIDTH, HEIGHT, (uint16_t)y, ROWS, title,
                              "INDEX ON", "G0.0 R0.0 IO0.0 MS", "F0.0 P95 -- SKIP 0 N0");
        for (i = 0U; i < WIDTH * ROWS; ++i) {
            uint16_t color = pixels[i];
            unsigned char rgb[3];
            rgb[0] = (unsigned char)(((color >> 11) & 31U) * 255U / 31U);
            rgb[1] = (unsigned char)(((color >> 5) & 63U) * 255U / 63U);
            rgb[2] = (unsigned char)((color & 31U) * 255U / 31U);
            if (fwrite(rgb, 1U, sizeof(rgb), output) != sizeof(rgb)) {
                fclose(output);
                return 1;
            }
        }
    }
    if (fclose(output) != 0) return 1;
    printf("%s: pixels=%u bounds=%u,%u-%u,%u prepared=%u\n",
           title, coverage, left, top, right, bottom, renderer.triangle_count);
    if (coverage < 400U || left == 0U || right >= WIDTH - 1U || top < 32U || bottom >= HEIGHT - 16U) {
        fprintf(stderr, "Preview is blank or extends beyond the scene viewport.\n");
        return 1;
    }
    return 0;
}
