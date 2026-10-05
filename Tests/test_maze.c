#include "soft3d_maze.h"

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define MAZE_CELLS (SOFT3D_MAZE_COLUMNS * SOFT3D_MAZE_ROWS)

static void near_value(float actual, float expected, float tolerance)
{
    assert(fabsf(actual - expected) <= tolerance);
}

static void assert_clear(const Soft3D_MazeState *state)
{
    size_t count, i;
    const Soft3D_MazeWall *walls = Soft3D_MazeWalls(&count);
    const float radius = SOFT3D_MAZE_BALL_RADIUS;
    assert(state->x >= -SOFT3D_MAZE_HALF_WIDTH + radius - 0.0001f);
    assert(state->x <= SOFT3D_MAZE_HALF_WIDTH - radius + 0.0001f);
    assert(state->y >= -SOFT3D_MAZE_HALF_HEIGHT + radius - 0.0001f);
    assert(state->y <= SOFT3D_MAZE_HALF_HEIGHT - radius + 0.0001f);
    for (i = 0U; i < count; ++i) {
        float x = fminf(fmaxf(state->x, walls[i].min_x), walls[i].max_x);
        float y = fminf(fmaxf(state->y, walls[i].min_y), walls[i].max_y);
        float dx = state->x - x, dy = state->y - y;
        assert(dx * dx + dy * dy >= radius * radius - 0.00001f);
    }
    assert(state->vx * state->vx + state->vy * state->vy <=
           SOFT3D_MAZE_MAX_SPEED * SOFT3D_MAZE_MAX_SPEED + 0.0001f);
    near_value(state->ball_orientation[0]*state->ball_orientation[0] +
               state->ball_orientation[1]*state->ball_orientation[1] +
               state->ball_orientation[2]*state->ball_orientation[2] +
               state->ball_orientation[3]*state->ball_orientation[3], 1.0f, 0.00001f);
}

static void test_initial_state_and_gravity(void)
{
    Soft3D_MazeState state;
    const Soft3D_MazePoint *start = Soft3D_MazeStartGet();
    size_t count;
    unsigned i;
    Soft3D_MazeInit(NULL);
    Soft3D_MazeUpdate(NULL, 10U, 0.0f, 0.0f, true);
    Soft3D_MazeInit(&state);
    assert(state.paused && !state.won && state.elapsed_ms == 0U);
    near_value(state.x, start->x, 0.00001f);
    near_value(state.y, start->y, 0.00001f);
    assert(state.ball_orientation[0] == 1.0f && state.ball_orientation[1] == 0.0f &&
           state.ball_orientation[2] == 0.0f && state.ball_orientation[3] == 0.0f);
    assert(Soft3D_MazeWalls(&count) != NULL && count == 18U && count <= 24U);
    assert(Soft3D_MazeWalls(NULL) != NULL);
    assert_clear(&state);
    for (i = 0U; i < 100U; ++i) Soft3D_MazeUpdate(&state, 10U, 0.0f, 0.0f, true);
    near_value(state.x, start->x, 0.00001f);
    near_value(state.y, start->y, 0.00001f);
    assert(state.elapsed_ms == 1000U && !state.paused);
    Soft3D_MazeUpdate(&state, 10U, FLT_MAX, FLT_MAX, true);
    assert(state.tilt_x > 0.0f && state.tilt_y > 0.0f);
    near_value(state.tilt_x * state.tilt_x + state.tilt_y * state.tilt_y, 0.25f, 0.00001f);
    assert_clear(&state);
    Soft3D_MazeUpdate(&state, 100U, NAN, 0.0f, true);
    assert(state.paused && state.accumulator_ms == 0U && state.vx == 0.0f);
    Soft3D_MazeUpdate(&state, 100U, 0.0f, INFINITY, true);
    assert(state.paused && state.elapsed_ms == 1010U);
}

