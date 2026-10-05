#include "soft3d_maze_scene.h"

#include <math.h>
#include <stddef.h>

#define VIEW_DISTANCE 7.35f
#define VIEW_HEIGHT 0.24f
#define WALL_HEIGHT 0.22f
#define DISC_SEGMENTS 16U

static const uint16_t box_indices[] = {
    0, 2, 1, 0, 3, 2,
    0, 1, 5, 0, 5, 4, 3, 7, 6, 3, 6, 2,
    0, 4, 7, 0, 7, 3, 1, 2, 6, 1, 6, 5
};
static const uint16_t floor_pixels[16] = {
    0xD69AU, 0xCE59U, 0xD69AU, 0xCE59U,
    0xCE59U, 0xD69AU, 0xCE59U, 0xD69AU,
    0xD69AU, 0xCE59U, 0xD69AU, 0xCE59U,
    0xCE59U, 0xD69AU, 0xCE59U, 0xD69AU
};
static const Soft3D_Texture floor_texture = {floor_pixels, 4U, 4U};

static float bounded_tilt(float value)
{
    if (!(value >= -1.0f && value <= 1.0f)) return 0.0f;
    if (value < -0.35f) return -0.35f;
    if (value > 0.35f) return 0.35f;
    return value;
}

Soft3D_Camera Soft3D_MazeSceneCamera(void)
{
    const Soft3D_Camera camera = {1.0471975512f, 0.35f, 20.0f};
    return camera;
}

static int submit_box(Soft3D_Context *ctx, float left, float bottom,
                       float right, float top, float front_z, float back_z,
                       const Soft3D_Transform *view, uint16_t top_color,
                       uint16_t y_color, uint16_t x_color)
{
    const Soft3D_Vertex vertices[] = {
        {left, bottom, front_z, 0, 1}, {right, bottom, front_z, 1, 1},
        {right, top, front_z, 1, 0}, {left, top, front_z, 0, 0},
        {left, bottom, back_z, 0, 1}, {right, bottom, back_z, 1, 1},
        {right, top, back_z, 1, 0}, {left, top, back_z, 0, 0}
    };
    Soft3D_Mesh mesh = {vertices, box_indices, 8U, 2U};
    Soft3D_Material material = {SOFT3D_LIT, top_color, NULL, 1U};
    int success = soft3d_submit(ctx, &mesh, view, &material);
    mesh.indices = box_indices + 6U;
    mesh.triangle_count = 4U;
    material.color = y_color;
    if (!soft3d_submit(ctx, &mesh, view, &material)) success = 0;
    mesh.indices = box_indices + 18U;
    material.color = x_color;
    if (!soft3d_submit(ctx, &mesh, view, &material)) success = 0;
    return success;
}

static int submit_quad(Soft3D_Context *ctx, float left, float bottom,
                        float right, float top, float z,
                        const Soft3D_Transform *view, uint16_t color, int textured)
{
    const Soft3D_Vertex vertices[] = {
        {left, bottom, z, 0, 1}, {right, bottom, z, 1, 1},
        {right, top, z, 1, 0}, {left, top, z, 0, 0}
    };
    const Soft3D_Mesh mesh = {vertices, box_indices, 4U, 2U};
    const Soft3D_Material material = {
        textured ? SOFT3D_TEXTURED : SOFT3D_LIT, color,
        textured ? &floor_texture : NULL, 1U
    };
    return soft3d_submit(ctx, &mesh, view, &material);
}

static int submit_disc(Soft3D_Context *ctx, float x, float y, float radius,
                        float z, uint16_t color, const Soft3D_Transform *view)
{
    Soft3D_Vertex vertices[DISC_SEGMENTS + 1U];
    uint16_t indices[DISC_SEGMENTS * 3U];
    const Soft3D_Mesh mesh = {vertices, indices, DISC_SEGMENTS + 1U, DISC_SEGMENTS};
    const Soft3D_Material material = {SOFT3D_LIT, color, NULL, 0U};
    unsigned i;
    vertices[0].x = x; vertices[0].y = y; vertices[0].z = z;
    vertices[0].u = vertices[0].v = 0.0f;
    for (i = 0U; i < DISC_SEGMENTS; ++i) {
        float angle = (float)i * 0.3926990817f;
        vertices[i + 1U].x = x + cosf(angle) * radius;
        vertices[i + 1U].y = y + sinf(angle) * radius;
        vertices[i + 1U].z = z;
        vertices[i + 1U].u = vertices[i + 1U].v = 0.0f;
        indices[i * 3U] = 0U;
        indices[i * 3U + 1U] = (uint16_t)(1U + ((i + 1U) % DISC_SEGMENTS));
        indices[i * 3U + 2U] = (uint16_t)(i + 1U);
    }
    return soft3d_submit(ctx, &mesh, view, &material);
}

