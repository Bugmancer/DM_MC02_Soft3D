#include "soft3d.h"
#include "soft3d_models.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define WIDTH 96U
#define HEIGHT 80U
#define PIXELS (WIDTH * HEIGHT)
#define GUARD 0x13579BDFU

static struct {
    unsigned before;
    Soft3D_Context value;
    unsigned after;
} renderer;
static struct {
    unsigned before;
    uint16_t value[PIXELS];
    unsigned after;
} image;
static struct {
    unsigned before;
    float value[PIXELS];
    unsigned after;
} zbuffer;
static uint16_t reference[PIXELS];
static float reference_depth[PIXELS];
static Soft3D_Vertex reference_vertices[SOFT3D_MAX_VERTICES];
static const Soft3D_Camera camera = { 1.570796327f, 0.5f, 10.0f };
static const Soft3D_Transform identity = { {0, 0, 0}, {0, 0, 0}, 1.0f };
static const Soft3D_Material red = { SOFT3D_LIT, 0xF800U, NULL, 0U };
static const Soft3D_Material blue = { SOFT3D_LIT, 0x001FU, NULL, 0U };
static const uint16_t triangle_indices[] = { 0, 1, 2 };
static Soft3D_Context baseline;

/* Linked from the frozen pre-optimization renderer using renamed public APIs. */
void reference_soft3d_init(Soft3D_Context *, uint16_t, uint16_t);
void reference_soft3d_begin_frame(Soft3D_Context *, const Soft3D_Camera *, uint16_t);
int reference_soft3d_submit(Soft3D_Context *, const Soft3D_Mesh *,
                            const Soft3D_Transform *, const Soft3D_Material *);
int reference_soft3d_submit_quaternion(Soft3D_Context *, const Soft3D_Mesh *,
                                       const Soft3D_Vec3 *, const Soft3D_Quaternion *,
                                       float, const Soft3D_Material *);
int reference_soft3d_render_band(Soft3D_Context *, uint16_t, uint16_t, uint16_t *, float *);

static void reset(void)
{
    renderer.before = renderer.after = GUARD;
    image.before = image.after = GUARD;
    zbuffer.before = zbuffer.after = GUARD;
    soft3d_init(&renderer.value, WIDTH, HEIGHT);
    soft3d_begin_frame(&renderer.value, &camera, 0x0861U);
}

static void check_guards(void)
{
    assert(renderer.before == GUARD && renderer.after == GUARD);
    assert(image.before == GUARD && image.after == GUARD);
    assert(zbuffer.before == GUARD && zbuffer.after == GUARD);
}

static unsigned occupied(void)
{
    unsigned i, count = 0;
    for (i = 0; i < PIXELS; ++i) {
        if (zbuffer.value[i] > 0.0f) ++count;
    }
    return count;
}

static void render(void)
{
    assert(soft3d_render_band(&renderer.value, 0, HEIGHT, image.value, zbuffer.value));
    check_guards();
}

static void test_validation(void)
{
    Soft3D_Camera bad_camera = camera;
    Soft3D_Mesh bad_mesh = soft3d_mesh_cube;
    Soft3D_Transform bad_transform = identity;
    uint16_t bad_indices[] = { 0, 1, 65535 };
    reset();
    render();
    assert(occupied() == 0U);
    image.value[0] = 0x1234U;
    zbuffer.value[0] = 42.0f;
    assert(!soft3d_render_band(&renderer.value, HEIGHT - 1U, 2U, image.value, zbuffer.value));
    assert(!soft3d_render_band(&renderer.value, 0U, 0U, image.value, zbuffer.value));
    assert(image.value[0] == 0x1234U && zbuffer.value[0] == 42.0f);
    bad_camera.near_z = 0;
    soft3d_begin_frame(&renderer.value, &bad_camera, 0);
    assert(!soft3d_submit(&renderer.value, &soft3d_mesh_cube, &identity, &red));
    assert(!soft3d_render_band(&renderer.value, 0, HEIGHT, image.value, zbuffer.value));
    soft3d_begin_frame(&renderer.value, &camera, 0);
    bad_mesh.vertex_count = SOFT3D_MAX_VERTICES + 1U;
    assert(!soft3d_submit(&renderer.value, &bad_mesh, &identity, &red));
    bad_mesh = soft3d_mesh_cube;
    bad_mesh.indices = bad_indices;
    bad_mesh.triangle_count = 1U;
    assert(!soft3d_submit(&renderer.value, &bad_mesh, &identity, &red));
    bad_transform.scale = -1.0f;
    assert(!soft3d_submit(&renderer.value, &soft3d_mesh_cube, &bad_transform, &red));
    assert(renderer.value.triangle_count == 0U);
    check_guards();
}