static void test_wall_corner_and_speed(void)
{
    Soft3D_MazeState state;
    const Soft3D_MazeWall *walls = Soft3D_MazeWalls(NULL);
    float stopped_orientation[4];
    unsigned i;
    Soft3D_MazeInit(&state);
    state.vx = 10000.0f;
    for (i = 0U; i < 500U; ++i) {
        Soft3D_MazeUpdate(&state, 10U, 1.0f, 0.0f, true);
        assert_clear(&state);
        assert(state.x <= walls[4].min_x - SOFT3D_MAZE_BALL_RADIUS + 0.0001f);
    }
    near_value(state.vx, 0.0f, 0.0001f);
    memcpy(stopped_orientation, state.ball_orientation, sizeof(stopped_orientation));
    for (i = 0U; i < 100U; ++i) Soft3D_MazeUpdate(&state, 10U, 1.0f, 0.0f, true);
    for (i = 0U; i < 4U; ++i) near_value(state.ball_orientation[i], stopped_orientation[i], 0.00001f);
    Soft3D_MazeRestart(&state);
    state.x = -2.9f;
    state.y = -2.1f;
    state.vx = -10000.0f;
    state.vy = -10000.0f;
    for (i = 0U; i < 500U; ++i) {
        Soft3D_MazeUpdate(&state, 10U, -1.0f, -1.0f, true);
        assert_clear(&state);
    }
    near_value(state.x, -3.02f, 0.0001f);
    near_value(state.y, -2.22f, 0.0001f);
    Soft3D_MazeRestart(&state);
    state.x = walls[4].min_x - 0.3f;
    state.y = walls[4].max_y + 0.3f;
    state.vx = 2.0f;
    state.vy = -2.0f;
    for (i = 0U; i < 200U; ++i) {
        Soft3D_MazeUpdate(&state, 10U, 1.0f, -1.0f, true);
        assert_clear(&state);
    }
    Soft3D_MazeRestart(&state);
    state.x = Soft3D_MazeStartGet()->x;
    state.y = 0.0f;
    state.vy = -10000.0f;
    for (i = 0U; i < 500U; ++i) {
        Soft3D_MazeUpdate(&state, 10U, 0.0f, -1.0f, true);
        assert_clear(&state);
        assert(state.y >= walls[14].max_y + SOFT3D_MAZE_BALL_RADIUS - 0.0001f);
    }
}

static void test_frame_partition(void)
{
    static const uint32_t chunks[] = {7U, 13U, 31U, 49U};
    Soft3D_MazeState regular, split;
    unsigned i, j;
    Soft3D_MazeInit(&regular);
    Soft3D_MazeInit(&split);
    for (i = 0U; i < 1000U; ++i) {
        Soft3D_MazeUpdate(&regular, 10U, 0.12f, 0.25f, true);
    }
    for (i = 0U; i < 100U; ++i) {
        for (j = 0U; j < 4U; ++j) {
            Soft3D_MazeUpdate(&split, chunks[j], 0.12f, 0.25f, true);
        }
    }
    near_value(regular.x, split.x, 0.000001f);
    near_value(regular.y, split.y, 0.000001f);
    near_value(regular.vx, split.vx, 0.000001f);
    near_value(regular.vy, split.vy, 0.000001f);
    for (i = 0U; i < 4U; ++i) near_value(regular.ball_orientation[i], split.ball_orientation[i], 0.000001f);
    assert(regular.elapsed_ms == split.elapsed_ms && regular.accumulator_ms == split.accumulator_ms);
    assert_clear(&regular);
}

static void test_pause_and_bounded_backlog(void)
{
    Soft3D_MazeState state;
    float x, y, orientation[4];
    Soft3D_MazeInit(&state);
    Soft3D_MazeUpdate(&state, 9U, 0.3f, 0.0f, true);
    Soft3D_MazeUpdate(&state, 60000U, 0.3f, 0.0f, false);
    assert(state.accumulator_ms == 0U && state.elapsed_ms == 0U && state.paused);
    Soft3D_MazeUpdate(&state, 1U, 0.3f, 0.0f, true);
    assert(state.elapsed_ms == 0U && state.accumulator_ms == 1U);
    Soft3D_MazeUpdate(&state, 9U, 0.3f, 0.0f, true);
    assert(state.elapsed_ms == 10U && state.vx > 0.0f);
    x = state.x;
    y = state.y;
    memcpy(orientation, state.ball_orientation, sizeof(orientation));
    Soft3D_MazeUpdate(&state, UINT32_MAX, 0.3f, 0.0f, false);
    Soft3D_MazeUpdate(&state, 10U, 0.0f, 0.0f, true);
    assert(state.x == x && state.y == y && state.elapsed_ms == 20U);
    assert(memcmp(orientation, state.ball_orientation, sizeof(orientation)) == 0);
    Soft3D_MazeUpdate(&state, 100U, NAN, 1.0f, true);
    assert(memcmp(orientation, state.ball_orientation, sizeof(orientation)) == 0);
    Soft3D_MazeUpdate(&state, UINT32_MAX, 0.0f, 0.0f, true);
    assert(state.elapsed_ms == 120U && state.accumulator_ms == 0U);
    state.elapsed_ms = UINT32_MAX - 5U;
    Soft3D_MazeUpdate(&state, 10U, 0.0f, 0.0f, true);
    assert(state.elapsed_ms == UINT32_MAX);
}

