# MAZE 01 V3 Validation

Validated on 2026-10-05. The user reported that the earlier display and IMU
demo worked. Board feedback on Maze V1 reported that both up/down and left/right
controls were reversed. V3 retains the V2 direction correction and adds a
branching 7 x 5 maze, a more oblique camera, a thick platform, shaded wall
sides, contact shadows and a ball texture driven by rolling orientation.
Physical directions, visual readability and board frame rate still need
board checks for this build.
COM4 was left untouched per the user's instruction.
No firmware was flashed during this validation.

## Firmware

Keil ARM Compiler 5.06 update 5 (build 528): 0 errors, 0 warnings.
Every added module, including Fusion, was compiled with this compiler.

| Item | Bytes |
| --- | --- |
| Code | 71,808 |
| Read-only data | 4,040 |
| Read/write data | 452 |
| Zero-initialized / reserved data | 154,864 |
| DTCM used / available | 83,036 / 131,072 |
| LCD DMA bands in AXI | 17,920 |
| Other AXI data, including boot status buffer | 54,360 |

The renderer context starts at `0x20000000`; the 52-byte maze state starts at
`0x2000FDC8`, and the depth buffer starts at `0x2000FDFC`.
The LCD DMA buffers start at `0x24000000`. The CPU-polled boot
status buffer is 4,480 bytes at `0x240047C4`. Static callgraph analysis reports
1,216 bytes for the render task, 944 for the default/telemetry task and 344
for the motion task, plus paths it cannot resolve. These are not runtime
stack bounds. Allocated stacks are respectively 8,192, 2,048 and 8,192 bytes;
runtime high-water values are exported over USB.

Firmware: `Firmware/DM_MC02_Soft3D_Maze01_v3.hex`, 213,982 bytes.
This is a byte-identical copy of
`MDK-ARM/DM_MC02_Soft3D/DM_MC02_Soft3D.hex`, built at 21:19:28 on
2026-10-05 (Asia/Shanghai).
SHA-256: `841DAE70E5C4BC8AEB0DA7612AB361CB013223D270113534E12222785BD37FD5`.

The previously delivered `Firmware/DM_MC02_Soft3D_Maze01_v2.hex` is retained
unchanged for rollback. Its SHA-256 is
`CA68DEF20B881EA2B9065685223D8495E016F3068B6A37286F82497405F589CF`.

## Game and Display

The display initializes after GPIO, DMA clocks and SPI1, before ADC, SPI2,
external Flash and the RTOS. It paints a white operation/status page with
black text and RGB bars using blocking SPI. Peripheral startup hooks show
the current initialization phase. The render task attaches without resetting
the display, then keeps the status page visible until a short center press.
The maze defaults to IMU control and blocking SPI output.

The first valid orientation becomes neutral; up captures another neutral pose
without moving the ball or clearing its time. Short center pauses/resumes,
long center restarts, and long down returns to status. A short center press
from status resumes the maze with a complete redraw. Other status-page keys
are ignored. Center/down short and long presses are mutually exclusive, and
ADC failure cancels an incomplete gesture until stable release.

Maze physics uses cute_c2 circle/AABB manifolds, a 10 ms fixed step, damping,
bounded gravity and a speed cap. Its 0.024-unit maximum movement per step is
smaller than its 0.18-unit ball radius and 0.18-unit internal wall thickness. Inactive
input clears velocity and pending time. Recovery starts with zero elapsed
time, and an update discards elapsed time above 100 ms. The displayed timer
counts simulated active play time, not a measurement of wall-clock duration
under arbitrary stalls. Completion freezes the time; best scores survive
game restarts within one power-on session.

The 35-cell fixed layout has 18 merged collision boxes, including the four
outer walls. Its start-to-goal route crosses 30 passages and there are three
wrong dead ends. Start and goal positions are shared by rendering and physics.
The ball's rolling quaternion is normalized after integration from each
collision-corrected displacement; it is preserved by pause and recenter and
reset by restart. Render cadence does not drive this orientation.

Renderer failures remain on the status page until a successful frame and
are counted separately from display failures. A successfully recovered
display returns to `LCD POLLING OK` on the next refresh while retaining its
historical error code. Calibrated but stale/invalid IMU data shows
`IMU WAIT DATA`. Polling transfers now contribute to `wait_us`, and the
status page clears frame metrics rather than reporting the prior 3D frame.

The panel's reset/backlight/sleep-out timing and per-byte command CS packets
match the working BusScope driver. SPI1 keeps its IO driven between HAL
transactions. DMA support remains available; failures retain the backlight,
stop the transfer and allow polling recovery. These changes are diagnostic
and recovery measures, not proof of the physical blank-screen cause.

## Host Verification

V3 passed all thirteen suites in `Tools/test.ps1`: renderer,
scene, damage, LCD, IMU, motion, input/UI, maze, maze scene, app polling,
app DMA, boot, and USB, using strict GCC compilation settings. Direction checks
exercise positive and negative sensor pitch/roll through both gravity-to-motion
and the real render-task loop. Recentring the same pose must stop motion without
changing position, elapsed time or rolling orientation.
The scene suite renders 54 combinations. The damage suite compares 149
retained-screen updates with complete redraws, byte for byte.

