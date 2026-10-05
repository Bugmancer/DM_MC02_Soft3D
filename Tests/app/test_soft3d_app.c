#include "soft3d_maze_scene.h"
#include "soft3d_scene.h"
#include "soft3d_ui.h"

#include <assert.h>
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int observed_scene_submit(Soft3D_Context *, const Soft3D_MazeState *);
static int observed_demo_submit(Soft3D_Context *, const Soft3D_SceneView *);
static int observed_render_band(Soft3D_Context *, uint16_t, uint16_t, uint16_t *, float *);
static void observed_ui_band(uint16_t *, uint16_t, uint16_t, uint16_t, uint16_t,
                             const char *, const char *, uint8_t, uint32_t);
static void observed_profile_band(uint16_t *, uint16_t, uint16_t, uint16_t, uint16_t,
                                   const char *, const char *, const char *, const char *);

/* Execute the production loop. The three wrappers observe real rendering and
 * inject failures at its boundaries without replacing the control flow.
 */
#define Soft3D_MazeSceneSubmit observed_scene_submit
#define Soft3D_SceneSubmit observed_demo_submit
#define soft3d_render_band observed_render_band
#define Soft3D_UI_MazeLevelBand observed_ui_band
#define Soft3D_UI_ProfileBand observed_profile_band
#include "../../App/soft3d_app.c"
#undef Soft3D_MazeSceneSubmit
#undef Soft3D_SceneSubmit
#undef soft3d_render_band
#undef Soft3D_UI_MazeLevelBand
#undef Soft3D_UI_ProfileBand

#define MAX_STEPS 80U
#define WRITE_US 2000U

typedef struct {
    Soft3D_Key keys[8];
    unsigned key_count;
    bool ready;
    float quaternion[4];
    uint32_t gap_ms;
    uint32_t finish_ms;
    uint32_t flags, sensor_status;
    bool fail_prepare, fail_init, fail_status;
    unsigned fail_raster_at, fail_write_at, fail_wait_at, fail_submit_at;
} Step;

typedef struct {
    Soft3D_Status status, before;
    Soft3D_MazeState view, maze;
    bool scene_called;
    unsigned snapshots, rasters, writes, submits, waits, pending_waits, successful_bands;
    uint16_t bands;
    uint8_t won;
    uint32_t delay;
    bool profile_called;
    float animation_seconds;
    char title[24];
    char phase[40], detail[40], footer[64], mode[32];
} Observation;

CoreDebug_Type test_core_debug;
DWT_Type test_dwt;
uint32_t SystemCoreClock = 1000000U;
ADC_HandleTypeDef hadc1;
static Step steps[MAX_STEPS];
static Observation seen[MAX_STEPS];
static unsigned step_index, step_count, key_index, init_calls;
static uint32_t tick_ms;
static bool pending, telemetry;
static uint8_t *pending_pointer;
static uint8_t pending_copy[BAND_PIXELS * 2U];
static uint16_t expected_pixels[BAND_PIXELS];
static uint16_t expected_y;
static Soft3D_LCDError lcd_error;
static char telemetry_line[960];
static int queue_token;
static jmp_buf finished;

static Step *current(void)
{
    assert(step_index < step_count);
    return &steps[step_index];
}

static Observation *observation(void)
{
    assert(step_index < step_count);
    return &seen[step_index];
}

static void copy_text(char *target, size_t length, const char *text)
{
    int result = snprintf(target, length, "%s", text);
    assert(result >= 0 && (size_t)result < length);
}

static void assert_orientation_equal(const float actual[4], const float expected[4])
{
    unsigned i;
    for (i = 0U; i < 4U; ++i) assert(fabsf(actual[i] - expected[i]) < 0.000001f);
}

static void assert_orientation_changed(const float actual[4], const float previous[4])
{
    float difference = 0.0f;
    unsigned i;
    for (i = 0U; i < 4U; ++i) difference += fabsf(actual[i] - previous[i]);
    assert(difference > 0.00001f);
}

static void advance_us(uint32_t us)
{
    test_dwt.CYCCNT += us;
    tick_ms += us / 1000U;
}

static void assert_pending_unchanged(void)
{
    if (pending) assert(memcmp(pending_pointer, pending_copy, sizeof(pending_copy)) == 0);
}

static void check_wire(uint16_t y, uint16_t rows, const uint8_t *bytes)
{
    unsigned i;
    assert(rows == BAND_ROWS && y == expected_y);
    for (i = 0U; i < BAND_PIXELS; ++i) {
        assert(bytes[2U*i] == (uint8_t)(expected_pixels[i] >> 8));
        assert(bytes[2U*i + 1U] == (uint8_t)expected_pixels[i]);
    }
    observation()->bands |= (uint16_t)(1U << (y / BAND_ROWS));
    ++observation()->successful_bands;
}