static void test_goal_and_restart(void)
{
    Soft3D_MazeState state, fresh;
    const Soft3D_MazeGoal *goal = Soft3D_MazeGoalGet();
    float x, y;
    Soft3D_MazeInit(&state);
    state.x = goal->x + goal->radius - SOFT3D_MAZE_BALL_RADIUS + 0.01f;
    state.y = goal->y;
    Soft3D_MazeUpdate(&state, 10U, 0.0f, 0.0f, true);
    assert(!state.won);
    state.x = goal->x;
    Soft3D_MazeUpdate(&state, 100U, 0.0f, 0.0f, true);
    assert(state.won && state.elapsed_ms == 20U && state.accumulator_ms == 0U);
    x = state.x;
    y = state.y;
    Soft3D_MazeUpdate(&state, 100U, 0.5f, 0.5f, true);
    assert(state.x == x && state.y == y && state.elapsed_ms == 20U);
    assert(state.vx == 0.0f && state.vy == 0.0f);
    Soft3D_MazeRestart(&state);
    Soft3D_MazeInit(&fresh);
    assert(memcmp(&state, &fresh, sizeof(state)) == 0);
}

static void test_quaternion_mapping(void)
{
    const float identity[4] = {1.0f, 0.0f, 0.0f, 0.0f};
    const float pitch[4] = {0.965925826f, 0.0f, 0.258819045f, 0.0f};
    const float roll[4] = {0.965925826f, 0.258819045f, 0.0f, 0.0f};
    const float yaw[4] = {0.707106781f, 0.0f, 0.0f, 0.707106781f};
    const float zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float invalid[4] = {NAN, 0.0f, 0.0f, 0.0f};
    float scaled[4], gx, gy, nx, ny;
    unsigned i;
    assert(Soft3D_MazeGravity(identity, pitch, &gx, &gy));
    near_value(gx, -0.5f, 0.000001f);
    near_value(gy, 0.0f, 0.000001f);
    for (i = 0U; i < 4U; ++i) scaled[i] = -pitch[i] * 100.0f;
    assert(Soft3D_MazeGravity(identity, scaled, &nx, &ny));
    near_value(nx, gx, 0.000001f);
    near_value(ny, gy, 0.000001f);
    assert(Soft3D_MazeGravity(identity, roll, &gx, &gy));
    near_value(gx, 0.0f, 0.000001f);
    near_value(gy, 0.5f, 0.000001f);
    assert(Soft3D_MazeGravity(identity, yaw, &gx, &gy));
    near_value(gx, 0.0f, 0.000001f);
    near_value(gy, 0.0f, 0.000001f);
    assert(Soft3D_MazeGravity(roll, roll, &gx, &gy));
    near_value(gx, 0.0f, 0.000001f);
    near_value(gy, 0.0f, 0.000001f);
    for (i = 0U; i < 4U; ++i) scaled[i] = -roll[i];
    assert(Soft3D_MazeGravity(scaled, roll, &gx, &gy));
    near_value(gx, 0.0f, 0.000001f);
    near_value(gy, 0.0f, 0.000001f);
    scaled[0] = FLT_MAX; scaled[1] = 0.0f; scaled[2] = 0.0f; scaled[3] = 0.0f;
    assert(Soft3D_MazeGravity(scaled, pitch, &gx, &gy));
    near_value(gx, -0.5f, 0.000001f);
    assert(!Soft3D_MazeGravity(identity, zero, &gx, &gy));
    assert(gx == 0.0f && gy == 0.0f);
    assert(!Soft3D_MazeGravity(identity, invalid, &gx, &gy));
    invalid[0] = INFINITY;
    assert(!Soft3D_MazeGravity(invalid, identity, &gx, &gy));
    assert(!Soft3D_MazeGravity(NULL, identity, &gx, &gy));
    assert(!Soft3D_MazeGravity(identity, pitch, NULL, &gy));
}

