# Fusion

Upstream: https://github.com/xioTechnologies/Fusion

Pinned commit: `a8d7224f36a0ec82345ef49a3db50e65f8d3bab8`

Source files are vendored unchanged from the upstream `Fusion/` directory.
`LICENSE.md` is the upstream MIT license. This application builds the AHRS and
bias estimator, and uses the no-magnetometer update path. Yaw is relative and
can drift; this board has no magnetic heading reference.

Build with `FUSION_USE_NORMAL_SQRT`, using the Cortex-M7 hardware floating-point
square root instead of the optional fast inverse-square-root approximation.
The files use C99 and have no heap, OS, peripheral, or callback dependencies.

SHA-256 of the principal upstream files:

| File | SHA-256 |
| --- | --- |
| FusionAhrs.c | 611678dbaa531db3b360cf3f3097a52cd60d7cf2f93be0753748d8674b9cb5fb |
| FusionBias.c | b6409e21bc1a1590e46fd01cd93edae5e0d04494081c4182a48a6116de53e3e0 |
| FusionMath.h | c7f8a14111418ccc2e8c81ab7718baa7c2b3a6f87756954a2922579405d056a0 |

BMI088 register/protocol implementation is separate project code. It was
cross-checked against Bosch's BMI088 datasheet (`BST-BMI088-DS001`) and the
DM-MC02 `CtrBoard-H7_IMU/Device/BMI088` example distributed with this board.
No BMI088 vendor driver code was copied into Fusion.