static int observed_scene_submit(Soft3D_Context *ctx, const Soft3D_MazeState *view)
{
    float norm = 0.0f;
    unsigned i;
    for (i = 0U; i < 4U; ++i) {
        assert(isfinite(view->ball_orientation[i]));
        norm += view->ball_orientation[i] * view->ball_orientation[i];
    }
    assert(fabsf(norm - 1.0f) < 0.00001f);
    observation()->view = *view;
    observation()->scene_called = true;
    advance_us(20U);
    if (current()->fail_prepare) return 0;
    return Soft3D_MazeSceneSubmit(ctx, view);
}

static int observed_demo_submit(Soft3D_Context *ctx, const Soft3D_SceneView *view)
{
    observation()->scene_called = true;
    observation()->animation_seconds = view->animation_seconds;
    advance_us(20U);
    if (current()->fail_prepare) return 0;
    return Soft3D_SceneSubmit(ctx, view);
}

static int observed_render_band(Soft3D_Context *ctx, uint16_t y, uint16_t rows,
                                uint16_t *pixels, float *depth)
{
    assert_pending_unchanged();
    if (pending) assert((uint8_t *)pixels != pending_pointer);
    ++observation()->rasters;
    advance_us(100U);
    if (observation()->rasters == current()->fail_raster_at) return 0;
    return soft3d_render_band(ctx, y, rows, pixels, depth);
}

static void observed_ui_band(uint16_t *pixels, uint16_t width, uint16_t height,
                             uint16_t y, uint16_t rows, const char *mode,
                             const char *metrics, uint8_t won, uint32_t level)
{
    observation()->won = won;
    copy_text(observation()->mode, sizeof(observation()->mode), mode);
    assert(level == g_soft3d_status.maze_level);
    Soft3D_UI_MazeLevelBand(pixels, width, height, y, rows, mode, metrics, won, level);
    memcpy(expected_pixels, pixels, sizeof(expected_pixels));
    expected_y = y;
    advance_us(20U);
}

static void observed_profile_band(uint16_t *pixels, uint16_t width, uint16_t height,
                                   uint16_t y, uint16_t rows, const char *title,
                                   const char *mode, const char *timings, const char *metrics)
{
    observation()->profile_called = true;
    copy_text(observation()->title, sizeof(observation()->title), title);
    copy_text(observation()->mode, sizeof(observation()->mode), mode);
    copy_text(observation()->footer, sizeof(observation()->footer), metrics);
    Soft3D_UI_ProfileBand(pixels, width, height, y, rows, title, mode, timings, metrics);
    memcpy(expected_pixels, pixels, sizeof(expected_pixels));
    expected_y = y;
    advance_us(20U);
}

osMessageQueueId_t osMessageQueueNew(uint32_t count, uint32_t size, const void *attributes)
{
    assert(count == 8U && size == sizeof(Soft3D_Key) && attributes == NULL);
    return &queue_token;
}

osStatus_t osMessageQueueGet(osMessageQueueId_t queue, void *message, uint8_t *priority, uint32_t timeout)
{
    assert(queue == &queue_token && priority == NULL && timeout == 0U);
    if (key_index == current()->key_count) return osErrorResource;
    *(Soft3D_Key *)message = current()->keys[key_index++];
    return osOK;
}

osStatus_t osMessageQueuePut(osMessageQueueId_t queue, const void *message, uint8_t priority, uint32_t timeout)
{
    (void)queue; (void)message; (void)priority; (void)timeout;
    assert(!"InputTask is not part of this renderer-loop harness");
    return osErrorResource;
}

osStatus_t osDelay(uint32_t ticks)
{
    assert(key_index == current()->key_count);
    assert(!pending);
    observation()->status = g_soft3d_status;
    observation()->maze = s_maze;
    observation()->delay = ticks;
    advance_us((ticks + current()->gap_ms) * 1000U);
    ++step_index;
    key_index = 0U;
    if (step_index == step_count) longjmp(finished, 1);
    return osOK;
}

void Soft3D_MotionSnapshotRead(Soft3D_MotionSnapshot *snapshot)
{
    unsigned index = telemetry ? step_count - 1U : step_index;
    const Step *step = &steps[index];
    memset(snapshot, 0, sizeof(*snapshot));
    memcpy(snapshot->pose.quaternion, step->quaternion, sizeof(step->quaternion));
    snapshot->pose.flags = step->flags;
    snapshot->sensor_status = step->sensor_status;
    snapshot->sample_ms = tick_ms;
    snapshot->samples = 10U;
    if (!telemetry && step->finish_ms != 0U && observation()->snapshots == 0U) {
        const Soft3D_MazeGoal *goal = Soft3D_MazeGoalGet();
        s_maze.x = goal->x;
        s_maze.y = goal->y;
        s_maze.vx = s_maze.vy = 0.0f;
        s_maze.elapsed_ms = step->finish_ms;
    }
    if (!telemetry && observation()->snapshots++ == 0U) observation()->before = g_soft3d_status;
}

bool Soft3D_MotionSnapshotReady(const Soft3D_MotionSnapshot *snapshot, uint32_t now_ms)
{
    assert(snapshot != NULL && now_ms == tick_ms);
    return current()->ready;
}