static void test_board_control_directions(void)
{
    const float neutral[4] = {1.0f, 0.0f, 0.0f, 0.0f};
    static const struct {
        float pose[4];
        float expected_x, expected_y;
    } cases[] = {
        {{0.965925826f, 0.0f, 0.258819045f, 0.0f}, -0.5f, 0.0f},
        {{0.965925826f, 0.0f, -0.258819045f, 0.0f}, 0.5f, 0.0f},
        {{0.965925826f, 0.258819045f, 0.0f, 0.0f}, 0.0f, 0.5f},
        {{0.965925826f, -0.258819045f, 0.0f, 0.0f}, 0.0f, -0.5f}
    };
    unsigned i;
    for (i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        Soft3D_MazeState state;
        float gx, gy, start_x, start_y;
        Soft3D_MazeInit(&state);
        start_x = state.x;
        start_y = state.y;
        assert(Soft3D_MazeGravity(neutral, cases[i].pose, &gx, &gy));
        near_value(gx, cases[i].expected_x, 0.000001f);
        near_value(gy, cases[i].expected_y, 0.000001f);
        Soft3D_MazeUpdate(&state, 100U, gx, gy, true);
        if (cases[i].expected_x == 0.0f) assert(state.x == start_x);
        else assert((state.x - start_x) * cases[i].expected_x > 0.0f);
        if (cases[i].expected_y == 0.0f) assert(state.y == start_y);
        else assert((state.y - start_y) * cases[i].expected_y > 0.0f);
        assert_clear(&state);
    }
}

static Soft3D_MazePoint cell_center(unsigned cell)
{
    Soft3D_MazePoint point;
    point.x = -SOFT3D_MAZE_HALF_WIDTH + ((float)(cell % SOFT3D_MAZE_COLUMNS) + 0.5f) *
              (2.0f * SOFT3D_MAZE_HALF_WIDTH / (float)SOFT3D_MAZE_COLUMNS);
    point.y = -SOFT3D_MAZE_HALF_HEIGHT + ((float)(cell / SOFT3D_MAZE_COLUMNS) + 0.5f) *
              (2.0f * SOFT3D_MAZE_HALF_HEIGHT / (float)SOFT3D_MAZE_ROWS);
    return point;
}

static bool passage_open(unsigned from, unsigned to)
{
    Soft3D_MazePoint a = cell_center(from), b = cell_center(to);
    const Soft3D_MazeWall *walls;
    size_t count, i;
    unsigned sample;
    unsigned dx = from % SOFT3D_MAZE_COLUMNS > to % SOFT3D_MAZE_COLUMNS ?
                  from % SOFT3D_MAZE_COLUMNS - to % SOFT3D_MAZE_COLUMNS :
                  to % SOFT3D_MAZE_COLUMNS - from % SOFT3D_MAZE_COLUMNS;
    unsigned dy = from / SOFT3D_MAZE_COLUMNS > to / SOFT3D_MAZE_COLUMNS ?
                  from / SOFT3D_MAZE_COLUMNS - to / SOFT3D_MAZE_COLUMNS :
                  to / SOFT3D_MAZE_COLUMNS - from / SOFT3D_MAZE_COLUMNS;
    if (dx + dy != 1U) return false;
    walls = Soft3D_MazeWalls(&count);
    for (sample = 0U; sample <= 16U; ++sample) {
        float t = (float)sample / 16.0f;
        float x = a.x + (b.x - a.x) * t, y = a.y + (b.y - a.y) * t;
        for (i = 0U; i < count; ++i) {
            float closest_x = fminf(fmaxf(x, walls[i].min_x), walls[i].max_x);
            float closest_y = fminf(fmaxf(y, walls[i].min_y), walls[i].max_y);
            float gap_x = x - closest_x, gap_y = y - closest_y;
            if (gap_x * gap_x + gap_y * gap_y < SOFT3D_MAZE_BALL_RADIUS * SOFT3D_MAZE_BALL_RADIUS) {
                return false;
            }
        }
    }
    return true;
}

