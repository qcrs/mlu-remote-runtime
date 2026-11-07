# 当前工程状态

更新：2026-11-07。目标为 MLU370-S4 单设备、Neuware 3.5.2、CNRT 6.5.2、CNDrv 2.5.2、AArch64→AArch64。普通 CNCC/BANG 程序通过原生注册 ABI、image/module 上传和通用 launch 远程运行；客户端替换库不依赖本地 Driver/设备节点。第二物理主机和生产安全体系不是当前核心阶段门槛。

## API 状态

| 层 | IMPLEMENTED | PARTIAL | BLOCKED | OUT_OF_SCOPE | UNSUPPORTED | NOT_IMPLEMENTED | 合计 |
|---|---:|---:|---:|---:|---:|---:|---:|
| CNRT | 119 | 0 | 10 | 0 | 1 | 0 | 130 |
| CNDrv | 164 | 0 | 13 | 3 | 0 | 0 | 180 |
| 合计 | 283 | 0 | 23 | 3 | 1 | 0 | 310 |

已完成 [closure audit](API_COMPATIBILITY_CLOSURE.md)：冻结原 97 个 PARTIAL 并保留逐项初始决策，后续 resolution 将当前范围内可修复项收口。原剩余 49 项本批完成 36 项、确认 12 项 Native 前提阻塞、1 项私有扩展查询范围外；当前没有 PARTIAL 或 NOT_IMPLEMENTED。台账不会因导出存在而自动提升。最新机器可读数据见 [summary](../compat/summary.json)、[ledger](../compat/api-ledger.csv)、[family plan](../compat/family-plan.json)。6 个私有 `__bang*` hooks 独立统计，legacy `cnrtInit` 不在公共分母内。

## 共享基础能力

