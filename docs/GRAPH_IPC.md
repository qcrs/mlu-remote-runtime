# Graph / IPC 原生契约与实现计划

更新：2026-11-07。范围为 MLU370-S4 单设备、Neuware 3.5.2、CNRT 6.5.2、CNDrv 2.5.2、AArch64 LP64。G1 的 28 个及 G2 的 44 个 Graph API 已按声明边界实现，合计 72/102；其余 30 个 Graph API、8 个普通 memory/VMM IPC API 仍为 NOT_IMPLEMENTED，4 个 IPC notifier API 因本机原生前提缺失为 BLOCKED。G2 的 44/44 仍有 Host graph memcpy staging/publication 的功能边界；下文区分已实现能力与后续状态机计划。

分批归属、依赖和验收以 [Schema 的 graph_ipc_plan](../schema/mlu_api_schema.json) 为准，生成到 [family plan](../compat/family-plan.md)。原始运行和复现方式见 [本批证据](../evidence/graph-ipc-contracts-20261107/README.md)。

## 已验证的 Native 契约

两份探针直接链接 vendor CNRT/CNDrv，无 Shim 或 Remote Server；13/13 场景、74 个检查通过。IPC exporter/importer 是独立 exec 进程，并核对 PID 不同。

| 契约 | 观察与限制 |
|---|---|
| DAG 与查询 | 两库均建立 memset → D2D copy → empty 的 3 nodes / 2 edges 图，验证 node/edge/root/type、依赖删除与恢复。GetNodes 容量 5、实际 3 时将尾部两个元素写 NULL。 |
| Clone 与 instantiate | clone 参数与源图独立；分别 instantiate 后再修改源图，不改变既有 entity。销毁源图和 clone 后，四次执行的整段 4096 bytes 依次为 17、51、34、17。 |
| Capture | relaxed 模式状态 NONE → ACTIVE → NONE；捕获 pinned H2D/D2H 时不执行 copy。销毁捕获图后 entity 仍可运行；两次回放分别读取当前 Host source 的 42、43。 |
| Device memory IPC | CNRT→CNRT、CNDrv→CNDrv 及两种跨库组合成功。importer 在 offset 64 写入 85、close 后，exporter 检查中间区域及两端各 64 bytes 的 17。 |
| Host memory IPC | CNRT HostMalloc 的共享句柄可跨进程 map；两端直接 CPU memcpy/memset 观察同一内容。 |
| SDK memory IPC 特例 | 同 allocation 第二次 export 返回 CNDrv 100102 / CNRT 632015；第一次 handle 仍有效。CNDrv 同进程不同 Context 的 open 返回 100102。这与头文件称可重复 export 的说明有出入，保留实际运行边界。 |
| IPC notifier 前提 | IPCNOTIFIER_SUPPORTED（0x10a）为 0；两库合法 interprocess \| disable_timing_all（12）创建返回 100050 / NotSupported。未生成有效 notifier IPC handle，未运行四个 get/open 入口的成功路径。 |
| VMM POSIX FD | VMM/FD 支持属性均为 1，granularity 为 33,554,432 bytes；create/reserve/map/access/export → SCM_RIGHTS → 独立 importer/import/map/access/write → unmap/release 成功。 |

初始探针覆盖数值 DAG、relaxed capture 与正常 close-before-free 的共享路径。G2 后续另补 child、kernel、entity update 与 pending entity destroy 的两库 Native 数值基线。Host callback nodes、其他 capture 模式/线程交互、user objects、重复 importer 和源进程提前退出尚无完整认证。

## G1 已实现：owned DAG 与节点身份

双库各 14 个入口统一生成到 `graph_action`、`graph_edit`、`graph_query` 三个 RPC（opcode 80～82）。共享 CNDrv adapter 直接维护 native DAG；查询结果映射为 GraphID/NodeID，拓扑边与查询方向由本机 SDK 决定。`scripts/array_codegen.py` 生成有界 trailing u64 arrays、逐元素网络序编码、精确长度检查和 caller-owned 输出；客户端 ABI 句柄仅保存 opaque ID。

GraphID/NodeID 复用 session owner/generation/type registry，NodeID 带 parent GraphID。输入依赖逐个检查父图；clone 预留并登记全部新 NodeIDs，重复查询和 FindInClone 返回稳定身份。DestroyNode 先由 native 删除节点/边再撤销 ID；graph destroy/reset/EOF 撤销全部 child IDs。native graph 清理失败保留 session/context ownership 并 quarantine，不能假定 context destroy 自动释放这些 host-side 对象。

