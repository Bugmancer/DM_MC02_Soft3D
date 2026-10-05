#ifndef SOFT3D_H
#define SOFT3D_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SOFT3D_MAX_VERTICES 256U
#define SOFT3D_MAX_TRIANGLES 512U
#define SOFT3D_CLIP_VERTICES 10U
#define SOFT3D_BAND_ROWS 16U
#define SOFT3D_BAND_BINS 32U
#define SOFT3D_BAND_WORDS ((SOFT3D_MAX_TRIANGLES + 31U) / 32U)

typedef struct { float x, y, z; } Soft3D_Vec3;
typedef struct { float x, y, z, w; } Soft3D_Quaternion;
typedef struct { float x, y, z, u, v; } Soft3D_Vertex;

typedef struct {
    const Soft3D_Vertex *vertices;
    const uint16_t *indices;
    uint16_t vertex_count;
    uint16_t triangle_count;
} Soft3D_Mesh;

typedef struct {
    const uint16_t *pixels;
    uint16_t width;
    uint16_t height;
} Soft3D_Texture;

typedef enum {
    SOFT3D_TEXTURED = 0,
    SOFT3D_LIT,
    SOFT3D_WIREFRAME
} Soft3D_Mode;

typedef struct {
    Soft3D_Mode mode;
    uint16_t color;
    const Soft3D_Texture *texture;
    uint8_t cull_backfaces;
} Soft3D_Material;

/* Camera space looks along +Z; rotation is applied X, Y, then Z, in radians. */
typedef struct {
    Soft3D_Vec3 position;
    Soft3D_Vec3 rotation;
    float scale;
} Soft3D_Transform;

typedef struct {
    float fov_y_radians;
    float near_z;
    float far_z;
} Soft3D_Camera;

typedef struct {
    uint32_t submitted_triangles;
    uint32_t clipped_triangles;
    uint32_t prepared_triangles;
    uint32_t dropped_triangles;
    uint32_t pixels_shaded;
    /* Candidate triangle visits and the full-list visits they replace. Both
     * accumulate per valid render_band call and saturate at UINT32_MAX.
     */
    uint32_t band_candidates;
    uint32_t band_potential;
    uint32_t transformed_vertices;
    uint32_t rotation_cache_hits;
    uint32_t frustum_rejected;
} Soft3D_Stats;

typedef struct {
    float x, y, inverse_z, u_over_z, v_over_z;
} Soft3D_ScreenVertex;

typedef struct { float dx, dy; } Soft3D_Gradient;

typedef struct {
    Soft3D_ScreenVertex v[3];
    const Soft3D_Texture *texture;
    float inverse_area;
    float edge_threshold[3];
    Soft3D_Gradient inverse_z_gradient;
    Soft3D_Gradient u_over_z_gradient;
    Soft3D_Gradient v_over_z_gradient;
    uint16_t color;
    uint16_t min_x, max_x, min_y, max_y;
    uint8_t light;
    uint8_t mode;
} Soft3D_Triangle;

/* Allocate this once in static RAM, never on a small RTOS task stack.
 * Scratch vertices are reused by submissions; meshes need not remain resident.
 * Texture storage must remain valid until every band of the frame is rendered.
 */
typedef struct {
    Soft3D_Triangle triangles[SOFT3D_MAX_TRIANGLES];
    Soft3D_Vertex transformed[SOFT3D_MAX_VERTICES];
    Soft3D_Vertex clip[2][SOFT3D_CLIP_VERTICES];
    uint32_t band_index[SOFT3D_BAND_BINS][SOFT3D_BAND_WORDS];
    uint8_t vertex_outcodes[SOFT3D_MAX_VERTICES];
    Soft3D_Vec3 cached_rotation;
    float cached_trig[6];
    Soft3D_Stats stats;
    Soft3D_Camera camera;
    float tan_half_x, tan_half_y;
    float focal_length;
    uint16_t width, height;
    uint16_t clear_color;
    uint16_t triangle_count;
    uint8_t frame_valid;
    uint8_t band_index_enabled;
    uint8_t rotation_cache_valid;
} Soft3D_Context;

/* Invalid dimensions/camera leave frame_valid false and submissions fail. */
void soft3d_init(Soft3D_Context *ctx, uint16_t width, uint16_t height);
void soft3d_begin_frame(Soft3D_Context *ctx, const Soft3D_Camera *camera,
                       uint16_t clear_rgb565);
/* Enabled by init. The index is maintained even when disabled, so toggling
 * between render calls is safe. Heights above 512 always use the linear path.
 */
void soft3d_set_band_index_enabled(Soft3D_Context *ctx, int enable);
/* Returns 1 on success, 0 on invalid input/capacity exhaustion. Earlier valid
 * submissions are preserved. A failed mesh submission may prepare a prefix.
 */
int soft3d_submit(Soft3D_Context *ctx, const Soft3D_Mesh *mesh,
                  const Soft3D_Transform *transform,
                  const Soft3D_Material *material);
/* Active object-to-camera rotation in xyzw order. Internally normalized; zero
 * and nonfinite quaternions are rejected. q and -q describe the same rotation.
 */
int soft3d_submit_quaternion(Soft3D_Context *ctx, const Soft3D_Mesh *mesh,
                             const Soft3D_Vec3 *position,
                             const Soft3D_Quaternion *orientation, float scale,
                             const Soft3D_Material *material);
/* Caller supplies width*rows elements in each buffer. All pixels/depth values
 * are initialized on every call. RGB565 words are host-endian. Texture sampling
 * is nearest-neighbor, UV clamped to [0,1]. Out-of-frame bands are rejected.
 * Return 1 for a rendered band, 0 for invalid input (buffers remain untouched).
 */
int soft3d_render_band(Soft3D_Context *ctx, uint16_t y, uint16_t rows,
                       uint16_t *pixels, float *depth);

#ifdef __cplusplus
}
#endif
#endif
