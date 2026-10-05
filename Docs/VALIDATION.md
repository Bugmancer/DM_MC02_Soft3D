# Engine Lab V4 Validation

Validation date: 2026-10-05. V4 adds renderer fast paths, deterministic board
workloads and windowed profiling. The working early LCD initialization,
default polling output and V2 IMU direction correction are retained.
COM4 was not accessed and no firmware was flashed. Physical display behavior,
MCU performance and runtime stack margins remain board checks.

## Firmware and Resources

Keil ARM Compiler 5.06 update 5 (build 528), C99: full rebuild completed with
**0 errors and 0 warnings**. All production modules were rebuilt.

Firmware: `Firmware/DM_MC02_Soft3D_EngineLab_v4.hex`, 243,817 bytes, built
2026-10-05 at 22:17:45 (Asia/Shanghai). It is byte-identical to
`MDK-ARM/DM_MC02_Soft3D/DM_MC02_Soft3D.hex`.
SHA-256: `D03C5837BA9B76EB5AE09DB491960458F065AF8E5503D621738D93C72BF4FDAF`.

| ARM linker item | V4 bytes | Change from V3 |
| --- | ---: | ---: |
| Code | 77,584 | +5,776 |
| Read-only data | 8,860 | +4,820 |
| Read/write data | 468 | +16 |
| Zero-initialized / reserved data | 158,012 | +3,148 |
| DTCM used / available | 85,800 / 131,072 | +2,764 used |
| AXI LCD bands | 17,920 | 0 |
| Other AXI data | 54,760 | +400 |

The renderer context is 67,328 bytes at `0x20000000`, an increase of 2,360
bytes from V3's 64,968. Its bitmap accounts for exactly 2,048 bytes. Maze
state remains 52 bytes at `0x20010700`; the 352-byte profiler starts at
`0x20010738`, and the 17,920-byte band depth buffer at `0x20010898`.
LCD DMA bands remain at `0x24000000`; the boot polling buffer is 4,480 bytes
at `0x240047D4`. Total linked RAM growth is 3,164 bytes including alignment,
maze layout storage, profile state and status fields, not just the bitmap.

Static callgraph analysis reports render/default task depths of 1,472/1,328
bytes plus unresolved paths, and motion depth 344 bytes. These are not
runtime stack bounds. Allocated stacks remain 8,192/2,048/8,192 bytes.
Runtime render/motion high-water values are exported; the default task's
larger telemetry formatting stack also needs a debugger/runtime check.
The map and callgraph are `MDK-ARM/DM_MC02_Soft3D/DM_MC02_Soft3D.map` and
`DM_MC02_Soft3D.htm` in that directory.

V3 remains available as `Firmware/DM_MC02_Soft3D_Maze01_v3.hex`, 213,982 bytes,
SHA-256 `841DAE70E5C4BC8AEB0DA7612AB361CB013223D270113534E12222785BD37FD5`.
Its original validation and linker figures are preserved in
[VALIDATION_V3.md](VALIDATION_V3.md).
V2 remains unchanged at SHA-256
`CA68DEF20B881EA2B9065685223D8495E016F3068B6A37286F82497405F589CF`.

## Native Regression

All 14 suites in `Tools/test.ps1` passed after the final production edits:
renderer, scene, damage, LCD, IMU, motion, maze, maze scene, profile, input/UI,
boot, App polling, App DMA and USB. Native GCC treats warnings as errors.

The renderer suite links an independent frozen V3 implementation. It compares
RGB565 pixels and float inverse-depth values directly against V4 for 192
cube/torus/material Euler cases and 24 quaternion cases, all clipping planes,
near-plane and distant-endpoint extremes, full-frame, 13-row and reverse
single-row traversal. Index coverage includes heights 512 and 513, bitmap
boundaries 31/32, the final triangle index 511, equal-depth submission order,
same-frame on/off switching, frame reset and saturated counters. A synthetic
sparse case visits 32 candidates instead of 1024; that measures traversal
count, not CPU time or FPS.

The profiler checks partial windows at 1, 2 and 63 samples, two complete
64-frame windows, nearest-rank P95, reset/null behavior, zero values and
UINT32_MAX samples without sum overflow. Static state is 352 bytes.
Published completed-window results stay stable while the next window fills.

Both App suites execute the production task loop with the actual renderer,
scene, UI, physics and profiler. New checks cover all four lab workloads,
constant full-frame transfer volume, game freeze/return, IMU-offline lab use,
hold/resume/reset, status visits, index switching and window reset. HOLD-mode
A/B switching preserves the exact pose in all four workloads. Maze and orbit
tests verify the 64-pose cycle matches the 64-frame profile window. Existing
tests retain injected geometry/raster/transfer/init/status failures, DMA
retirement before recovery, polling fallback, four IMU directions, calibration,
pause/recenter/restart, wins and complete telemetry output at UINT32_MAX.

The scene suite renders 54 scene/mode/distance combinations. The retained-screen
suite verifies 149 updates against full redraw, byte for byte. Lab mode
intentionally bypasses partial updates. UI tests check bands against a full
render, guards, scene-row preservation and bounded numeric labels. Boot tests
verify all 30 eight-row polling bands, wire byte order and failure stops
without linking HAL or RTOS. LCD tests cover ownership, EOT, bounded waits,
attachment without reset and polling recovery. These are software boundary
tests, not physical SPI waveform measurements.

