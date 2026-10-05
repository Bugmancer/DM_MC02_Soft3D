#include "soft3d_app.h"
#include "soft3d.h"
#include "soft3d_maze.h"
#include "soft3d_maze_scene.h"
#include "soft3d_profile.h"
#include "soft3d_scene.h"
#include "soft3d_damage.h"
#include "soft3d_motion_task.h"
#include "soft3d_imu.h"
#include "soft3d_lcd.h"
#include "soft3d_input.h"
#include "soft3d_ui.h"
#include "soft3d_boot.h"
#include "main.h"
#include "adc.h"
#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "task.h"
#include "usbd_cdc_if.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

#define BAND_ROWS SOFT3D_BAND_ROWS
#define BAND_PIXELS (SOFT3D_LCD_WIDTH * BAND_ROWS)
#define LCD_WAIT_MS 100U
/* Keep this recovery build independent of the DMA display path. */
#ifndef SOFT3D_DIAGNOSTIC_POLLING
#define SOFT3D_DIAGNOSTIC_POLLING 1U
#endif

static Soft3D_Context s_renderer;
static Soft3D_MazeState s_maze;
static Soft3D_ProfileState s_profile;
static float s_depth[BAND_PIXELS];
#if defined(__CC_ARM)
static uint16_t s_pixels[2][BAND_PIXELS] __attribute__((section("LCD_DMA"), zero_init, aligned(32)));
#else
static uint16_t s_pixels[2][BAND_PIXELS] __attribute__((section("LCD_DMA"), aligned(32)));
#endif
static osMessageQueueId_t s_keys;
volatile Soft3D_Status g_soft3d_status;

static uint32_t cycles_to_us(uint32_t cycles)
{
    return cycles / (SystemCoreClock / 1000000U);
}

static unsigned display_tenths_ms(uint32_t microseconds)
{
    uint32_t tenths = microseconds / 100U;
    return (unsigned)(tenths > 99999U ? 99999U : tenths);
}

void Soft3D_AppInit(void)
{
    s_keys = osMessageQueueNew(8U, sizeof(Soft3D_Key), NULL);
    if (s_keys == NULL) {
        g_soft3d_status.fatal_error = 1U;
        Error_Handler();
    }
}

void InputTask_Entry(void *argument)
{
    Soft3D_KeyGesture gesture = {{SOFT3D_KEY_NONE, 0U, 0U}, 0U, SOFT3D_KEY_NONE, 0U};
    uint8_t calibrated = 0U;
    (void)argument;
    for (;;) {
        uint16_t adc;
        Soft3D_Key key;
        if (calibrated == 0U) {
            if (HAL_ADCEx_Calibration_Start(&hadc1, ADC_CALIB_OFFSET,
                                           ADC_SINGLE_ENDED) != HAL_OK) {
                ++g_soft3d_status.adc_errors;
                osDelay(500U);
                continue;
            }
            calibrated = 1U;
        }
        if (HAL_ADC_Start(&hadc1) != HAL_OK ||
            HAL_ADC_PollForConversion(&hadc1, 2U) != HAL_OK) {
            (void)HAL_ADC_Stop(&hadc1);
            ++g_soft3d_status.adc_errors;
            Soft3D_KeyGestureCancel(&gesture, HAL_GetTick());
            osDelay(10U);
            continue;
        }
        adc = (uint16_t)HAL_ADC_GetValue(&hadc1);
        (void)HAL_ADC_Stop(&hadc1);
        g_soft3d_status.key_adc = adc;
        key = Soft3D_KeyGestureUpdate(&gesture, adc, HAL_GetTick());
        if (key != SOFT3D_KEY_NONE && osMessageQueuePut(s_keys, &key, 0U, 0U) != osOK) {
            ++g_soft3d_status.input_dropped;
        }
        osDelay(10U);
    }
}