每 session 最多 1024 个 graph、所有 graph 合计 4096 个 node，并受共享 8192-object registry 限制。单 RPC 最多 65536 个 u64 identities；node 查询 capacity 上限 65536，edge 查询两组数组合计上限相同，因此 capacity 上限 32768。count-only、截断、零容量和 NULL tail 保留 native 实测语义；零条 Add/RemoveDependencies 成功。

本机两库 `NodeGetDependencies` 返回 outgoing 后继，`NodeGetDependentNodes` 返回 incoming 前驱；Remote 直接调用 native 后映射身份，不按名字反转。stale/destroyed handle 的 native 行为不能作为安全契约，Remote 的 stale/reset 拒绝单列为安全校验。G1 初始边界为 empty nodes；G2 增加的 memset、child 和 entity 见下一节。

[G1 证据](../evidence/graph-g1-20261107/README.md) 包含独立 Server/Client、两库 Native/replacement/preload/隔离客户端、owner/type/parent/clone/数组边界、reset/EOF 和 clean drain，专项 14/14。

## G2：memset / D2D memcpy / kernel / child / entity（边界内 44/44）

双库各 15 个入口由 Schema 生成 wrapper，共享 `graph_memset`、`graph_entity`、`graph_child`、`graph_memcpy` RPC（83～86）。memset 使用 AllocationID+offset 与有界依赖数组；按本 SDK 仅认证 height=1、elementSize=1/2/4。GetParams 反向恢复客户端 fake VA，已退役地址使用仍保留的 tombstone。诊断 buffer 上限 65536 bytes；当前 instantiate 失败节点映射覆盖顶层，嵌套失败诊断尚未认证。

D2D memcpy 首批支持 1..1048576 bytes、extent height=depth=1、两端 pitch=width/ysize=1。`src`/`dst` 各自编码为 owned live AllocationID+offset，分别校验 range，Host/peer/capture 不在本批边界。SDK 忽略的 position、ptr aliases、xsize 规范化；`dir` 保留 Native 判定（本机 0/4/5 成功，1/2/3/6 返回无效参数），不能用 CNRT 枚举名称推断该图入口的实际行为。GetParams 恢复两端客户端 VA，公开 free 后仍能使用 graph payload 持有的 tombstone。每个 payload 各持有源、目标一个 lease；即使同一 allocation 占据两端也分别增减，clone/child/entity/invocation 复用相同引用机制。

Native AddChildTopoNode 复制源图；embedded 图随 child node 所有，GetTopo 返回独立 GraphID/NodeID 树，修改它会影响该 child。clone 递归通过 Native NodeFindInClone 建立新身份，保留 immutable payload；源图销毁不影响 child 或 entity。所有 embedded graph 计入原有 1024 graph / 4096 node 配额，递归深度小于 64。Remote 拒绝直接 destroy node-owned borrowed 图；删除 child node 或销毁父图递归撤销 IDs，由 native 所有者统一释放。

Instantiate 先 native clone 独立快照，再建立 EntityID；每 session 所有原始 snapshot 合计最多 4096 nodes（含嵌套 child）。EntityUpdate 先分配 replacement refs，native 成功才切换，失败保留旧 revision 和 update result/error node。setter 保持最初 instantiate 图的顶层 node 对应关系；不会把新图 GetNodes 的枚举位置当作 native update 配对证据。整个当前 revision 的 allocation refs 与各 setter override 分开保留；被 setter 替换的 base payload 可能保守保留到下一次完整 update 或 entity destroy。

每次异步 EntityInvoke 在入队前分配 invocation record，持有该次 base/override payload leases，再绑定 Queue Transfer completion。成功 EntityDestroy 撤销公开 EntityID，SDK 负责 pending native entity 的异步释放，invocation 独立持有 backing 直到完成。公开 free 只撤销 allocation ID；有 graph/entity/invocation refs 时推迟 cnFree 和 quota release。提前 free 的支持是 Remote 安全回收策略，未称为 Native 允许该调用顺序。并发操作仍受当前 session/Queue 顺序约束，未额外认证同 entity 跨 Queue 同时运行。

Reset/EOF 先尝试同步，确认完成后释放 invocation refs，再销毁 entity/root graph；embedded graph 由 native node 所有。同步失败则保留在途 refs 至确认 Context 销毁。失败的 clone/child adoption 或 native rollback 留下可清理 ownership，native graph 释放失败保留 session/context 并 quarantine，重试成功后归还配额。已用真实 CNDrv 加失败注入验证两类 rollback 和清理重试，正常路径无伪造 native 成功。

