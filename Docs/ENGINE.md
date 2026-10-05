# Engine Lab V4

This revision makes the software renderer measurable and optimizes geometry
selection while retaining the established RGB565 and inverse-depth output.
It runs on the existing STM32H723, LCD and BMI088 without extra hardware.
The maze is one workload alongside the cube, torus and orbit scene.

## Pipeline

```text
Indexed mesh -> Euler/quaternion transform + vertex outcodes
             -> backface/degenerate rejection
             -> trivial frustum rejection or six-plane polygon clipping
             -> projection + prepared gradients + band membership
             -> ordered band candidates -> inverse-Z rasterization
             -> UI + wire byte order -> polling SPI / optional DMA
```

All renderer storage is bounded. There is no heap allocation, recursion,
full-screen firmware framebuffer, or GPU dependency. The application uses
16-row color/depth bands; one frame still has a shared prepared triangle list.

## Band Index

Each 16-row screen bin holds a 512-bit triangle-membership bitmap. The
32 bins occupy exactly `32 * 512 / 8 = 2048` bytes and cover heights up to
512 pixels. Taller render targets keep the original linear traversal.

Preparing a triangle marks all bins intersecting its inclusive vertical
pixel bounds. Rendering an arbitrary row range ORs the intersecting bins,
then visits set bits in ascending triangle index. The existing exact
row-range check still runs on each candidate. This preserves submission
order, including which surface wins equal-depth ties.

For T prepared triangles and B bands, linear traversal performs B*T
candidate visits. For ordinary aligned 16-row bands, indexed traversal
scans B*ceil(T/32) bitmap words plus the selected candidate visits. A range
spanning K bins needs K bitmap reads per word. Building membership costs
one bit insertion per covered bin, and each frame clears 2 KiB of bitmaps.
Worst-case geometry spanning the whole screen benefits little and still
pays this overhead. Candidate reductions are not proportional FPS gains:
pixel shading and SPI transfer may dominate.

`soft3d_set_band_index_enabled()` switches traversal at runtime. Membership
is maintained even while disabled, making a switch within a prepared frame
valid. The board's `INDEX OFF` keeps outcodes, trig caching and index
construction enabled. It isolates candidate traversal, not all V4 changes.
The frozen-reference host benchmark compares the complete V3 and V4 cores.

## Geometry Fast Paths

Each transformed vertex receives a six-bit outcode using the same plane
distance expressions and boundary comparisons as the existing clipper.
For each surviving triangle:

- A nonzero AND of its three codes rejects a triangle outside a common plane.
- A zero OR skips polygon clipping for a wholly inside triangle.
- Other triangles run the original Sutherland-Hodgman clipper.

Shared indexed vertices therefore reuse their classification. The existing
`clipped_triangles` statistic retains its old meaning; `frustum_rejected`
reports the subset removed by the common-plane fast path.

The most recently used Euler rotation and six sine/cosine values are cached.
Bit-identical angles reuse those values across submissions or frames. Position
and scale still apply per vertex. The transform's floating-point operation
order is unchanged; signed zero distinctions are preserved. Quaternion
submissions continue through their existing matrix transform and also produce
outcodes. This is a trig cache, not a transformed-mesh cache.

## On-Board Experiment

Boot remains on the bright operation/status page. Short center enters the
maze; short down opens the lab. Lab rendering works while the IMU is offline
or calibrating. Game position, orientation and elapsed play time are retained;
velocity is cleared and return consumes zero elapsed simulation time.

| Lab control | Action |
| --- | --- |
| Right | Cycle maze, cube, torus and orbit |
| Left | Toggle indexed/linear traversal; reset statistics and animated sequence, preserve held pose |
| Center, short | Hold/resume the pose; reset statistics |
| Center, hold | Reset pose and statistics; resume animation |
| Down, short | Return to the maze |
| Down, hold | Status page; short center resumes the lab |

The workload follows a deterministic 64-frame pose sequence, matching the
statistics window so every complete animated window covers the same poses.
All 15 bands
are sent on every lab frame, for exactly 134,400 RGB565 payload bytes,
regardless of the scene's screen coverage. This holds transfer volume
constant. Workload changes, index switches, pose hold/resume, mode changes,
status visits and failures reset the statistics window.

