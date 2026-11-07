# 当前工程状态

更新：2026-11-07。目标为 MLU370-S4 单设备、Neuware 3.5.2、CNRT 6.5.2、CNDrv 2.5.2、AArch64→AArch64。普通 CNCC/BANG 程序通过原生注册 ABI、image/module 上传和通用 launch 远程运行；客户端替换库不依赖本地 Driver/设备节点。第二物理主机和生产安全体系不是当前核心阶段门槛。

## API 状态

| 层 | IMPLEMENTED | PARTIAL | BLOCKED | OUT_OF_SCOPE | UNSUPPORTED | NOT_IMPLEMENTED | 合计 |
|---|---:|---:|---:|---:|---:|---:|---:|
| CNRT | 95 | 0 | 5 | 0 | 1 | 29 | 130 |
| CNDrv | 122 | 0 | 6 | 2 | 0 | 50 | 180 |
| 合计 | 217 | 0 | 11 | 2 | 1 | 79 | 310 |

已完成 [closure audit](API_COMPATIBILITY_CLOSURE.md)：冻结原 97 个 PARTIAL，77 项以已有实现与证据收口、18 项保留、2 项范围外；另纠正 CNRT queue destroy 的虚高状态。每个剩余 PARTIAL 都有具体 gap/fix/acceptance；台账不会因导出存在而自动提升。最新机器可读数据见 [summary](../compat/summary.json)、[ledger](../compat/api-ledger.csv)、[family plan](../compat/family-plan.json)。6 个私有 `__bang*` hooks 独立统计，legacy `cnrtInit` 不在公共分母内。

## 共享基础能力

- TCP session、HELLO、序号/长度/deadline、失败连接失效和不重放；server 独立 native context，Driver current-context 为 TLS。
- AllocationID + offset、guarded fake VA/tombstone、owner/range/free/reset 校验；七种新增 Driver allocation 复用同一路径；两库统一同步 copy 已支持 Host/device 推断，CNDrv unified async 补入 H2D；共享 `fake_va.h` 检查范围/存活/owner。
- native NULL 与显式 Queue、Notifier flags/priority/timing/cross-queue wait；Queue destroy 立即退役公开 ID，native queue/H2D staging 保持至完成后回收，D2D 原生入队。CNRT SyncDevice 同步 Server context，包含已退役队列。
- 两库共享进程级 Host registry，支持跨 Context 分配/查询/free；CNRT 分配及属性查询初始化前、离线可用，CNDrv 分配/free 只需 init、属性仍需当前 Context。异步 D2H 原生入队至 Server pinned buffer，以 TransferID fetch 在成功完成 API 边界写回；Host lease 保护在途目标，Server 独立计量和清理。没有后台写回，pageable 目标须由调用者保持到完成。
- 当前 CNCC CNFBv2/ELF64 LE、共享 module→多个 kernel、DSO reload、pending unload/reset；metadata v2 支持 C/enum/template/zero args，以及自然 LP64 nested struct/array 按值参数内的 pointer 字段。两库共享字段重定位；Driver 从 image 读取编译器 metadata，保留 v1 和 primitive fallback。
- CNRT kernel memory usage 按客户端存活 registration 计量，首 launch 前已计入、reset 保留、最终 unregister 扣除；独立 CNDrv load 不计入。普通未初始化、初始化和 const device globals 的 size 与 kernel 数值已对照。
- Schema 覆盖 310 个声明，生成 10 对对象 RPC、55 个 typed RPC、116 个公共 RPC wrapper 和 4 个本地 error API。生成器支持有界 borrowed request buffer 与 caller-owned response buffer、逐元素网络序 u64 arrays/size_from、连接前本地处理及精确成功形状；复杂 owner/routing/relocation 仍有手写 adapter。
- G1 的 28 个 Graph 入口共享 native DAG adapter、GraphID/NodeID registry、父图校验、clone remap 和有界查询。Graph destroy/reset/EOF 撤销所属 node identities；失败 native graph 清理保留 session/context ownership。G2 双库各 22 个 memset/D2D memcpy/kernel/child/entity 入口已按边界实现，含递归 child ownership、独立 snapshot/update、逐次 invocation allocation/module leases 与 pending destroy。Kernel 参数复用编译器类型与嵌套 pointer relocation，getter 在客户端恢复 node-owned 快照；参数存储计入 staging，公开 module unload 后延迟回收。Host graph memcpy 已实现逐次输入、独立 pinned staging、alias、完成边界批量写回与 Host leases；capture 仍待实现。
- 两个 Host cast 共用类型/量化/half/bfloat/rounding 引擎；scalar 与 full-shape by-channel、owned 参数和实际 SDK 数值 quirks 已差分，不依赖远端或本地设备。
- Reviewed error catalogue 生成四个本地查询，覆盖 139 个枚举值并集及未知值回退，初始化前/离线可用；返回文本有进程级稳定生命周期。
- 会话资源配额、失败回滚/隔离、EOF/关闭清理、server worker drain 及 parser bounds 已有回归证据。