Maze generation tests check 128 deterministic layouts, connectivity, passage
clearance, solution/dead-end constraints and invalid-selection atomicity.
The generator has bounded search and a fixed-layout fallback. Tests solve
levels 1, 2, 17 and 9999 through the actual fixed-step physics in 68.81,
54.94, 41.07 and 40.83 simulated seconds respectively. These are automated
trajectories, not human completion times. The first-level scene suite retains
760 extreme position/view cases, minimum visible ball/goal counts of 20/34
pixels, minimum coverage 27,012 pixels and maximum 260 prepared triangles.
These exhaustive scene-framing numbers apply to level 1, not all 9999 levels.

## Paired Host Benchmark

[pipeline-benchmark.json](pipeline-benchmark.json) contains the complete
report from `Tools/benchmark-pipeline.ps1`: source/executable hashes, flags,
host, clock, raw paired measurements and results. Frozen-reference provenance
is in [Tests/reference/README.md](../Tests/reference/README.md).

Environment: Windows 10, Intel family 6 model 154, 16 logical processors;
GCC 15.2.0 `-O2`, QueryPerformanceCounter. Each of 20 workloads uses eight
warmup frames and 128 timed frames, repeated in three alternating pairs
(old/new, new/old, old/new). Eight full-frame RGB565 hashes per workload match
before timing, and all measured runs repeat the oracle check. Source hashes
were unchanged throughout measurement.

| Phase | Range of median paired old/new time ratios |
| --- | --- |
| Geometry | 0.776x to 2.014x |
| Rasterization | 0.841x to 1.373x |
| Combined | 0.837x to 1.374x |

A ratio above 1 means lower V4 CPU time. Nine workloads improved and eleven
regressed in these runs. The fixed textured cube had 27.2% less total time;
the changing wireframe torus had 19.5% more. Maze workloads ranged from
0.928x to 1.037x, mostly near parity, with the largest increase 7.7%.
This is mixed evidence, not an across-the-board speedup. The index reduces
candidate traversal but adds construction/enumeration overhead; outcodes
also classify vertices before triangle rejection. Compiler decisions and
host scheduling affect these measurements. Three pairs do not establish
statistical significance or an MCU performance result.

Host geometry includes begin-frame and submission; raster covers all 15
bands. Pose construction, level generation, hashing, warmup, LCD, SPI, RTOS
and sensor acquisition are excluded. Total is the sum of the two timed
phases using the same poses. See [ENGINE.md](ENGINE.md) for the differences
from board timing and for algorithm costs. The current public header is also
used by the old reference executable, so its context size cannot establish
the historical RAM cost; the ARM linker-map comparison must be used.

The older `Docs/benchmark.json` records an earlier gradient optimization.
It is historical and must not be presented as evidence for V4.

## Visual Verification

`Tools/lab-preview.ps1` generated four views using the actual scene and UI
at deterministic lab frame 16. Each passed nonblank/framing checks; geometry
stays within the viewport under the header and above the footer:

| Workload | Non-background pixels | Bounds (left,top)-(right,bottom) |
| --- | ---: | --- |
| Maze | 27,577 | (15,57)-(260,202) |
| Cube | 6,666 | (87,65)-(196,170) |
| Torus | 4,547 | (101,78)-(183,165) |
| Orbit | 5,083 | (84,78)-(214,156) |

`Docs/engine-lab-preview.png` and the native 280x240
`Docs/engine-lab-torus.png` were visually inspected for complete models and
nonoverlapping labels. Timing fields are startup zero values, explicitly
captioned as host previews, not measured board performance.
`Tools/status-preview.ps1` regenerated the white boot/status/error page with
the `ENGINE LAB V4` tag and new controls; its text and spacing were inspected.

## Board Checks

1. Flash `Firmware/DM_MC02_Soft3D_EngineLab_v4.hex` and reset. Verify the
   bright operation page, RGB bars and `ENGINE LAB V4` footer before entering
   3D. USB is optional.
2. Short center enters the maze; short down enters `LAB MAZE`. This must
   render even while the sensor reports calibrating/offline. Right cycles
   through cube, torus and orbit. Verify all four remain visible.
3. Let each workload reach N64. Record G/R/IO/F/P95 plus telemetry max_us.
   Every animated full window covers the same 64 poses. Left toggles
   INDEX ON/OFF and resets statistics. INDEX OFF changes traversal only;
   caching, outcodes and index construction remain active.
4. Short center holds the current pose. Left must change INDEX and clear
   the window without moving geometry. Wait for N64 on each side and compare.
   Long center restarts the deterministic sequence and measurement window.
5. Hold down for status, then short center to return. Down short returns to
   the game, preserving position/time and preventing catch-up motion. Right
   in the game selects the next deterministic maze and clears that level's
   best time. Verify corrected physical directions and recentering.
6. When serial access is available, check lab/workload/index, candidates,
   potential, prof_n, mean_us/p95_us/max_us, tx_bytes=134400 in lab, sticky
   display errors, free heap and runtime task stack high-water values.

The default transport is polling. A full RGB565 payload at 30 Mbit/s requires
at least 35.84 ms on the wire. IO is blocking transfer elapsed time or exposed
DMA wait time, depending on the build. F excludes input/physics and statistics
maintenance; it is not the complete task-loop execution time. No board FPS,
hard real-time bound, physical failure recovery or tearing-free display claim
is made without hardware measurement.