static unsigned solve_route(unsigned route[MAZE_CELLS], bool report)
{
    bool edges[MAZE_CELLS][MAZE_CELLS] = {{false}};
    int parent[MAZE_CELLS];
    unsigned queue[MAZE_CELLS], distance[MAZE_CELLS];
    unsigned a, b, head = 0U, tail = 1U, connections = 0U, dead_ends = 0U, branches = 0U;
    unsigned route_length = 0U;
    unsigned wrong_ends = 0U;
    int cell;
    Soft3D_MazePoint start = cell_center(0U), goal = cell_center(MAZE_CELLS - 1U);
    near_value(Soft3D_MazeStartGet()->x, start.x, 0.00001f);
    near_value(Soft3D_MazeStartGet()->y, start.y, 0.00001f);
    near_value(Soft3D_MazeGoalGet()->x, goal.x, 0.00001f);
    near_value(Soft3D_MazeGoalGet()->y, goal.y, 0.00001f);
    for (a = 0U; a < MAZE_CELLS; ++a) {
        unsigned degree = 0U;
        Soft3D_MazeState state;
        Soft3D_MazePoint center = cell_center(a);
        Soft3D_MazeInit(&state);
        state.x = center.x;
        state.y = center.y;
        assert_clear(&state);
        parent[a] = -1;
        distance[a] = 0U;
        for (b = 0U; b < MAZE_CELLS; ++b) {
            edges[a][b] = passage_open(a, b);
            if (edges[a][b]) ++degree;
        }
        connections += degree;
        if (degree == 1U) {
            ++dead_ends;
            if (a != 0U && a + 1U != MAZE_CELLS) ++wrong_ends;
        }
        if (degree >= 3U) ++branches;
    }
    assert(connections == 2U * (MAZE_CELLS - 1U));
    assert(wrong_ends >= 3U);
    if (Soft3D_MazeSeedGet() == 0U) assert(dead_ends == 5U && branches == 3U);
    queue[0] = 0U;
    parent[0] = 0;
    while (head < tail) {
        a = queue[head++];
        for (b = 0U; b < MAZE_CELLS; ++b) {
            if (edges[a][b] && parent[b] == -1) {
                parent[b] = (int)a;
                distance[b] = distance[a] + 1U;
                queue[tail++] = b;
            }
        }
    }
    assert(tail == MAZE_CELLS);
    assert(distance[MAZE_CELLS - 1U] >= 18U);
    if (Soft3D_MazeSeedGet() == 0U) assert(distance[MAZE_CELLS - 1U] == 30U);
    cell = (int)MAZE_CELLS - 1;
    for (;;) {
        route[route_length++] = (unsigned)cell;
        if (cell == 0) break;
        cell = parent[cell];
    }
    for (a = 0U; a < route_length / 2U; ++a) {
        unsigned swap = route[a];
        route[a] = route[route_length - 1U - a];
        route[route_length - 1U - a] = swap;
    }
    if (report) printf("maze graph level %lu: %u connected cells, %u route steps, %u wrong ends\n",
                        (unsigned long)Soft3D_MazeLevelGet(), MAZE_CELLS, route_length - 1U, wrong_ends);
    return route_length;
}

static void test_rolling(void)
{
    Soft3D_MazeState state;
    float x, y, angle;
    Soft3D_MazeInit(&state);
    x = state.x;
    Soft3D_MazeUpdate(&state, 10U, 0.3f, 0.0f, true);
    angle = (state.x - x) / (2.0f * SOFT3D_MAZE_BALL_RADIUS);
    near_value(state.ball_orientation[0], cosf(angle), 0.000001f);
    near_value(state.ball_orientation[1], 0.0f, 0.000001f);
    near_value(state.ball_orientation[2], -sinf(angle), 0.000001f);
    near_value(state.ball_orientation[3], 0.0f, 0.000001f);
    Soft3D_MazeRestart(&state);
    y = state.y;
    Soft3D_MazeUpdate(&state, 10U, 0.0f, 0.3f, true);
    angle = (state.y - y) / (2.0f * SOFT3D_MAZE_BALL_RADIUS);
    near_value(state.ball_orientation[0], cosf(angle), 0.000001f);
    near_value(state.ball_orientation[1], sinf(angle), 0.000001f);
    near_value(state.ball_orientation[2], 0.0f, 0.000001f);
    near_value(state.ball_orientation[3], 0.0f, 0.000001f);
    assert_clear(&state);
}