bool Soft3D_LCD_Init(void)
{
    ++init_calls;
    assert(!pending);
    if (current()->fail_init) {
        lcd_error = SOFT3D_LCD_ERROR_INITIALIZATION;
        return false;
    }
    return true;
}

bool Soft3D_BootStatus(const char *phase, const char *detail, const char *footer)
{
    assert(!pending);
    copy_text(observation()->phase, sizeof(observation()->phase), phase);
    copy_text(observation()->detail, sizeof(observation()->detail), detail);
    copy_text(observation()->footer, sizeof(observation()->footer), footer);
    if (current()->fail_status) {
        lcd_error = SOFT3D_LCD_ERROR_POLLING;
        return false;
    }
    return true;
}

bool Soft3D_LCD_WriteBlocking(uint16_t y, uint16_t rows, uint8_t *bytes)
{
    assert(!pending);
    ++observation()->writes;
    advance_us(WRITE_US);
    if (observation()->writes == current()->fail_write_at) {
        lcd_error = SOFT3D_LCD_ERROR_POLLING;
        return false;
    }
    check_wire(y, rows, bytes);
    return true;
}

bool Soft3D_LCD_Submit(uint16_t y, uint16_t rows, uint8_t *bytes)
{
    assert(!pending);
    ++observation()->submits;
    if (observation()->submits == current()->fail_submit_at) {
        lcd_error = SOFT3D_LCD_ERROR_DMA_START;
        return false;
    }
    check_wire(y, rows, bytes);
    pending = true;
    pending_pointer = bytes;
    memcpy(pending_copy, bytes, sizeof(pending_copy));
    return true;
}

bool Soft3D_LCD_Wait(uint32_t timeout)
{
    assert(timeout == LCD_WAIT_MS);
    ++observation()->waits;
    if (pending) {
        assert_pending_unchanged();
        ++observation()->pending_waits;
        advance_us(WRITE_US);
        pending = false;
        if (observation()->pending_waits == current()->fail_wait_at) {
            lcd_error = SOFT3D_LCD_ERROR_DMA_TIMEOUT;
            return false;
        }
    }
    return true;
}

Soft3D_LCDError Soft3D_LCD_LastError(void) { return lcd_error; }
void Soft3D_LCD_Backlight(bool on) { assert(on && !pending); }
uint32_t HAL_GetTick(void) { return tick_ms; }
void Error_Handler(void) { assert(!"Unexpected fatal error"); abort(); }
size_t xPortGetFreeHeapSize(void) { return 8192U; }
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task) { assert(task == NULL); return 1024U; }
HAL_StatusTypeDef HAL_ADCEx_Calibration_Start(ADC_HandleTypeDef *adc, uint32_t mode, uint32_t input)
{ (void)adc; (void)mode; (void)input; return HAL_OK; }
HAL_StatusTypeDef HAL_ADC_Start(ADC_HandleTypeDef *adc) { (void)adc; return HAL_OK; }
HAL_StatusTypeDef HAL_ADC_Stop(ADC_HandleTypeDef *adc) { (void)adc; return HAL_OK; }
HAL_StatusTypeDef HAL_ADC_PollForConversion(ADC_HandleTypeDef *adc, uint32_t timeout)
{ (void)adc; (void)timeout; return HAL_OK; }
uint32_t HAL_ADC_GetValue(ADC_HandleTypeDef *adc) { (void)adc; return 0U; }
uint8_t CDC_Transmit_HS(uint8_t *bytes, uint16_t length)
{
    assert(length > 2U && length < sizeof(telemetry_line));
    assert(bytes[length - 2U] == '\r' && bytes[length - 1U] == '\n');
    memcpy(telemetry_line, bytes, length);
    telemetry_line[length] = '\0';
    return 0U;
}

static void prepare(unsigned count)
{
    unsigned i;
    assert(count > 0U && count <= MAX_STEPS);
    memset(steps, 0, sizeof(steps));
    memset(seen, 0, sizeof(seen));
    memset(&test_core_debug, 0, sizeof(test_core_debug));
    memset(&test_dwt, 0, sizeof(test_dwt));
    memset(telemetry_line, 0, sizeof(telemetry_line));
    g_soft3d_status = (Soft3D_Status){0};
    step_index = key_index = init_calls = 0U;
    step_count = count;
    tick_ms = 1000U;
    pending = telemetry = false;
    lcd_error = SOFT3D_LCD_ERROR_NONE;
    for (i = 0U; i < count; ++i) {
        steps[i].ready = true;
        steps[i].quaternion[0] = 1.0f;
        steps[i].sensor_status = SOFT3D_IMU_READY;
        steps[i].flags = SOFT3D_MOTION_CALIBRATED;
    }
}

static void key(unsigned index, Soft3D_Key value)
{
    assert(index < step_count && steps[index].key_count < 8U);
    steps[index].keys[steps[index].key_count++] = value;
}

