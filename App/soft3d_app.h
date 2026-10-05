#ifndef SOFT3D_APP_H
#define SOFT3D_APP_H

#include <stdint.h>

typedef enum {
    SOFT3D_RENDER_ERROR_NONE = 0,
    SOFT3D_RENDER_ERROR_PREPARE,
    SOFT3D_RENDER_ERROR_RASTER
} Soft3D_RenderError;

typedef enum {
    SOFT3D_GAME_STATUS = 0,
    SOFT3D_GAME_CALIBRATING,
    SOFT3D_GAME_PLAYING,
    SOFT3D_GAME_PAUSED,
    SOFT3D_GAME_WON,
    SOFT3D_GAME_WAIT,
    SOFT3D_GAME_OFFLINE,
    SOFT3D_GAME_LAB
} Soft3D_GameStatus;

typedef struct {
    uint32_t frames;
    uint32_t frame_us;
    uint32_t render_us;
    uint32_t geometry_us;
    uint32_t dma_wait_us;
    uint32_t transmitted_bytes;
    uint32_t render_stack_free_words;
    uint32_t fps_tenths;
    uint32_t triangles;
    uint32_t lcd_errors;
    uint32_t adc_errors;
    uint32_t input_dropped;
    uint32_t geometry_errors;
    uint32_t key_adc;
    uint32_t stage;
    uint32_t fatal_error;
    uint32_t motion_control;
    uint32_t display_mode;
    uint32_t display_error;
    uint32_t render_error;
    uint32_t game_state;
    uint32_t game_ms;
    uint32_t best_ms;
    uint32_t game_wins;
    uint32_t maze_level;
    uint32_t maze_seed;
    uint32_t lab_mode;
    uint32_t lab_workload;
    uint32_t band_index;
    uint32_t raster_us;
    uint32_t band_candidates;
    uint32_t band_potential;
    uint32_t profile_samples;
    uint32_t frame_mean_us;
    uint32_t frame_p95_us;
    uint32_t frame_max_us;
} Soft3D_Status;

/* Inspect this symbol in a debugger even when USB/LCD are unavailable. */
extern volatile Soft3D_Status g_soft3d_status;

void Soft3D_AppInit(void);
void Soft3D_AppTelemetry(void);
void RenderTask_Entry(void *argument);
void InputTask_Entry(void *argument);

#endif
