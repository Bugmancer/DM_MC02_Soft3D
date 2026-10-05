#include "soft3d.h"
#include "soft3d_scene.h"
#include "soft3d_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIDTH 280U
#define HEIGHT 240U
#define BAND_ROWS 16U

static Soft3D_Context renderer;
static uint16_t pixels[WIDTH * BAND_ROWS];
static float depth[WIDTH * BAND_ROWS];

int main(int argc, char **argv)
{
    static const char *const mode_names[] = { "TEXTURE", "LIGHT", "WIRE" };
    const Soft3D_Camera camera = { 1.0471975512f, 0.35f, 20.0f };
    Soft3D_SceneView view = { SOFT3D_SCENE_CUBE, SOFT3D_TEXTURED,
                            {0.0f, 0.0f, 0.0f, 1.0f}, 3.6f, 0.0f };
    unsigned y, non_background = 0U, pose = 0U;
    uint32_t hash = 2166136261U;
    const char *scene_name = "cube";
    char title[40];
    char metrics[40];
    FILE *output;
    if (argc < 2 || argc > 5) {
        fprintf(stderr, "Usage: render_preview output.ppm [cube|torus|orbit] [0=textured|1=lit|2=wire] [0=initial|1=one-second]\n");
        return 1;
    }
    if (argc >= 3) {
        if (strcmp(argv[2], "torus") == 0) view.scene = SOFT3D_SCENE_TORUS;
        else if (strcmp(argv[2], "orbit") == 0) view.scene = SOFT3D_SCENE_ORBIT;
        else if (strcmp(argv[2], "cube") != 0) return 1;
        scene_name = argv[2];
    }
    if (argc >= 4) {
        char *end;
        long mode = strtol(argv[3], &end, 10);
        if (end == argv[3] || *end != '\0' || mode < 0 || mode > 2) return 1;
        view.mode = (Soft3D_Mode)mode;
    }
    if (argc == 5) {
        char *end;
        long parsed_pose = strtol(argv[4], &end, 10);
        if (end == argv[4] || *end != '\0' || parsed_pose < 0 || parsed_pose > 1) return 1;
        pose = (unsigned)parsed_pose;
    }
    view.animation_seconds = (float)pose;
    view.orientation = Soft3D_SceneAutoOrientation((float)pose);
    output = fopen(argv[1], "wb");
    if (output == NULL) {
        perror(argv[1]);
        return 1;
    }
    soft3d_init(&renderer, WIDTH, HEIGHT);
    soft3d_begin_frame(&renderer, &camera, 0x1082U);
    if (!Soft3D_SceneSubmit(&renderer, &view) || renderer.stats.dropped_triangles != 0U) {
        fclose(output);
        return 1;
    }
    (void)snprintf(metrics, sizeof(metrics), "HOST PREVIEW  TRI %lu",
                   (unsigned long)renderer.stats.prepared_triangles);
    (void)snprintf(title, sizeof(title), "%s HOST PREVIEW", Soft3D_SceneName(view.scene));
    fprintf(output, "P6\n%u %u\n255\n", WIDTH, HEIGHT);
    for (y = 0; y < HEIGHT; y += BAND_ROWS) {
        unsigned i, rows = HEIGHT - y;
        if (rows > BAND_ROWS) rows = BAND_ROWS;
        if (!soft3d_render_band(&renderer, (uint16_t)y, (uint16_t)rows, pixels, depth)) {
            fclose(output);
            return 1;
        }
        /* Verify geometry before applying UI so label changes cannot masquerade
         * as moving models in the preview script's image hash checks.
         */
        for (i = 0; i < WIDTH * rows; ++i) {
            if (pixels[i] != 0x1082U) ++non_background;
            hash = (hash ^ pixels[i]) * 16777619U;
        }
        Soft3D_UI_Band(pixels, WIDTH, HEIGHT, (uint16_t)y, (uint16_t)rows,
                       title, mode_names[view.mode], metrics, 0U);
        for (i = 0; i < WIDTH * rows; ++i) {
            uint16_t color = pixels[i];
            unsigned char rgb[3];
            rgb[0] = (unsigned char)(((color >> 11) & 31U) * 255U / 31U);
            rgb[1] = (unsigned char)(((color >> 5) & 63U) * 255U / 63U);
            rgb[2] = (unsigned char)((color & 31U) * 255U / 31U);
            if (fwrite(rgb, 1, 3, output) != 3U) {
                fclose(output);
                return 1;
            }
        }
    }
    if (fclose(output) != 0) return 1;
    printf("preview: scene=%s mode=%u pose=%u non_background=%u hash=%08lx triangles=%lu -> %s\n",
           scene_name, (unsigned)view.mode, pose, non_background, (unsigned long)hash,
           (unsigned long)renderer.stats.prepared_triangles, argv[1]);
    return 0;
}