[G2 证据](../evidence/graph-g2-memcpy-20261107/README.md) 包含两库 Native/replacement/preload/隔离客户端、snapshot/clone/update/child 数值、源图销毁后回放、pending destroy、Remote allocation retirement、nested reset/EOF、协议 ownership 与资源归零。G1 查询/编辑回归 14/14。

### Kernel 参数、属性与 module leases

续批双库各 7 个 kernel Add/Get/Set/EntitySet 与属性 Get/Set/Copy wrapper 由 Schema 生成，共享 `graph_kernel` / `graph_kernel_attr` RPC（87～88）及 `shim/graph_kernel.inc`。仅认证 `kernelParams`、`reserve=0`、`extra=NULL`。参数类型来自已有 CNCC image metadata / primitive fallback，复用 `bang_pack_args`；支持 scalar、device pointer、自然 LP64 nested struct/array 内的 pointer fields 和零参数，不按 scalar 位值推断指针。

`include/graph_kernel_wire.h` 定义 canonical 参数记录：每参数 32-byte 网络序 header（kind/size/AllocationID/offset/relocation count/reserved）、scalar bytes 和每字段 24-byte relocation。pointer 字节归零，Server 验证 NULL、owner/range、alignment、order、overlap 与精确长度后恢复 native 地址。单请求含依赖数组最多 2 MiB、256 args、512 nested relocations，每参数最多 4096 bytes；当前 metadata/image ABI 的 device pointer graph、packed/opaque layout 继续不在认证范围。

Server 使用 immutable kernel record 持有 canonical wire、16-byte 对齐参数存储和 native `kernelParams` 指针数组。record 持有 module 及每个重定位 allocation 的 lease；clone、embedded child、entity revision、setter override 和 invocation 共享引用，属性副本另持有 AccessPolicyWindow allocation。持久 record/wire/参数存储计入 STAGING byte 配额，native Add/Set/EntitySet 前准入；失败释放新 record，保留旧参数和配额。成功替换可回收已公开 free 的旧 allocation。

GetParams 在客户端按 NodeID 保存恢复后的参数数组与 aggregate 存储，恢复原始 Runtime func / Driver kernel identity、offset/NULL 和退役 allocation tombstone。祖先 NodeID/GraphID 链用于父图/child 销毁时回收 cache；reset/context close/析构清理全部 cache 和 identity map。Get 不向调用者返回 RPC 临时 buffer 或 Server 地址。

公开 Driver module unload 或 Runtime registration 注销使公开 module/kernel 身份失效，graph/entity/invocation 引用则延后 native unload 和 image 额度释放。新构图/参数更新仍要求公开存活身份；已有 getter 和回放使用已保留记录。最后引用释放若 native unload 失败，保留 native module/image 及 IMAGE 额度，成功 Context sync 会重试回收。这是 Remote 安全回收策略；Native 数值对照在合法存活资源下运行，提前 unload/free 单列 Remote 用例。

属性仅认证 SDK 的 AccessPolicyWindow（ID 1），baseAddr 以 owned allocation+offset 或 NULL 编码，剩余值交给 native 校验；CopyAttributes 限同图 kernel nodes。当前 Native 默认 Get、零值 Set 和 Copy 成功；所测 64-byte、hitRatio=0.5 的非空 window 返回 100006，Remote 状态一致，没有认证非空 window 成功。

[Kernel 证据](../evidence/graph-g2-kernel-20261107/README.md) 专项 18/18：两库 Native/replacement/preload/隔离客户端，scalar 与 nested pointer 数值、guard、getter 快照、clone/child/entity setter/update、零参数、属性，以及 Remote retirement、reset/context close/EOF、失败回滚、配额准入和资源归零。既有 G2 16/16、G1 14/14、功能族 15/15、核心回归 76/76 通过。

## G2 剩余与后续 Graph 对象模型

G2 的 44 项入口已按上述边界实现。8 个 memcpy 入口的 owned 1D D2D 边界外，Host graph copy staging/publication 尚未认证。Host memcpy 需要每次回放读取当前客户端输入，并为每个 D2H 单独保留 staging、目标 Host lease 与 publication；不能固化构图时的 Host bytes。Native update 后保留最初 setter 身份，Host payload 路由也不能未经证据按 GetNodes 位置配对。

GraphID/NodeID/EntityID 与 module/allocation leases 已落地；G3 需新增 session generation / Queue / 逻辑客户端线程 / mode 范围的 CaptureID，G4 再接 callback 与 user-object refs。Invocation record 后续扩展 Host staging 与 D2H publication，各资源保留到对应执行完成。图/公开 ID 销毁与 backing 最终释放分开处理；失败清理保持可重试。