- TCP session、HELLO、序号/长度/deadline、失败连接失效和不重放；server 独立 native context，Driver current-context 为 TLS。
- AllocationID + offset、guarded fake VA/tombstone、owner/range/free/reset 校验；七种新增 Driver allocation 复用同一路径；两库统一同步 copy 已支持 Host/device 推断，CNDrv unified async 补入 H2D；共享 `fake_va.h` 检查范围/存活/owner。
- native NULL 与显式 Queue、Notifier flags/priority/timing/cross-queue wait；Queue destroy 立即退役公开 ID，native queue/H2D staging 保持至完成后回收，D2D 原生入队。CNRT SyncDevice 同步 Server context，包含已退役队列。
- 两库共享进程级 Host registry，支持跨 Context 分配/查询/free；CNRT 分配及属性查询初始化前、离线可用，CNDrv 分配/free 只需 init、属性仍需当前 Context。异步 D2H 原生入队至 Server pinned buffer，以 TransferID fetch 在成功完成 API 边界写回；Host lease 保护在途目标，Server 独立计量和清理。没有后台写回，pageable 目标须由调用者保持到完成。
- 当前 CNCC CNFBv2/ELF64 LE、共享 module→多个 kernel、DSO reload、pending unload/reset；metadata v2 支持 C/enum/template/zero args，以及自然 LP64 nested struct/array 按值参数内的 pointer 字段。两库共享字段重定位；Driver 从 image 读取编译器 metadata，保留 v1 和 primitive fallback。
- CNRT kernel memory usage 按客户端存活 registration 计量，首 launch 前已计入、reset 保留、最终 unregister 扣除；独立 CNDrv load 不计入。普通未初始化、初始化和 const device globals 的 size 与 kernel 数值已对照。
- Schema 覆盖 310 个声明，生成 10 对对象 RPC、61 个 typed RPC、146 个公共 RPC wrapper 和 4 个本地 error API。生成器支持有界 borrowed request buffer 与 caller-owned response buffer、逐元素网络序 u64 arrays/size_from、连接前本地处理及精确成功形状；复杂 owner/routing/relocation 仍有手写 adapter。
- G1 的 28 个 Graph 入口共享 native DAG adapter、GraphID/NodeID registry、父图校验、clone remap 和有界查询。Graph destroy/reset/EOF 撤销所属 node identities；失败 native graph 清理保留 session/context ownership。G2 双库各 22 个 memset/D2D memcpy/kernel/child/entity 入口已按边界实现，含递归 child ownership、独立 snapshot/update、逐次 invocation allocation/module leases 与 pending destroy。Kernel 参数复用编译器类型与嵌套 pointer relocation，getter 在客户端恢复 node-owned 快照；参数存储计入 staging，公开 module unload 后延迟回收。Host graph memcpy 已实现逐次输入、独立 pinned staging、alias、完成边界批量写回与 Host leases；G3 另以同一 payload/replay 引擎支持逻辑线程 capture、三种 mode、跨 Context Global invalidation、Notifier fork/join 和 ADD/SET frontier；captured H2D 不读取源数据，Host 输出在成功完成边界写回。
- G4 配对 callback channel 在客户端执行函数与 destructor，私有 native DAG 的每个 Host copy 边界维护 callback 可见的当前输入/输出顺序；clone/child/update/entity setter、native user refs 与在途保活、客户端 DOT 输出共用一个生成 RPC。DONE 先释放 Host leases 再 ACK，reset/EOF/断线统一回收，Runtime 重入在 shim 锁前拒绝。普通 Host async 的 deferred publication 语义保持；callback 图的逐节点 publication 是专用路径。
- 两个 Host cast 共用类型/量化/half/bfloat/rounding 引擎；scalar 与 full-shape by-channel、owned 参数和实际 SDK 数值 quirks 已差分，不依赖远端或本地设备。
- Shared Context 统一进程/设备身份与 acquire refs，最后 release/reset drain 并撤销旧资源；TLS current identity、flags 与重新 acquire 已对照 Native。
- VMM 将 generic handle、backing、VA reservation、mapping 和 exported descriptor refs 分开；支持 offset-zero aliases、access descriptors、retain/release/remap 和 Context cleanup。Symbol 使用 module-owned AllocationID/fake VA，六个地址/同步异步拷贝入口复用 copy/completion 引擎，真实 kernel 验证符号更新。
- 普通队列 Host 函数及 callback 复用客户端 executor，先发布之前的普通/Graph D2H，再执行原函数/userData。三类 atomic 入口使用 pinned 64-bit cells、每 Context 独立侧通道与 Native 原子操作；主连接 EOF 可取消未满足的 compare，避免同步和 cleanup 挂起。
- Device IPC 使用跨 session broker、importer-owned identity、backing/源 Context refs 和一次物理计量；源 free/reset/EOF 撤销新 opens，已导入和 Graph refs 保留至最后释放。Host IPC 使用 memfd/MAP_SHARED、同 UID Unix socket 与 SCM_RIGHTS；VMM 导出可 dup/SCM_RIGHTS 的真实客户端 socket FD，最后一个 descriptor close 释放对应 Server lease。
- Reviewed error catalogue 生成四个本地查询，覆盖 139 个枚举值并集及未知值回退，初始化前/离线可用；返回文本有进程级稳定生命周期。
- 会话资源配额、失败回滚/隔离、EOF/关闭清理、server worker drain 及 parser bounds 已有回归证据。

## 当前真实缺口

当前 PARTIAL 为 0；两个 cast 入口已按本机 SDK 支持矩阵收口，见 [Host cast](HOST_CAST.md)。INT4/fix8/quant8 等拒绝组合与 Native 一致。当前 image ABI 的 device pointer graph、packed/opaque aggregate 和未知地址空间不在认证范围，不能从字段重定位推导支持。

23 个 BLOCKED 为 Peer 7（缺第二物理设备）、IPC notifier get/open 4（Native 支持属性 0，合法创建 NotSupported）、CPU mapping/cache 8（两库 map/cached map 返回 100050，cache/unmap 缺有效 mapping 前提）、merge 1（MLU300+ 不支持，实测 100050）、属性设置 2（device/pinned Host/VMM 上未取得成功的 documented mutation）、FilterReshape 1（合法 padded FP16/FP32/INT8 输入在 CNRT topology helper 查询 device -1 时失败）。后两组记录实际错误及解除条件，不能把 probe 退出成功当作 API 成功。见 [Native 阻塞证据](../evidence/remaining-native-20261107/README.md)。

security/framebuffer 专用消费者与通用私有扩展 `cnGetExportFunction` 范围外；后者 ABI 异构且不能直接将 Server 函数指针返回客户端。Server 仅内部使用已验证的 `cnQueueAbort` ABI 处理 EOF，不宣称通用扩展 forwarding。Runtime MallocConstant 仍明确拒绝。CNNL 缺库是分母外的独立阻塞。