void RenderTask_Entry(void *argument)
{
    const Soft3D_Camera camera = Soft3D_MazeSceneCamera();
    float reference[4] = {1.0f, 0.0f, 0.0f, 0.0f};
    uint32_t previous_ms, rate_start_ms, rate_frames = 0U;
    uint32_t best_ms = 0U, previous_game_state = SOFT3D_GAME_STATUS;
    uint32_t lab_frame = 0U;
    unsigned lab_workload = 0U;
    bool lab_mode = false, lab_paused = false, band_index = true;
    bool paused = false, reference_pending = true, previously_active = false;
    bool win_recorded = false;
    uint16_t previous_geometry = 0U;
    bool full_refresh = true;
    bool lcd_ready = false;
    bool show_help = true;
    bool display_recovering = false;
    bool polling = SOFT3D_DIAGNOSTIC_POLLING != 0U;
    (void)argument;

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    soft3d_init(&s_renderer, SOFT3D_LCD_WIDTH, SOFT3D_LCD_HEIGHT);
    (void)Soft3D_MazeSelectLevel(1U);
    Soft3D_MazeInit(&s_maze);
    Soft3D_ProfileInit(&s_profile);
    g_soft3d_status.motion_control = 1U;
    g_soft3d_status.game_state = SOFT3D_GAME_STATUS;
    previous_ms = HAL_GetTick();
    rate_start_ms = previous_ms;

    for (;;) {
        uint32_t frame_cycles, work_cycles, work_start, now_ms, wait_cycles = 0U;
        uint32_t bytes_sent = 0U;
        uint32_t raster_cycles = 0U;
        uint16_t y;
        uint16_t current_geometry, damage_bands;
        uint8_t buffer = 0U;
        Soft3D_Key key;
        Soft3D_MotionSnapshot motion;
        bool motion_ready;
        bool active;
        float gravity_x = 0.0f, gravity_y = 0.0f;
        const char *control_label;
        char metrics[48];
        char profile_title[24], profile_timings[48], profile_metrics[48];
        Soft3D_ProfileSnapshot profile;
        bool transfer_failed = false;
        bool raster_failed = false;
        if (!lcd_ready) {
            g_soft3d_status.stage = 1U;
            if (!Soft3D_LCD_Init()) {
                ++g_soft3d_status.lcd_errors;
                g_soft3d_status.display_error = (uint32_t)Soft3D_LCD_LastError();
                g_soft3d_status.stage = 3U;
                g_soft3d_status.display_mode = 0U;
                display_recovering = true;
                osDelay(500U);
                continue;
            }
            lcd_ready = true;
            full_refresh = true;
            Soft3D_ProfileInit(&s_profile);
            previous_ms = HAL_GetTick();
            rate_start_ms = previous_ms;
            rate_frames = 0U;
            g_soft3d_status.display_error = (uint32_t)Soft3D_LCD_LastError();
        }
        Soft3D_MotionSnapshotRead(&motion);
        now_ms = HAL_GetTick();
        motion_ready = Soft3D_MotionSnapshotReady(&motion, now_ms);
        while (osMessageQueueGet(s_keys, &key, NULL, 0U) == osOK) {
            if (show_help) {
                if (key == SOFT3D_KEY_SELECT) {
                    show_help = false;
                    full_refresh = true;
                    break;
                }
                continue;
            }
            switch (key) {
            case SOFT3D_KEY_SELECT:
                if (lab_mode) {
                    lab_paused = !lab_paused;
                    Soft3D_ProfileInit(&s_profile);
                } else if (!s_maze.won) paused = !paused;
                break;
            case SOFT3D_KEY_SELECT_HOLD:
                Soft3D_ProfileInit(&s_profile);
                lab_frame = 0U;
                lab_paused = false;
                if (lab_mode) break;
                Soft3D_MazeRestart(&s_maze);
                paused = false;
                reference_pending = true;
                previously_active = false;
                win_recorded = false;
                full_refresh = true;
                break;
            case SOFT3D_KEY_LEFT:
                if (lab_mode) {
                    band_index = !band_index;
                    soft3d_set_band_index_enabled(&s_renderer, band_index ? 1 : 0);
                    if (!lab_paused) lab_frame = 0U;
                    Soft3D_ProfileInit(&s_profile);
                    full_refresh = true;
                }
                break;
            case SOFT3D_KEY_RIGHT:
                if (lab_mode) {
                    lab_workload = (lab_workload + 1U) % (SOFT3D_SCENE_COUNT + 1U);
                    lab_frame = 0U;
                    Soft3D_ProfileInit(&s_profile);
                    full_refresh = true;
                    break;
                }
                if (Soft3D_MazeSelectLevel(Soft3D_MazeLevelGet() < SOFT3D_MAZE_MAX_LEVEL ?
                                          Soft3D_MazeLevelGet() + 1U : 1U)) {
                    Soft3D_MazeRestart(&s_maze);
                    best_ms = 0U;
                    paused = false;
                    reference_pending = true;
                    previously_active = false;
                    win_recorded = false;
                    Soft3D_ProfileInit(&s_profile);
                    full_refresh = true;
                }
                break;
            case SOFT3D_KEY_DOWN:
                lab_mode = !lab_mode;
                lab_frame = 0U;
                previously_active = false;
                Soft3D_ProfileInit(&s_profile);
                full_refresh = true;
                break;
            case SOFT3D_KEY_UP:
                reference_pending = true;
                previously_active = false;
                Soft3D_MazeUpdate(&s_maze, 0U, 0.0f, 0.0f, false);
                break;
            case SOFT3D_KEY_DOWN_HOLD:
                show_help = true;
                full_refresh = true;
                break;
            default: break;
            }
            if (show_help) break;
        }
        g_soft3d_status.maze_level = Soft3D_MazeLevelGet();
        g_soft3d_status.maze_seed = Soft3D_MazeSeedGet();
        g_soft3d_status.lab_mode = lab_mode ? 1U : 0U;
        g_soft3d_status.lab_workload = lab_workload;
        g_soft3d_status.band_index = band_index ? 1U : 0U;
        if (show_help) {
            char footer[48];
            const char *detail;
            const char *phase = display_recovering ? "DISPLAY RECOVERY" : "LCD POLLING OK";
            Soft3D_MazeUpdate(&s_maze, 0U, 0.0f, 0.0f, false);
            previously_active = false;
            Soft3D_ProfileInit(&s_profile);
            g_soft3d_status.game_state = SOFT3D_GAME_STATUS;
            g_soft3d_status.game_ms = s_maze.elapsed_ms;
            g_soft3d_status.best_ms = best_ms;
            if (g_soft3d_status.render_error != SOFT3D_RENDER_ERROR_NONE) {
                phase = "RENDER ERROR";
                detail = g_soft3d_status.render_error == SOFT3D_RENDER_ERROR_PREPARE ?
                         "GEOMETRY FAILED" : "RASTER FAILED";
            } else if (lab_mode) detail = "ENGINE LAB READY";
            else if (motion_ready) detail = "IMU READY";
            else if (motion.sensor_status != SOFT3D_IMU_READY) detail = "IMU OFFLINE";
            else if ((motion.pose.flags & SOFT3D_MOTION_CALIBRATED) == 0U ||
                     (motion.pose.flags & SOFT3D_MOTION_INITIALIZING) != 0U) detail = "IMU CALIBRATING";
            else detail = "IMU WAIT DATA";
            (void)snprintf(footer, sizeof(footer), SOFT3D_DISPLAY_BUILD " ADC %lu E%lu R%lu",
                           (unsigned long)g_soft3d_status.key_adc,
                           (unsigned long)g_soft3d_status.display_error,
                           (unsigned long)g_soft3d_status.render_error);
            g_soft3d_status.stage = 4U;
            g_soft3d_status.display_mode = 1U;
            g_soft3d_status.fps_tenths = 0U;
            g_soft3d_status.frame_us = 0U;
            g_soft3d_status.render_us = 0U;
            g_soft3d_status.geometry_us = 0U;
            g_soft3d_status.dma_wait_us = 0U;
            g_soft3d_status.transmitted_bytes = 0U;
            g_soft3d_status.triangles = 0U;
            g_soft3d_status.raster_us = 0U;
            g_soft3d_status.band_candidates = 0U;
            g_soft3d_status.band_potential = 0U;
            g_soft3d_status.profile_samples = 0U;
            g_soft3d_status.frame_mean_us = 0U;
            g_soft3d_status.frame_p95_us = 0U;
            g_soft3d_status.frame_max_us = 0U;
            if (!Soft3D_BootStatus(phase, detail, footer)) {
                g_soft3d_status.display_error = (uint32_t)Soft3D_LCD_LastError();
                ++g_soft3d_status.lcd_errors;
                lcd_ready = false;
                display_recovering = true;
                g_soft3d_status.stage = 3U;
                g_soft3d_status.display_mode = 0U;
            } else display_recovering = false;
            osDelay(250U);
            previous_ms = HAL_GetTick();
            rate_start_ms = previous_ms;
            rate_frames = 0U;
            continue;
        }
        if (motion_ready) {
            if (reference_pending) {
                memcpy(reference, motion.pose.quaternion, sizeof(reference));
                reference_pending = false;
                previously_active = false;
            }
            motion_ready = Soft3D_MazeGravity(reference, motion.pose.quaternion,
                                               &gravity_x, &gravity_y);
            if (!motion_ready) reference_pending = true;
        } else if (motion.sensor_status != SOFT3D_IMU_READY ||
                   (motion.pose.flags & SOFT3D_MOTION_CALIBRATED) == 0U ||
                   (motion.pose.flags & SOFT3D_MOTION_INITIALIZING) != 0U) {
            reference_pending = true;
        }
        active = motion_ready && !paused && !s_maze.won && !lab_mode;
        Soft3D_MazeUpdate(&s_maze, active && previously_active ? now_ms - previous_ms : 0U,
                           gravity_x, gravity_y, active);
        previously_active = active;
        previous_ms = now_ms;
        if (lab_mode) {
            control_label = "LAB";
            g_soft3d_status.game_state = SOFT3D_GAME_LAB;
        } else if (s_maze.won) {
            control_label = "WON";
            g_soft3d_status.game_state = SOFT3D_GAME_WON;
            if (!win_recorded) {
                if (best_ms == 0U || s_maze.elapsed_ms < best_ms) {
                    best_ms = s_maze.elapsed_ms;
                }
                ++g_soft3d_status.game_wins;
                win_recorded = true;
            }
        } else if (!motion_ready) {
            if (motion.sensor_status != SOFT3D_IMU_READY) {
                control_label = "NO IMU";
                g_soft3d_status.game_state = SOFT3D_GAME_OFFLINE;
            } else if ((motion.pose.flags & SOFT3D_MOTION_CALIBRATED) == 0U ||
                       (motion.pose.flags & SOFT3D_MOTION_INITIALIZING) != 0U) {
                control_label = "CAL";
                g_soft3d_status.game_state = SOFT3D_GAME_CALIBRATING;
            } else {
                control_label = "WAIT";
                g_soft3d_status.game_state = SOFT3D_GAME_WAIT;
            }
        } else if (paused) {
            control_label = "PAUSED";
            g_soft3d_status.game_state = SOFT3D_GAME_PAUSED;
        } else {
            control_label = (motion.pose.flags & SOFT3D_MOTION_ACCEL_REJECTED) != 0U ? "GYRO" : "IMU";
            g_soft3d_status.game_state = SOFT3D_GAME_PLAYING;
        }
        if (g_soft3d_status.game_state != previous_game_state) full_refresh = true;
        previous_game_state = g_soft3d_status.game_state;
        g_soft3d_status.game_ms = s_maze.elapsed_ms;
        g_soft3d_status.best_ms = best_ms;
        Soft3D_ProfileGet(&s_profile, &profile);
        g_soft3d_status.profile_samples = profile.samples;
        g_soft3d_status.frame_mean_us = profile.frame_mean_us;
        g_soft3d_status.frame_p95_us = profile.frame_p95_us;
        g_soft3d_status.frame_max_us = profile.frame_max_us;
        frame_cycles = DWT->CYCCNT;
        work_start = frame_cycles;
        soft3d_begin_frame(&s_renderer, &camera, SOFT3D_MAZE_CLEAR_COLOR);
        {
            int submitted;
            float phase = 6.2831853072f * (float)lab_frame / (float)SOFT3D_PROFILE_WINDOW_SIZE;
            if (lab_mode && lab_workload != 0U) {
                Soft3D_SceneView view;
                view.scene = (Soft3D_Scene)(lab_workload - 1U);
                view.mode = SOFT3D_TEXTURED;
                view.orientation = Soft3D_SceneAutoOrientation((float)lab_frame * 0.05f);
                view.distance = 4.8f;
                view.animation_seconds = (float)lab_frame * 0.05f;
                submitted = Soft3D_SceneSubmit(&s_renderer, &view);
            } else if (lab_mode) {
                Soft3D_MazeState view;
                Soft3D_MazeInit(&view);
                view.tilt_x = sinf(phase) * 0.3f;
                view.tilt_y = cosf(phase) * 0.3f;
                view.ball_orientation[0] = cosf(phase * 0.5f);
                view.ball_orientation[2] = sinf(phase * 0.5f);
                submitted = Soft3D_MazeSceneSubmit(&s_renderer, &view);
            } else submitted = Soft3D_MazeSceneSubmit(&s_renderer, &s_maze);
            if (!submitted) {
                ++g_soft3d_status.geometry_errors;
                g_soft3d_status.render_error = SOFT3D_RENDER_ERROR_PREPARE;
                Soft3D_ProfileInit(&s_profile);
                show_help = true;
                full_refresh = true;
                continue;
            }
        }
        g_soft3d_status.geometry_us = cycles_to_us(DWT->CYCCNT - work_start);
        current_geometry = Soft3D_GeometryBands(&s_renderer);
        damage_bands = (uint16_t)(Soft3D_DamageBands(previous_geometry, current_geometry,
                                                    full_refresh, false) | SOFT3D_MAZE_UI_BANDS);
        if (lab_mode) damage_bands = SOFT3D_ALL_BANDS;
        if (best_ms != 0U) {
            (void)snprintf(metrics, sizeof(metrics), "TIME %lu.%luS  BEST %lu.%luS",
                           (unsigned long)(s_maze.elapsed_ms / 1000U),
                           (unsigned long)((s_maze.elapsed_ms / 100U) % 10U),
                           (unsigned long)(best_ms / 1000U), (unsigned long)((best_ms / 100U) % 10U));
        } else {
            (void)snprintf(metrics, sizeof(metrics), "TIME %lu.%luS  BEST --",
                           (unsigned long)(s_maze.elapsed_ms / 1000U),
                           (unsigned long)((s_maze.elapsed_ms / 100U) % 10U));
        }
        if (lab_mode) {
            unsigned geometry = display_tenths_ms(profile.geometry_mean_us);
            unsigned raster = display_tenths_ms(profile.raster_mean_us);
            unsigned transport = display_tenths_ms(profile.transport_mean_us);
            unsigned frame = display_tenths_ms(profile.frame_mean_us);
            unsigned p95 = display_tenths_ms(profile.frame_p95_us);
            unsigned skipped = profile.potential_triangles == 0U ? 0U :
                (unsigned)(((uint64_t)(profile.potential_triangles - profile.candidate_triangles) * 100U) /
                           profile.potential_triangles);
            (void)snprintf(profile_title, sizeof(profile_title), "LAB %s%s",
                lab_workload == 0U ? "MAZE" : Soft3D_SceneName((Soft3D_Scene)(lab_workload - 1U)),
                lab_paused ? " HOLD" : "");
            (void)snprintf(profile_timings, sizeof(profile_timings), "G%u.%u R%u.%u IO%u.%u MS",
                geometry / 10U, geometry % 10U, raster / 10U, raster % 10U,
                transport / 10U, transport % 10U);
            if (profile.samples == SOFT3D_PROFILE_WINDOW_SIZE) {
                (void)snprintf(profile_metrics, sizeof(profile_metrics), "F%u.%u P95 %u.%u SKIP %u N64",
                    frame / 10U, frame % 10U, p95 / 10U, p95 % 10U, skipped);
            } else {
                (void)snprintf(profile_metrics, sizeof(profile_metrics), "F%u.%u P95 -- SKIP %u N%lu",
                    frame / 10U, frame % 10U, skipped, (unsigned long)profile.samples);
            }
        }
        work_cycles = DWT->CYCCNT - work_start;
        for (y = 0U; y < SOFT3D_LCD_HEIGHT; y = (uint16_t)(y + BAND_ROWS)) {
            uint32_t i;
            if ((damage_bands & (1U << (y/BAND_ROWS))) == 0U) continue;
            work_start = DWT->CYCCNT;
            if (!soft3d_render_band(&s_renderer, y, BAND_ROWS, s_pixels[buffer], s_depth)) {
                ++g_soft3d_status.geometry_errors;
                g_soft3d_status.render_error = SOFT3D_RENDER_ERROR_RASTER;
                raster_failed = true;
                break;
            }
            raster_cycles += DWT->CYCCNT - work_start;
            if (lab_mode) {
                Soft3D_UI_ProfileBand(s_pixels[buffer], SOFT3D_LCD_WIDTH, SOFT3D_LCD_HEIGHT,
                                     y, BAND_ROWS, profile_title, band_index ? "INDEX ON" : "INDEX OFF",
                                     profile_timings, profile_metrics);
            } else Soft3D_UI_MazeLevelBand(s_pixels[buffer], SOFT3D_LCD_WIDTH, SOFT3D_LCD_HEIGHT,
                                   y, BAND_ROWS, control_label, metrics, s_maze.won ? 1U : 0U,
                                   Soft3D_MazeLevelGet());
            /* SPI uses bytes; transmit each RGB565 word most-significant byte first. */
            for (i = 0U; i < BAND_PIXELS; ++i) {
                uint16_t color = s_pixels[buffer][i];
                s_pixels[buffer][i] = (uint16_t)((color << 8) | (color >> 8));
            }
            work_cycles += DWT->CYCCNT - work_start;
            if (polling) {
                work_start = DWT->CYCCNT;
                if (!Soft3D_LCD_WriteBlocking(y, BAND_ROWS, (uint8_t *)s_pixels[buffer])) {
                    transfer_failed = true;
                }
                wait_cycles += DWT->CYCCNT - work_start;
                if (transfer_failed) break;
                bytes_sent += BAND_PIXELS*2U;
                continue;
            }
            /* The alternate buffer renders while the previous band is on the wire. */
            work_start = DWT->CYCCNT;
            if (!Soft3D_LCD_Wait(LCD_WAIT_MS)) {
                transfer_failed = true;
                break;
            }
            wait_cycles += DWT->CYCCNT - work_start;
            if (!Soft3D_LCD_Submit(y, BAND_ROWS, (uint8_t *)s_pixels[buffer])) {
                transfer_failed = true;
                break;
            }
            bytes_sent += BAND_PIXELS*2U;
            buffer ^= 1U;
        }
        /* Retire the last transfer, including when rasterization failed mid-frame. */
        if (!polling) {
            work_start = DWT->CYCCNT;
            if (!Soft3D_LCD_Wait(LCD_WAIT_MS)) transfer_failed = true;
            wait_cycles += DWT->CYCCNT - work_start;
        }
        if (raster_failed || transfer_failed) {
            Soft3D_ProfileInit(&s_profile);
            if (transfer_failed) {
                ++g_soft3d_status.lcd_errors;
                g_soft3d_status.display_error = (uint32_t)Soft3D_LCD_LastError();
                display_recovering = true;
                polling = true;
                lcd_ready = false;
            }
            show_help = true;
            full_refresh = true;
            continue;
        }
        Soft3D_LCD_Backlight(true);
        previous_geometry = current_geometry;
        full_refresh = false;
        g_soft3d_status.stage = 2U;
        g_soft3d_status.display_mode = polling ? 2U : 3U;
        g_soft3d_status.render_error = SOFT3D_RENDER_ERROR_NONE;
        ++g_soft3d_status.frames;
        ++rate_frames;
        g_soft3d_status.render_us = cycles_to_us(work_cycles);
        g_soft3d_status.dma_wait_us = cycles_to_us(wait_cycles);
        g_soft3d_status.transmitted_bytes = bytes_sent;
        g_soft3d_status.render_stack_free_words = (uint32_t)uxTaskGetStackHighWaterMark(NULL);
        g_soft3d_status.triangles = s_renderer.stats.prepared_triangles;
        g_soft3d_status.raster_us = cycles_to_us(raster_cycles);
        g_soft3d_status.band_candidates = s_renderer.stats.band_candidates;
        g_soft3d_status.band_potential = s_renderer.stats.band_potential;
        osDelay(1U);
        g_soft3d_status.frame_us = cycles_to_us(DWT->CYCCNT - frame_cycles);
        if (lab_mode) {
            const Soft3D_ProfileSample sample = {
                g_soft3d_status.frame_us, g_soft3d_status.geometry_us,
                g_soft3d_status.raster_us, g_soft3d_status.dma_wait_us,
                s_renderer.stats.band_candidates, s_renderer.stats.band_potential
            };
            Soft3D_ProfilePush(&s_profile, &sample);
            if (!lab_paused) lab_frame = (lab_frame + 1U) % SOFT3D_PROFILE_WINDOW_SIZE;
        }
        now_ms = HAL_GetTick();
        if (now_ms - rate_start_ms >= 500U) {
            g_soft3d_status.fps_tenths = rate_frames * 10000U / (now_ms - rate_start_ms);
            rate_start_ms = now_ms;
            rate_frames = 0U;
        }
    }
}

