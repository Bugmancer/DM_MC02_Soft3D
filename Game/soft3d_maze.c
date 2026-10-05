#include "soft3d_maze.h"

#include <float.h>
#include <math.h>
#include <string.h>

#define CUTE_C2_IMPLEMENTATION
#include "../ThirdParty/cute_c2/cute_c2_port.h"

#define MAZE_MAX_TILT 0.5f
#define MAZE_TILT_DEADZONE 0.015f
#define MAZE_ACCELERATION 9.81f
#define MAZE_DRAG 2.0f
#define MAZE_CONTACT_MARGIN 0.00001f
#define WALL_HALF 0.09f
#define CELL_WIDTH (2.0f * SOFT3D_MAZE_HALF_WIDTH / (float)SOFT3D_MAZE_COLUMNS)
#define CELL_HEIGHT (2.0f * SOFT3D_MAZE_HALF_HEIGHT / (float)SOFT3D_MAZE_ROWS)
#define GRID_X(column) (-SOFT3D_MAZE_HALF_WIDTH + (float)(column) * CELL_WIDTH)
#define GRID_Y(row) (-SOFT3D_MAZE_HALF_HEIGHT + (float)(row) * CELL_HEIGHT)
#define MAZE_CELLS (SOFT3D_MAZE_COLUMNS * SOFT3D_MAZE_ROWS)
#define MAZE_GENERATION_ATTEMPTS 64U

/* The first four boxes are the boundary. Collinear grid walls are merged. */
static const Soft3D_MazeWall s_walls[] = {
    {-3.4f, -2.6f, -3.2f, 2.6f},
    { 3.2f, -2.6f,  3.4f, 2.6f},
    {-3.4f, -2.6f,  3.4f, -2.4f},
    {-3.4f,  2.4f,  3.4f, 2.6f},
    {GRID_X(1) - WALL_HALF, GRID_Y(0) - WALL_HALF, GRID_X(1) + WALL_HALF, GRID_Y(1) + WALL_HALF},
    {GRID_X(1) - WALL_HALF, GRID_Y(3) - WALL_HALF, GRID_X(1) + WALL_HALF, GRID_Y(4) + WALL_HALF},
    {GRID_X(2) - WALL_HALF, GRID_Y(1) - WALL_HALF, GRID_X(2) + WALL_HALF, GRID_Y(2) + WALL_HALF},
    {GRID_X(3) - WALL_HALF, GRID_Y(0) - WALL_HALF, GRID_X(3) + WALL_HALF, GRID_Y(1) + WALL_HALF},
    {GRID_X(3) - WALL_HALF, GRID_Y(2) - WALL_HALF, GRID_X(3) + WALL_HALF, GRID_Y(3) + WALL_HALF},
    {GRID_X(4) - WALL_HALF, GRID_Y(1) - WALL_HALF, GRID_X(4) + WALL_HALF, GRID_Y(2) + WALL_HALF},
    {GRID_X(4) - WALL_HALF, GRID_Y(3) - WALL_HALF, GRID_X(4) + WALL_HALF, GRID_Y(4) + WALL_HALF},
    {GRID_X(5) - WALL_HALF, GRID_Y(0) - WALL_HALF, GRID_X(5) + WALL_HALF, GRID_Y(1) + WALL_HALF},
    {GRID_X(6) - WALL_HALF, GRID_Y(2) - WALL_HALF, GRID_X(6) + WALL_HALF, GRID_Y(3) + WALL_HALF},
    {GRID_X(5) - WALL_HALF, GRID_Y(1) - WALL_HALF, GRID_X(6) + WALL_HALF, GRID_Y(1) + WALL_HALF},
    {GRID_X(0) - WALL_HALF, GRID_Y(2) - WALL_HALF, GRID_X(6) + WALL_HALF, GRID_Y(2) + WALL_HALF},
    {GRID_X(2) - WALL_HALF, GRID_Y(3) - WALL_HALF, GRID_X(3) + WALL_HALF, GRID_Y(3) + WALL_HALF},
    {GRID_X(5) - WALL_HALF, GRID_Y(3) - WALL_HALF, GRID_X(6) + WALL_HALF, GRID_Y(3) + WALL_HALF},
    {GRID_X(1) - WALL_HALF, GRID_Y(4) - WALL_HALF, GRID_X(7) + WALL_HALF, GRID_Y(4) + WALL_HALF}
};
static const Soft3D_MazePoint s_start = {GRID_X(0.5f), GRID_Y(0.5f)};
static const Soft3D_MazeGoal s_goal = {GRID_X(6.5f), GRID_Y(4.5f), 0.35f};
static Soft3D_MazeWall s_generated_walls[SOFT3D_MAZE_MAX_WALLS];
static const Soft3D_MazeWall *s_active_walls = s_walls;
static size_t s_active_wall_count = sizeof(s_walls) / sizeof(s_walls[0]);
static uint32_t s_level = 1U;
static uint32_t s_seed;

