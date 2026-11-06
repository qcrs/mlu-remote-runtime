# Parameterized INT8 cast expansion — 2026-11-12

`cnrtCastDataType` now supports the observed scalar quantization subset FLOAT32↔INT8 with a non-channel parameter: `scale / 2^pos`, nearest signed rounding and INT8 clamp. The existing null-parameter FLOAT32/INT32/UINT8 cases and create/destroy/by-channel lifecycle remain covered.

Native and remote-shim probes agree. Current SDK observations show offset is not used by this conversion path; by-channel parameter use, V2, half/int4/fix/quantized types and full dequantization semantics remain PARTIAL.
