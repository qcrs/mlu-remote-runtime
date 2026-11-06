# BANG symbol size expansion — 2026-11-08

`cnrtGetSymbolSize` now reads the existing `__bangRegisterVar` metadata for uninitialized `__mlu_device__` variables. It returns the registered byte size without guessing from symbol values or sending a host pointer across the wire. Unknown/null symbols are rejected. Native, Remote replacement and LD_PRELOAD use the same CNCC executable and verify `module_state[4]` size `16` while the existing two-kernel module/global workload still passes.

Address lookup, symbol copies, initialized/constant/external globals and pointer relocation remain unsupported; the API stays PARTIAL. The current hook rejects those registration categories explicitly.

L0 build/schema/codegen/contracts and the affected module/metadata family probes were run. No transport, quota, restart, DSO stress, fuzz or full historical suite was repeated.
