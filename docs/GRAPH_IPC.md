# Graph / IPC 原生契约与实现计划

更新：2026-11-07。范围为 MLU370-S4 单设备、Neuware 3.5.2、CNRT 6.5.2、CNDrv 2.5.2、AArch64 LP64。本阶段完成 Native 契约探针和共享对象模型设计；以下 Remote 状态机尚未实现。102 个 Graph API、8 个普通 memory/VMM IPC API 仍为 NOT_IMPLEMENTED；4 个 IPC notifier API 因本机原生前提缺失为 BLOCKED。不能从 Native 成功推导客户端导出或 RPC 能力。

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

当前探针只覆盖数值 DAG、relaxed capture 与正常 close-before-free 的共享路径。Kernel/Host/child nodes、entity update、pending entity destroy、其他 capture 模式/线程交互、user objects、重复 importer 和源进程提前退出尚无 Native 成功基线，不能称为整族认证。

## 计划中的 Graph 对象模型

现有 [object registry](../server/object_registry.h) 提供 session owner/generation 和不可直接作为 native 指针使用的 identity；[backend](../server/mlu_backend.h) 为每会话持有独立 CNcontext。Graph 需要新增以下对象，不能把所有句柄都塞进 Queue 或 module 的现有表。

| 对象 | 归属与内容 | 退役与回收 |
|---|---|---|
| GraphID | session generation；mutable DAG、NodeID 集合、typed payload、user refs | graph destroy 使图及所属 node 的公开 ID 失效；不释放 entity 已持有的独立 revision |
| NodeID | parent GraphID + session generation；类型、依赖、参数 | 删除时断开边；其他图的 node 不可作该图的依赖；clone 生成新 identity 并保存源→clone 映射 |
| EntityID | session generation；instantiate/update 生成的独立 revision | 公开 ID 退役与底层释放分开；已提交 invocation 完成后才释放 native entity 与其 refs |
| CaptureID | session generation、Queue、逻辑客户端线程、capture mode | end 转交捕获 DAG；invalidated 状态仍需 end/cleanup，失败不能遗留 staging 或半个公开图 |
| Invocation record | Entity revision、Queue 顺序及 completion marker | 持有本次执行的 payload/staging/Host lease，完成并发布 D2H 后释放 |

Node 参数中的 AllocationID+offset、KernelID/module、Host 目标属于引用，不能以传递原始客户端地址代替。Graph 参数记录复制 scalar、shape、dependency array 与 kernel argument metadata；GetParams 需要反向重建客户端句柄和地址。clone 复制参数记录、生成新的 node IDs，不修改源图。

instantiate 获取独立快照；entity update 应先构建新 revision，成功后原子切换供后续 invoke 使用，失败保留旧 revision。在途 invocation 保持它实际提交的旧 revision。每个 entity 的并发 invoke 约束须按 SDK 补齐基线，不能假定不同 Queue 可以无条件并发执行同一 entity。

SDK 的 EntityDestroy 注释规定 pending 执行不被终止，完成后异步释放。计划复用 Queue retirement 的公开 ID 退役方式，并增加 entity、allocation、module 和 Host payload leases。图销毁不等于 kernel module unload，也不等于目标 buffer free。公开 free/unload/reset 与在途 native 资源释放必须分离，失败清理可重试；这部分尚待 G2 实现与 Native/Remote 验证。

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
| G1 | 28 | owned DAG、node identities、父图校验、clone remap、有界 node/edge arrays；两库 Native/Remote 查询、编辑、reset/EOF 和隔离客户端 |
| G2 | 44 | typed copy/memset/kernel/child nodes、entity revision/update、module/allocation leases、invoke completion；数值与 pending destroy 回收 |
| G3 | 10 | 逻辑线程 capture 状态、frontier、每次 Host 输入与 D2H publication；多次 replay、模式/线程与依赖转移 |
| G4 | 20 | 客户端 callback/destructor、user refs、客户端 DOT 路径；顺序与恰好一次析构 |
| I1 | 6 | 跨 session backing broker、兼容 Context 执行、imported identities、同 client-host 域共享；独立双客户端与多 importer、close/EOF/reset、计量 |
| I2 | 2 | VMM backing/VA/handle/FD refs 及客户端 OS transport；依赖的 memory family API 另计，不包含在本行的 2 项中 |
| I3 | 4 | 当前 BLOCKED；先换至 IPCNOTIFIER_SUPPORTED=1 且合法创建成功的设备/SDK，再做跨进程 record/wait 基线和 Remote refs |

G1→G2→G3→G4；I1→I2。I3 的 native 前提独立检查。每批均需正常 Native 数值、独立 Remote Server/Client、replacement/preload、无本地 SDK/设备客户端与必要 ownership/资源回收证据，才按逐 API boundary 升级；本阶段未增加 wire opcode、Shim 导出或运行期共享资源。7 个 Peer API 仍单独因缺第二设备 BLOCKED。
