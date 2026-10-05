#include "soft3d.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

static int finite_value(float value)
{
    return value == value && value > -1000000.0f && value < 1000000.0f;
}

static float edge(const Soft3D_ScreenVertex *a, const Soft3D_ScreenVertex *b,
                  float x, float y)
{
    return (b->x - a->x) * (y - a->y) - (b->y - a->y) * (x - a->x);
}

static uint16_t shade(uint16_t color, unsigned light)
{
    unsigned r = (((unsigned)color >> 11) & 31U) * light / 255U;
    unsigned g = (((unsigned)color >> 5) & 63U) * light / 255U;
    unsigned b = ((unsigned)color & 31U) * light / 255U;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

void soft3d_init(Soft3D_Context *ctx, uint16_t width, uint16_t height)
{
    if (ctx == NULL) {
        return;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->band_index_enabled = 1U;
    if (width > 0U && width <= 4096U && height > 0U && height <= 4096U) {
        ctx->width = width;
        ctx->height = height;
    }
}

void soft3d_set_band_index_enabled(Soft3D_Context *ctx, int enable)
{
    if (ctx != NULL) ctx->band_index_enabled = enable != 0 ? 1U : 0U;
}

static void add_counter(uint32_t *counter, uint32_t value)
{
    *counter = value > UINT32_MAX - *counter ? UINT32_MAX : *counter + value;
}

void soft3d_begin_frame(Soft3D_Context *ctx, const Soft3D_Camera *camera,
                       uint16_t clear_rgb565)
{
    if (ctx == NULL) {
        return;
    }
    ctx->frame_valid = 0U;
    ctx->triangle_count = 0U;
    ctx->clear_color = clear_rgb565;
    memset(&ctx->stats, 0, sizeof(ctx->stats));
    memset(ctx->band_index, 0, sizeof(ctx->band_index));
    if (camera == NULL || ctx->width == 0U || ctx->height == 0U ||
        !finite_value(camera->fov_y_radians) ||
        !finite_value(camera->near_z) || !finite_value(camera->far_z) ||
        camera->fov_y_radians < 0.1f || camera->fov_y_radians > 3.0f ||
        camera->near_z < 0.001f || camera->far_z <= camera->near_z) {
        return;
    }
    ctx->camera = *camera;
    ctx->tan_half_y = tanf(camera->fov_y_radians * 0.5f);
    ctx->tan_half_x = ctx->tan_half_y * (float)ctx->width / (float)ctx->height;
    ctx->focal_length = (float)ctx->height * 0.5f / ctx->tan_half_y;
    ctx->frame_valid = 1U;
}

static float plane_distance(const Soft3D_Context *ctx,
                            const Soft3D_Vertex *v, unsigned plane)
{
    switch (plane) {
    case 0U: return v->z - ctx->camera.near_z;
    case 1U: return ctx->camera.far_z - v->z;
    case 2U: return v->x + v->z * ctx->tan_half_x;
    case 3U: return v->z * ctx->tan_half_x - v->x;
    case 4U: return v->y + v->z * ctx->tan_half_y;
    default: return v->z * ctx->tan_half_y - v->y;
    }
}

static uint8_t vertex_outcode(const Soft3D_Context *ctx, const Soft3D_Vertex *vertex)
{
    unsigned plane;
    uint8_t code = 0U;
    for (plane = 0U; plane < 6U; ++plane) {
        if (plane_distance(ctx, vertex, plane) < 0.0f) code = (uint8_t)(code | (1U << plane));
    }
    return code;
}

static Soft3D_Vertex interpolate(const Soft3D_Vertex *a,
                                 const Soft3D_Vertex *b, float t)
{
    Soft3D_Vertex out;
    out.x = a->x + (b->x - a->x) * t;
    out.y = a->y + (b->y - a->y) * t;
    out.z = a->z + (b->z - a->z) * t;
    out.u = a->u + (b->u - a->u) * t;
    out.v = a->v + (b->v - a->v) * t;
    return out;
}

/* Sutherland-Hodgman in camera space preserves linear UV at intersections.
 * A triangle clipped by six planes has at most nine vertices.
 */
static unsigned clip_polygon(Soft3D_Context *ctx, unsigned count,
                              unsigned *result_buffer)
{
    unsigned plane, input = 0U;
    for (plane = 0U; plane < 6U && count != 0U; ++plane) {
        const Soft3D_Vertex *src = ctx->clip[input];
        Soft3D_Vertex *dst = ctx->clip[input ^ 1U];
        unsigned output_count = 0U;
        unsigned i;
        const Soft3D_Vertex *previous = &src[count - 1U];
        float previous_distance = plane_distance(ctx, previous, plane);
        for (i = 0U; i < count; ++i) {
            const Soft3D_Vertex *current = &src[i];
            float current_distance = plane_distance(ctx, current, plane);
            if ((previous_distance > 0.0f && current_distance < 0.0f) ||
                (previous_distance < 0.0f && current_distance > 0.0f)) {
                float t = previous_distance / (previous_distance - current_distance);
                Soft3D_Vertex intersection;
                if (output_count >= SOFT3D_CLIP_VERTICES) {
                    return 0U;
                }
                intersection = interpolate(previous, current, t);
                /* Snap the constrained component to avoid cancellation near a
                 * very close clip plane when endpoints are far apart.
                 */
                switch (plane) {
                case 0U: intersection.z = ctx->camera.near_z; break;
                case 1U: intersection.z = ctx->camera.far_z; break;
                case 2U: intersection.x = -intersection.z * ctx->tan_half_x; break;
                case 3U: intersection.x = intersection.z * ctx->tan_half_x; break;
                case 4U: intersection.y = -intersection.z * ctx->tan_half_y; break;
                default: intersection.y = intersection.z * ctx->tan_half_y; break;
                }
                dst[output_count++] = intersection;
            }
            if (current_distance >= 0.0f) {
                if (output_count >= SOFT3D_CLIP_VERTICES) {
                    return 0U;
                }
                dst[output_count++] = *current;
            }
            previous = current;
            previous_distance = current_distance;
        }
        count = output_count;
        input ^= 1U;
    }
    *result_buffer = input;
    return count;
}

static void project(const Soft3D_Context *ctx, const Soft3D_Vertex *src,
                     Soft3D_ScreenVertex *dst)
{
    dst->inverse_z = 1.0f / src->z;
    dst->x = (float)ctx->width * 0.5f + src->x * ctx->focal_length * dst->inverse_z;
    dst->y = (float)ctx->height * 0.5f - src->y * ctx->focal_length * dst->inverse_z;
    dst->u_over_z = src->u * dst->inverse_z;
    dst->v_over_z = src->v * dst->inverse_z;
}

static Soft3D_Gradient attribute_gradient(const Soft3D_Triangle *triangle,
                                          float f0, float f1, float f2)
{
    Soft3D_Gradient gradient;
    float x1 = triangle->v[1].x - triangle->v[0].x;
    float y1 = triangle->v[1].y - triangle->v[0].y;
    float x2 = triangle->v[2].x - triangle->v[0].x;
    float y2 = triangle->v[2].y - triangle->v[0].y;
    gradient.dx = ((f1 - f0) * y2 - (f2 - f0) * y1) * triangle->inverse_area;
    gradient.dy = (x1 * (f2 - f0) - x2 * (f1 - f0)) * triangle->inverse_area;
    return gradient;
}

static int prepare_triangle(Soft3D_Context *ctx,
                            const Soft3D_Vertex *a, const Soft3D_Vertex *b,
                            const Soft3D_Vertex *c,
                            const Soft3D_Material *material, uint8_t light)
{
    Soft3D_Triangle *triangle;
    float area, low_x, high_x, low_y, high_y;
    int min_x, max_x, min_y, max_y;
    unsigned i;
    if (ctx->triangle_count >= SOFT3D_MAX_TRIANGLES) {
        ++ctx->stats.dropped_triangles;
        return 0;
    }
    triangle = &ctx->triangles[ctx->triangle_count];
    project(ctx, a, &triangle->v[0]);
    project(ctx, b, &triangle->v[1]);
    project(ctx, c, &triangle->v[2]);
    area = edge(&triangle->v[0], &triangle->v[1], triangle->v[2].x, triangle->v[2].y);
    if (fabsf(area) < 0.00001f) {
        return 1;
    }
    if (area < 0.0f) {
        Soft3D_ScreenVertex swap = triangle->v[1];
        triangle->v[1] = triangle->v[2];
        triangle->v[2] = swap;
        area = -area;
    }
    low_x = high_x = triangle->v[0].x;
    low_y = high_y = triangle->v[0].y;
    for (i = 1U; i < 3U; ++i) {
        if (triangle->v[i].x < low_x) low_x = triangle->v[i].x;
        if (triangle->v[i].x > high_x) high_x = triangle->v[i].x;
        if (triangle->v[i].y < low_y) low_y = triangle->v[i].y;
        if (triangle->v[i].y > high_y) high_y = triangle->v[i].y;
    }
    min_x = (int)ceilf(low_x - 0.5f);
    max_x = (int)floorf(high_x - 0.5f);
    min_y = (int)ceilf(low_y - 0.5f);
    max_y = (int)floorf(high_y - 0.5f);
    if (min_x < 0) min_x = 0;
    if (min_y < 0) min_y = 0;
    if (max_x >= ctx->width) max_x = (int)ctx->width - 1;
    if (max_y >= ctx->height) max_y = (int)ctx->height - 1;
    if (min_x > max_x || min_y > max_y) {
        return 1;
    }
    triangle->min_x = (uint16_t)min_x;
    triangle->max_x = (uint16_t)max_x;
    triangle->min_y = (uint16_t)min_y;
    triangle->max_y = (uint16_t)max_y;
    triangle->inverse_area = 1.0f / area;
    triangle->inverse_z_gradient = attribute_gradient(triangle,
        triangle->v[0].inverse_z, triangle->v[1].inverse_z, triangle->v[2].inverse_z);
    if (material->mode == SOFT3D_TEXTURED) {
        triangle->u_over_z_gradient = attribute_gradient(triangle,
            triangle->v[0].u_over_z, triangle->v[1].u_over_z, triangle->v[2].u_over_z);
        triangle->v_over_z_gradient = attribute_gradient(triangle,
            triangle->v[0].v_over_z, triangle->v[1].v_over_z, triangle->v[2].v_over_z);
    } else {
        triangle->u_over_z_gradient.dx = triangle->u_over_z_gradient.dy = 0.0f;
        triangle->v_over_z_gradient.dx = triangle->v_over_z_gradient.dy = 0.0f;
    }
    triangle->texture = material->texture;
    triangle->light = light;
    triangle->mode = (uint8_t)material->mode;
    triangle->color = shade(material->color, light);
    if (material->mode == SOFT3D_WIREFRAME) {
        for (i = 0U; i < 3U; ++i) {
            const Soft3D_ScreenVertex *v1 = &triangle->v[(i + 1U) % 3U];
            const Soft3D_ScreenVertex *v2 = &triangle->v[(i + 2U) % 3U];
            float dx = v2->x - v1->x;
            float dy = v2->y - v1->y;
            triangle->edge_threshold[i] = sqrtf(dx * dx + dy * dy) * 0.85f;
        }
    }
    if (ctx->height <= SOFT3D_BAND_ROWS * SOFT3D_BAND_BINS) {
        unsigned first_bin = (unsigned)triangle->min_y / SOFT3D_BAND_ROWS;
        unsigned last_bin = (unsigned)triangle->max_y / SOFT3D_BAND_ROWS;
        unsigned word = (unsigned)ctx->triangle_count / 32U;
        uint32_t bit = (uint32_t)1U << ((unsigned)ctx->triangle_count % 32U);
        for (i = first_bin; i <= last_bin; ++i) ctx->band_index[i][word] |= bit;
    }
    ++ctx->triangle_count;
    ctx->stats.prepared_triangles = ctx->triangle_count;
    return 1;
}

static int validate_submission(Soft3D_Context *ctx, const Soft3D_Mesh *mesh,
                                const Soft3D_Vec3 *position, float scale,
                                const Soft3D_Material *material)
{
    unsigned i;
    if (ctx == NULL || !ctx->frame_valid || mesh == NULL || position == NULL ||
        material == NULL || mesh->vertices == NULL || mesh->indices == NULL ||
        mesh->vertex_count == 0U || mesh->vertex_count > SOFT3D_MAX_VERTICES ||
        !finite_value(scale) || scale <= 0.0f ||
        !finite_value(position->x) || !finite_value(position->y) ||
        !finite_value(position->z) ||
        (unsigned)material->mode > (unsigned)SOFT3D_WIREFRAME) {
        return 0;
    }
    if (material->mode == SOFT3D_TEXTURED &&
        (material->texture == NULL || material->texture->pixels == NULL ||
         material->texture->width == 0U || material->texture->height == 0U ||
         material->texture->width > 1024U || material->texture->height > 1024U)) {
        return 0;
    }
    for (i = 0U; i < (unsigned)mesh->triangle_count * 3U; ++i) {
        if (mesh->indices[i] >= mesh->vertex_count) {
            return 0;
        }
    }
    return 1;
}

static int submit_transformed(Soft3D_Context *ctx, const Soft3D_Mesh *mesh,
                              const Soft3D_Material *material);

int soft3d_submit(Soft3D_Context *ctx, const Soft3D_Mesh *mesh,
                  const Soft3D_Transform *transform,
                  const Soft3D_Material *material)
{
    float sx, cx, sy, cy, sz, cz;
    unsigned i;
    if (transform == NULL || !finite_value(transform->rotation.x) ||
        !finite_value(transform->rotation.y) || !finite_value(transform->rotation.z) ||
        !validate_submission(ctx, mesh, &transform->position, transform->scale, material)) {
        return 0;
    }
    if (ctx->rotation_cache_valid &&
        memcmp(&ctx->cached_rotation, &transform->rotation, sizeof(transform->rotation)) == 0) {
        sx = ctx->cached_trig[0]; cx = ctx->cached_trig[1];
        sy = ctx->cached_trig[2]; cy = ctx->cached_trig[3];
        sz = ctx->cached_trig[4]; cz = ctx->cached_trig[5];
        add_counter(&ctx->stats.rotation_cache_hits, 1U);
    } else {
        sx = sinf(transform->rotation.x); cx = cosf(transform->rotation.x);
        sy = sinf(transform->rotation.y); cy = cosf(transform->rotation.y);
        sz = sinf(transform->rotation.z); cz = cosf(transform->rotation.z);
        ctx->cached_rotation = transform->rotation;
        ctx->cached_trig[0] = sx; ctx->cached_trig[1] = cx;
        ctx->cached_trig[2] = sy; ctx->cached_trig[3] = cy;
        ctx->cached_trig[4] = sz; ctx->cached_trig[5] = cz;
        ctx->rotation_cache_valid = 1U;
    }
    for (i = 0U; i < mesh->vertex_count; ++i) {
        const Soft3D_Vertex *src = &mesh->vertices[i];
        Soft3D_Vertex *dst = &ctx->transformed[i];
        float x, y, z, rx, ry, rz;
        if (!finite_value(src->x) || !finite_value(src->y) || !finite_value(src->z) ||
            !finite_value(src->u) || !finite_value(src->v)) {
            return 0;
        }
        x = src->x * transform->scale;
        y = src->y * transform->scale;
        z = src->z * transform->scale;
        ry = y * cx - z * sx;
        rz = y * sx + z * cx;
        rx = x * cy + rz * sy;
        dst->z = -x * sy + rz * cy + transform->position.z;
        dst->x = rx * cz - ry * sz + transform->position.x;
        dst->y = rx * sz + ry * cz + transform->position.y;
        dst->u = src->u;
        dst->v = src->v;
        if (!finite_value(dst->x) || !finite_value(dst->y) || !finite_value(dst->z)) {
            return 0;
        }
        ctx->vertex_outcodes[i] = vertex_outcode(ctx, dst);
        add_counter(&ctx->stats.transformed_vertices, 1U);
    }
    return submit_transformed(ctx, mesh, material);
}

int soft3d_submit_quaternion(Soft3D_Context *ctx, const Soft3D_Mesh *mesh,
                             const Soft3D_Vec3 *position,
                             const Soft3D_Quaternion *orientation, float scale,
                             const Soft3D_Material *material)
{
    float norm, factor, xx, xy, xz, yy, yz, zz, wx, wy, wz;
    unsigned i;
    if (orientation == NULL || !finite_value(orientation->x) ||
        !finite_value(orientation->y) || !finite_value(orientation->z) ||
        !finite_value(orientation->w) ||
        !validate_submission(ctx, mesh, position, scale, material)) {
        return 0;
    }
    norm = orientation->x * orientation->x + orientation->y * orientation->y +
           orientation->z * orientation->z + orientation->w * orientation->w;
    if (norm < 0.000000000001f) return 0;
    /* Dividing quadratic products by the squared norm normalizes without a
     * square root and handles both quaternion signs identically.
     */
    factor = 2.0f / norm;
    xx = orientation->x * orientation->x * factor;
    xy = orientation->x * orientation->y * factor;
    xz = orientation->x * orientation->z * factor;
    yy = orientation->y * orientation->y * factor;
    yz = orientation->y * orientation->z * factor;
    zz = orientation->z * orientation->z * factor;
    wx = orientation->w * orientation->x * factor;
    wy = orientation->w * orientation->y * factor;
    wz = orientation->w * orientation->z * factor;
    for (i = 0U; i < mesh->vertex_count; ++i) {
        const Soft3D_Vertex *src = &mesh->vertices[i];
        Soft3D_Vertex *dst = &ctx->transformed[i];
        float x, y, z;
        if (!finite_value(src->x) || !finite_value(src->y) || !finite_value(src->z) ||
            !finite_value(src->u) || !finite_value(src->v)) return 0;
        x = src->x * scale;
        y = src->y * scale;
        z = src->z * scale;
        dst->x = (1.0f - yy - zz) * x + (xy - wz) * y + (xz + wy) * z + position->x;
        dst->y = (xy + wz) * x + (1.0f - xx - zz) * y + (yz - wx) * z + position->y;
        dst->z = (xz - wy) * x + (yz + wx) * y + (1.0f - xx - yy) * z + position->z;
        dst->u = src->u;
        dst->v = src->v;
        if (!finite_value(dst->x) || !finite_value(dst->y) || !finite_value(dst->z)) return 0;
        ctx->vertex_outcodes[i] = vertex_outcode(ctx, dst);
        add_counter(&ctx->stats.transformed_vertices, 1U);
    }
    return submit_transformed(ctx, mesh, material);
}

static int submit_transformed(Soft3D_Context *ctx, const Soft3D_Mesh *mesh,
                              const Soft3D_Material *material)
{
    unsigned i;
    int success = 1;
    for (i = 0U; i < mesh->triangle_count; ++i) {
        const Soft3D_Vertex *a = &ctx->transformed[mesh->indices[i * 3U]];
        const Soft3D_Vertex *b = &ctx->transformed[mesh->indices[i * 3U + 1U]];
        const Soft3D_Vertex *c = &ctx->transformed[mesh->indices[i * 3U + 2U]];
        float abx = b->x - a->x, aby = b->y - a->y, abz = b->z - a->z;
        float acx = c->x - a->x, acy = c->y - a->y, acz = c->z - a->z;
        float nx = aby * acz - abz * acy;
        float ny = abz * acx - abx * acz;
        float nz = abx * acy - aby * acx;
        float normal_length = sqrtf(nx * nx + ny * ny + nz * nz);
        float facing = nx * a->x + ny * a->y + nz * a->z;
        float diffuse;
        unsigned vertex, count = 3U, buffer = 0U;
        uint8_t code_a = ctx->vertex_outcodes[mesh->indices[i * 3U]];
        uint8_t code_b = ctx->vertex_outcodes[mesh->indices[i * 3U + 1U]];
        uint8_t code_c = ctx->vertex_outcodes[mesh->indices[i * 3U + 2U]];
        uint8_t light;
        ++ctx->stats.submitted_triangles;
        if (normal_length < 0.000001f ||
            (material->cull_backfaces && facing >= 0.0f)) {
            continue;
        }
        /* Keep clipped_triangles comparable with the original implementation:
         * only nondegenerate, nonculled triangles with outside vertices count.
         */
        if ((code_a & code_b & code_c) != 0U) {
            ++ctx->stats.clipped_triangles;
            add_counter(&ctx->stats.frustum_rejected, 1U);
            continue;
        }
        if (facing > 0.0f) { nx = -nx; ny = -ny; nz = -nz; }
        diffuse = (nx * -0.32444284f + ny * 0.48666427f + nz * -0.81110711f) /
                  normal_length;
        if (diffuse < 0.0f) diffuse = 0.0f;
        if (diffuse > 1.0f) diffuse = 1.0f;
        light = (uint8_t)(56.0f + 199.0f * diffuse);
        ctx->clip[0][0] = *a;
        ctx->clip[0][1] = *b;
        ctx->clip[0][2] = *c;
        if ((code_a | code_b | code_c) != 0U) {
            ++ctx->stats.clipped_triangles;
            count = clip_polygon(ctx, count, &buffer);
        }
        for (vertex = 1U; vertex + 1U < count; ++vertex) {
            if (!prepare_triangle(ctx, &ctx->clip[buffer][0],
                                  &ctx->clip[buffer][vertex],
                                  &ctx->clip[buffer][vertex + 1U], material, light)) {
                success = 0;
            }
        }
    }
    return success;
}

static uint16_t sample_texture(const Soft3D_Texture *texture, float u, float v)
{
    unsigned x, y;
    if (u < 0.0f) u = 0.0f;
    if (u > 1.0f) u = 1.0f;
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    x = (unsigned)(u * (float)texture->width);
    y = (unsigned)(v * (float)texture->height);
    if (x >= texture->width) x = (unsigned)texture->width - 1U;
    if (y >= texture->height) y = (unsigned)texture->height - 1U;
    return texture->pixels[y * texture->width + x];
}

static void render_triangle(Soft3D_Context *ctx, const Soft3D_Triangle *tri,
                             unsigned y, unsigned band_end,
                             uint16_t *pixels, float *depth)
{
    unsigned start_y, end_y;
    unsigned py;
    float dx0, dx1, dx2;
    if (tri->max_y < y || tri->min_y >= band_end) return;
    start_y = tri->min_y > y ? tri->min_y : y;
    end_y = (unsigned)tri->max_y + 1U;
    dx0 = tri->v[1].y - tri->v[2].y;
    dx1 = tri->v[2].y - tri->v[0].y;
    dx2 = tri->v[0].y - tri->v[1].y;
    if (end_y > band_end) end_y = band_end;
    for (py = start_y; py < end_y; ++py) {
        unsigned px;
        unsigned offset = (py - y) * ctx->width + tri->min_x;
        float sample_x = (float)tri->min_x + 0.5f;
        float sample_y = (float)py + 0.5f;
        float e0 = edge(&tri->v[1], &tri->v[2], sample_x, sample_y);
        float e1 = edge(&tri->v[2], &tri->v[0], sample_x, sample_y);
        float e2 = edge(&tri->v[0], &tri->v[1], sample_x, sample_y);
        float relative_x = sample_x - tri->v[0].x;
        float relative_y = sample_y - tri->v[0].y;
        float inverse_z = tri->v[0].inverse_z +
            relative_x * tri->inverse_z_gradient.dx + relative_y * tri->inverse_z_gradient.dy;
        float u_over_z = tri->v[0].u_over_z +
            relative_x * tri->u_over_z_gradient.dx + relative_y * tri->u_over_z_gradient.dy;
        float v_over_z = tri->v[0].v_over_z +
            relative_x * tri->v_over_z_gradient.dx + relative_y * tri->v_over_z_gradient.dy;
        /* Rebase once per absolute screen row, so floating-point drift does
         * not depend on band height or the order bands are rendered.
         */
        for (px = tri->min_x; px <= tri->max_x; ++px, ++offset) {
            if (e0 >= 0.0f && e1 >= 0.0f && e2 >= 0.0f) {
                if (inverse_z > depth[offset]) {
                    uint16_t color = tri->color;
                    if (tri->mode == SOFT3D_TEXTURED) {
                        float z = 1.0f / inverse_z;
                        float u = u_over_z * z;
                        float v = v_over_z * z;
                        color = shade(sample_texture(tri->texture, u, v), tri->light);
                    } else if (tri->mode == SOFT3D_WIREFRAME &&
                               e0 > tri->edge_threshold[0] &&
                               e1 > tri->edge_threshold[1] &&
                               e2 > tri->edge_threshold[2]) {
                        /* Opaque interiors hide edges on the far side. */
                        color = ctx->clear_color;
                    }
                    depth[offset] = inverse_z;
                    pixels[offset] = color;
                    ++ctx->stats.pixels_shaded;
                }
            }
            e0 += dx0;
            e1 += dx1;
            e2 += dx2;
            inverse_z += tri->inverse_z_gradient.dx;
            u_over_z += tri->u_over_z_gradient.dx;
            v_over_z += tri->v_over_z_gradient.dx;
        }
    }
}

static unsigned first_set_bit(uint32_t bits)
{
    unsigned bit = 0U;
    if ((bits & 0xFFFFU) == 0U) { bits >>= 16U; bit += 16U; }
    if ((bits & 0xFFU) == 0U) { bits >>= 8U; bit += 8U; }
    if ((bits & 0xFU) == 0U) { bits >>= 4U; bit += 4U; }
    if ((bits & 0x3U) == 0U) { bits >>= 2U; bit += 2U; }
    if ((bits & 0x1U) == 0U) ++bit;
    return bit;
}

int soft3d_render_band(Soft3D_Context *ctx, uint16_t y, uint16_t rows,
                       uint16_t *pixels, float *depth)
{
    unsigned i, band_end;
    if (ctx == NULL || !ctx->frame_valid || pixels == NULL || depth == NULL ||
        rows == 0U || y >= ctx->height || (unsigned)y + rows > ctx->height) return 0;
    band_end = (unsigned)y + rows;
    for (i = 0U; i < (unsigned)ctx->width * rows; ++i) {
        pixels[i] = ctx->clear_color;
        depth[i] = 0.0f;
    }
    add_counter(&ctx->stats.band_potential, ctx->triangle_count);
    if (ctx->band_index_enabled && ctx->height <= SOFT3D_BAND_ROWS * SOFT3D_BAND_BINS) {
        unsigned first_bin = (unsigned)y / SOFT3D_BAND_ROWS;
        unsigned last_bin = (band_end - 1U) / SOFT3D_BAND_ROWS;
        unsigned words = ((unsigned)ctx->triangle_count + 31U) / 32U;
        for (i = 0U; i < words; ++i) {
            uint32_t candidates = 0U;
            unsigned bin;
            for (bin = first_bin; bin <= last_bin; ++bin) candidates |= ctx->band_index[bin][i];
            /* Ascending bits preserve submission order, including equal-Z ties. */
            while (candidates != 0U) {
                unsigned triangle = i * 32U + first_set_bit(candidates);
                candidates &= candidates - 1U;
                add_counter(&ctx->stats.band_candidates, 1U);
                render_triangle(ctx, &ctx->triangles[triangle], y, band_end, pixels, depth);
            }
        }
    } else {
        add_counter(&ctx->stats.band_candidates, ctx->triangle_count);
        for (i = 0U; i < ctx->triangle_count; ++i) {
            render_triangle(ctx, &ctx->triangles[i], y, band_end, pixels, depth);
        }
    }
    return 1;
}
