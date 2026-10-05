#include "soft3d_maze_scene.h"
#include "soft3d_ui.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define WIDTH 280U
#define HEIGHT 240U
#define ROWS 16U
#define PIXELS (WIDTH * HEIGHT)

static Soft3D_Context renderer;
static uint16_t full[PIXELS];
static float full_depth[PIXELS];
static uint16_t retained[PIXELS];
static struct {
    uint32_t before;
    uint16_t pixels[WIDTH * ROWS];
    uint32_t after;
} band;
static float band_depth[WIDTH * ROWS];
static uint32_t minimum_red = UINT32_MAX, minimum_green = UINT32_MAX;
static uint32_t minimum_coverage = UINT32_MAX;
static unsigned maximum_prepared;

static int ball_color(uint16_t color)
{
    unsigned r = (color >> 11) & 31U;
    unsigned g = (color >> 5) & 63U;
    unsigned b = color & 31U;
    return r > 10U && r * 3U > g * 2U && b < 8U;
}

static int clear_position(float x, float y)
{
    const Soft3D_MazeWall *walls;
    size_t count, i;
    float radius = SOFT3D_MAZE_BALL_RADIUS - 0.0001f;
    if (fabsf(x) > SOFT3D_MAZE_HALF_WIDTH || fabsf(y) > SOFT3D_MAZE_HALF_HEIGHT) return 0;
    walls = Soft3D_MazeWalls(&count);
    for (i = 0U; i < count; ++i) {
        float near_x = fmaxf(walls[i].min_x, fminf(walls[i].max_x, x));
        float near_y = fmaxf(walls[i].min_y, fminf(walls[i].max_y, y));
        float dx = x - near_x, dy = y - near_y;
        if (dx*dx + dy*dy < radius*radius) return 0;
    }
    return 1;
}

static uint32_t render_case(float x, float y, float tilt_x, float tilt_y, int won, float roll)
{
    Soft3D_MazeState maze;
    Soft3D_Camera camera = Soft3D_MazeSceneCamera();
    uint32_t i, colored = 0U, red_ball = 0U, green_goal = 0U, hash = 2166136261U;
    unsigned row;
    size_t wall_count;
    (void)Soft3D_MazeWalls(&wall_count);
    Soft3D_MazeInit(&maze);
    maze.x = x; maze.y = y;
    maze.tilt_x = tilt_x; maze.tilt_y = tilt_y;
    maze.won = won != 0;
    maze.ball_orientation[0] = cosf(roll * 0.5f);
    maze.ball_orientation[2] = sinf(roll * 0.5f);
    soft3d_begin_frame(&renderer, &camera, SOFT3D_MAZE_CLEAR_COLOR);
    assert(Soft3D_MazeSceneSubmit(&renderer, &maze));
    assert(renderer.stats.dropped_triangles == 0U);
    assert(renderer.stats.submitted_triangles == 156U + 12U * wall_count);
    assert(renderer.stats.submitted_triangles <= 444U);
    assert(renderer.triangle_count > 200U && renderer.triangle_count < SOFT3D_MAX_TRIANGLES);
    if (renderer.triangle_count > maximum_prepared) maximum_prepared = renderer.triangle_count;
    for (i = 0U; i < renderer.triangle_count; ++i) {
        assert(renderer.triangles[i].min_x >= 2U && renderer.triangles[i].max_x < WIDTH - 2U);
        assert(renderer.triangles[i].min_y >= 32U && renderer.triangles[i].max_y < HEIGHT - 16U);
    }
    assert(soft3d_render_band(&renderer, 0U, HEIGHT, full, full_depth));
    for (i = 0U; i < PIXELS; ++i) {
        unsigned r = (full[i] >> 11) & 31U;
        unsigned g = (full[i] >> 5) & 63U;
        unsigned b = full[i] & 31U;
        if (full_depth[i] > 0.0f) ++colored;
        if (ball_color(full[i])) ++red_ball;
        if (g > 20U && r < 8U && b < 8U) ++green_goal;
        hash = (hash ^ full[i]) * 16777619U;
    }
    if (colored < minimum_coverage) minimum_coverage = colored;
    if (red_ball < minimum_red) minimum_red = red_ball;
    if (green_goal < minimum_green) minimum_green = green_goal;
    if (red_ball < 18U || green_goal < 30U) {
        fprintf(stderr, "Visibility at (%f,%f) tilt=(%f,%f) roll=%f: red=%lu green=%lu coverage=%lu\n",
            (double)x, (double)y, (double)tilt_x, (double)tilt_y, (double)roll,
            (unsigned long)red_ball, (unsigned long)green_goal, (unsigned long)colored);
    }
    assert(colored > 26000U);
    assert(red_ball >= 18U);
    assert(green_goal >= 30U);
    Soft3D_UI_MazeBand(full, WIDTH, HEIGHT, 0U, HEIGHT,
                       won ? "WON" : "IMU", "TIME 12.3  BEST --.-", (uint8_t)won);
    for (row = 0U; row < HEIGHT; row += ROWS) {
        band.before = band.after = 0x13579BDFU;
        assert(soft3d_render_band(&renderer, (uint16_t)row, ROWS, band.pixels, band_depth));
        assert(memcmp(band_depth, &full_depth[row * WIDTH], sizeof(band_depth)) == 0);
        Soft3D_UI_MazeBand(band.pixels, WIDTH, HEIGHT, (uint16_t)row, ROWS,
                           won ? "WON" : "IMU", "TIME 12.3  BEST --.-", (uint8_t)won);
        assert(memcmp(band.pixels, &full[row * WIDTH], sizeof(band.pixels)) == 0);
        assert(band.before == 0x13579BDFU && band.after == 0x13579BDFU);
    }
    return hash;
}