static void execute(void)
{
    Soft3D_AppInit();
    if (setjmp(finished) == 0) {
        RenderTask_Entry(NULL);
        assert(!"RenderTask unexpectedly returned");
    }
    assert(step_index == step_count && !pending);
}

static void assert_status_metrics_clear(unsigned index)
{
    const Soft3D_Status *status = &seen[index].status;
    assert(status->fps_tenths == 0U && status->frame_us == 0U && status->render_us == 0U);
    assert(status->geometry_us == 0U && status->dma_wait_us == 0U);
    assert(status->transmitted_bytes == 0U && status->triangles == 0U);
}

static void test_navigation(void)
{
    static const float identity[4] = {1.0f, 0.0f, 0.0f, 0.0f};
    unsigned i;
    prepare(13U);
    for (i = 2U; i < step_count; ++i) {
        steps[i].quaternion[0] = cosf(0.18f);
        steps[i].quaternion[2] = sinf(0.18f);
    }
    key(0U, SOFT3D_KEY_RIGHT); key(0U, SOFT3D_KEY_UP); key(0U, SOFT3D_KEY_DOWN);
    key(0U, SOFT3D_KEY_SELECT_HOLD); key(0U, SOFT3D_KEY_DOWN_HOLD);
    key(1U, SOFT3D_KEY_SELECT);
    key(3U, SOFT3D_KEY_SELECT);
    key(5U, SOFT3D_KEY_SELECT);
    key(7U, SOFT3D_KEY_UP);
    key(9U, SOFT3D_KEY_DOWN_HOLD);
    steps[9].gap_ms = 5000U;
    key(10U, SOFT3D_KEY_SELECT);
    key(11U, SOFT3D_KEY_SELECT_HOLD);
    execute();
    assert(!seen[0].scene_called && strcmp(seen[0].phase, "LCD POLLING OK") == 0);
    assert(seen[1].status.motion_control == 1U && seen[1].status.game_state == SOFT3D_GAME_PLAYING);
    assert(seen[1].maze.elapsed_ms == 0U && strcmp(seen[1].mode, "IMU") == 0);
    assert(seen[1].maze.x == Soft3D_MazeStartGet()->x && seen[1].maze.y == Soft3D_MazeStartGet()->y);
    assert_orientation_equal(seen[1].maze.ball_orientation, identity);
    assert(seen[1].bands == SOFT3D_ALL_BANDS);
    assert(seen[2].maze.x < seen[1].maze.x && seen[2].maze.elapsed_ms > 0U);
    assert_orientation_changed(seen[2].view.ball_orientation, seen[1].view.ball_orientation);
    assert(seen[3].status.game_state == SOFT3D_GAME_PAUSED && strcmp(seen[3].mode, "PAUSED") == 0);
    for (i = 3U; i <= 5U; ++i) {
        assert(seen[i].maze.x == seen[2].maze.x && seen[i].maze.y == seen[2].maze.y);
        assert(seen[i].maze.elapsed_ms == seen[2].maze.elapsed_ms);
        assert(seen[i].maze.vx == 0.0f && seen[i].maze.vy == 0.0f);
        assert_orientation_equal(seen[i].maze.ball_orientation, seen[2].maze.ball_orientation);
    }
    assert(seen[5].status.game_state == SOFT3D_GAME_PLAYING);
    assert(seen[6].maze.x < seen[5].maze.x);
    assert(seen[7].maze.x == seen[6].maze.x && seen[7].maze.elapsed_ms == seen[6].maze.elapsed_ms);
    assert_orientation_equal(seen[7].maze.ball_orientation, seen[6].maze.ball_orientation);
    assert(fabsf(seen[7].maze.tilt_x) < 0.000001f && fabsf(seen[7].maze.tilt_y) < 0.000001f);
    assert(seen[8].maze.x == seen[7].maze.x && seen[8].maze.y == seen[7].maze.y);
    assert_orientation_equal(seen[8].maze.ball_orientation, seen[7].maze.ball_orientation);
    assert(seen[8].maze.elapsed_ms > seen[7].maze.elapsed_ms);
    assert(seen[9].before.frame_us > 0U && !seen[9].scene_called);
    assert_status_metrics_clear(9U);
    assert_orientation_equal(seen[9].maze.ball_orientation, seen[8].maze.ball_orientation);
    assert(seen[10].maze.x == seen[8].maze.x && seen[10].maze.elapsed_ms == seen[8].maze.elapsed_ms);
    assert_orientation_equal(seen[10].maze.ball_orientation, seen[8].maze.ball_orientation);
    assert(seen[10].bands == SOFT3D_ALL_BANDS);
    assert(seen[11].maze.x == seen[1].maze.x && seen[11].maze.y == seen[1].maze.y);
    assert(seen[11].maze.elapsed_ms == 0U && !seen[11].maze.won && !seen[11].maze.paused);
    assert_orientation_equal(seen[11].maze.ball_orientation, seen[1].maze.ball_orientation);
    assert(seen[12].maze.x == seen[11].maze.x && seen[12].maze.y == seen[11].maze.y);
    assert(seen[12].maze.elapsed_ms > 0U && seen[12].status.motion_control == 1U);
    assert(seen[1].status.dma_wait_us == 15U * WRITE_US);
    assert(seen[1].status.transmitted_bytes == 280U * 240U * 2U);
}

