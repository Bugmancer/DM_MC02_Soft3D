#include "soft3d_motion_task.h"
#include "soft3d_imu.h"
#include "main.h"
#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "task.h"

static Soft3D_Motion s_filter;
static Soft3D_MotionSnapshot s_snapshot;

static void publish(const Soft3D_MotionSnapshot *snapshot)
{
    taskENTER_CRITICAL();
    s_snapshot = *snapshot;
    taskEXIT_CRITICAL();
}

void Soft3D_MotionSnapshotRead(Soft3D_MotionSnapshot *snapshot)
{
    taskENTER_CRITICAL();
    *snapshot = s_snapshot;
    taskEXIT_CRITICAL();
}

bool Soft3D_MotionSnapshotReady(const Soft3D_MotionSnapshot *snapshot, uint32_t now_ms)
{
    uint32_t flags = snapshot->pose.flags;
    return snapshot->sensor_status == SOFT3D_IMU_READY &&
           snapshot->samples != 0U && now_ms - snapshot->sample_ms <= 100U &&
           (flags & SOFT3D_MOTION_CALIBRATED) != 0U &&
           (flags & (SOFT3D_MOTION_INITIALIZING | SOFT3D_MOTION_INVALID_SAMPLE)) == 0U;
}

void MotionTask_Entry(void *argument)
{
    Soft3D_MotionSnapshot snapshot = {0};
    uint32_t interval = osKernelGetTickFreq()/100U;
    uint32_t previous_ms = 0U, next_tick = 0U;
    bool online = false;
    (void)argument;
    if (interval == 0U) interval = 1U;
    for (;;) {
        Soft3D_IMUSample sample;
        uint32_t now_ms;
        if (!online) {
            Soft3D_MotionInit(&s_filter);
            Soft3D_MotionGetOutput(&s_filter, &snapshot.pose);
            online = Soft3D_IMU_Init();
            snapshot.sensor_status = Soft3D_IMU_Status();
            if (!online) {
                ++snapshot.io_errors;
                publish(&snapshot);
                osDelay(osKernelGetTickFreq());
                continue;
            }
            previous_ms = HAL_GetTick();
            next_tick = osKernelGetTickCount();
            publish(&snapshot);
        }
        next_tick += interval;
        if ((int32_t)(next_tick - osKernelGetTickCount()) <= 0) {
            next_tick = osKernelGetTickCount() + interval;
        }
        (void)osDelayUntil(next_tick);
        if (!Soft3D_IMU_Read(&sample)) {
            snapshot.sensor_status = Soft3D_IMU_Status();
            ++snapshot.io_errors;
            publish(&snapshot);
            online = false;
            osDelay(osKernelGetTickFreq());
            continue;
        }
        now_ms = HAL_GetTick();
        if ((sample.flags & SOFT3D_IMU_SATURATED) != 0U) {
            ++snapshot.rejected_samples;
            /* Saturation also breaks the filter's continuous calibration window. */
            (void)Soft3D_MotionUpdate(&s_filter, NULL, NULL, 0.0f);
            Soft3D_MotionGetOutput(&s_filter, &snapshot.pose);
        } else {
            bool accepted = Soft3D_MotionUpdate(&s_filter, sample.accel_g,
                                                sample.gyro_dps, (float)(now_ms - previous_ms)*0.001f);
            Soft3D_MotionGetOutput(&s_filter, &snapshot.pose);
            if (accepted) {
                snapshot.sample_ms = now_ms;
                ++snapshot.samples;
            } else {
                ++snapshot.rejected_samples;
            }
        }
        previous_ms = now_ms;
        if (snapshot.samples % 100U == 0U) {
            snapshot.stack_free_words = (uint32_t)uxTaskGetStackHighWaterMark(NULL);
        }
        publish(&snapshot);
    }
}