## 当前真实缺口

当前 PARTIAL 为 0；两个 cast 入口已按本机 SDK 支持矩阵收口，见 [Host cast](HOST_CAST.md)。INT4/fix8/quant8 等拒绝组合与 Native 一致。当前 image ABI 的 device pointer graph、packed/opaque aggregate 和未知地址空间不在认证范围，不能从字段重定位推导支持。

11 个 BLOCKED 为 7 个 Peer（缺第二物理设备）和 4 个 IPC notifier get/open（Native 支持属性 0，合法 interprocess notifier 创建返回 NotSupported）。后者的 native_test 仅证明前提缺失，未运行成功的 get/open。security/framebuffer 专用消费者范围外。Runtime MallocConstant 仍明确拒绝。CNNL 无对应库，不能把明确拒绝用例称为 CNNL native 成功。

Graph 102 项中 G1 的 28 项和 G2 的 44 项已 IMPLEMENTED，合计 72/102；G3～G4 尚余 30 项，普通 memory/VMM IPC 8 项仍 NOT_IMPLEMENTED。G2 的 44/44 是逐 API 声明边界内完成，已含 1D Host graph memcpy 重放；更新后结构角色不唯一时，影响 Host 重放的 entity setter 明确拒绝。[设计](GRAPH_IPC.md) 与 Schema / family plan 记录逐 API 依赖和验收；EntityID 与 allocation/module/Host invocation leases 已落地；CaptureID、broker、VMM refs 尚待实现。Native 图与跨进程 memory/Host/VMM IPC 已有正例，这些实现缺口不能标成硬件 BLOCKED。

下一批 G3 接逻辑客户端线程 capture、操作拦截与依赖前沿，并复用已实现的 Host replay/completion；G4 接客户端 callback/user refs/DOT。I1 需先解决当前同进程多 session Server 无法直接 native IPC open 的 Context/执行模型，以及同 client-host 域 CPU 共享；I2 还需 VMM memory family 前提和真实客户端 OS handle transport。完整剩余工作与升级条件见 [分批计划](../compat/family-plan.md)。

## 验证与证据

最新 [Host graph memcpy](../evidence/graph-host-20261107/README.md) 完成逐次 H2D 输入、private graph/staging、D2H publication、Host alias 与 nested child/entity override，API 计数不变。Host 专项 18/18、含 Host→kernel→Host 与普通异步 copy 混排的 kernel 专项 18/18、既有 G2 16/16、G1 14/14、功能族 15/15、普通 Host async 15/15、核心 76/76 全部通过；codegen、台账、API contracts 与 closure audit 通过。协议覆盖旧 descriptor、owner/type、无 Host override、更新后歧义 setter 拒绝，故障注入覆盖分配/clone/改写/instantiate/invoke/completion 与释放重试。单 copy <=1 MiB、Host endpoint 总和 <=16 MiB；pageable 地址仍由调用者保持有效。

此前 [G2 kernel](../evidence/graph-g2-kernel-20261107/README.md) 新增 14 项 API，专项 18/18、既有 G2 回归 16/16、G1 回归 14/14、功能族 15/15、核心 76/76 通过。两库 Native/replacement/preload/隐藏 SDK 与设备客户端验证 scalar/offset/NULL/nested struct-array pointer、零参数、getter、clone/snapshot/child、entity setter/update 和属性 Native 状态。另验证 allocation/module 提前退役后的 Remote 保活、reset/context close/EOF、Add/Set/EntitySet 失败回滚和持久参数配额；native unload 失败保留 image 额度，同步重试释放。属性默认 get、零值 set/copy 成功，所测非空 AccessPolicyWindow 返回 Native 100006，未声称其成功。

此前 [G2 D2D memcpy](../evidence/graph-g2-memcpy-20261107/README.md) 新增 8 项 API，专项 16/16、G1 回归 14/14、功能族 15/15、核心 76/76 通过。两库验证 1/2/4-byte memset、snapshot/clone、update、child、源图销毁后回放与 pending EntityDestroy。D2D 数值回放检查整段目标及 guard，涵盖 offsets、当前 source bytes、same-allocation endpoints、retired VA getter 和失败双端 lease 回滚。公开 allocation 提前 free 的保留是 Remote 安全策略。