static void assert_frozen(unsigned index, unsigned baseline)
{
    assert(seen[index].maze.x == seen[baseline].maze.x);
    assert(seen[index].maze.y == seen[baseline].maze.y);
    assert(seen[index].maze.elapsed_ms == seen[baseline].maze.elapsed_ms);
    assert(seen[index].maze.vx == 0.0f && seen[index].maze.vy == 0.0f);
    assert(seen[index].maze.accumulator_ms == 0U);
    assert_orientation_equal(seen[index].maze.ball_orientation, seen[baseline].maze.ball_orientation);
}

static void test_tilt_directions(void)
{
    static const struct {
        unsigned axis;
        float angle, x_direction, y_direction;
    } cases[] = {
        {1U,  0.18f,  0.0f,  1.0f},
        {1U, -0.18f,  0.0f, -1.0f},
        {2U,  0.18f, -1.0f,  0.0f},
        {2U, -0.18f,  1.0f,  0.0f}
    };
    unsigned c, i;
    for (c = 0U; c < sizeof(cases) / sizeof(cases[0]); ++c) {
        float dx, dy;
        prepare(4U);
        key(0U, SOFT3D_KEY_SELECT);
        for (i = 1U; i < step_count; ++i) {
            steps[i].quaternion[0] = cosf(cases[c].angle);
            steps[i].quaternion[cases[c].axis] = sinf(cases[c].angle);
        }
        key(2U, SOFT3D_KEY_UP);
        execute();
        dx = seen[1].maze.x - seen[0].maze.x;
        dy = seen[1].maze.y - seen[0].maze.y;
        if (cases[c].x_direction == 0.0f) assert(dx == 0.0f);
        else assert(dx * cases[c].x_direction > 0.0f);
        if (cases[c].y_direction == 0.0f) assert(dy == 0.0f);
        else assert(dy * cases[c].y_direction > 0.0f);
        assert_orientation_changed(seen[1].view.ball_orientation, seen[0].view.ball_orientation);
        assert_frozen(2U, 1U);
        assert(seen[2].maze.tilt_x == 0.0f && seen[2].maze.tilt_y == 0.0f);
        assert(seen[3].maze.x == seen[2].maze.x && seen[3].maze.y == seen[2].maze.y);
        assert_orientation_equal(seen[3].maze.ball_orientation, seen[2].maze.ball_orientation);
        assert(seen[3].maze.elapsed_ms > seen[2].maze.elapsed_ms);
    }
}

static void test_motion_freeze(void)
{
    unsigned i;
    prepare(15U);
    key(0U, SOFT3D_KEY_SELECT);
    for (i = 3U; i < step_count; ++i) {
        steps[i].quaternion[0] = cosf(0.18f);
        steps[i].quaternion[2] = sinf(0.18f);
    }
    steps[0].ready = false; steps[0].flags = SOFT3D_MOTION_INITIALIZING;
    steps[1].ready = false; steps[1].flags = 0U; steps[1].gap_ms = 5000U;
    steps[4].ready = false; steps[4].gap_ms = 9000U;
    steps[7].ready = false; steps[7].sensor_status = SOFT3D_IMU_SPI_ERROR; steps[7].gap_ms = 12000U;
    steps[10].ready = false; steps[10].flags = SOFT3D_MOTION_INITIALIZING; steps[10].gap_ms = 5000U;
    memset(steps[12].quaternion, 0, sizeof(steps[12].quaternion));
    steps[12].gap_ms = 8000U;
    steps[14].flags |= SOFT3D_MOTION_ACCEL_REJECTED;
    execute();
    assert(seen[0].status.game_state == SOFT3D_GAME_CALIBRATING && strcmp(seen[0].mode, "CAL") == 0);
    assert_frozen(1U, 0U);
    assert_frozen(2U, 0U);
    assert(seen[2].status.game_state == SOFT3D_GAME_PLAYING);
    assert(seen[3].maze.x < seen[2].maze.x && seen[3].maze.elapsed_ms > 0U);
    assert(seen[4].status.game_state == SOFT3D_GAME_WAIT && strcmp(seen[4].mode, "WAIT") == 0);
    assert_frozen(4U, 3U);
    assert_frozen(5U, 3U);
    assert(seen[6].maze.x < seen[5].maze.x);
    assert(seen[7].status.game_state == SOFT3D_GAME_OFFLINE && strcmp(seen[7].mode, "NO IMU") == 0);
    assert_frozen(7U, 6U);
    assert_frozen(8U, 6U);
    assert(seen[9].maze.x == seen[8].maze.x && seen[9].maze.elapsed_ms > seen[8].maze.elapsed_ms);
    assert(seen[10].status.game_state == SOFT3D_GAME_CALIBRATING);
    assert_frozen(10U, 9U);
    assert_frozen(11U, 9U);
    assert(seen[12].status.game_state == SOFT3D_GAME_WAIT);
    assert_frozen(12U, 11U);
    assert_frozen(13U, 11U);
    assert(seen[14].status.game_state == SOFT3D_GAME_PLAYING && strcmp(seen[14].mode, "GYRO") == 0);
    assert(seen[14].maze.x == seen[13].maze.x && seen[14].maze.elapsed_ms > seen[13].maze.elapsed_ms);
}

