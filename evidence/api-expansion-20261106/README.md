# Memory allocation and unified copy expansion — 2026-11-06

MLU370-S4 / Neuware 3.5.2 / AArch64，单设备。本批扩展 CNDrv allocation family：`cnMallocNode`、`cnZmalloc`、`cnZmallocNode`、`cnMallocConstant`、`cnMallocNodeConstant`、`cnMallocSecurity`、`cnMallocFrameBuffer`，复用已有 AllocationID、fake VA、range/owner registry 与 free 生命周期；新增 `cnMemcpy` 的 registry-inferred H2D/D2H/D2D/H2H 适配。`cnrtDeviceQueryKernelMemoryUsage` 也接入 typed RPC，空上下文查询作为 PARTIAL 证据。

Native 与 Remote replacement 使用同一 `native_memory_allocations` 程序，七种分配同时存活，覆盖 zero-output preservation、zero-fill、offset copy、address range、context owner、invalid node、free 后 stale invalidation，以及 unified copy 四种方向。结果见 [Native](memory-native.log) 和 [Remote](memory-replacement.log)；preload 输出见 [Preload](memory-preload.log)。

原生探针确认 node=0、zero-fill、constant、security、framebuffer 均可在当前设备分配；cache/access 属性设置在当前 SDK 返回 invalid/not-supported，因此没有宣称支持。多设备、跨 context、VMM shareable handle、security/codec 专用消费者仍未实现。

验证：build、Schema/codegen、compat/ledger/contracts；Memory family Native/Remote 对照；无关 transport、restart、quota、DSO、fuzz 和 Full Regression 未运行。