The LCD suite checks that pre-RTOS initialization and polling make no RTOS
calls, that attachment preserves the display, and that polling can recover
after DMA failures. The boot suite links the actual boot/UI code without
HAL or RTOS, checks all 30 eight-row bands and RGB565 wire byte order, and
injects failure at each band to verify that no later band is sent.

The maze suite checks wall faces, corners, rounded wall ends, high initial
velocities, equivalent frame partitions, inactive/invalid input, timer
saturation, completion, restart and normalized quaternion gravity. A scripted
route is solved from the collision geometry, then completes the level in
68.81 seconds of simulation while checking wall clearance and quaternion
normalization after every physics step; this is not a human play time.
Connectivity checks cover all 35 cells, 34 open connections, three junctions,
three wrong dead ends and the 30-passage solution. Rolling tests cover both
axis signs, stopped wall contact, pause, restart and frame partitioning.

The maze scene suite checks 760 extreme combinations of valid cell centers,
wall faces, wall ends and visual tilt, with all geometry within rows 32..223.
The level submits 372 triangles and prepares at most 260 in these cases,
below the unchanged 512-triangle capacity. The minimum visible ball and goal
counts are 20 red and 34 green pixels. Scene coverage is at least 27,012 pixels.
Complete-frame/band pixel and depth equivalence and UI bounds are checked.
A fixed-position, fixed-camera test changes only the ball quaternion and
checks that the texture changes within the ball's visible pixels.

The two app suites execute the production `RenderTask_Entry` with the real
maze physics, renderer, scene, UI and damage code. Mock hardware/scheduler boundaries
inject prepare, raster, initialization, status-page, transfer and DMA-wait
failures. Checks cover non-identity IMU input driving the ball, default IMU
control, pause/restart/recenter, position/time preservation across status,
calibration and sensor-loss freezes, rolling orientation through pause/recenter,
unit orientation after restart, recovery without catch-up, complete
repaint after resume, completion counted once, best-time retention and update,
error classification and persistence, DMA retirement before status output,
DMA-to-polling fallback, polling wait time and complete USB telemetry lines.
These tests verify control flow and buffer use, not physical SPI waveforms.

`Docs/maze-preview.png`, `Docs/maze-v3-start.png` and `Docs/status-preview.png` are generated by
`Tools/maze-preview.ps1` and `Tools/status-preview.ps1` using the actual game
scene and UI. Start, paused and completed game images were visually checked
for complete framing, visible ball/goal/walls and nonoverlapping text. These
are host renderings, not board photographs. `Docs/maze-preview-v2.png` retains
the previous two-wall level for visual comparison.

The following renderer previews and benchmark records come from the earlier
renderer upgrade; this maze implementation did not repeat the benchmark:

`Tools/preview.ps1` generated and checked 18 nonblank, distinct geometry frames.
`Docs/preview.png` shows the three scenes in three rendering modes, produced
by the same scene and renderer source used in the firmware.

`Docs/benchmark.json` records five alternating baseline/current pairs, each
result the median of three runs of 1,024 frames. CPU, compiler, source hashes,
executable hashes, and raw timings are preserved. The baseline executable was
built from the pre-upgrade renderer during this session. Future comparisons
require providing a baseline executable to `Tools/benchmark.ps1`.

These host timings exclude LCD, SPI, RTOS scheduling and sensor acquisition.
They are not estimates of MCU frame rate. Fixed-pose raster time and animated
complete-frame time use different pose workloads and cannot be subtracted to
derive geometry cost.

## Board Checks

1. Flash `Firmware/DM_MC02_Soft3D_Maze01_v3.hex` and reset. Confirm RGB bars,
   the game controls and the `MAZE 01 V3` footer. USB is not required.
2. Keep the board still for about five seconds until `IMU READY`. Short-press
   center and release. Confirm the maze, ball near the lower left, goal near
   the upper right, `IMU` state and time starting at zero.
3. Gently tilt in both directions. Check the physical axes match the expected
   screen directions and that the ball slows when the board returns to neutral.
   Press up in a comfortable steady pose; position and elapsed time must be
   retained and subsequent motion should use the new neutral pose.
4. Push against the outer and internal walls, including their ends. Confirm
   the ball stays inside the playable area, passes through narrow corridors
   without sticking, and remains visible against far-side walls. Check that
   the ball's stripe rolls during travel and stops against a wall.
5. Pause with a short center press and wait several seconds; ball and timer
   must stop. Resume, then hold down to visit status and center to return.
   Confirm no time or position jump and no stale status-page pixels.
6. Reach the goal. Confirm `LEVEL CLEAR`, frozen time and best time. Hold
   center for about 0.8 seconds; ball/time must restart while best remains.
   Repeat a faster completion and verify the best time updates.
7. When serial access is available, inspect `game`, `game_ms`, `best_ms`,
   `wins`, `display_err`, `render_err`, heap and task stack high-water values.
   Measure sustained FPS and check `CAL`/`WAIT`/`NO IMU` freeze behavior before
   making performance or hardware fault-recovery claims.

Yaw is relative because the BMI088 provides no magnetic heading reference.
Temperature-dependent bias, noise, actual timing, LCD scanout tearing, and
orientation drift need physical measurements.