static void test_win_and_best(void)
{
    prepare(8U);
    key(0U, SOFT3D_KEY_SELECT);
    steps[1].finish_ms = 1000U;
    key(2U, SOFT3D_KEY_SELECT);
    key(3U, SOFT3D_KEY_DOWN_HOLD);
    key(4U, SOFT3D_KEY_SELECT);
    key(5U, SOFT3D_KEY_SELECT_HOLD);
    steps[6].finish_ms = 500U;
    key(7U, SOFT3D_KEY_SELECT_HOLD);
    execute();
    assert(seen[1].maze.won && seen[1].won == 1U && strcmp(seen[1].mode, "WON") == 0);
    assert(seen[1].status.game_state == SOFT3D_GAME_WON && seen[1].status.game_wins == 1U);
    assert(seen[1].status.best_ms == seen[1].maze.elapsed_ms);
    assert_frozen(2U, 1U);
    assert_frozen(3U, 1U);
    assert_frozen(4U, 1U);
    assert(seen[4].bands == SOFT3D_ALL_BANDS && seen[4].status.game_wins == 1U);
    assert(!seen[5].maze.won && seen[5].maze.elapsed_ms == 0U);
    assert(seen[5].status.best_ms == seen[1].status.best_ms);
    assert(seen[6].maze.won && seen[6].status.game_wins == 2U);
    assert(seen[6].status.best_ms == seen[6].maze.elapsed_ms);
    assert(seen[6].status.best_ms < seen[1].status.best_ms);
    assert(seen[7].status.best_ms == seen[6].status.best_ms && seen[7].status.game_wins == 2U);
    assert(!seen[7].maze.won && seen[7].maze.elapsed_ms == 0U);
    telemetry = true;
    Soft3D_AppTelemetry();
    assert(strstr(telemetry_line, "wins=2") != NULL);
}

static void test_motion_status(void)
{
    prepare(4U);
    steps[0].ready = false;
    steps[1].ready = false; steps[1].flags = SOFT3D_MOTION_INITIALIZING;
    steps[2].ready = false; steps[2].sensor_status = SOFT3D_IMU_SPI_ERROR;
    execute();
    assert(strcmp(seen[0].detail, "IMU WAIT DATA") == 0);
    assert(strcmp(seen[1].detail, "IMU CALIBRATING") == 0);
    assert(strcmp(seen[2].detail, "IMU OFFLINE") == 0);
    assert(strcmp(seen[3].detail, "IMU READY") == 0);
}

static void test_engine_lab(void)
{
    unsigned i;
    prepare(16U);
    key(0U, SOFT3D_KEY_SELECT);
    key(1U, SOFT3D_KEY_DOWN);
    key(2U, SOFT3D_KEY_RIGHT);
    key(3U, SOFT3D_KEY_RIGHT);
    key(4U, SOFT3D_KEY_RIGHT);
    key(5U, SOFT3D_KEY_LEFT);
    key(6U, SOFT3D_KEY_SELECT);
    key(8U, SOFT3D_KEY_SELECT);
    key(9U, SOFT3D_KEY_DOWN);
    key(10U, SOFT3D_KEY_RIGHT);
    key(11U, SOFT3D_KEY_DOWN);
    key(12U, SOFT3D_KEY_SELECT_HOLD);
    key(13U, SOFT3D_KEY_DOWN_HOLD);
    key(14U, SOFT3D_KEY_SELECT);
    key(15U, SOFT3D_KEY_DOWN);
    for (i = 1U; i <= 8U; ++i) {
        steps[i].ready = false;
        steps[i].sensor_status = SOFT3D_IMU_SPI_ERROR;
    }
    execute();
    for (i = 1U; i <= 8U; ++i) {
        assert(seen[i].profile_called && seen[i].status.game_state == SOFT3D_GAME_LAB);
        assert_frozen(i, 0U);
        assert(seen[i].bands == SOFT3D_ALL_BANDS);
        assert(seen[i].status.transmitted_bytes == 280U * 240U * 2U);
    }
    assert(strcmp(seen[1].title, "LAB MAZE") == 0);
    assert(strcmp(seen[2].title, "LAB CUBE") == 0);
    assert(strcmp(seen[3].title, "LAB TORUS") == 0);
    assert(strcmp(seen[4].title, "LAB ORBIT") == 0);
    assert(strcmp(seen[4].mode, "INDEX ON") == 0);
    assert(strcmp(seen[5].mode, "INDEX OFF") == 0);
    assert(seen[5].status.band_candidates == seen[5].status.band_potential);
    assert(seen[5].animation_seconds == 0.0f && seen[5].status.profile_samples == 0U);
    assert(strstr(seen[6].title, "HOLD") != NULL);
    assert(seen[6].animation_seconds == seen[7].animation_seconds);
    assert(seen[7].animation_seconds == seen[8].animation_seconds);
    assert_frozen(9U, 0U);
    assert(!seen[9].profile_called && seen[9].status.game_state == SOFT3D_GAME_PLAYING);
    assert(seen[10].status.maze_level == 2U && seen[10].maze.elapsed_ms == 0U);
    assert(seen[12].animation_seconds == 0.0f && seen[12].status.profile_samples == 0U);
    assert(strcmp(seen[13].detail, "ENGINE LAB READY") == 0);
    assert_status_metrics_clear(13U);
    assert(seen[14].profile_called && seen[14].bands == SOFT3D_ALL_BANDS);
    assert(seen[15].maze.elapsed_ms == 0U && !seen[15].profile_called);
}

