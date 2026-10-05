#include "soft3d.h"
#include "soft3d_models.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#if defined(_WIN32)
#include <windows.h>
#endif

#define WIDTH 280U
#define HEIGHT 240U
#define ROWS 16U

static Soft3D_Context renderer;
static uint16_t pixels[WIDTH * ROWS];
static float depth[WIDTH * ROWS];
static volatile uint32_t checksum;

static double seconds(void)
{
#if defined(_WIN32)
    LARGE_INTEGER count, frequency;
    QueryPerformanceCounter(&count);
    QueryPerformanceFrequency(&frequency);
    return (double)count.QuadPart / (double)frequency.QuadPart;
#else
    return (double)clock() / (double)CLOCKS_PER_SEC;
#endif
}

static double benchmark(const Soft3D_Mesh *mesh, Soft3D_Mode mode,
                         unsigned frames, int animate)
{
    const Soft3D_Camera camera = {1.0471975512f, 0.35f, 20.0f};
    Soft3D_Transform transform = {{0, 0, 3.6f}, {0.45f, 0.60f, 0}, 1};
    Soft3D_Material material = {mode, 0x07DFU, &soft3d_texture_checker, 1U};
    double start;
    unsigned frame;
    soft3d_init(&renderer, WIDTH, HEIGHT);
    soft3d_begin_frame(&renderer, &camera, 0x1082U);
    if (!soft3d_submit(&renderer, mesh, &transform, &material)) exit(2);
    start = seconds();
    for (frame = 0; frame < frames; ++frame) {
        unsigned y;
        if (animate) {
            float phase = (float)(frame % 128U) * 0.02f;
            transform.rotation.x = 0.45f + phase * 0.37f;
            transform.rotation.y = 0.60f + phase * 0.61f;
            transform.rotation.z = phase * 0.13f;
            soft3d_begin_frame(&renderer, &camera, 0x1082U);
            if (!soft3d_submit(&renderer, mesh, &transform, &material)) exit(2);
        }
        for (y = 0; y < HEIGHT; y += ROWS) {
            if (!soft3d_render_band(&renderer, (uint16_t)y, ROWS, pixels, depth)) exit(2);
            checksum += pixels[(frame * 17U + y) % (WIDTH * ROWS)];
        }
    }
    return (seconds() - start) * 1000000.0 / (double)frames;
}

static double median(double a, double b, double c)
{
    if (a > b) { double t = a; a = b; b = t; }
    if (b > c) { double t = b; b = c; c = t; }
    return a > b ? a : b;
}

int main(int argc, char **argv)
{
    const Soft3D_Mesh *meshes[] = {&soft3d_mesh_cube, &soft3d_mesh_torus};
    const char *names[] = {"cube", "torus"};
    const char *modes[] = {"texture", "lit", "wire"};
    unsigned model, mode, frames = 1024U;
    if (argc == 2) {
        int count = atoi(argv[1]);
        if (count < 128 || count > 100000) return 1;
        frames = (unsigned)count;
    }
    printf("Host benchmark: 280x240, 16-row bands, median of 3 x %u frames; no LCD/MCU timing\n", frames);
    for (model = 0; model < 2; ++model) {
        for (mode = 0; mode < 3; ++mode) {
            double raster[3], full[3];
            unsigned run;
            (void)benchmark(meshes[model], (Soft3D_Mode)mode, 128U, 1);
            for (run = 0; run < 3; ++run) {
                raster[run] = benchmark(meshes[model], (Soft3D_Mode)mode, frames, 0);
                full[run] = benchmark(meshes[model], (Soft3D_Mode)mode, frames, 1);
            }
            printf("%s %s raster_us=%.3f frame_us=%.3f\n", names[model], modes[mode],
                   median(raster[0], raster[1], raster[2]), median(full[0], full[1], full[2]));
        }
    }
    printf("context=%lu bytes checksum=%08lx\n", (unsigned long)sizeof(renderer), (unsigned long)checksum);
    return 0;
}