## Capture 与每次回放的 Host 数据

Server worker TLS 不能代表应用线程。一个 RPC session 可以承载多个客户端线程，Driver shim 又可能因当前 Context 选择不同 session；capture 请求须带逻辑线程 identity，按 session generation、Queue、mode 和 capture ID 校验。正常 capture、invalidated、end 和跨 Queue frontier 使用显式状态，不根据当前 RPC 所在线程推断。

捕获期间的 copy/kernel/依赖操作构建 DAG，不提交普通异步工作。end 返回完整图后，instantiate 才建立可执行资源；失败捕获需要统一回滚。已有 Host async 的 submit/completion/fetch 可作为执行基础，但不能直接把捕获操作当成普通 copy 提交。

Native 回放读取当前 Host source，因而 H2D 不能在 capture 时固定成 bytes 快照。计划在每次 invoke 读取该次输入并上传到持久 pinned staging；staging 与对应 entity revision / invocation 保持关联。在途回放完成前，不能用下一次 invoke 的输入覆盖其 staging。D2H 同样需要每次 invocation 的 TransferID、输出目标、Host lease 和 publication record；仅在成功完成边界向客户端写回，end capture 或 instantiate 不得提前 fetch。

module/allocation 的内部 leases 不赋予调用者任意提前释放 Host 地址的权利。pageable Host 内存仍需由调用者保持有效；注册/分配 Host buffer 可复用现有 registry lease。队列、Notifier、Context 完成操作需要汇集 Graph invocation 的输出，EOF 时清理 Server 资源而不再写客户端地址。复用当前 deferred writeback 时需验证与普通异步 copy 混排的顺序。

## Callback、user object 与文件路径

Host node callback、userdata、user-object destructor 是客户端进程中的代码/地址。不得把函数指针发给 Server 后在 Server 调用。G4 需要客户端 callback executor、回调 identity、顺序通知与完成 ACK；Server DAG 在相应位置等待所需的客户端完成。成功/失败/连接断开路径的引用释放和重入限制须以专门 Native 基线确定。Callback 不能只在整个图结束后集中执行，因为后续 device nodes 可能依赖其结果。

User-object refs 应分别记录调用者、graph、entity revision 与在途 invocation 的持有，满足最终 release 时 destructor 恰好一次；不能仅数公开 GraphID。DOT 输出同样属于客户端：Server 返回有界内容，由客户端写入调用者的路径。远端同名路径不等价。

## 计划中的 memory IPC broker

Native 的独立进程共享成功，同一进程跨 Context 的 IPC open 失败。当前 Server 是一个进程中的多个 session workers，直接对 native handle 调 open 无法满足两个独立客户端的含义。I1 必须引入进程级 backing broker，并解决实际 native Context 中的访问/执行路由。

计划将以下身份分开：

- IpcExportToken：server instance 内跨 session 可解析的 opaque token，绑定 backing 与 kind；ABI 为 8-byte opaque memory handle，不泄露 native 地址，也不能与 64-byte notifier handle 混用。
- Backing record：实际 native storage、拥有它的 Context/执行器、allocation size/kind、export/open/operation refs，以及计量 owner。
- ImportedAllocationID：importer session 内独立 identity 与 fake VA，借用 backing lease；其 offset/range 检查复用现有共享内存路径。

普通未导出的 allocation 继续受原 session owner 限制；只有合法 broker token 才能建立 imported mapping。close 撤销该 importer 的公开 mapping，完成它自己的在途操作后减 ref，不可对 exporter storage 调普通 free，也不能影响其他 importer。

source free/reset/EOF 后，计划撤销新 opens，同时由既有 importer 和在途操作 refs 延后 backing 及源 Context 的释放，最终一个 ref 释放后回收。Native 文档对 import 未关闭就 free 源内存的行为未定义；这里的保留是 Remote 内部安全回收策略，不能宣称已证明 Native 在源退出后仍可继续使用。正常兼容性验收仍使用 close-before-free，提前断连只验证资源安全与回收。

Broker refs 本身不能让 native 地址跨 Context 合法。还需选择并验证执行方案：使用能在 importer Context 内建立的共享 backing 映射，或把涉及共享 allocation 的操作路由到兼容的 native 执行 Context；另一方案是隔离 Server worker 进程以使用原生 IPC。Kernel 同时引用本地与导入 allocation、跨 Queue/Context ordering、source worker 消失后的执行器存活均必须能解释，不能只实现 copy adapter 后提升整族状态。