void Soft3D_AppTelemetry(void)
{
    Soft3D_MotionSnapshot motion;
    char line[960];
    Soft3D_MotionSnapshotRead(&motion);
    int length = snprintf(line, sizeof(line),
        "soft3d frames=%lu fps10=%lu frame_us=%lu render_us=%lu tri=%lu lcd_err=%lu adc=%lu adc_err=%lu geom_err=%lu stage=%lu geom_us=%lu wait_us=%lu tx_bytes=%lu imu=%lu imu_flags=%lu imu_err=%lu imu_reject=%lu heap=%lu render_stack=%lu motion_stack=%lu display=%lu display_err=%lu render_err=%lu game=%lu game_ms=%lu best_ms=%lu wins=%lu maze=%lu seed=%lu lab=%lu workload=%lu index=%lu raster_us=%lu candidates=%lu potential=%lu prof_n=%lu mean_us=%lu p95_us=%lu max_us=%lu\r\n",
        (unsigned long)g_soft3d_status.frames, (unsigned long)g_soft3d_status.fps_tenths,
        (unsigned long)g_soft3d_status.frame_us, (unsigned long)g_soft3d_status.render_us,
        (unsigned long)g_soft3d_status.triangles, (unsigned long)g_soft3d_status.lcd_errors,
        (unsigned long)g_soft3d_status.key_adc, (unsigned long)g_soft3d_status.adc_errors,
        (unsigned long)g_soft3d_status.geometry_errors, (unsigned long)g_soft3d_status.stage,
        (unsigned long)g_soft3d_status.geometry_us, (unsigned long)g_soft3d_status.dma_wait_us,
        (unsigned long)g_soft3d_status.transmitted_bytes, (unsigned long)motion.sensor_status,
        (unsigned long)motion.pose.flags, (unsigned long)motion.io_errors,
        (unsigned long)motion.rejected_samples, (unsigned long)xPortGetFreeHeapSize(),
        (unsigned long)g_soft3d_status.render_stack_free_words,
        (unsigned long)motion.stack_free_words,
        (unsigned long)g_soft3d_status.display_mode, (unsigned long)g_soft3d_status.display_error,
        (unsigned long)g_soft3d_status.render_error,
        (unsigned long)g_soft3d_status.game_state, (unsigned long)g_soft3d_status.game_ms,
        (unsigned long)g_soft3d_status.best_ms, (unsigned long)g_soft3d_status.game_wins,
        (unsigned long)g_soft3d_status.maze_level, (unsigned long)g_soft3d_status.maze_seed,
        (unsigned long)g_soft3d_status.lab_mode, (unsigned long)g_soft3d_status.lab_workload,
        (unsigned long)g_soft3d_status.band_index, (unsigned long)g_soft3d_status.raster_us,
        (unsigned long)g_soft3d_status.band_candidates, (unsigned long)g_soft3d_status.band_potential,
        (unsigned long)g_soft3d_status.profile_samples, (unsigned long)g_soft3d_status.frame_mean_us,
        (unsigned long)g_soft3d_status.frame_p95_us, (unsigned long)g_soft3d_status.frame_max_us);
    if (length > 0 && (unsigned)length < sizeof(line)) {
        /* The CDC wrapper copies into its owned buffer before this function returns. */
        (void)CDC_Transmit_HS((uint8_t *)line, (uint16_t)length);
    }
}

void vApplicationMallocFailedHook(void)
{
    g_soft3d_status.fatal_error = 2U;
    Error_Handler();
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task;
    (void)name;
    g_soft3d_status.fatal_error = 3U;
    Error_Handler();
}