static void test_depth_order(void)
{
    static const Soft3D_Vertex vertices[] = {
        {-1, -1, 0, 0, 0}, {1, -1, 0, 1, 0}, {0, 1, 0, 0.5f, 1}
    };
    Soft3D_Mesh mesh = { vertices, triangle_indices, 3, 1 };
    Soft3D_Transform near_transform = identity;
    Soft3D_Transform far_transform = identity;
    unsigned center = (HEIGHT / 2U) * WIDTH + WIDTH / 2U;
    near_transform.position.z = 2;
    far_transform.position.z = 3;
    reset();
    assert(soft3d_submit(&renderer.value, &mesh, &far_transform, &blue));
    assert(soft3d_submit(&renderer.value, &mesh, &near_transform, &red));
    render();
    assert((image.value[center] & 0xF800U) != 0U);
    assert((image.value[center] & 0x07FFU) == 0U);
    assert(fabsf(zbuffer.value[center] - 0.5f) < 0.00001f);
    memcpy(reference, image.value, sizeof(reference));
    memcpy(reference_depth, zbuffer.value, sizeof(reference_depth));
    reset();
    assert(soft3d_submit(&renderer.value, &mesh, &near_transform, &red));
    assert(soft3d_submit(&renderer.value, &mesh, &far_transform, &blue));
    render();
    assert(memcmp(reference, image.value, sizeof(reference)) == 0);
    assert(memcmp(reference_depth, zbuffer.value, sizeof(reference_depth)) == 0);
}

static void test_clipping(void)
{
    unsigned plane;
    for (plane = 0; plane < 6; ++plane) {
        Soft3D_Vertex vertices[] = {
            {-0.6f, -0.6f, 2, 0, 0}, {0.6f, -0.6f, 2, 1, 0}, {0, 0.6f, 2, 0.5f, 1}
        };
        Soft3D_Mesh mesh = { vertices, triangle_indices, 3, 1 };
        unsigned triangle, vertex;
        if (plane == 0U) vertices[0].z = 0.1f;
        if (plane == 1U) vertices[0].z = 20.0f;
        if (plane == 2U) vertices[0].x = -10.0f;
        if (plane == 3U) vertices[0].x = 10.0f;
        if (plane == 4U) vertices[0].y = -10.0f;
        if (plane == 5U) vertices[0].y = 10.0f;
        reset();
        assert(soft3d_submit(&renderer.value, &mesh, &identity, &red));
        assert(renderer.value.stats.clipped_triangles == 1U);
        assert(renderer.value.triangle_count >= 1U);
        for (triangle = 0; triangle < renderer.value.triangle_count; ++triangle) {
            for (vertex = 0; vertex < 3; ++vertex) {
                const Soft3D_ScreenVertex *v = &renderer.value.triangles[triangle].v[vertex];
                assert(v->x >= -0.001f && v->x <= WIDTH + 0.001f);
                assert(v->y >= -0.001f && v->y <= HEIGHT + 0.001f);
                assert(v->inverse_z >= 0.09999f && v->inverse_z <= 2.00001f);
            }
        }
        render();
        assert(occupied() > 10U);
    }
    {
        Soft3D_Transform transform = identity;
        transform.position.z = -4.0f;
        reset();
        assert(soft3d_submit(&renderer.value, &soft3d_mesh_cube, &transform, &red));
        render();
        assert(renderer.value.triangle_count == 0U && occupied() == 0U);
        transform.position.z = 20.0f;
        assert(soft3d_submit(&renderer.value, &soft3d_mesh_cube, &transform, &red));
        render();
        assert(renderer.value.triangle_count == 0U && occupied() == 0U);
    }
}

