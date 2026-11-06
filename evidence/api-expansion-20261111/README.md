# By-channel quantization parameter lifecycle — 2026-11-11

Added host-local `cnrtCreateQuantizedParamByChannel` lifecycle support. The Shim validates dimension/channel inputs, copies `poses`, `scales` and `offsets` for the channel count, and frees them through `cnrtDestroyQuantizedParam`. Native and remote-shim probes pass with a two-channel parameter set.

Actual by-channel quantized cast and full SDK type semantics remain PARTIAL; no remote pointer or buffer is involved.