/* The seam vertices keep the stripe UVs local while the quaternion rolls them. */
static const Soft3D_Vertex ball_vertices[] = {
    {0, 0, -1, 0.5f, 0},
    {0.707107f, 0, -0.707107f, 0, 0.25f}, {0.5f, 0.5f, -0.707107f, 0.125f, 0.25f},
    {0, 0.707107f, -0.707107f, 0.25f, 0.25f}, {-0.5f, 0.5f, -0.707107f, 0.375f, 0.25f},
    {-0.707107f, 0, -0.707107f, 0.5f, 0.25f}, {-0.5f, -0.5f, -0.707107f, 0.625f, 0.25f},
    {0, -0.707107f, -0.707107f, 0.75f, 0.25f}, {0.5f, -0.5f, -0.707107f, 0.875f, 0.25f},
    {0.707107f, 0, -0.707107f, 1, 0.25f},
    {1, 0, 0, 0, 0.5f}, {0.707107f, 0.707107f, 0, 0.125f, 0.5f},
    {0, 1, 0, 0.25f, 0.5f}, {-0.707107f, 0.707107f, 0, 0.375f, 0.5f},
    {-1, 0, 0, 0.5f, 0.5f}, {-0.707107f, -0.707107f, 0, 0.625f, 0.5f},
    {0, -1, 0, 0.75f, 0.5f}, {0.707107f, -0.707107f, 0, 0.875f, 0.5f},
    {1, 0, 0, 1, 0.5f},
    {0.707107f, 0, 0.707107f, 0, 0.75f}, {0.5f, 0.5f, 0.707107f, 0.125f, 0.75f},
    {0, 0.707107f, 0.707107f, 0.25f, 0.75f}, {-0.5f, 0.5f, 0.707107f, 0.375f, 0.75f},
    {-0.707107f, 0, 0.707107f, 0.5f, 0.75f}, {-0.5f, -0.5f, 0.707107f, 0.625f, 0.75f},
    {0, -0.707107f, 0.707107f, 0.75f, 0.75f}, {0.5f, -0.5f, 0.707107f, 0.875f, 0.75f},
    {0.707107f, 0, 0.707107f, 1, 0.75f},
    {0, 0, 1, 0.5f, 1}
};
static const uint16_t ball_indices[] = {
    0, 2, 1, 0, 3, 2, 0, 4, 3, 0, 5, 4, 0, 6, 5, 0, 7, 6, 0, 8, 7, 0, 9, 8,
    1, 2, 10, 2, 11, 10, 2, 3, 11, 3, 12, 11, 3, 4, 12, 4, 13, 12,
    4, 5, 13, 5, 14, 13, 5, 6, 14, 6, 15, 14, 6, 7, 15, 7, 16, 15,
    7, 8, 16, 8, 17, 16, 8, 9, 17, 9, 18, 17,
    10, 11, 19, 11, 20, 19, 11, 12, 20, 12, 21, 20, 12, 13, 21, 13, 22, 21,
    13, 14, 22, 14, 23, 22, 14, 15, 23, 15, 24, 23, 15, 16, 24, 16, 25, 24,
    16, 17, 25, 17, 26, 25, 17, 18, 26, 18, 27, 26,
    28, 19, 20, 28, 20, 21, 28, 21, 22, 28, 22, 23,
    28, 23, 24, 28, 24, 25, 28, 25, 26, 28, 26, 27
};
static const Soft3D_Mesh ball_mesh = {ball_vertices, ball_indices, 29U, 48U};
static const uint16_t ball_pixels[16] = {
    0xFA80U, 0xFB40U, 0xFB40U, 0xFB40U, 0xFFBCU, 0xFFBCU, 0xD940U, 0xF240U,
    0xFA80U, 0xFB40U, 0xFB40U, 0xFB40U, 0xFFBCU, 0xFFBCU, 0xD940U, 0xF240U
};
static const Soft3D_Texture ball_texture = {ball_pixels, 16U, 1U};

