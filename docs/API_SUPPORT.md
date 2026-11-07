# API 与能力支持入口

更新：2026-11-07。当前认证范围为 MLU370-S4 单设备、Neuware 3.5.2、CNRT 6.5.2、CNDrv 2.5.2、AArch64→AArch64。逐 API 状态、boundary 和 Native/Remote 证据见 [compatibility ledger](../compat/api-ledger.csv)；人工审查来源为 [Schema](../schema/mlu_api_schema.json)，统计和头文件哈希见 [summary](../compat/summary.json)。导出存在不等于语义实现。

| 层 | IMPLEMENTED | PARTIAL | BLOCKED | OUT_OF_SCOPE | UNSUPPORTED | NOT_IMPLEMENTED | 合计 |
|---|---:|---:|---:|---:|---:|---:|---:|
| CNRT | 84 | 0 | 5 | 0 | 1 | 40 | 130 |
| CNDrv | 111 | 0 | 6 | 2 | 0 | 61 | 180 |
| 总计 | 195 | 0 | 11 | 2 | 1 | 101 | 310 |

6 个私有 BANG hooks 和 legacy `cnrtInit` 不计入公共分母。IMPLEMENTED 以行内范围为限；NOT_IMPLEMENTED 未提供客户端导出，应用可能在符号解析时失败；UNSUPPORTED 为 Shim 明确拒绝，不等于 Native 硬件不支持。原 97 个 PARTIAL 的审计及后续 resolution 见 [收口记录](API_COMPATIBILITY_CLOSURE.md)。

当前核心支持普通 CNCC/BANG image/module 注册和通用 launch、Driver 独立 Context/TLS、AllocationID+offset、显式/default Queue、Notifier ordering/timing、统一四方向 copy、Host registry 与真异步 D2H deferred writeback。当前编译器 metadata 支持自然 LP64 nested struct/array 的 pointer 字段重定位、普通初始化/const globals 和 module/DSO/reset 生命周期；device pointer graph、packed/opaque aggregate 和未知 image ABI 不在该认证中。两套 Host cast 按本机 SDK 实测支持/拒绝矩阵收口。

最近完整核心回归为 76/76，功能族 15/15，8 个普通 BANG workload 共 19 输入在 Native/replacement/preload 的 57 次运行通过。隔离客户端隐藏本地 SDK 和设备节点；尚未作为第二物理主机认证。实现范围、共享基础能力和证据入口见 [PROJECT_STATE](PROJECT_STATE.md)、[Host completion](../evidence/host-async-20261107/README.md)、[Host cast](HOST_CAST.md)。

Graph 102 项中已实现 50 项：G1 的 28 项和 G2 的 memset/child/entity 22 项。最新 [G2 专项](../evidence/graph-g2-execution-20261107/README.md) 为 16/16，G1 回归 14/14；覆盖独立 snapshot/update、嵌套 child ownership、源图销毁后回放和逐次 invocation allocation leases。G2 仍余 kernel 14 项、memcpy 8 项；G3～G4 30 项与普通 memory/VMM IPC 8 项按 [设计](GRAPH_IPC.md) 和 [family plan](../compat/family-plan.md) 推进。11 个 BLOCKED 仍为 7 个 Peer 缺第二设备、4 个 notifier IPC 缺 Native 支持前提；普通 Host/device IPC 与 VMM 已有 Native 正例，继续列为实现工作。

`cnMallocSecurity`、`cnMallocFrameBuffer` 的专用消费者语义范围外；`cnrtMallocConstant` 明确拒绝。CNNL 库缺失属于独立工程限制，不在此 310 项分母。生产认证/TLS、异构 CPU 和其他 SDK/image ABI 未包含在当前完成声明中。
