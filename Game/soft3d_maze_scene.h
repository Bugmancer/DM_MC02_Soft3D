#ifndef SOFT3D_MAZE_SCENE_H
#define SOFT3D_MAZE_SCENE_H

#include "soft3d.h"
#include "soft3d_maze.h"

#define SOFT3D_MAZE_CLEAR_COLOR 0x1082U

Soft3D_Camera Soft3D_MazeSceneCamera(void);
/* Submit after begin_frame using MazeSceneCamera. Physics owns the layout and
 * rolling quaternion; this adapter adds depth, materials and bounded view tilt.
 * At most 24 wall boxes require 444 submitted triangles, with no frame heap.
 */
int Soft3D_MazeSceneSubmit(Soft3D_Context *ctx, const Soft3D_MazeState *maze);

#endif
