#include "soft3d_scene.h"
#include "soft3d_models.h"

#include <math.h>
#include <stddef.h>

static Soft3D_Quaternion multiply(Soft3D_Quaternion a, Soft3D_Quaternion b)
{
    Soft3D_Quaternion q;
    q.x = a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y;
    q.y = a.w*b.y - a.x*b.z + a.y*b.w + a.z*b.x;
    q.z = a.w*b.z + a.x*b.y - a.y*b.x + a.z*b.w;
    q.w = a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z;
    return q;
}

static Soft3D_Quaternion from_euler(float x, float y, float z)
{
    Soft3D_Quaternion qx = {0.0f, 0.0f, 0.0f, 0.0f};
    Soft3D_Quaternion qy = {0.0f, 0.0f, 0.0f, 0.0f};
    Soft3D_Quaternion qz = {0.0f, 0.0f, 0.0f, 0.0f};
    qx.x = sinf(x*0.5f); qx.w = cosf(x*0.5f);
    qy.y = sinf(y*0.5f); qy.w = cosf(y*0.5f);
    qz.z = sinf(z*0.5f); qz.w = cosf(z*0.5f);
    return multiply(qz, multiply(qy, qx));
}

static Soft3D_Vec3 rotate(Soft3D_Quaternion q, Soft3D_Vec3 v)
{
    Soft3D_Quaternion p = {v.x, v.y, v.z, 0.0f};
    Soft3D_Quaternion conjugate = {-q.x, -q.y, -q.z, q.w};
    Soft3D_Quaternion result = multiply(multiply(q, p), conjugate);
    Soft3D_Vec3 out = {result.x, result.y, result.z};
    return out;
}

const char *Soft3D_SceneName(Soft3D_Scene scene)
{
    static const char *const names[] = {"CUBE", "TORUS", "ORBIT"};
    return (unsigned)scene < SOFT3D_SCENE_COUNT ? names[(unsigned)scene] : "UNKNOWN";
}

Soft3D_Quaternion Soft3D_SceneAutoOrientation(float seconds)
{
    return from_euler(0.45f + 0.37f*seconds, 0.60f + 0.61f*seconds, 0.13f*seconds);
}

Soft3D_Quaternion Soft3D_SceneRelativeOrientation(Soft3D_Quaternion reference,
                                                Soft3D_Quaternion current)
{
    Soft3D_Quaternion inverse = {-reference.x, -reference.y, -reference.z, reference.w};
    return multiply(Soft3D_SceneAutoOrientation(0.0f), multiply(inverse, current));
}

static int submit_part(Soft3D_Context *ctx, const Soft3D_Mesh *mesh,
                       const Soft3D_SceneView *view, Soft3D_Quaternion group,
                       Soft3D_Vec3 offset, Soft3D_Quaternion local,
                       float scale, uint16_t color)
{
    Soft3D_Material material = {view->mode, color, &soft3d_texture_checker, 1U};
    Soft3D_Vec3 position = rotate(group, offset);
    Soft3D_Quaternion orientation = multiply(group, local);
    position.z += view->distance;
    return soft3d_submit_quaternion(ctx, mesh, &position, &orientation, scale, &material);
}

int Soft3D_SceneSubmit(Soft3D_Context *ctx, const Soft3D_SceneView *view)
{
    const Soft3D_Vec3 origin = {0.0f, 0.0f, 0.0f};
    const Soft3D_Quaternion identity = {0.0f, 0.0f, 0.0f, 1.0f};
    Soft3D_Quaternion q;
    float norm, t;
    unsigned i;
    int success;
    if (ctx == NULL || view == NULL || (unsigned)view->scene >= SOFT3D_SCENE_COUNT ||
        !(view->distance > 0.0f && view->distance < 1000.0f) ||
        !(view->animation_seconds >= 0.0f && view->animation_seconds < 100000.0f)) return 0;
    q = view->orientation;
    norm = q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w;
    if (!(norm > 0.00000001f && norm < 1000000.0f)) return 0;
    norm = 1.0f/sqrtf(norm);
    q.x *= norm; q.y *= norm; q.z *= norm; q.w *= norm;
    if (view->scene != SOFT3D_SCENE_ORBIT) {
        return submit_part(ctx, view->scene == SOFT3D_SCENE_CUBE ? &soft3d_mesh_cube :
                           &soft3d_mesh_torus, view, q, origin, identity, 1.0f,
                           view->scene == SOFT3D_SCENE_CUBE ? 0x07DFU : 0xFDA0U);
    }
    t = view->animation_seconds;
    success = submit_part(ctx, &soft3d_mesh_torus, view, q, origin,
                          from_euler(0.0f, 0.0f, 0.25f*t), 0.8f, 0xFDA0U);
    if (!submit_part(ctx, &soft3d_mesh_cube, view, q, origin,
                      from_euler(0.7f*t, 0.9f*t, 0.0f), 0.5f, 0x07DFU)) success = 0;
    for (i = 0U; i < 4U; ++i) {
        static const uint16_t colors[] = {0xF81FU, 0x07DFU, 0xA7E0U, 0xFD20U};
        float phase = 0.8f*t + (float)i*1.57079632679f;
        Soft3D_Vec3 offset = {0.0f, 0.0f, 0.0f};
        offset.x = 1.3f*cosf(phase);
        offset.y = 0.4f*sinf(phase);
        offset.z = 1.3f*sinf(phase);
        if (!submit_part(ctx, &soft3d_mesh_cube, view, q, offset,
                          from_euler(phase, 0.9f*t, phase*0.5f), 0.28f, colors[i])) success = 0;
    }
    return success;
}