static bool normalize_quaternion(const float input[4], float output[4]);

static uint32_t maze_random(uint32_t *state)
{
    uint32_t value = *state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return value;
}

static uint32_t maze_seed(uint32_t level, uint32_t attempt)
{
    uint32_t value = level + (attempt + 1U) * UINT32_C(0x9e3779b9);
    value = (value ^ (value >> 16)) * UINT32_C(0x7feb352d);
    value = (value ^ (value >> 15)) * UINT32_C(0x846ca68b);
    value ^= value >> 16;
    return value != 0U ? value : UINT32_C(0x6d2b79f5);
}

/* Passage bits are east, north, west, south. Invalid neighbors never index arrays. */
static uint8_t neighbor(unsigned cell, unsigned direction)
{
    unsigned x = cell % SOFT3D_MAZE_COLUMNS;
    unsigned y = cell / SOFT3D_MAZE_COLUMNS;
    switch (direction) {
    case 0U: return x + 1U < SOFT3D_MAZE_COLUMNS ? (uint8_t)(cell + 1U) : UINT8_MAX;
    case 1U: return y + 1U < SOFT3D_MAZE_ROWS ? (uint8_t)(cell + SOFT3D_MAZE_COLUMNS) : UINT8_MAX;
    case 2U: return x > 0U ? (uint8_t)(cell - 1U) : UINT8_MAX;
    default: return y > 0U ? (uint8_t)(cell - SOFT3D_MAZE_COLUMNS) : UINT8_MAX;
    }
}

static void generate_tree(uint32_t seed, uint8_t passages[MAZE_CELLS])
{
    uint8_t stack[MAZE_CELLS];
    uint8_t visited[MAZE_CELLS] = {0U};
    unsigned depth = 1U;
    memset(passages, 0, MAZE_CELLS * sizeof(passages[0]));
    stack[0] = 0U;
    visited[0] = 1U;
    /* Each iteration either visits a new cell or removes one stack entry. */
    while (depth != 0U) {
        unsigned cell = stack[depth - 1U];
        uint8_t directions[4];
        unsigned direction, count = 0U;
        for (direction = 0U; direction < 4U; ++direction) {
            uint8_t next = neighbor(cell, direction);
            if (next != UINT8_MAX && visited[next] == 0U) {
                directions[count++] = (uint8_t)direction;
            }
        }
        if (count == 0U) --depth;
        else {
            uint8_t next;
            direction = directions[maze_random(&seed) % count];
            next = neighbor(cell, direction);
            passages[cell] |= (uint8_t)(1U << direction);
            passages[next] |= (uint8_t)(1U << ((direction + 2U) % 4U));
            visited[next] = 1U;
            stack[depth++] = next;
        }
    }
}

static bool tree_is_playable(const uint8_t passages[MAZE_CELLS])
{
    uint8_t queue[MAZE_CELLS], distance[MAZE_CELLS];
    unsigned head = 0U, tail = 1U, wrong_ends = 0U, cell;
    memset(distance, UINT8_MAX, sizeof(distance));
    queue[0] = 0U;
    distance[0] = 0U;
    while (head < tail) {
        unsigned direction;
        cell = queue[head++];
        for (direction = 0U; direction < 4U; ++direction) {
            uint8_t next = neighbor(cell, direction);
            if ((passages[cell] & (1U << direction)) != 0U &&
                next != UINT8_MAX && distance[next] == UINT8_MAX) {
                distance[next] = (uint8_t)(distance[cell] + 1U);
                queue[tail++] = next;
            }
        }
    }
    for (cell = 1U; cell + 1U < MAZE_CELLS; ++cell) {
        unsigned bits = passages[cell];
        if (bits != 0U && (bits & (bits - 1U)) == 0U) ++wrong_ends;
    }
    return tail == MAZE_CELLS && distance[MAZE_CELLS - 1U] >= 18U && wrong_ends >= 3U;
}

