# Host datatype conversion expansion — 2026-11-09

`cnrtCastDataType` now has a host-local semantic implementation for the validated subset: FLOAT32, INT32 and UINT8, null quantization parameter, identity copy, signed nearest conversion, and UINT8 clamp/truncate. It does not send host buffers or pointer values over RPC.

Native and remote-shim execution of the same probe agree: `-2.2, 0, 1.5, 255.7` converts to INT32 `-2, 0, 2, 256`, and UINT8 `0, 0, 1, 255`. V2, half/int4/fixed/quantized types and rounding modes remain PARTIAL.

L0 build/schema/codegen/contracts and the affected host utility probe were run. No transport, quota, restart, DSO, fuzz or full historical suite was repeated.