static void test_profile_window_and_telemetry(void)
{
    prepare(68U);
    key(0U, SOFT3D_KEY_SELECT);
    key(1U, SOFT3D_KEY_DOWN);
    key(66U, SOFT3D_KEY_LEFT);
    execute();
    assert(seen[1].status.profile_samples == 0U);
    assert(seen[2].status.profile_samples == 1U && seen[2].status.frame_p95_us == 0U);
    assert(seen[65].status.profile_samples == 64U);
    assert(seen[65].status.frame_mean_us == seen[65].status.frame_p95_us);
    assert(seen[65].status.frame_max_us == seen[65].status.frame_mean_us);
    assert(seen[65].status.band_candidates < seen[65].status.band_potential);
    assert(seen[65].status.raster_us == 15U * 100U);
    assert(seen[66].status.profile_samples == 0U && seen[66].status.frame_p95_us == 0U);
    assert(seen[67].status.profile_samples == 1U);
    telemetry = true;
    Soft3D_AppTelemetry();
    assert(strstr(telemetry_line, "lab=1 workload=0 index=0") != NULL);
    assert(strstr(telemetry_line, "raster_us=1500") != NULL);
    assert(strstr(telemetry_line, "p95_us=") != NULL);
    memset((void *)&g_soft3d_status, 0xFF, sizeof(g_soft3d_status));
    Soft3D_AppTelemetry();
    assert(strstr(telemetry_line, "max_us=4294967295\r\n") != NULL);
}

static void assert_lab_pose_equal(unsigned first, unsigned second, unsigned workload)
{
    if (workload == 0U) {
        assert(seen[first].view.tilt_x == seen[second].view.tilt_x);
        assert(seen[first].view.tilt_y == seen[second].view.tilt_y);
        assert_orientation_equal(seen[first].view.ball_orientation, seen[second].view.ball_orientation);
    } else assert(seen[first].animation_seconds == seen[second].animation_seconds);
}

static void test_lab_hold_index_toggle(void)
{
    unsigned workload, i;
    for (workload = 0U; workload <= SOFT3D_SCENE_COUNT; ++workload) {
        prepare(9U);
        key(0U, SOFT3D_KEY_SELECT);
        key(1U, SOFT3D_KEY_DOWN);
        for (i = 0U; i < workload; ++i) key(1U, SOFT3D_KEY_RIGHT);
        key(4U, SOFT3D_KEY_SELECT);
        key(5U, SOFT3D_KEY_LEFT);
        key(6U, SOFT3D_KEY_LEFT);
        key(7U, SOFT3D_KEY_SELECT);
        execute();
        for (i = 4U; i <= 7U; ++i) {
            assert_lab_pose_equal(4U, i, workload);
            assert(seen[i].status.profile_samples == 0U);
            assert(seen[i].bands == SOFT3D_ALL_BANDS);
        }
        assert(strstr(seen[4].title, "HOLD") != NULL);
        assert(strcmp(seen[5].mode, "INDEX OFF") == 0);
        assert(strcmp(seen[6].mode, "INDEX ON") == 0);
        assert(seen[5].status.band_candidates == seen[5].status.band_potential);
        if (workload == 0U) {
            assert(seen[4].view.tilt_x != seen[1].view.tilt_x);
            assert(seen[8].view.tilt_x != seen[7].view.tilt_x);
        } else {
            assert(seen[4].animation_seconds > 0.0f);
            assert(seen[8].animation_seconds > seen[7].animation_seconds);
        }
    }
}