static bool append_wall(Soft3D_MazeWall walls[SOFT3D_MAZE_MAX_WALLS], size_t *count,
                        float min_x, float min_y, float max_x, float max_y)
{
    Soft3D_MazeWall *wall;
    if (*count >= SOFT3D_MAZE_MAX_WALLS) return false;
    wall = &walls[(*count)++];
    wall->min_x = min_x;
    wall->min_y = min_y;
    wall->max_x = max_x;
    wall->max_y = max_y;
    return true;
}

static bool merge_walls(const uint8_t passages[MAZE_CELLS],
                         Soft3D_MazeWall walls[SOFT3D_MAZE_MAX_WALLS], size_t *count)
{
    unsigned x, y;
    memcpy(walls, s_walls, 4U * sizeof(walls[0]));
    *count = 4U;
    for (x = 1U; x < SOFT3D_MAZE_COLUMNS; ++x) {
        unsigned start = SOFT3D_MAZE_ROWS;
        for (y = 0U; y <= SOFT3D_MAZE_ROWS; ++y) {
            bool blocked = y < SOFT3D_MAZE_ROWS &&
                           (passages[y * SOFT3D_MAZE_COLUMNS + x - 1U] & 1U) == 0U;
            if (blocked && start == SOFT3D_MAZE_ROWS) start = y;
            if (!blocked && start != SOFT3D_MAZE_ROWS) {
                if (!append_wall(walls, count, GRID_X(x) - WALL_HALF, GRID_Y(start) - WALL_HALF,
                                  GRID_X(x) + WALL_HALF, GRID_Y(y) + WALL_HALF)) return false;
                start = SOFT3D_MAZE_ROWS;
            }
        }
    }
    for (y = 1U; y < SOFT3D_MAZE_ROWS; ++y) {
        unsigned start = SOFT3D_MAZE_COLUMNS;
        for (x = 0U; x <= SOFT3D_MAZE_COLUMNS; ++x) {
            bool blocked = x < SOFT3D_MAZE_COLUMNS &&
                           (passages[(y - 1U) * SOFT3D_MAZE_COLUMNS + x] & 2U) == 0U;
            if (blocked && start == SOFT3D_MAZE_COLUMNS) start = x;
            if (!blocked && start != SOFT3D_MAZE_COLUMNS) {
                if (!append_wall(walls, count, GRID_X(start) - WALL_HALF, GRID_Y(y) - WALL_HALF,
                                  GRID_X(x) + WALL_HALF, GRID_Y(y) + WALL_HALF)) return false;
                start = SOFT3D_MAZE_COLUMNS;
            }
        }
    }
    return true;
}

bool Soft3D_MazeSelectLevel(uint32_t level)
{
    uint32_t attempt;
    if (level == 0U || level > SOFT3D_MAZE_MAX_LEVEL) return false;
    if (level == s_level) return true;
    if (level != 1U) {
        for (attempt = 0U; attempt < MAZE_GENERATION_ATTEMPTS; ++attempt) {
            uint8_t passages[MAZE_CELLS];
            Soft3D_MazeWall walls[SOFT3D_MAZE_MAX_WALLS];
            size_t count;
            uint32_t seed = maze_seed(level, attempt);
            generate_tree(seed, passages);
            if (tree_is_playable(passages) && merge_walls(passages, walls, &count)) {
                memcpy(s_generated_walls, walls, count * sizeof(walls[0]));
                s_active_walls = s_generated_walls;
                s_active_wall_count = count;
                s_seed = seed;
                s_level = level;
                return true;
            }
        }
    }
    s_active_walls = s_walls;
    s_active_wall_count = sizeof(s_walls) / sizeof(s_walls[0]);
    s_seed = 0U;
    s_level = level;
    return true;
}

uint32_t Soft3D_MazeLevelGet(void)
{
    return s_level;
}

uint32_t Soft3D_MazeSeedGet(void)
{
    return s_seed;
}

