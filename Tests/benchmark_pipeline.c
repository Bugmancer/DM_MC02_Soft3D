#include "soft3d.h"
#include "soft3d_models.h"
#include "soft3d_maze.h"
#include "soft3d_maze_scene.h"

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(_WIN32)
#include <windows.h>
#endif

#define WIDTH 280U
#define HEIGHT 240U
#define BAND_ROWS 16U
#define ORACLE_FRAMES 8U
#define WARMUP_FRAMES 8U

typedef struct {
    const char *name;
    const Soft3D_Mesh *mesh;
    Soft3D_Mode mode;
    uint32_t level;
    int changing;
} Workload;

typedef struct {
    Soft3D_MazeState maze;
    Soft3D_Transform transform;
} Pose;

static Soft3D_Context s_renderer;
static uint16_t s_pixels[WIDTH * BAND_ROWS];
static float s_depth[WIDTH * BAND_ROWS];
static volatile uint32_t s_sink;
#if defined(_WIN32)
static double s_counter_frequency;
#endif

static void fail(const char *message)
{
    fprintf(stderr, "pipeline benchmark: %s\n", message);
    exit(2);
}

static double seconds(void)
{
#if defined(_WIN32)
    LARGE_INTEGER count;
    if (!QueryPerformanceCounter(&count)) fail("performance counter unavailable");
    return (double)count.QuadPart / s_counter_frequency;
#else
    return (double)clock() / (double)CLOCKS_PER_SEC;
#endif
}

static void pose_for_frame(const Workload *workload, unsigned frame, Pose *pose)
{
    float phase = workload->changing ? (float)(frame % 128U) * 0.0490873852f : 0.37f;
    Soft3D_MazeInit(&pose->maze);
    pose->maze.tilt_x = 0.30f * sinf(phase);
    pose->maze.tilt_y = 0.30f * cosf(phase);
    pose->maze.ball_orientation[0] = cosf(phase * 0.5f);
    pose->maze.ball_orientation[1] = 0.7071067812f * sinf(phase * 0.5f);
    pose->maze.ball_orientation[2] = pose->maze.ball_orientation[1];
    pose->maze.ball_orientation[3] = 0.0f;
    pose->transform.position.x = 0.0f;
    pose->transform.position.y = 0.0f;
    pose->transform.position.z = 3.6f;
    pose->transform.rotation.x = 0.45f + phase * 0.37f;
    pose->transform.rotation.y = 0.60f + phase * 0.61f;
    pose->transform.rotation.z = phase * 0.13f;
    pose->transform.scale = 1.0f;
}

static void submit_frame(const Workload *workload, const Pose *pose)
{
    Soft3D_Camera camera = {1.0471975512f, 0.35f, 20.0f};
    Soft3D_Material material = {workload->mode, 0x07DFU, &soft3d_texture_checker, 1U};
    int success;
    if (workload->level != 0U) camera = Soft3D_MazeSceneCamera();
    soft3d_begin_frame(&s_renderer, &camera, SOFT3D_MAZE_CLEAR_COLOR);
    success = workload->level != 0U ? Soft3D_MazeSceneSubmit(&s_renderer, &pose->maze) :
              soft3d_submit(&s_renderer, workload->mesh, &pose->transform, &material);
    if (!success || s_renderer.stats.dropped_triangles != 0U) fail("scene submission failed");
}

static uint64_t hash_pixels(uint64_t hash)
{
    unsigned i;
    for (i = 0U; i < WIDTH * BAND_ROWS; ++i) {
        hash = (hash ^ (uint64_t)(s_pixels[i] & 0xffU)) * UINT64_C(1099511628211);
        hash = (hash ^ (uint64_t)(s_pixels[i] >> 8)) * UINT64_C(1099511628211);
    }
    return hash;
}

static void render_bands(void)
{
    unsigned y;
    for (y = 0U; y < HEIGHT; y += BAND_ROWS) {
        if (!soft3d_render_band(&s_renderer, (uint16_t)y, BAND_ROWS, s_pixels, s_depth)) {
            fail("band rasterization failed");
        }
    }
}

static uint64_t oracle(const Workload *workload)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    unsigned frame;
    for (frame = 0U; frame < ORACLE_FRAMES; ++frame) {
        unsigned y;
        Pose pose;
        pose_for_frame(workload, frame * 17U, &pose);
        submit_frame(workload, &pose);
        for (y = 0U; y < HEIGHT; y += BAND_ROWS) {
            if (!soft3d_render_band(&s_renderer, (uint16_t)y, BAND_ROWS, s_pixels, s_depth)) {
                fail("oracle rasterization failed");
            }
            hash = hash_pixels(hash);
        }
    }
    return hash;
}

