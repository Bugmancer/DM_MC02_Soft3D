# Frozen V3 Renderer

`soft3d_linear_reference.c` is an unchanged copy of `Renderer/soft3d.c`
from the delivered Maze 01 V3 build, captured before the V4 band index,
vertex outcodes and Euler trigonometry cache were added.

SHA-256:
`2F6AE96BB1AECAA2D9B1DEC2925D34AF154E306684CF093BD652C056DE85BAF6`.

There is no Git history in this project directory. This source checksum,
the retained `Firmware/DM_MC02_Soft3D_Maze01_v3.hex`, and the V3 validation
record identify the baseline; no commit ID is implied.

The reference includes the current public `soft3d.h`, so it can share mesh,
camera and triangle definitions with the comparison harness. It leaves the
new context fields unused. Consequently, `sizeof(Soft3D_Context)` in the
reference executable is **not** the historical V3 memory footprint. Compare
the ARM linker maps for firmware RAM costs.

`Tools/test.ps1` compiles the reference as a separate object, renaming its
five public entry points. Tests compare RGB565 pixels and float depth values
against the current renderer. `Tools/benchmark-pipeline.ps1` instead builds
two standalone executables with the same harness, models and scene sources.
It checks full-frame hashes before accepting timing comparisons.

This file is only a test oracle. It is not linked into the firmware. Keep it
unchanged when extending the production renderer; updating the baseline
requires an explicit new provenance record and comparison.
