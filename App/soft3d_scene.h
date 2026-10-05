#ifndef SOFT3D_SCENE_H
#define SOFT3D_SCENE_H

#include "soft3d.h"

typedef enum {
    SOFT3D_SCENE_CUBE = 0,
    SOFT3D_SCENE_TORUS,
    SOFT3D_SCENE_ORBIT,
    SOFT3D_SCENE_COUNT
} Soft3D_Scene;

typedef struct {
    Soft3D_Scene scene;
    Soft3D_Mode mode;
    Soft3D_Quaternion orientation;
    float distance;
    float animation_seconds;
} Soft3D_SceneView;

const char *Soft3D_SceneName(Soft3D_Scene scene);
Soft3D_Quaternion Soft3D_SceneAutoOrientation(float seconds);
/* Preserve the demo's initial view when centering the physical board. */
Soft3D_Quaternion Soft3D_SceneRelativeOrientation(Soft3D_Quaternion reference,
                                                Soft3D_Quaternion current);
int Soft3D_SceneSubmit(Soft3D_Context *ctx, const Soft3D_SceneView *view);

#endif
