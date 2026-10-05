#include "soft3d_maze_scene.h"
#include "soft3d_ui.h"

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
    Soft3D_MazeState maze;
    Soft3D_Camera camera = Soft3D_MazeSceneCamera();
    const char *state = "IMU";
    const char *metrics = "TIME 0.0  BEST --.-";
    int pose = argc > 2 ? atoi(argv[2]) : 0;
    unsigned y;
    FILE *output;
    if (argc < 2 || argc > 3 || pose < 0 || pose > 2) return 1;
    Soft3D_MazeInit(&maze);
    if (pose == 1) {
        maze.x = 0.0f; maze.y = 0.0f;
        maze.tilt_x = 0.28f; maze.tilt_y = -0.20f;
        maze.ball_orientation[0] = 0.70710678f;
        maze.ball_orientation[2] = 0.70710678f;
        metrics = "TIME 12.3  BEST --.-";
    } else if (pose == 2) {
        const Soft3D_MazeGoal *goal = Soft3D_MazeGoalGet();
        maze.x = goal->x; maze.y = goal->y;
        maze.ball_orientation[0] = 0.70710678f;
        maze.ball_orientation[1] = 0.70710678f;
        maze.won = true;
        state = "WON";
        metrics = "TIME 24.6  BEST 24.6";
    }
    soft3d_init(&renderer, WIDTH, HEIGHT);
    soft3d_begin_frame(&renderer, &camera, SOFT3D_MAZE_CLEAR_COLOR);
    if (!Soft3D_MazeSceneSubmit(&renderer, &maze)) return 1;
    output = fopen(argv[1], "wb");
    if (output == NULL) return 1;
    fprintf(output, "P6\n%u %u\n255\n", WIDTH, HEIGHT);
    for (y = 0U; y < HEIGHT; y += ROWS) {
        unsigned i;
        if (!soft3d_render_band(&renderer, (uint16_t)y, ROWS, pixels, depth)) {
            fclose(output);
            return 1;
        }
        Soft3D_UI_MazeBand(pixels, WIDTH, HEIGHT, (uint16_t)y, ROWS, state, metrics, maze.won ? 1U : 0U);
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
    printf("maze preview pose=%d prepared=%u submitted=%lu\n", pose, renderer.triangle_count,
           (unsigned long)renderer.stats.submitted_triangles);
    return 0;
}