Graph 102/102 和普通 memory/Host/VMM IPC 8/8 已按逐 API 声明边界 IMPLEMENTED；Graph/IPC 只剩 I3 的 4 个 notifier 原生前提阻塞。G2 含 1D Host graph memcpy 重放，G4 保证 callback 与每个 Host copy 的依赖顺序。EntityUpdate 后结构角色不唯一时，影响 Host/callback 重放的 entity setter 明确拒绝。[设计](GRAPH_IPC.md) 与 Schema/family plan 记录限制；callback 每 session 串行且须在 deadline 内返回，Runtime/Driver API 重入拒绝。单 copy <=1 MiB，Host endpoints 总和 <=16 MiB，pageable 地址由调用者保活。

I1/I2 已完成，后续按硬件/SDK 解除条件重开 BLOCKED 分支。当前支持边界：Host IPC 的直接 CPU 共享限同客户端主机、UID 与 Unix namespace；不同客户端机器不提供裸 CPU load/store 一致性。原子单元限 aligned pinned Host 64-bit cells，绑定后通过 atomic API 更新，在 read/Queue/Context completion 发布镜像；同指针跨 Context 与跨主机 raw CPU coherence 不在边界内。VMM 非零 backing offset 在本 SDK 为 Native 100050，offset-zero aliases 已通过。第二物理主机、其他 SDK/ABI 和生产认证仍未作为本批验收。

## 验证与证据

最新 [剩余 49 项收口](../evidence/remaining-closure-20261107/README.md)：Shared Context/VMM/Symbol/普通队列与 Graph Host 顺序 39/39；[IPC](../evidence/remaining-ipc-20261107/README.md) 正常跨进程四模式 24/24、生命周期 12/12、双库单份 backing 配额 2/2；受影响 Graph callback 20/20、Queue retirement 12/12、完整核心 76/76、8 个 workload 的 19 组输入/57 次运行全部通过。Server resources、workers、cleanup errors 清零；生成器、API contracts 与 closure audit 另有最终检查记录。

以下为此前批次的历史验证记录。

最新 [G4 callbacks/user objects/DOT](../evidence/graph-callbacks-20261107/README.md) 新增 20 项 API，专项 20/20：两库 Native/replacement/preload/隐藏 SDK 与设备客户端，callback PID/数值顺序、clone/child/update/setter、无 wait 推进、用户引用/唯一析构和 DOT flags/本地路径错误。Remote 另验证重入拒绝、reset/Context close/EOF、pinned Host retirement、paired channel/plan/owner、192-byte staging 配额回滚及提交断线 API error。Capture 97/97、Host 18/18、kernel 18/18、execution 16/16、G1 14/14、功能族 15/15、Host async 15/15、核心 76/76，连同 G4 合计 289/289；Server 资源与 workers 清零，codegen/台账/API contracts/closure audit 通过。源码、二进制与 SDK 头文件哈希在该目录。

最新 [G3 Capture](../evidence/graph-capture-20261107/README.md) 新增 10 项 API，专项 97/97：三种 mode、异线程 End、跨 Context Global invalidation、不可读 Host 源、两次当前输入 kernel 回放、identity/snapshot、Host publication、EntityInvoke Native 特例、Notifier fork/join、ADD/SET frontier、raw dispatch guard、配额与 reset/EOF。两库 Native/replacement/preload/隐藏 SDK 与设备客户端均通过；ContextSync 三种 mode 均保留 ACTIVE，EntityInvoke 正常执行且不加入捕获图。受影响回归 Host 18/18、kernel 18/18、execution 16/16、G1 14/14、功能族 15/15、Host async 15/15、完整核心 76/76 均通过，合计 269/269；codegen、台账、API contracts 与 closure audit 通过，记录与源码/二进制哈希保存在该目录。

此前 最新 [Host graph memcpy](../evidence/graph-host-20261107/README.md) 完成逐次 H2D 输入、private graph/staging、D2H publication、Host alias 与 nested child/entity override，API 计数不变。Host 专项 18/18、含 Host→kernel→Host 与普通异步 copy 混排的 kernel 专项 18/18、既有 G2 16/16、G1 14/14、功能族 15/15、普通 Host async 15/15、核心 76/76 全部通过；codegen、台账、API contracts 与 closure audit 通过。协议覆盖旧 descriptor、owner/type、无 Host override、更新后歧义 setter 拒绝，故障注入覆盖分配/clone/改写/instantiate/invoke/completion 与释放重试。单 copy <=1 MiB、Host endpoint 总和 <=16 MiB；pageable 地址仍由调用者保持有效。

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
