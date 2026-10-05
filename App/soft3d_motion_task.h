#ifndef SOFT3D_MOTION_TASK_H
#define SOFT3D_MOTION_TASK_H

#include "soft3d_motion.h"

typedef struct {
    Soft3D_MotionOutput pose;
    uint32_t sensor_status;
    uint32_t sample_ms;
    uint32_t samples;
    uint32_t io_errors;
    uint32_t rejected_samples;
    uint32_t stack_free_words;
} Soft3D_MotionSnapshot;

void MotionTask_Entry(void *argument);
void Soft3D_MotionSnapshotRead(Soft3D_MotionSnapshot *snapshot);
bool Soft3D_MotionSnapshotReady(const Soft3D_MotionSnapshot *snapshot, uint32_t now_ms);

#endif
