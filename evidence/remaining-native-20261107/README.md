# 剩余 Native 前提阻塞，2026-11-07

环境：MLU370-S4、Neuware 3.5.2、CNRT 6.5.2、CNDrv 2.5.2、AArch64 LP64。以下探针直接使用 vendor 库，未把 Remote 实现难度当作硬件阻塞。原 49 项中新确认 12 个 BLOCKED、1 个 OUT_OF_SCOPE，其余 36 个有 [Remote 完成证据](../remaining-closure-20261107/README.md)。

## 新增 12 个 BLOCKED

| 项目 | API | 原生依据与解除条件 |
|---|---|---|
| CPU mapping/cache：8 | cnrtMmap、cnrtMmapCached、cnrtMunmap、cnrtMcacheOperation；cnMmap、cnMmapCached、cnMunmap、cnCacheOperation | [mapping.log](mapping.log)、[rtmapping.log](rtmapping.log)：合法 1 MiB device allocation 上两种 map 均返回 100050 NotSupported。cache/unmap 缺有效 Native mapping 前提，未声称单独证明它们不可执行。需要可成功 mapping 的设备/SDK，再做 CPU 读写/cache/unmap 正例及 Remote view/coherence。 |
| Memory merge：1 | cnMemMerge | [mapping.log](mapping.log)：两个合法 allocation，merge=100050；安装的 cn_api.h 明示 MLU300 或更高系列不支持。需支持 merge 的设备/SDK。 |
| 属性设置：2 | cnSetMemAttribute、cnSetMemRangeAttribute | [focused-attributes.log](focused-attributes.log)、[vmm.log](vmm.log)：device、pinned Host、mapped VMM 未取得成功 mutation；documented CACHE_OPS 1/2 的 single=100006、range=100050，ACCESS_FLAGS 0/1 与 CACHE_CONFIG 0..3 为 100102。typed adapter 仅保留所测错误，不能算实现成功。需实际成功的合法 allocation/attribute/value 和可观察结果后完成差分。 |
| Filter reshape：1 | cnrtFilterReshape | [FP16](focused-reshape-64-64-18.log)、[FP32](focused-reshape-128-64-19.log)、[INT8](focused-reshape-64-128-33.log)：合法 padded n/c、3×3、足量 Host buffer，均返回 100006。Native 内部 topology helper 查询 device -1，ClusterCount/McorePerCluster 失败；[disassembly](reshape-disasm.txt) 辅助定位。需修复该 helper 的 CNRT 版本，取得 Native 转换后 bytes 才实现 reshape。 |

`focused-results.json` 的 returncode=0 表示探针按计划运行并记录状态，**不表示 API 返回成功**。reshape SDK 错误和 Native 返回码均保留，未用替代算法伪造该 API 成功。早期 atomic/shared/VMM 探索日志只用于开发；其最终支持状态由 remaining-closure 的正例决定。

探针源码：[probe_remaining.c](../../probes/probe_remaining.c)、[probe_remaining_blockers.c](../../probes/probe_remaining_blockers.c)。focused 探针可在 vendor library path 下执行 `build/probes/probe_remaining_blockers`、`export`、`reshape 64 64 18`、`reshape 128 64 19`、`reshape 64 128 33`。

## cnGetExportFunction：OUT_OF_SCOPE

安装的 cn_api.h 在 Group Impl Private / `@cond impl_private` 声明此接口。它返回不同私有 profiler/debug/transport 等扩展的 native 函数地址，没有统一的远程调用 ABI；当前普通 BANG 目标没有通用私有扩展消费者。不能将 Server function pointer 直接返回客户端。因此标范围外，若将来有具体扩展 workload，应逐扩展定义 ABI、ownership 与适配器后重开。

[focused-export.log](focused-export.log) 使用真实名称 cnQueueAbort，Native lookup 成功，纠正早期无效名称探针不能证明可用性的局限。Server 内部仅调用单独验证的 `CNresult(CNqueue)` ABI 来取消 EOF 下的未满足 atomic compare，未实现公共通用 extension forwarding。[probe-queue-abort.c](probe-queue-abort.c) 与 [queue-abort.log](queue-abort.log) 验证 lookup/abort=0、后续 sync=100060、queue/Host/Context cleanup 成功。

加上原 Peer 7 和 notifier IPC 4，当前共 23 BLOCKED。Peer 需要第二物理设备；notifier IPC 需要 IPCNOTIFIER_SUPPORTED=1 且合法创建成功，基线见 [原生 Graph/IPC](../graph-ipc-contracts-20261107/README.md)。security/framebuffer 已有范围外理由，cnrtMallocConstant 仍是 Shim 明确 UNSUPPORTED，均未混入这 12 项。