并发 open/revoke/ref 更新应受 broker 锁或统一执行器保护，耗时 native 调用不得在全局锁内阻塞其他 session。device bytes 对 backing 只计一次；importer 单独计 mapping/object 配额，不重复计物理分配。失败导入、partial launch、close、EOF 和 reset 均需恰好释放本次取得的 refs，保持核心现有 quota 与 drain 可观测性。

### CNRT Host IPC 的客户端域

本机 Native HostMalloc IPC 是两个进程的 CPU 共享内存，不能用一次 H2D/D2H 快照模拟。当前 Host registry 是进程内 malloc/lease；它没有跨客户端进程的 OS backing 或共同映射。

I1 需定义 client host domain 并提供同主机客户端可传递的共享 OS backing、map/unmap 与 lease。不同客户端机器之间的裸 CPU load/store 没有当前 TCP 协议可表达的共享语义，必须明确部署边界和返回行为，不能声称透明跨主机 CPU coherence。客户端域与 backing transport 属于尚未实现的架构工作，Native 已成功的普通 Host/device IPC 继续列 NOT_IMPLEMENTED，不因当前模型缺失而标硬件 BLOCKED。

## VMM / OS descriptor

I2 的两个 IPC API 还依赖 memory family 中的 create/release/retain、granularity/properties、VA reserve/free、map/unmap、access set/get。需拆开 VmmBackingID、generic handle ref、VA reservation、mapping 和 exported descriptor ref；释放 generic handle 不自动销毁已有映射，最后的 backing 回收须等待相应 refs。offset、granularity 与映射 range 不能沿用普通 Malloc 的单一对象模型。

本机 `cnMemExportToShareableHandle(void *)` 在 POSIX FD 模式向输出位置写入 **8 bytes**。探针用前后 guard 与预填 `0xa5a5a5a5a5a5a5a5` 的 uint64 槽观察到 `000000000000000a`，高 4 bytes 也被写零；仅分配 4-byte int 会覆盖邻近对象。后续 ABI adapter 须按本 SDK 实测宽度处理，并将该边界与一般 POSIX FD 的 int 宽度分开记录。Import 的 `void *osHandle` 则按头文件接口传递 FD 数值，不解引用为输出槽。

SCM_RIGHTS 只在本机 Unix socket 中传递 descriptor；本批 Python runner 将收到的真实 FD 传入独立 exec importer。Server 上的 FD 整数经 TCP 发送后不会在客户端变成 FD。Remote 需要客户端 OS handle proxy/共享 descriptor transport、dup/close 生命周期及 server restart 失效策略，且客户端仍不能依赖本地 MLU Driver。尚未选定并验证该方案，不能先返回 Server FD 或任意整数宣称实现。

## 分批验收

| 批次 | 本族 API 数 | 共享能力与升级条件 |
|---|---:|---|
| G1 | 28 | 已完成；owned DAG、node identities、父图校验、clone remap、有界 node/edge arrays；两库 Native/Remote 查询、编辑、reset/EOF 和隔离客户端 |
| G2 | 44 | 边界内 44/44：memset/D2D memcpy/kernel/child/entity snapshot/update、allocation/module/invocation leases；Host graph memcpy staging/publication 仍缺 |
| G3 | 10 | 逻辑线程 capture 状态、frontier、每次 Host 输入与 D2H publication；多次 replay、模式/线程与依赖转移 |
| G4 | 20 | 客户端 callback/destructor、user refs、客户端 DOT 路径；顺序与恰好一次析构 |
| I1 | 6 | 跨 session backing broker、兼容 Context 执行、imported identities、同 client-host 域共享；独立双客户端与多 importer、close/EOF/reset、计量 |
| I2 | 2 | VMM backing/VA/handle/FD refs 及客户端 OS transport；依赖的 memory family API 另计，不包含在本行的 2 项中 |
| I3 | 4 | 当前 BLOCKED；先换至 IPCNOTIFIER_SUPPORTED=1 且合法创建成功的设备/SDK，再做跨进程 record/wait 基线和 Remote refs |

G1→G2→G3→G4；I1→I2。I3 的 native 前提独立检查。每批均需相关 Native 语义、独立 Remote Server/Client、replacement/preload、无本地 SDK/设备客户端与必要 ownership/资源回收证据，才按逐 API boundary 升级；当前 memset/D2D memcpy/kernel/child typed graph 执行已验证，Host memcpy/capture 与 IPC 仍未实现。7 个 Peer API 仍单独因缺第二设备 BLOCKED。
