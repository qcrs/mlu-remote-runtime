# V2 datatype conversion expansion — 2026-11-13

`cnrtCastDataType_V2` now supports the host-local FLOAT/INT/UCHAR subset with null quantization and rounding modes round-nearest, toward-zero, down and up. Native and remote-shim probes use the same input and agree.

Half, bfloat, 64-bit, quantized parameter and round-off/round-math modes remain PARTIAL. No host buffer crosses RPC.
