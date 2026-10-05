# cute_c2

Upstream: https://github.com/RandyGaul/cute_headers

Pinned commit: `389aa9554f478c49d5db2715548f52b8d5286db7`

Source: https://raw.githubusercontent.com/RandyGaul/cute_headers/389aa9554f478c49d5db2715548f52b8d5286db7/cute_c2.h

Header version: `1.10`

SHA-256: `4cdbba9019c9eb11c097fb64124608e8e16c0fcbcdb3579466af849a2ea14a5b`

The upstream single header is vendored unchanged. The project uses
`c2CircletoAABBManifold` for the maze ball and walls. The implementation is
compiled once in `Game/soft3d_maze.c`. No library heap allocation is used by
this collision query.

`cute_c2_port.h` marks the unmodified vendor header as a system header on GCC;
on ARMCC 5 it locally suppresses warning 111 for upstream `break` statements
after `return`, then restores the caller's diagnostics. Project code retains
strict warning checks.

The upstream header offers zlib or public-domain licensing. This project uses
the zlib option, reproduced in `LICENSE`.