static unsigned test_position(float x, float y)
{
    unsigned tilt;
    if (!clear_position(x, y)) return 0U;
    for (tilt = 0U; tilt < 4U; ++tilt) {
        (void)render_case(x, y,
            (tilt & 1U) != 0U ? 0.8f : -0.8f, (tilt & 2U) != 0U ? 0.8f : -0.8f,
            0, (float)tilt * 0.7f);
    }
    return 4U;
}

static void test_rolling_texture(void)
{
    unsigned i, changed = 0U;
    const Soft3D_MazePoint *start = Soft3D_MazeStartGet();
    (void)render_case(start->x, start->y, 0, 0, 0, 0);
    memcpy(retained, full, sizeof(full));
    (void)render_case(start->x, start->y, 0, 0, 0, 1.35f);
    for (i = 0U; i < PIXELS; ++i) {
        if (retained[i] != full[i] && (ball_color(retained[i]) || ball_color(full[i]))) ++changed;
    }
    assert(changed >= 8U);
}

static void test_ui_coverage(void)
{
    unsigned row;
    memset(full, 0x12, sizeof(full));
    memcpy(retained, full, sizeof(full));
    Soft3D_UI_MazeBand(full, WIDTH, HEIGHT, 0U, HEIGHT, "WON", "TIME 8.2  BEST 8.2", 1U);
    for (row = 0U; row < HEIGHT; row += ROWS) {
        if ((SOFT3D_MAZE_UI_BANDS & (1U << (row / ROWS))) != 0U) {
            Soft3D_UI_MazeBand(&retained[row * WIDTH], WIDTH, HEIGHT, (uint16_t)row, ROWS,
                               "WON", "TIME 8.2  BEST 8.2", 1U);
        }
    }
    assert(memcmp(full, retained, sizeof(full)) == 0);
    assert(full[100U * WIDTH + 100U] == 0x1212U);
    assert(full[0] == 0x06A0U);
    Soft3D_UI_MazeBand(full, WIDTH, HEIGHT, 0U, HEIGHT, "PAUSED", "TIME 8.2  BEST 8.2", 0U);
    assert(full[0] == 0x18C3U);
}

int main(void)
{
    const Soft3D_MazeWall *walls;
    const Soft3D_MazeGoal *goal = Soft3D_MazeGoalGet();
    const Soft3D_MazePoint *start = Soft3D_MazeStartGet();
    size_t wall_count, wall;
    unsigned column, row, point, cases = 0U;
    float margin = SOFT3D_MAZE_BALL_RADIUS + 0.0002f;
    soft3d_init(&renderer, WIDTH, HEIGHT);
    assert(clear_position(start->x, start->y));
    assert(clear_position(goal->x, goal->y));
    (void)render_case(goal->x, goal->y, 0, 0, 1, 0.8f);
    for (row = 0U; row < SOFT3D_MAZE_ROWS; ++row) {
        for (column = 0U; column < SOFT3D_MAZE_COLUMNS; ++column) {
            float x = -SOFT3D_MAZE_HALF_WIDTH + ((float)column + 0.5f) *
                (2.0f * SOFT3D_MAZE_HALF_WIDTH / (float)SOFT3D_MAZE_COLUMNS);
            float y = -SOFT3D_MAZE_HALF_HEIGHT + ((float)row + 0.5f) *
                (2.0f * SOFT3D_MAZE_HALF_HEIGHT / (float)SOFT3D_MAZE_ROWS);
            assert(clear_position(x, y));
            cases += test_position(x, y);
        }
    }
    walls = Soft3D_MazeWalls(&wall_count);
    for (wall = 0U; wall < wall_count; ++wall) {
        for (point = 0U; point < 3U; ++point) {
            float fraction = (float)point * 0.5f;
            float x = walls[wall].min_x + (walls[wall].max_x - walls[wall].min_x) * fraction;
            float y = walls[wall].min_y + (walls[wall].max_y - walls[wall].min_y) * fraction;
            cases += test_position(x, walls[wall].min_y - margin);
            cases += test_position(x, walls[wall].max_y + margin);
            cases += test_position(walls[wall].min_x - margin, y);
            cases += test_position(walls[wall].max_x + margin, y);
        }
        for (point = 0U; point < 4U; ++point) {
            float offset = margin * 0.707107f;
            float x = (point & 1U) != 0U ? walls[wall].max_x + offset : walls[wall].min_x - offset;
            float y = (point & 2U) != 0U ? walls[wall].max_y + offset : walls[wall].min_y - offset;
            cases += test_position(x, y);
        }
    }
    {
        Soft3D_MazeState invalid;
        Soft3D_MazeInit(&invalid);
        invalid.x = NAN;
        assert(!Soft3D_MazeSceneSubmit(&renderer, &invalid));
        assert(!Soft3D_MazeSceneSubmit(&renderer, NULL));
        Soft3D_MazeInit(&invalid);
        invalid.ball_orientation[0] = NAN;
        assert(!Soft3D_MazeSceneSubmit(&renderer, &invalid));
    }
    test_rolling_texture();
    test_ui_coverage();
    printf("maze_scene: %u cell/wall/endpoint extreme poses; min ball=%lu goal=%lu coverage=%lu pixels; max prepared=%u; rolling texture and band equivalence passed\n",
        cases, (unsigned long)minimum_red, (unsigned long)minimum_green,
        (unsigned long)minimum_coverage, maximum_prepared);
    return 0;
}