static void run_workload(const Workload *workload, unsigned frames, int oracle_only, int comma)
{
    static const char *const mode_names[] = {"texture", "lit", "wire"};
    uint64_t oracle_hash;
    double geometry = 0.0, raster = 0.0, total = 0.0, triangles = 0.0;
    unsigned frame;
    size_t walls = 0U;
    uint32_t seed = 0U;
    const char *mode = workload->level != 0U ? "mixed" : mode_names[workload->mode];
    if (workload->level != 0U) {
        if (!Soft3D_MazeSelectLevel(workload->level)) fail("invalid maze level");
        (void)Soft3D_MazeWalls(&walls);
        seed = Soft3D_MazeSeedGet();
    }
    soft3d_init(&s_renderer, WIDTH, HEIGHT);
    oracle_hash = oracle(workload);
    if (!oracle_only) {
        for (frame = 0U; frame < WARMUP_FRAMES; ++frame) {
            Pose pose;
            pose_for_frame(workload, frame, &pose);
            submit_frame(workload, &pose);
            render_bands();
        }
        for (frame = 0U; frame < frames; ++frame) {
            Pose pose;
            double begin, submitted, rendered;
            pose_for_frame(workload, frame, &pose);
            begin = seconds();
            submit_frame(workload, &pose);
            submitted = seconds();
            render_bands();
            rendered = seconds();
            geometry += submitted - begin;
            raster += rendered - submitted;
            total += rendered - begin;
            triangles += (double)s_renderer.stats.prepared_triangles;
            s_sink += s_pixels[frame % (WIDTH * BAND_ROWS)];
        }
    }
    printf("%s{\"id\":\"%s.%s.%s\",\"level\":%lu,\"seed\":%lu,\"walls\":%lu,"
           "\"oracle_hash\":\"%016" PRIx64 "\",", comma ? ",\n" : "\n", workload->name, mode,
           workload->changing ? "changing" : "fixed", (unsigned long)workload->level,
           (unsigned long)seed, (unsigned long)walls, oracle_hash);
    if (oracle_only) {
        printf("\"geometry_us\":null,\"raster_us\":null,\"total_us\":null,\"prepared_triangles_mean\":null}");
    } else {
        printf("\"geometry_us\":%.6f,\"raster_us\":%.6f,\"total_us\":%.6f,\"prepared_triangles_mean\":%.6f}",
               geometry * 1000000.0 / (double)frames, raster * 1000000.0 / (double)frames,
               total * 1000000.0 / (double)frames, triangles / (double)frames);
    }
}

int main(int argc, char **argv)
{
    static const Soft3D_Mesh *const meshes[] = {&soft3d_mesh_cube, &soft3d_mesh_torus};
    static const char *const model_names[] = {"cube", "torus"};
    static const uint32_t levels[] = {1U, 2U, 17U, 9999U};
    static const char *const maze_names[] = {"maze1", "maze2", "maze17", "maze9999"};
    unsigned frames = 128U, model, mode, changing, rows = 0U;
    int oracle_only = 0;
    const char *clock_name;
#if defined(_WIN32)
    LARGE_INTEGER frequency;
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0) fail("invalid timer frequency");
    s_counter_frequency = (double)frequency.QuadPart;
    clock_name = "QueryPerformanceCounter";
#else
    clock_name = "clock process CPU time";
#endif
    if (argc == 2 && strcmp(argv[1], "--oracle-only") == 0) oracle_only = 1;
    else if (argc == 2) {
        char *end;
        unsigned long parsed;
        errno = 0;
        parsed = strtoul(argv[1], &end, 10);
        if (errno != 0 || end == argv[1] || *end != '\0' || parsed < 1UL || parsed > 100000UL) return 1;
        frames = (unsigned)parsed;
    } else if (argc != 1) return 1;
    printf("{\"schema_version\":1,\"frames\":%u,\"width\":%u,\"height\":%u,\"band_rows\":%u,"
           "\"oracle_only\":%s,\"oracle_frames\":%u,\"warmup_frames\":%u,\"clock\":\"%s\","
           "\"context_bytes\":%lu,\"workloads\":[", frames, WIDTH, HEIGHT, BAND_ROWS,
           oracle_only ? "true" : "false", ORACLE_FRAMES, WARMUP_FRAMES, clock_name,
           (unsigned long)sizeof(s_renderer));
    for (model = 0U; model < 2U; ++model) {
        for (mode = 0U; mode < 3U; ++mode) {
            for (changing = 0U; changing < 2U; ++changing) {
                Workload workload = {model_names[model], meshes[model], (Soft3D_Mode)mode, 0U, (int)changing};
                run_workload(&workload, frames, oracle_only, rows++ != 0U);
            }
        }
    }
    for (model = 0U; model < sizeof(levels) / sizeof(levels[0]); ++model) {
        for (changing = 0U; changing < 2U; ++changing) {
            Workload workload = {maze_names[model], NULL, SOFT3D_TEXTURED, levels[model], (int)changing};
            run_workload(&workload, frames, oracle_only, rows++ != 0U);
        }
    }
    printf("\n]}\n");
    return 0;
}
