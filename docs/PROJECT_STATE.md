# 当前工程状态

更新：2026-11-07。目标为 MLU370-S4 单设备、Neuware 3.5.2、CNRT 6.5.2、CNDrv 2.5.2、AArch64→AArch64。普通 CNCC/BANG 程序通过原生注册 ABI、image/module 上传和通用 launch 远程运行；客户端替换库不依赖本地 Driver/设备节点。第二物理主机和生产安全体系不是当前核心阶段门槛。

## API 状态

| 层 | IMPLEMENTED | PARTIAL | BLOCKED | OUT_OF_SCOPE | UNSUPPORTED | NOT_IMPLEMENTED | 合计 |
|---|---:|---:|---:|---:|---:|---:|---:|
| CNRT | 49 | 8 | 3 | 0 | 3 | 67 | 130 |
| CNDrv | 77 | 6 | 4 | 2 | 1 | 90 | 180 |
| 合计 | 126 | 14 | 7 | 2 | 4 | 157 | 310 |

已完成 [closure audit](API_COMPATIBILITY_CLOSURE.md)：冻结原 97 个 PARTIAL，77 项以已有实现与证据收口、18 项保留、2 项范围外；另纠正 CNRT queue destroy 的虚高状态。每个剩余 PARTIAL 都有具体 gap/fix/acceptance；台账不会因导出存在而自动提升。最新机器可读数据见 [summary](../compat/summary.json)、[ledger](../compat/api-ledger.csv)、[family plan](../compat/family-plan.json)。6 个私有 `__bang*` hooks 独立统计，legacy `cnrtInit` 不在公共分母内。

## 共享基础能力

- TCP session、HELLO、序号/长度/deadline、失败连接失效和不重放；server 独立 native context，Driver current-context 为 TLS。
- AllocationID + offset、guarded fake VA/tombstone、owner/range/free/reset 校验；七种新增 Driver allocation 复用同一路径；两库统一同步 copy 已支持 Host/device 推断，CNDrv unified async 补入 H2D；共享 `fake_va.h` 检查范围/存活/owner。
- native NULL 与显式 Queue、Notifier flags/priority/timing/cross-queue wait；H2D staging 保持至完成，D2D 原生入队。
- 当前 CNCC CNFBv2/ELF64 LE、共享 module→多个 kernel、DSO reload、pending unload/reset；编译器 metadata 支持 C/enum/template/zero args、primitive pointers 和无 pointer 的 nested struct/array 按值参数。
- Schema 覆盖 310 个声明，生成 10 对对象 RPC、43 个 typed RPC 、45 个公共 RPC wrapper 和 4 个本地 error API。生成器管控 wire shape、字段、绑定、导出与 evidence 契约；复杂 owner/routing/relocation 仍有手写 adapter。
- Reviewed error catalogue 生成四个本地查询，覆盖 139 个枚举值并集及未知值回退，初始化前/离线可用；返回文本有进程级稳定生命周期。
- 会话资源配额、失败回滚/隔离、EOF/关闭清理、server worker drain 及 parser bounds 已有回归证据。

## 当前真实缺口

14 个 PARTIAL 的完整清单与升级条件见 [family plan](../compat/family-plan.json)：`cnrtCastDataType`、`cnrtCastDataType_V2`、`cnrtDeviceQueryKernelMemoryUsage`、`cnrtGetSymbolSize`、`cnrtInvokeKernel`、`cnInvokeKernel`、`cnModuleGetKernel`、`cnrtMemcpyAsync`、`cnMemcpyAsync`、`cnrtPointerGetAttributes`、`cnGetMemAttribute`、`cnGetMemAttributes`、`cnDestroyQueue`、`cnrtQueueDestroy`。依次推进立即返回的 Queue destroy、Host registry/attributes 和真异步 D2H/deferred writeback、module/symbol/可信参数 metadata 和嵌套 pointer relocation、Host cast，再收口对应生命周期。

7 个 Peer 入口因只有一个物理设备 BLOCKED。security/framebuffer 专用消费者范围外。HostMalloc/FreeHost、Runtime MallocConstant 和 Driver D2HAsync 仍明确拒绝。CNNL 无对应库，不能把明确拒绝用例称为 CNNL native 成功。Graph/IPC 尚未实现；核心补齐后分别推进 capture/TaskTopo 状态机和跨进程导出/导入引用生命周期。

## 验证与证据

本批 [error/copy 16 项差分与完整核心回归](../evidence/core-copy-20261107/README.md) 通过，包括 Native、replacement、preload、独立 TCP Server/Client 与隐藏 SDK 和 `/dev` 的 namespace；错误查询在初始化前、远端不可达时仍完整一致。L0 build、codegen/ledger/contracts 和 closure resolution 检查通过。此前 [audit 功能族 15 项、查询 11 项](../evidence/closure-20261107/README.md) 保留。

已有 [完整核心回归](../evidence/context-default-20261102-final/README.md)、[Driver](../evidence/driver-shim-20261102/README.md)、[资源生命周期](RESOURCE_LIFECYCLE.md) 和 [连接生命周期](TRANSPORT_LIFECYCLE.md) 记录保留。八个普通 BANG workload 共 19 输入，Native/replacement/preload 的 57 次运行 CPU reference 和 guard 均一致，最新 [迭代 workload 证据](../evidence/workloads-20261107-iterative/README.md) 包含 heat diffusion；GEMM/row_center 最大误差 9.53674316e-7，heat diffusion 5.96046448e-8，其余 0。

本机独立地址空间和 namespace 证明客户端无需本地设备；尚未作为第二物理主机认证。异构 CPU、其他 SDK/image ABI 与生产认证/TLS 不包含在当前完成声明中。