static bool finite_value(float value)
{
    return value == value && value <= FLT_MAX && value >= -FLT_MAX;
}

static void limit_vector(float *x, float *y, float limit)
{
    float largest = fmaxf(fabsf(*x), fabsf(*y));
    float nx, ny, length;
    if (largest == 0.0f) return;
    nx = *x / largest;
    ny = *y / largest;
    length = sqrtf(nx * nx + ny * ny);
    if (largest > limit / length) {
        *x = nx * (limit / length);
        *y = ny * (limit / length);
    }
}

void Soft3D_MazeInit(Soft3D_MazeState *state)
{
    Soft3D_MazeRestart(state);
}

void Soft3D_MazeRestart(Soft3D_MazeState *state)
{
    if (state == NULL) return;
    memset(state, 0, sizeof(*state));
    state->x = s_start.x;
    state->y = s_start.y;
    state->ball_orientation[0] = 1.0f;
    state->paused = true;
}

const Soft3D_MazeWall *Soft3D_MazeWalls(size_t *count)
{
    if (count != NULL) *count = s_active_wall_count;
    return s_active_walls;
}

const Soft3D_MazePoint *Soft3D_MazeStartGet(void)
{
    return &s_start;
}

const Soft3D_MazeGoal *Soft3D_MazeGoalGet(void)
{
    return &s_goal;
}

static void resolve_walls(Soft3D_MazeState *state)
{
    unsigned pass;
    size_t count;
    const Soft3D_MazeWall *walls = Soft3D_MazeWalls(&count);
    /* Revisit adjacent walls after a corner correction; normals point into walls. */
    for (pass = 0U; pass < 4U; ++pass) {
        size_t i;
        bool touched = false;
        for (i = 0U; i < count; ++i) {
            c2Circle ball;
            c2AABB wall;
            c2Manifold contact;
            float inward;
            ball.p = c2V(state->x, state->y);
            ball.r = SOFT3D_MAZE_BALL_RADIUS;
            wall.min = c2V(walls[i].min_x, walls[i].min_y);
            wall.max = c2V(walls[i].max_x, walls[i].max_y);
            c2CircletoAABBManifold(ball, wall, &contact);
            if (contact.count == 0) continue;
            touched = true;
            state->x -= contact.n.x * (contact.depths[0] + MAZE_CONTACT_MARGIN);
            state->y -= contact.n.y * (contact.depths[0] + MAZE_CONTACT_MARGIN);
            inward = state->vx * contact.n.x + state->vy * contact.n.y;
            if (inward > 0.0f) {
                state->vx -= contact.n.x * inward;
                state->vy -= contact.n.y * inward;
            }
        }
        if (!touched) break;
    }
}

static void roll_ball(Soft3D_MazeState *state, float dx, float dy)
{
    float distance = sqrtf(dx * dx + dy * dy);
    float half_angle, sine, dw, ax, ay;
    float next[4];
    const float *q = state->ball_orientation;
    if (distance == 0.0f) return;
    half_angle = distance / (2.0f * SOFT3D_MAZE_BALL_RADIUS);
    sine = sinf(half_angle) / distance;
    dw = cosf(half_angle);
    ax = dy * sine;
    ay = -dx * sine;
    /* World-axis delta left-multiplies the accumulated model orientation. */
    next[0] = dw*q[0] - ax*q[1] - ay*q[2];
    next[1] = dw*q[1] + ax*q[0] + ay*q[3];
    next[2] = dw*q[2] - ax*q[3] + ay*q[0];
    next[3] = dw*q[3] + ax*q[2] - ay*q[1];
    (void)normalize_quaternion(next, state->ball_orientation);
}