static int submit_ball(Soft3D_Context *ctx, const Soft3D_MazeState *maze,
                        const Soft3D_Transform *view)
{
    Soft3D_Vec3 position;
    Soft3D_Quaternion orientation;
    const Soft3D_Material material = {SOFT3D_TEXTURED, 0xFFFFU, &ball_texture, 1U};
    float z = -SOFT3D_MAZE_BALL_RADIUS - 0.008f;
    float cx = cosf(view->rotation.x), sx = sinf(view->rotation.x);
    float cy = cosf(view->rotation.y), sy = sinf(view->rotation.y);
    float ry = maze->y * cx - z * sx;
    float rz = maze->y * sx + z * cx;
    float hx = view->rotation.x * 0.5f, hy = view->rotation.y * 0.5f;
    float vw = cosf(hy) * cosf(hx), vx = cosf(hy) * sinf(hx);
    float vy = sinf(hy) * cosf(hx), vz = -sinf(hy) * sinf(hx);
    const float *q = maze->ball_orientation;
    position.x = maze->x * cy + rz * sy;
    position.y = ry + view->position.y;
    position.z = -maze->x * sy + rz * cy + view->position.z;
    /* World-space rolling precedes the board-to-camera rotation. */
    orientation.w = vw*q[0] - vx*q[1] - vy*q[2] - vz*q[3];
    orientation.x = vw*q[1] + vx*q[0] + vy*q[3] - vz*q[2];
    orientation.y = vw*q[2] - vx*q[3] + vy*q[0] + vz*q[1];
    orientation.z = vw*q[3] + vx*q[2] - vy*q[1] + vz*q[0];
    return soft3d_submit_quaternion(ctx, &ball_mesh, &position, &orientation,
                                     SOFT3D_MAZE_BALL_RADIUS, &material);
}

int Soft3D_MazeSceneSubmit(Soft3D_Context *ctx, const Soft3D_MazeState *maze)
{
    Soft3D_Transform view = {{0, VIEW_HEIGHT, VIEW_DISTANCE}, {0, 0, 0}, 1};
    const Soft3D_MazeWall *walls;
    const Soft3D_MazeGoal *goal;
    const Soft3D_MazePoint *start;
    size_t count, i;
    int success;
    if (ctx == NULL || maze == NULL ||
        !(maze->x >= -SOFT3D_MAZE_HALF_WIDTH && maze->x <= SOFT3D_MAZE_HALF_WIDTH) ||
        !(maze->y >= -SOFT3D_MAZE_HALF_HEIGHT && maze->y <= SOFT3D_MAZE_HALF_HEIGHT)) return 0;
    view.rotation.x = 0.65f + bounded_tilt(maze->tilt_y) * 0.20f;
    view.rotation.y = -0.15f - bounded_tilt(maze->tilt_x) * 0.14f;
    walls = Soft3D_MazeWalls(&count);
    goal = Soft3D_MazeGoalGet();
    start = Soft3D_MazeStartGet();
    if (walls == NULL || goal == NULL || start == NULL || count > 24U) return 0;
    success = submit_box(ctx, -SOFT3D_MAZE_HALF_WIDTH - 0.25f,
        -SOFT3D_MAZE_HALF_HEIGHT - 0.25f, SOFT3D_MAZE_HALF_WIDTH + 0.25f,
        SOFT3D_MAZE_HALF_HEIGHT + 0.25f, 0.015f, 0.57f, &view, 0x9CF4U, 0x7C12U, 0x6411U);
    if (!submit_quad(ctx, -SOFT3D_MAZE_HALF_WIDTH, -SOFT3D_MAZE_HALF_HEIGHT,
        SOFT3D_MAZE_HALF_WIDTH, SOFT3D_MAZE_HALF_HEIGHT, 0.0f, &view, 0xFFFFU, 1)) success = 0;
    for (i = 0U; i < count; ++i) {
        if (!submit_quad(ctx, walls[i].min_x + 0.04f, walls[i].min_y - 0.10f,
            walls[i].max_x + 0.08f, walls[i].max_y + 0.02f,
            -0.001f, &view, 0x8410U, 0)) success = 0;
    }
    for (i = 0U; i < count; ++i) {
        if (!submit_box(ctx, walls[i].min_x, walls[i].min_y, walls[i].max_x, walls[i].max_y,
                         -WALL_HEIGHT, 0.0f, &view, 0x96FAU, 0x4475U, 0x5D78U)) success = 0;
    }
    if (!submit_disc(ctx, start->x, start->y, 0.31f, -0.004f, 0x051FU, &view)) success = 0;
    if (!submit_disc(ctx, start->x, start->y, 0.22f, -0.008f, 0xBFFFU, &view)) success = 0;
    if (!submit_disc(ctx, goal->x, goal->y, goal->radius + 0.04f, -0.004f, 0xFFE0U, &view)) success = 0;
    if (!submit_disc(ctx, goal->x, goal->y, goal->radius, -0.008f, 0x07E0U, &view)) success = 0;
    if (!submit_disc(ctx, maze->x + 0.045f, maze->y - 0.065f,
                      SOFT3D_MAZE_BALL_RADIUS * 1.16f, -0.010f, 0x8410U, &view)) success = 0;
    if (!submit_disc(ctx, maze->x + 0.025f, maze->y - 0.025f,
                      SOFT3D_MAZE_BALL_RADIUS * 0.75f, -0.012f, 0x39E7U, &view)) success = 0;
    if (!submit_ball(ctx, maze, &view)) success = 0;
    return success;
}
