#ifndef SOFT3D_MAZE_H
#define SOFT3D_MAZE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SOFT3D_MAZE_HALF_WIDTH 3.2f
#define SOFT3D_MAZE_HALF_HEIGHT 2.4f
#define SOFT3D_MAZE_BALL_RADIUS 0.18f
#define SOFT3D_MAZE_COLUMNS 7U
#define SOFT3D_MAZE_ROWS 5U
#define SOFT3D_MAZE_STEP_MS 10U
#define SOFT3D_MAZE_MAX_UPDATE_MS 100U
#define SOFT3D_MAZE_MAX_SPEED 2.4f
#define SOFT3D_MAZE_MAX_LEVEL 9999U
#define SOFT3D_MAZE_MAX_WALLS 24U

typedef struct {
    float min_x, min_y, max_x, max_y;
} Soft3D_MazeWall;

typedef struct {
    float x, y;
} Soft3D_MazePoint;

typedef struct {
    float x, y, radius;
} Soft3D_MazeGoal;

typedef struct {
    float x, y, vx, vy;
    float tilt_x, tilt_y;
    /* w,x,y,z; model-to-world rolling rotation on the negative-z floor side. */
    float ball_orientation[4];
    uint32_t elapsed_ms;
    uint32_t accumulator_ms;
    bool won, paused;
} Soft3D_MazeState;

void Soft3D_MazeInit(Soft3D_MazeState *state);
void Soft3D_MazeRestart(Soft3D_MazeState *state);
/* Select on the game-owning thread between frames, then restart its state.
 * Init/Restart preserve the selected level. Invalid levels change nothing.
 * Levels 2..9999 use deterministic DFS, with at most 64 candidate attempts.
 * Level 1 and the bounded-generation fallback use the original fixed layout.
 */
bool Soft3D_MazeSelectLevel(uint32_t level);
uint32_t Soft3D_MazeLevelGet(void);
/* Actual accepted DFS initial seed; zero denotes the fixed layout (including
 * fallback). LevelGet still returns the requested level after fallback.
 */
uint32_t Soft3D_MazeSeedGet(void);
/* x points right and y up. Gravity components are fractions of one g.
 * Inactive/invalid input stops motion and discards pending simulation time.
 * Each call advances at most 100 ms; callers must not feed paused time later.
 */
void Soft3D_MazeUpdate(Soft3D_MazeState *state, uint32_t elapsed_ms,
                       float gravity_x, float gravity_y, bool active);
const Soft3D_MazeWall *Soft3D_MazeWalls(size_t *count);
const Soft3D_MazePoint *Soft3D_MazeStartGet(void);
const Soft3D_MazeGoal *Soft3D_MazeGoalGet(void);
/* Normalizes w,x,y,z quaternions and maps relative tilt to LCD game axes.
 * Positive sensor rotation about Y gives -x; about X gives +y, matching the
 * board's landscape display. Invalid inputs return false and zero outputs.
 */
bool Soft3D_MazeGravity(const float reference_wxyz[4], const float current_wxyz[4],
                        float *gravity_x, float *gravity_y);

#endif