static void test_lab_profile_pose_cycle(void)
{
    static const unsigned workloads[] = {0U, SOFT3D_SCENE_COUNT};
    unsigned test, i;
    for (test = 0U; test < sizeof(workloads) / sizeof(workloads[0]); ++test) {
        unsigned workload = workloads[test];
        unsigned second_cycle = 1U + SOFT3D_PROFILE_WINDOW_SIZE;
        prepare(second_cycle + 2U);
        key(0U, SOFT3D_KEY_SELECT);
        key(1U, SOFT3D_KEY_DOWN);
        for (i = 0U; i < workload; ++i) key(1U, SOFT3D_KEY_RIGHT);
        execute();
        assert_lab_pose_equal(1U, second_cycle, workload);
        assert_lab_pose_equal(2U, second_cycle + 1U, workload);
        assert(seen[second_cycle].status.profile_samples == SOFT3D_PROFILE_WINDOW_SIZE);
        if (workload == 0U) assert(seen[1].view.tilt_x != seen[2].view.tilt_x);
        else assert(seen[1].animation_seconds != seen[2].animation_seconds);
    }
}

static void test_render_failure(bool prepare_failure)
{
    prepare(4U);
    key(0U, SOFT3D_KEY_SELECT);
    steps[0].fail_prepare = prepare_failure;
    if (!prepare_failure) steps[0].fail_raster_at = 2U;
    key(2U, SOFT3D_KEY_SELECT);
    key(3U, SOFT3D_KEY_DOWN_HOLD);
    execute();
    assert(strcmp(seen[0].phase, "RENDER ERROR") == 0);
    assert(strcmp(seen[1].phase, "RENDER ERROR") == 0);
    assert(strcmp(seen[1].detail, prepare_failure ? "GEOMETRY FAILED" : "RASTER FAILED") == 0);
    assert(seen[1].status.render_error == (uint32_t)(prepare_failure ? SOFT3D_RENDER_ERROR_PREPARE : SOFT3D_RENDER_ERROR_RASTER));
    assert(seen[1].status.geometry_errors == 1U && seen[1].status.lcd_errors == 0U);
    assert(seen[2].status.render_error == SOFT3D_RENDER_ERROR_NONE && seen[2].bands == SOFT3D_ALL_BANDS);
    assert(strcmp(seen[3].phase, "LCD POLLING OK") == 0);
    if (!prepare_failure && SOFT3D_DIAGNOSTIC_POLLING == 0U) assert(seen[0].pending_waits == 1U);
    telemetry = true;
    Soft3D_AppTelemetry();
    assert(strstr(telemetry_line, "render_err=0") != NULL);
    assert(strstr(telemetry_line, "geom_err=1") != NULL);
}

static void test_transport_failure(bool submit_failure)
{
    prepare(3U);
    key(0U, SOFT3D_KEY_SELECT);
    if (SOFT3D_DIAGNOSTIC_POLLING != 0U) steps[0].fail_write_at = 2U;
    else if (submit_failure) steps[0].fail_submit_at = 2U;
    else steps[0].fail_wait_at = 2U;
    key(2U, SOFT3D_KEY_SELECT);
    execute();
    assert(seen[0].status.lcd_errors == 1U && seen[0].status.geometry_errors == 0U);
    assert(seen[0].status.render_error == SOFT3D_RENDER_ERROR_NONE);
    assert(strcmp(seen[0].phase, "DISPLAY RECOVERY") == 0);
    assert(strcmp(seen[1].phase, "LCD POLLING OK") == 0);
    assert(seen[1].status.display_error != 0U && seen[1].status.lcd_errors == 1U);
    assert(seen[2].writes == 15U && seen[2].submits == 0U && seen[2].bands == SOFT3D_ALL_BANDS);
    assert(seen[2].status.display_mode == 2U && seen[2].status.dma_wait_us == 15U * WRITE_US);
    assert(init_calls == 2U);
}

static void test_status_and_init_failures(void)
{
    prepare(3U);
    steps[0].fail_status = true;
    execute();
    assert(seen[0].status.lcd_errors == 1U && seen[0].status.stage == 3U);
    assert(strcmp(seen[1].phase, "DISPLAY RECOVERY") == 0);
    assert(strcmp(seen[2].phase, "LCD POLLING OK") == 0);
    assert(init_calls == 2U);
    prepare(2U);
    steps[0].fail_init = true;
    execute();
    assert(seen[0].delay == 500U && seen[0].status.lcd_errors == 1U);
    assert(strcmp(seen[1].phase, "DISPLAY RECOVERY") == 0);
}

int main(void)
{
    test_navigation();
    test_tilt_directions();
    test_motion_freeze();
    test_win_and_best();
    test_motion_status();
    test_engine_lab();
    test_profile_window_and_telemetry();
    test_lab_hold_index_toggle();
    test_lab_profile_pose_cycle();
    test_render_failure(true);
    test_render_failure(false);
    test_transport_failure(false);
    if (SOFT3D_DIAGNOSTIC_POLLING == 0U) test_transport_failure(true);
    test_status_and_init_failures();
    printf("soft3d_app: real %s maze loop controls, four tilt directions, IMU freeze/recovery, wins, render/transport failures and telemetry passed\n",
            SOFT3D_DIAGNOSTIC_POLLING != 0U ? "polling" : "DMA");
    return 0;
}