static void test_route_is_solvable(void)
{
    unsigned route[MAZE_CELLS];
    unsigned route_length = solve_route(route, true);
    Soft3D_MazeState state;
    unsigned waypoint = 1U;
    unsigned tick;
    Soft3D_MazeInit(&state);
    for (tick = 0U; tick < 15000U && !state.won; ++tick) {
        Soft3D_MazePoint target = cell_center(route[waypoint]);
        float dx = target.x - state.x;
        float dy = target.y - state.y;
        float gx, gy;
        if (dx * dx + dy * dy < 0.0016f && waypoint + 1U < route_length) {
            ++waypoint;
            target = cell_center(route[waypoint]);
            dx = target.x - state.x;
            dy = target.y - state.y;
        }
        gx = 0.9f * dx - 0.5f * state.vx;
        gy = 0.9f * dy - 0.5f * state.vy;
        Soft3D_MazeUpdate(&state, 10U, gx, gy, true);
        assert_clear(&state);
    }
    assert(state.won && waypoint == route_length - 1U);
    assert(state.elapsed_ms > 10000U && state.elapsed_ms < 120000U);
    printf("maze route: %lu ms\n", (unsigned long)state.elapsed_ms);
}

static void test_level_selection(void)
{
    Soft3D_MazeWall fixed[SOFT3D_MAZE_MAX_WALLS], saved[SOFT3D_MAZE_MAX_WALLS];
    size_t fixed_count, count, repeated_count;
    uint32_t level;
    unsigned route[MAZE_CELLS];
    const Soft3D_MazeWall *walls;
    assert(Soft3D_MazeSelectLevel(1U));
    walls = Soft3D_MazeWalls(&fixed_count);
    memcpy(fixed, walls, fixed_count * sizeof(fixed[0]));
    for (level = 2U; level <= 129U; ++level) {
        Soft3D_MazeState state;
        uint32_t seed;
        assert(Soft3D_MazeSelectLevel(level));
        seed = Soft3D_MazeSeedGet();
        walls = Soft3D_MazeWalls(&count);
        assert(count >= 4U && count <= SOFT3D_MAZE_MAX_WALLS);
        assert(memcmp(walls, fixed, 4U * sizeof(fixed[0])) == 0);
        memcpy(saved, walls, count * sizeof(saved[0]));
        (void)solve_route(route, false);
        assert(!Soft3D_MazeSelectLevel(0U));
        assert(!Soft3D_MazeSelectLevel(SOFT3D_MAZE_MAX_LEVEL + 1U));
        assert(!Soft3D_MazeSelectLevel(UINT32_MAX));
        assert(Soft3D_MazeLevelGet() == level && Soft3D_MazeSeedGet() == seed);
        Soft3D_MazeInit(&state);
        Soft3D_MazeRestart(&state);
        assert(Soft3D_MazeLevelGet() == level && Soft3D_MazeSeedGet() == seed);
        walls = Soft3D_MazeWalls(&repeated_count);
        assert(count == repeated_count && memcmp(saved, walls, count * sizeof(saved[0])) == 0);
        assert(Soft3D_MazeSelectLevel(1U));
        assert(Soft3D_MazeSelectLevel(level));
        walls = Soft3D_MazeWalls(&repeated_count);
        assert(count == repeated_count && Soft3D_MazeSeedGet() == seed);
        assert(memcmp(saved, walls, count * sizeof(saved[0])) == 0);
    }
    assert(Soft3D_MazeSelectLevel(2U));
    test_route_is_solvable();
    assert(Soft3D_MazeSelectLevel(17U));
    test_route_is_solvable();
    assert(Soft3D_MazeSelectLevel(SOFT3D_MAZE_MAX_LEVEL));
    test_route_is_solvable();
    assert(Soft3D_MazeSelectLevel(1U));
    walls = Soft3D_MazeWalls(&count);
    assert(count == fixed_count && Soft3D_MazeSeedGet() == 0U);
    assert(memcmp(fixed, walls, count * sizeof(fixed[0])) == 0);
    puts("maze levels: 128 reproducible connected layouts and selected pressure scenes passed");
}

int main(void)
{
    assert(Soft3D_MazeLevelGet() == 1U && Soft3D_MazeSeedGet() == 0U);
    test_initial_state_and_gravity();
    test_wall_corner_and_speed();
    test_frame_partition();
    test_pause_and_bounded_backlog();
    test_goal_and_restart();
    test_quaternion_mapping();
    test_board_control_directions();
    test_rolling();
    test_route_is_solvable();
    test_level_selection();
    puts("soft3d_maze: collision, fixed-step, pause, goal and quaternion tests passed");
    return 0;
}