static unsigned expected_light(const Soft3D_Vertex *v)
{
    double ax = v[1].x - v[0].x, ay = v[1].y - v[0].y, az = v[1].z - v[0].z;
    double bx = v[2].x - v[0].x, by = v[2].y - v[0].y, bz = v[2].z - v[0].z;
    double nx = ay * bz - az * by, ny = az * bx - ax * bz, nz = ax * by - ay * bx;
    double diffuse;
    if (nx * v[0].x + ny * v[0].y + nz * v[0].z > 0) {
        nx = -nx; ny = -ny; nz = -nz;
    }
    diffuse = (-0.32444284 * nx + 0.48666427 * ny - 0.81110711 * nz) /
              sqrt(nx * nx + ny * ny + nz * nz);
    if (diffuse < 0) diffuse = 0;
    return (unsigned)(56.0 + 199.0 * diffuse);
}

static uint16_t expected_shade(uint16_t color, unsigned light)
{
    unsigned r = ((color >> 11) & 31U) * light / 255U;
    unsigned g = ((color >> 5) & 63U) * light / 255U;
    unsigned b = (color & 31U) * light / 255U;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static void test_perspective_uv(void)
{
    /* These coordinates project to (12,10), (84,18), (30,70), with distinct Z.
     * Compare against independent double-precision barycentric interpolation.
     */
    Soft3D_Vertex vertices[] = {
        {-0.9f, 0.75f, 1, 0, 0}, {3.6f, 2.2f, 4, 1, 0}, {-0.9f, -1.5f, 2, 0, 1}
    };
    Soft3D_Mesh mesh = { vertices, triangle_indices, 3, 1 };
    uint16_t texture_pixels[64];
    Soft3D_Texture texture = { texture_pixels, 8, 8 };
    Soft3D_Material material = { SOFT3D_TEXTURED, 0xFFFFU, &texture, 0U };
    unsigned i, differences = 0U, verified = 0U, light = expected_light(vertices);
    for (i = 0; i < 64; ++i) texture_pixels[i] = (uint16_t)(((i & 7U) * 4U << 11) | ((i >> 3) * 8U << 5) | 31U);
    reset();
    assert(soft3d_submit(&renderer.value, &mesh, &identity, &material));
    render();
    for (i = 0; i < PIXELS; ++i) {
        unsigned x = i % WIDTH, y = i / WIDTH;
        double px = (double)x + 0.5, py = (double)y + 0.5;
        double denominator = (18.0 - 70.0) * (12.0 - 30.0) + (30.0 - 84.0) * (10.0 - 70.0);
        double w0 = ((18.0 - 70.0) * (px - 30.0) + (30.0 - 84.0) * (py - 70.0)) / denominator;
        double w1 = ((70.0 - 10.0) * (px - 30.0) + (12.0 - 30.0) * (py - 70.0)) / denominator;
        double w2 = 1.0 - w0 - w1;
        double inverse_z = w0 + w1 / 4.0 + w2 / 2.0;
        double texture_x, texture_y;
        unsigned tu, tv, affine_u, affine_v;
        uint16_t expected;
        if (w0 < 0.02 || w1 < 0.02 || w2 < 0.02) continue;
        texture_x = 8.0 * (w1 / 4.0) / inverse_z;
        texture_y = 8.0 * (w2 / 2.0) / inverse_z;
        tu = (unsigned)texture_x;
        tv = (unsigned)texture_y;
        affine_u = (unsigned)(8.0 * w1);
        affine_v = (unsigned)(8.0 * w2);
        expected = expected_shade(texture_pixels[tv * 8U + tu], light);
        assert(fabs((double)zbuffer.value[y * WIDTH + x] - inverse_z) < 0.00001);
        /* Nearest-neighbor samples exactly on texel boundaries may round either
         * way; validate all interior texels and every interior depth sample.
         */
        if (fabs(texture_x - floor(texture_x + 0.5)) > 0.0003 &&
            fabs(texture_y - floor(texture_y + 0.5)) > 0.0003) {
            assert(image.value[y * WIDTH + x] == expected);
            ++verified;
        }
        if (tu != affine_u || tv != affine_v) ++differences;
    }
    assert(differences >= 800U && verified >= 1000U);
}

static void test_models_and_bands(void)
{
    const Soft3D_Mesh *models[] = { &soft3d_mesh_cube, &soft3d_mesh_torus };
    unsigned model, mode;
    for (model = 0; model < 2; ++model) {
        for (mode = 0; mode < 3; ++mode) {
            Soft3D_Transform transform = { {0, 0, 3.2f}, {0.35f, 0.55f, 0.15f}, 1 };
            Soft3D_Material material = { (Soft3D_Mode)mode, 0x07FFU, &soft3d_texture_checker, 1U };
            unsigned y, visible = 0U;
            reset();
            assert(soft3d_submit(&renderer.value, models[model], &transform, &material));
            assert(renderer.value.triangle_count > 0U);
            assert(renderer.value.triangle_count < models[model]->triangle_count);
            render();
            assert(occupied() > 300U);
            for (y = 0; y < PIXELS; ++y) if (image.value[y] != 0x0861U) ++visible;
            assert(visible > 80U);
            if (mode == SOFT3D_WIREFRAME) assert(visible < occupied());
            memcpy(reference, image.value, sizeof(reference));
            memcpy(reference_depth, zbuffer.value, sizeof(reference_depth));
            memset(image.value, 0, sizeof(image.value));
            memset(zbuffer.value, 0, sizeof(zbuffer.value));
            for (y = 0; y < HEIGHT; y += 7U) {
                unsigned rows = HEIGHT - y;
                if (rows > 7U) rows = 7U;
                assert(soft3d_render_band(&renderer.value, (uint16_t)y, (uint16_t)rows,
                                          &image.value[y * WIDTH], &zbuffer.value[y * WIDTH]));
            }
            assert(memcmp(reference, image.value, sizeof(reference)) == 0);
            assert(memcmp(reference_depth, zbuffer.value, sizeof(reference_depth)) == 0);
            /* Absolute-row rebasing must also support reverse-order one-row bands. */
            for (y = HEIGHT; y > 0U; --y) {
                assert(soft3d_render_band(&renderer.value, (uint16_t)(y - 1U), 1U,
                                          &image.value[(y - 1U) * WIDTH],
                                          &zbuffer.value[(y - 1U) * WIDTH]));
            }
            assert(memcmp(reference, image.value, sizeof(reference)) == 0);
            assert(memcmp(reference_depth, zbuffer.value, sizeof(reference_depth)) == 0);
            check_guards();
        }
    }
}

static void test_capacity(void)
{
    static const Soft3D_Vertex vertices[] = {
        {-1, -1, 2, 0, 0}, {1, -1, 2, 1, 0}, {0, 1, 2, 0, 1}
    };
    Soft3D_Mesh mesh = { vertices, triangle_indices, 3, 1 };
    unsigned i;
    reset();
    for (i = 0; i < SOFT3D_MAX_TRIANGLES; ++i) {
        assert(soft3d_submit(&renderer.value, &mesh, &identity, &red));
    }
    assert(!soft3d_submit(&renderer.value, &mesh, &identity, &red));
    assert(renderer.value.triangle_count == SOFT3D_MAX_TRIANGLES);
    assert(renderer.value.stats.dropped_triangles == 1U);
    render();
    assert(occupied() > 0U);
}

static void test_clip_boundaries(void)
{
    Soft3D_Vertex vertices[] = {
        {-0.1f, -0.1f, 0.5f, 0, 0}, {0.6f, -0.6f, 0.1f, 1, 0}, {0, 0.6f, 2, 0, 1}
    };
    Soft3D_Mesh mesh = { vertices, triangle_indices, 3, 1 };
    Soft3D_Camera close_camera = camera;
    unsigned triangle, vertex;
    reset();
    assert(soft3d_submit(&renderer.value, &mesh, &identity, &red));
    assert(renderer.value.triangle_count == 1U);
    render();
    assert(occupied() > 0U);
    /* A distant edge crossing a close near plane must not round its Z to zero. */
    vertices[0].x = -0.1f; vertices[0].y = -0.1f; vertices[0].z = -100000.0f;
    vertices[1].x = 0.6f; vertices[1].y = -0.6f; vertices[1].z = 2.0f;
    close_camera.near_z = 0.001f;
    soft3d_begin_frame(&renderer.value, &close_camera, 0x0861U);
    assert(soft3d_submit(&renderer.value, &mesh, &identity, &red));
    assert(renderer.value.triangle_count > 0U);
    for (triangle = 0; triangle < renderer.value.triangle_count; ++triangle) {
        for (vertex = 0; vertex < 3; ++vertex) {
            const Soft3D_ScreenVertex *v = &renderer.value.triangles[triangle].v[vertex];
            assert(isfinite(v->x) && isfinite(v->y) && isfinite(v->inverse_z));
            assert(v->x >= -0.01f && v->x <= WIDTH + 0.01f);
            assert(v->y >= -0.01f && v->y <= HEIGHT + 0.01f);
            assert(v->inverse_z > 0.0f && v->inverse_z <= 1000.001f);
        }
    }
    render();
    assert(occupied() > 0U);
}

static void test_quaternion_submission(void)
{
    Soft3D_Transform transform = {{0.2f, -0.1f, 3.0f}, {0.35f, 1.4f, -0.9f}, 0.8f};
    Soft3D_Quaternion q;
    float sx = sinf(transform.rotation.x * 0.5f), cx = cosf(transform.rotation.x * 0.5f);
    float sy = sinf(transform.rotation.y * 0.5f), cy = cosf(transform.rotation.y * 0.5f);
    float sz = sinf(transform.rotation.z * 0.5f), cz = cosf(transform.rotation.z * 0.5f);
    unsigned i, pass;
    q.x = sx * cy * cz - cx * sy * sz;
    q.y = cx * sy * cz + sx * cy * sz;
    q.z = cx * cy * sz - sx * sy * cz;
    q.w = cx * cy * cz + sx * sy * sz;
    reset();
    assert(soft3d_submit(&renderer.value, &soft3d_mesh_cube, &transform, &red));
    memcpy(reference_vertices, renderer.value.transformed, sizeof(reference_vertices));
    for (pass = 0; pass < 3U; ++pass) {
        float multiplier = pass == 0U ? 1.0f : pass == 1U ? -1.0f : 7.0f;
        Soft3D_Quaternion scaled = {q.x * multiplier, q.y * multiplier, q.z * multiplier, q.w * multiplier};
        reset();
        assert(soft3d_submit_quaternion(&renderer.value, &soft3d_mesh_cube, &transform.position,
                                         &scaled, transform.scale, &red));
        for (i = 0; i < soft3d_mesh_cube.vertex_count; ++i) {
            assert(fabsf(renderer.value.transformed[i].x - reference_vertices[i].x) < 0.000002f);
            assert(fabsf(renderer.value.transformed[i].y - reference_vertices[i].y) < 0.000002f);
            assert(fabsf(renderer.value.transformed[i].z - reference_vertices[i].z) < 0.000002f);
        }
        render();
        assert(occupied() > 100U);
    }
    {
        Soft3D_Quaternion zero = {0, 0, 0, 0};
        Soft3D_Quaternion not_finite = {NAN, 0, 0, 1};
        uint16_t count = renderer.value.triangle_count;
        assert(!soft3d_submit_quaternion(&renderer.value, &soft3d_mesh_cube, &transform.position,
                                          &zero, 1, &red));
        assert(!soft3d_submit_quaternion(&renderer.value, &soft3d_mesh_cube, &transform.position,
                                          &not_finite, 1, &red));
        not_finite.x = INFINITY;
        assert(!soft3d_submit_quaternion(&renderer.value, &soft3d_mesh_cube, &transform.position,
                                          &not_finite, 1, &red));
        assert(!soft3d_submit_quaternion(&renderer.value, &soft3d_mesh_cube, &transform.position,
                                          NULL, 1, &red));
        assert(!soft3d_submit_quaternion(&renderer.value, &soft3d_mesh_cube, &transform.position,
                                          &q, -1, &red));
        assert(renderer.value.triangle_count == count);
    }
    transform.position.z = 0.55f;
    reset();
    assert(soft3d_submit_quaternion(&renderer.value, &soft3d_mesh_cube, &transform.position,
                                     &q, transform.scale, &red));
    assert(renderer.value.stats.clipped_triangles > 0U);
    render();
    assert(occupied() > 100U);
    for (i = 0; i < PIXELS; ++i) {
        assert(isfinite(zbuffer.value[i]));
        assert(zbuffer.value[i] >= 0.0f && zbuffer.value[i] <= 2.0001f);
    }
    check_guards();
}

static void reset_comparison(uint16_t width, uint16_t height)
{
    assert((unsigned)width * height <= PIXELS);
    reset();
    soft3d_init(&renderer.value, width, height);
    reference_soft3d_init(&baseline, width, height);
    soft3d_begin_frame(&renderer.value, &camera, 0x0861U);
    reference_soft3d_begin_frame(&baseline, &camera, 0x0861U);
    assert(renderer.value.band_index_enabled != 0U);
}

static void compare_geometry(void)
{
    assert(renderer.value.triangle_count == baseline.triangle_count);
    assert(renderer.value.stats.submitted_triangles == baseline.stats.submitted_triangles);
    assert(renderer.value.stats.clipped_triangles == baseline.stats.clipped_triangles);
    assert(renderer.value.stats.prepared_triangles == baseline.stats.prepared_triangles);
    assert(renderer.value.stats.dropped_triangles == baseline.stats.dropped_triangles);
}

static void compare_submission(const Soft3D_Mesh *mesh, const Soft3D_Transform *transform,
                                 const Soft3D_Material *material)
{
    assert(soft3d_submit(&renderer.value, mesh, transform, material) ==
           reference_soft3d_submit(&baseline, mesh, transform, material));
    assert(memcmp(renderer.value.transformed, baseline.transformed,
                  mesh->vertex_count * sizeof(Soft3D_Vertex)) == 0);
    compare_geometry();
}

static void compare_band(uint16_t y, uint16_t rows)
{
    size_t count = (size_t)renderer.value.width * rows;
    assert(count <= PIXELS);
    assert(soft3d_render_band(&renderer.value, y, rows, image.value, zbuffer.value));
    assert(reference_soft3d_render_band(&baseline, y, rows, reference, reference_depth));
    assert(memcmp(image.value, reference, count * sizeof(uint16_t)) == 0);
    assert(memcmp(zbuffer.value, reference_depth, count * sizeof(float)) == 0);
    assert(renderer.value.stats.pixels_shaded == baseline.stats.pixels_shaded);
    check_guards();
}

static void test_reference_pipeline(void)
{
    unsigned pose, mode;
    for (pose = 0U; pose < 64U; ++pose) {
        for (mode = 0U; mode < 3U; ++mode) {
            const Soft3D_Mesh *mesh = (pose & 1U) != 0U ? &soft3d_mesh_torus : &soft3d_mesh_cube;
            Soft3D_Transform transform = {
                {sinf((float)pose) * 2.3f, cosf((float)pose * 0.3f),
                 0.35f + (float)(pose % 11U) * 0.65f},
                {(float)pose * 0.31f, (float)pose * -0.47f, (float)pose * 0.13f}, 1.0f
            };
            Soft3D_Material material = {(Soft3D_Mode)mode, 0x07FFU, &soft3d_texture_checker, 1U};
            unsigned y;
            reset_comparison(WIDTH, HEIGHT);
            compare_submission(mesh, &transform, &material);
            compare_band(0U, HEIGHT);
            soft3d_set_band_index_enabled(&renderer.value, 0);
            compare_band(0U, HEIGHT);
            soft3d_set_band_index_enabled(&renderer.value, 1);
            for (y = 0U; y < HEIGHT; y += 13U) {
                unsigned rows = HEIGHT - y;
                if (rows > 13U) rows = 13U;
                compare_band((uint16_t)y, (uint16_t)rows);
            }
            for (y = HEIGHT; y > 0U; --y) compare_band((uint16_t)(y - 1U), 1U);
        }
    }
    for (pose = 0U; pose < 24U; ++pose) {
        Soft3D_Vec3 position = {sinf((float)pose), cosf((float)pose), 0.3f + (float)pose * 0.2f};
        Soft3D_Quaternion q = {sinf((float)pose * 0.13f), cosf((float)pose * 0.17f),
                              sinf((float)pose * 0.23f), 0.7f};
        reset_comparison(WIDTH, HEIGHT);
        assert(soft3d_submit_quaternion(&renderer.value, &soft3d_mesh_cube, &position, &q, 1, &red) ==
               reference_soft3d_submit_quaternion(&baseline, &soft3d_mesh_cube, &position, &q, 1, &red));
        assert(memcmp(renderer.value.transformed, baseline.transformed,
                      soft3d_mesh_cube.vertex_count * sizeof(Soft3D_Vertex)) == 0);
        assert(renderer.value.stats.transformed_vertices == soft3d_mesh_cube.vertex_count);
        compare_geometry();
        compare_band(0U, HEIGHT);
    }
}

static void test_outcode_rejection_and_cache(void)
{
    unsigned plane;
    for (plane = 0U; plane < 6U; ++plane) {
        Soft3D_Transform transform = identity;
        transform.position.z = 3.0f;
        if (plane == 0U) transform.position.z = -3.0f;
        if (plane == 1U) transform.position.z = 15.0f;
        if (plane == 2U) transform.position.x = -15.0f;
        if (plane == 3U) transform.position.x = 15.0f;
        if (plane == 4U) transform.position.y = -15.0f;
        if (plane == 5U) transform.position.y = 15.0f;
        reset_comparison(WIDTH, HEIGHT);
        compare_submission(&soft3d_mesh_cube, &transform, &red);
        assert(renderer.value.stats.frustum_rejected == 12U);
        assert(renderer.value.stats.clipped_triangles == 12U);
        assert(renderer.value.stats.transformed_vertices == soft3d_mesh_cube.vertex_count);
        assert(renderer.value.stats.rotation_cache_hits == 0U);
        compare_submission(&soft3d_mesh_cube, &transform, &red);
        assert(renderer.value.stats.rotation_cache_hits == 1U);
        compare_band(0U, HEIGHT);
        soft3d_begin_frame(&renderer.value, &camera, 0x0861U);
        reference_soft3d_begin_frame(&baseline, &camera, 0x0861U);
        assert(renderer.value.stats.rotation_cache_hits == 0U);
        compare_submission(&soft3d_mesh_cube, &transform, &red);
        assert(renderer.value.stats.rotation_cache_hits == 1U);
        transform.rotation.x = -0.0f;
        compare_submission(&soft3d_mesh_cube, &transform, &red);
        assert(renderer.value.stats.rotation_cache_hits == 1U);
        compare_submission(&soft3d_mesh_cube, &transform, &red);
        assert(renderer.value.stats.rotation_cache_hits == 2U);
    }
}

static void test_reference_clip_boundaries(void)
{
    unsigned plane;
    for (plane = 0U; plane < 7U; ++plane) {
        Soft3D_Vertex vertices[] = {
            {-0.6f, -0.6f, 2, 0, 0}, {0.6f, -0.6f, 2, 1, 0}, {0, 0.6f, 2, 0.5f, 1}
        };
        Soft3D_Mesh mesh = {vertices, triangle_indices, 3U, 1U};
        Soft3D_Camera clip_camera = camera;
        if (plane == 0U) { vertices[0].z = 0.5f; vertices[1].z = 0.1f; }
        if (plane == 1U) vertices[0].z = 20.0f;
        if (plane == 2U) vertices[0].x = -10.0f;
        if (plane == 3U) vertices[0].x = 10.0f;
        if (plane == 4U) vertices[0].y = -10.0f;
        if (plane == 5U) vertices[0].y = 10.0f;
        if (plane == 6U) { vertices[0].z = -100000.0f; clip_camera.near_z = 0.001f; }
        reset_comparison(WIDTH, HEIGHT);
        soft3d_begin_frame(&renderer.value, &clip_camera, 0x0861U);
        reference_soft3d_begin_frame(&baseline, &clip_camera, 0x0861U);
        compare_submission(&mesh, &identity, &red);
        assert(renderer.value.stats.clipped_triangles == 1U);
        compare_band(0U, HEIGHT);
    }
}

static void submit_screen_triangle(float low_y, float high_y)
{
    Soft3D_Vertex vertices[] = {{1, 0, 2, 0, 0}, {7, 0, 2, 1, 0}, {4, 0, 2, 0.5f, 1}};
    Soft3D_Mesh mesh = {vertices, triangle_indices, 3U, 1U};
    unsigned i;
    vertices[0].y = vertices[1].y = low_y;
    vertices[2].y = high_y;
    for (i = 0U; i < 3U; ++i) {
        vertices[i].x = (vertices[i].x - (float)renderer.value.width * 0.5f) * 2.0f / renderer.value.focal_length;
        vertices[i].y = ((float)renderer.value.height * 0.5f - vertices[i].y) * 2.0f / renderer.value.focal_length;
    }
    compare_submission(&mesh, &identity, &red);
}

static void test_band_index_boundaries(void)
{
    unsigned bin, word;
    reset_comparison(8U, 512U);
    soft3d_set_band_index_enabled(&renderer.value, 0);
    for (bin = 0U; bin < 32U; ++bin) submit_screen_triangle((float)(bin * 16U + 4U), (float)(bin * 16U + 12U));
    assert(renderer.value.triangle_count == 32U);
    soft3d_set_band_index_enabled(&renderer.value, 1);
    for (bin = 0U; bin < 32U; ++bin) compare_band((uint16_t)(bin * 16U), 16U);
    assert(renderer.value.stats.band_candidates == 32U);
    assert(renderer.value.stats.band_potential == 1024U);
    compare_band(15U, 2U);
    assert(renderer.value.stats.band_candidates == 34U);
    compare_band(0U, 512U);
    for (bin = 512U; bin > 0U; --bin) compare_band((uint16_t)(bin - 1U), 1U);
    soft3d_set_band_index_enabled(&renderer.value, 0);
    compare_band(15U, 2U);
    soft3d_set_band_index_enabled(&renderer.value, 1);
    renderer.value.stats.band_candidates = UINT32_MAX - 1U;
    renderer.value.stats.band_potential = UINT32_MAX - 1U;
    compare_band(0U, 512U);
    assert(renderer.value.stats.band_candidates == UINT32_MAX);
    assert(renderer.value.stats.band_potential == UINT32_MAX);
    soft3d_begin_frame(&renderer.value, &camera, 0x0861U);
    reference_soft3d_begin_frame(&baseline, &camera, 0x0861U);
    assert(renderer.value.stats.band_candidates == 0U && renderer.value.stats.band_potential == 0U);
    for (bin = 0U; bin < SOFT3D_BAND_BINS; ++bin) {
        for (word = 0U; word < SOFT3D_BAND_WORDS; ++word) assert(renderer.value.band_index[bin][word] == 0U);
    }
    compare_band(0U, 512U);
    assert(renderer.value.stats.band_potential == 0U);
    submit_screen_triangle(15.0f, 17.0f);
    assert((renderer.value.band_index[0][0] & 1U) != 0U);
    assert((renderer.value.band_index[1][0] & 1U) != 0U);
    compare_band(15U, 2U);

    reset_comparison(8U, 513U);
    for (bin = 0U; bin < 32U; ++bin) submit_screen_triangle((float)(bin * 16U + 4U), (float)(bin * 16U + 12U));
    compare_band(0U, 513U);
    compare_band(511U, 2U);
    assert(renderer.value.stats.band_candidates == renderer.value.stats.band_potential);
    for (bin = 0U; bin < SOFT3D_BAND_BINS; ++bin) {
        for (word = 0U; word < SOFT3D_BAND_WORDS; ++word) assert(renderer.value.band_index[bin][word] == 0U);
    }
}

static void test_reference_capacity_order(void)
{
    Soft3D_Transform transform = {{0, 0, 3}, {0, 0, 0}, 1};
    static const Soft3D_Vertex vertices[] = {{-1, -1, 0, 0, 0}, {1, -1, 0, 1, 0}, {0, 1, 0, 0.5f, 1}};
    const Soft3D_Mesh mesh = {vertices, triangle_indices, 3U, 1U};
    unsigned i;
    reset_comparison(WIDTH, HEIGHT);
    for (i = 0U; i < SOFT3D_MAX_TRIANGLES + 1U; ++i) {
        compare_submission(&mesh, &transform, (i & 1U) != 0U ? &blue : &red);
    }
    assert(renderer.value.triangle_count == SOFT3D_MAX_TRIANGLES);
    assert(renderer.value.stats.dropped_triangles == 1U);
    compare_band(0U, HEIGHT);
    assert(renderer.value.stats.band_candidates == SOFT3D_MAX_TRIANGLES);
    for (i = 0U; i < HEIGHT; i += 16U) compare_band((uint16_t)i, 16U);
    soft3d_set_band_index_enabled(&renderer.value, 0);
    compare_band(0U, HEIGHT);
}

int main(void)
{
    test_validation();
    test_depth_order();
    test_clipping();
    test_perspective_uv();
    test_models_and_bands();
    test_capacity();
    test_clip_boundaries();
    test_quaternion_submission();
    test_reference_pipeline();
    test_outcode_rejection_and_cache();
    test_reference_clip_boundaries();
    test_band_index_boundaries();
    test_reference_capacity_order();
    assert(sizeof(renderer.value.band_index) == 2048U);
    assert(sizeof(Soft3D_Context) < 100U * 1024U);
    printf("soft3d: all tests passed; frozen baseline color/depth identical; sparse bands 32/1024 candidates; context=%lu bytes\n",
        (unsigned long)sizeof(Soft3D_Context));
    return 0;
}