static void step(Soft3D_MazeState *state)
{
    const float dt = (float)SOFT3D_MAZE_STEP_MS * 0.001f;
    const float previous_x = state->x, previous_y = state->y;
    c2Circle center, goal;
    state->vx = (state->vx + MAZE_ACCELERATION * state->tilt_x * dt) / (1.0f + MAZE_DRAG * dt);
    state->vy = (state->vy + MAZE_ACCELERATION * state->tilt_y * dt) / (1.0f + MAZE_DRAG * dt);
    /* At this limit a step is 0.024 units, below the 0.18 radius and wall width. */
    limit_vector(&state->vx, &state->vy, SOFT3D_MAZE_MAX_SPEED);
    state->x += state->vx * dt;
    state->y += state->vy * dt;
    resolve_walls(state);
    roll_ball(state, state->x - previous_x, state->y - previous_y);
    if (state->elapsed_ms <= UINT32_MAX - SOFT3D_MAZE_STEP_MS) {
        state->elapsed_ms += SOFT3D_MAZE_STEP_MS;
    } else state->elapsed_ms = UINT32_MAX;
    center.p = c2V(state->x, state->y);
    center.r = 0.0f;
    goal.p = c2V(s_goal.x, s_goal.y);
    goal.r = s_goal.radius - SOFT3D_MAZE_BALL_RADIUS;
    if (c2CircletoCircle(center, goal)) {
        state->won = true;
        state->vx = 0.0f;
        state->vy = 0.0f;
        state->accumulator_ms = 0U;
    }
}

void Soft3D_MazeUpdate(Soft3D_MazeState *state, uint32_t elapsed_ms,
                       float gravity_x, float gravity_y, bool active)
{
    if (state == NULL) return;
    state->paused = !active || !finite_value(gravity_x) || !finite_value(gravity_y);
    if (state->paused) {
        state->vx = 0.0f;
        state->vy = 0.0f;
        state->tilt_x = 0.0f;
        state->tilt_y = 0.0f;
        state->accumulator_ms = 0U;
        return;
    }
    if (state->won) return;
    limit_vector(&gravity_x, &gravity_y, MAZE_MAX_TILT);
    if (gravity_x * gravity_x + gravity_y * gravity_y < MAZE_TILT_DEADZONE * MAZE_TILT_DEADZONE) {
        gravity_x = 0.0f;
        gravity_y = 0.0f;
    }
    state->tilt_x = gravity_x;
    state->tilt_y = gravity_y;
    if (elapsed_ms > SOFT3D_MAZE_MAX_UPDATE_MS) elapsed_ms = SOFT3D_MAZE_MAX_UPDATE_MS;
    state->accumulator_ms += elapsed_ms;
    while (state->accumulator_ms >= SOFT3D_MAZE_STEP_MS && !state->won) {
        state->accumulator_ms -= SOFT3D_MAZE_STEP_MS;
        step(state);
    }
}

static bool normalize_quaternion(const float input[4], float output[4])
{
    float largest = 0.0f;
    float square = 0.0f;
    unsigned i;
    if (input == NULL) return false;
    for (i = 0U; i < 4U; ++i) {
        if (!finite_value(input[i])) return false;
        if (fabsf(input[i]) > largest) largest = fabsf(input[i]);
    }
    if (largest == 0.0f) return false;
    for (i = 0U; i < 4U; ++i) {
        output[i] = input[i] / largest;
        square += output[i] * output[i];
    }
    square = 1.0f / sqrtf(square);
    for (i = 0U; i < 4U; ++i) output[i] *= square;
    return true;
}

bool Soft3D_MazeGravity(const float reference_wxyz[4], const float current_wxyz[4],
                        float *gravity_x, float *gravity_y)
{
    float r[4], q[4];
    float w, x, y, z;
    if (gravity_x != NULL) *gravity_x = 0.0f;
    if (gravity_y != NULL) *gravity_y = 0.0f;
    if (gravity_x == NULL || gravity_y == NULL ||
        !normalize_quaternion(reference_wxyz, r) || !normalize_quaternion(current_wxyz, q)) return false;
    w = r[0]*q[0] + r[1]*q[1] + r[2]*q[2] + r[3]*q[3];
    x = r[0]*q[1] - r[1]*q[0] - r[2]*q[3] + r[3]*q[2];
    y = r[0]*q[2] + r[1]*q[3] - r[2]*q[0] - r[3]*q[1];
    z = r[0]*q[3] - r[1]*q[2] + r[2]*q[1] - r[3]*q[0];
    /* Board feedback requires both sensor axes reversed for landscape play. */
    *gravity_x = -2.0f * (w*y - x*z);
    *gravity_y = 2.0f * (w*x + y*z);
    limit_vector(gravity_x, gravity_y, 1.0f);
    return true;
}