| Screen / telemetry | Meaning |
| --- | --- |
| `G` / `geom_us` | Begin-frame, pose construction and scene submission |
| `R` / `raster_us` | Sum of the raster calls, excluding UI and byte conversion |
| `IO` / `wait_us` | Polling transfer elapsed time, or exposed DMA wait time |
| `F` / `mean_us` | Mean completed-frame elapsed time |
| `P95` / `p95_us` | Nearest-rank 95th percentile of 64 completed frames |
| `SKIP` | Integer percentage of candidate visits removed, not pixels or CPU time |
| `N` / `prof_n` | Number of samples represented; 64 after a completed window |
| `max_us` | Maximum completed-frame time in the published window |
| `candidates`, `potential` | Candidate visits and corresponding linear traversal visits |

Timing uses `DWT->CYCCNT` at the CPU clock. Intervals include interrupts and
preemption. Frame timing starts before `begin_frame` and ends after the
frame's transfers and `osDelay(1)`. It excludes input handling, game physics
and profile publication. `render_us` also includes overlays, damage-bound
calculation and RGB byte swapping; it is broader than G+R. In DMA mode IO is
only the wait not hidden by drawing, not the full physical wire duration.

The profiler uses 352 bytes of static state, uint64 sums and a 64-entry
frame-time array. The first partial window publishes its mean/max with P95
unavailable. Once complete, that window stays published while the next fills.
Every 64th sample sorts the array in place, then picks zero-based element 60
for P95. Sorting and profile publication are outside frame timing. The screen
shows the previous published results and clamps values to 9999.9 ms; USB
retains full integer microsecond fields. This is windowed profiling, not a
continuous sliding percentile or a hard real-time deadline proof.

## Reproducible Host Comparison

```powershell
.\Tools\test.ps1
.\Tools\benchmark-pipeline.ps1
.\Tools\lab-preview.ps1
```

The benchmark builds the frozen V3 renderer and current renderer with the
same GCC flags and scene harness. Twenty workloads cover cube/torus in
texture/lit/wire modes and four deterministic maze layouts, each with fixed
and changing poses. Eight full-frame RGB565 hashes per workload must match
before timings are accepted. Regression tests additionally compare individual
pixels and float depth values, avoiding reliance on hashes alone.

Default measurements use eight warmup frames, 128 measured frames per
workload and three paired runs in AB/BA/AB order. Geometry and raster phases
share the same poses and frame loop; total is their sum. Host pose construction,
level generation, hashing and warmup are outside timing. Unlike board G,
host geometry timing does not include pose construction. The JSON report
records source/executable hashes, compiler, host, clock, per-run means and
median paired ratios. It does not claim statistical significance from three
pairs. See [VALIDATION.md](VALIDATION.md) for this build's results.

Differential tests include all three material modes, Euler/quaternion
transforms, all six clipping planes, near-plane extremes, full-frame/13-row/
reverse-single-row rendering, heights 512/513, bitmap indices 31/32/511,
equal-depth overlap, same-frame index switching, reset and counter saturation.
The oracle source and provenance are in [Tests/reference](../Tests/reference).
Passing these cases demonstrates equivalence for tested inputs, not a formal
proof over all floating-point inputs or all compilers.

## Embedded Limits

The 30 Mbit/s SPI link needs at least 35.84 ms just to send a full RGB565
frame, before LCD commands, software and scheduling. Host CPU improvements
must not be presented as board FPS improvements. Default output remains
blocking SPI to preserve the working display path; DMA needs separate board
measurement. No tearing-effect synchronization is implemented.

Deterministic generated maze levels provide varied occlusion/triangle stress.
They use iterative DFS, a bounded 64-candidate search and a fixed-layout
fallback. Level and actual seed appear in telemetry; seed zero identifies
the original/fallback layout. These levels are supporting test inputs for
the engine, not evidence of a general-purpose 3D physics implementation.