此前 [G1 owned DAG](../evidence/graph-g1-20261107/README.md) 专项 14/14 通过：两库 Native/replacement/preload/隐藏 SDK 与设备的客户端，DAG/clone 编辑查询、reset/EOF 回收、owner/type/parent/数组 wire 检查。正常查询结果一致；本机 `NodeGetDependencies` 返回后继、`NodeGetDependentNodes` 返回前驱，Remote 直接保留 native 行为。stale ID 拒绝单独作为 Remote 安全语义，未称为 Native 差分。

此前 [Graph/IPC 原生契约](../evidence/graph-ipc-contracts-20261107/README.md) 为 Native 13/13 场景、74 个检查：两库数值 DAG、clone/instantiate snapshot、源图销毁后回放、relaxed capture 当前 Host 输入、四种跨进程/跨库 device IPC、CNRT Host CPU 共享和 VMM POSIX FD。重复 export 被当前 SDK 拒绝、同进程跨 Context IPC open 返回 100102、VMM FD 输出写 8 bytes；这些仍是后续实现约束。

G1 当时的完整核心回归 76/76、功能族 15/15、API contracts/codegen/ledger 与 closure audit 均通过，见 [验证记录](../evidence/graph-g1-20261107/verification.log)。此前核心实现提交 `58b7e6c` 的 [Host cast](../evidence/host-cast-20261107/README.md) 有 7,507 个观察在 Native/replacement/preload/隔离客户端逐字节一致，源/目标 guard、64-bit 大数、by-channel ownership 和 UBSan 均通过。

本批 [module accounting / globals / argument relocation](../evidence/module-accounting-20261107/README.md) 验证两库 Native、replacement、preload 和隐藏 SDK/设备的客户端。覆盖两 image 注册计量、DSO 引用/注销、reset/reload、三类 globals、嵌套 pointer/NULL/offset/scalar guards、跨 owner/stale 拒绝、畸形 wire/metadata、v1 兼容和 ASan/UBSan；五个 module/kernel PARTIAL 已按逐项 boundary 收口。

本批 [Host memory / deferred D2H](../evidence/host-async-20261107/README.md) 专项 15/15、功能族 15/15、copy/error 16/16、完整核心回归 76/76 通过，验证两库显式/default Queue、pageable/pinned Host、动态 buffer、Queue/Notifier/Context/cross-queue 完成及 reset/unload/销毁时的写回。Native 数值基线在 SDK 可能取消 pending D2H 的 destroy/reset/unload 前先等完成，Remote 仍以 pending 操作验证 drain/writeback；这部分不是相同调用顺序的差分。失败提交/Notifier/wait 保留 staging ownership，fetch/release 才释放 D2H 配额，EOF 不向客户端目标补写。

本批 [Queue retirement](../evidence/queue-retirement-20261107/README.md) 专项 12/12、完整核心回归 67/67 通过，覆盖 pending kernel/H2D/D2D 的 Notifier wait/query、context/free/module 和 cross-queue 完成，1200 次退役、pending reset/teardown、失败回收重试及隔离客户端；两个 destroy 入口已收口。本机 SDK 的 pending destroy 可出现 Notifier 成功但数据未完成，单独保留原生观测；Native 数值基线先等待完成，Remote 则在仍 pending 时销毁，不能将两种流程称为相同顺序的差分。

本批 [error/copy 16 项差分与完整核心回归](../evidence/core-copy-20261107/README.md) 通过，包括 Native、replacement、preload、独立 TCP Server/Client 与隐藏 SDK 和 `/dev` 的 namespace；错误查询在初始化前、远端不可达时仍完整一致。L0 build、codegen/ledger/contracts 和 closure resolution 检查通过。此前 [audit 功能族 15 项、查询 11 项](../evidence/closure-20261107/README.md) 保留。

已有 [完整核心回归](../evidence/context-default-20261102-final/README.md)、[Driver](../evidence/driver-shim-20261102/README.md)、[资源生命周期](RESOURCE_LIFECYCLE.md) 和 [连接生命周期](TRANSPORT_LIFECYCLE.md) 记录保留。八个普通 BANG workload 共 19 输入，Native/replacement/preload 的 57 次运行 CPU reference 和 guard 均一致，最新 [迭代 workload 证据](../evidence/workloads-20261107-iterative/README.md) 包含 heat diffusion；GEMM/row_center 最大误差 9.53674316e-7，heat diffusion 5.96046448e-8，其余 0。

本机独立地址空间和 namespace 证明客户端无需本地设备；尚未作为第二物理主机认证。异构 CPU、其他 SDK/image ABI 与生产认证/TLS 不包含在当前完成声明中。
