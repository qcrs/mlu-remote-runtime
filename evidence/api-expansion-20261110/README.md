# Quantization parameter lifecycle expansion — 2026-11-10

Added host-local lifecycle support for `cnrtCreateQuantizedParam` and `cnrtDestroyQuantizedParam`. The opaque parameter stores the validated scalar metadata and uses a magic guard for invalid handles. Native and remote shim probes create and destroy one parameter while the previously validated FLOAT32/INT32/UINT8 conversion results remain unchanged.

Parameterized quantized cast, by-channel creation, and additional data types remain PARTIAL until an independent SDK numeric matrix is available.
