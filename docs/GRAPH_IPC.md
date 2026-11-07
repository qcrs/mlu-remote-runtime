# Graph / IPC 原生契约与实现

更新：2026-11-07。范围为 MLU370-S4 单设备、Neuware 3.5.2、CNRT 6.5.2、CNDrv 2.5.2、AArch64 LP64。Graph G1～G4 共 102/102，普通 memory/Host/VMM IPC I1/I2 共 8/8，均已按逐 API 声明边界实现；4 个 IPC notifier API 因本机原生前提缺失为 BLOCKED。最新 [IPC 证据](../evidence/remaining-ipc-20261107/README.md) 覆盖独立进程、跨库、隔离客户端、引用/Context 生命周期、真实客户端 FD 和一次物理计量。

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

初始探针覆盖数值 DAG、relaxed capture 与正常 close-before-free 的共享路径。G2 后续另补 child、kernel、entity update 与 pending entity destroy 的两库 Native 数值基线。G3 后续补齐三种 capture 模式、线程/Context 交互和依赖前沿；G4 后续补齐 Host callback nodes、user objects 和 DOT。最新 IPC 批次补齐多 importer、源 free/reset/EOF、Graph leases 与 FD retirement；源提前退役的保留属于 Remote 安全策略，未称为 Native 定义行为。

## G1 已实现：owned DAG 与节点身份

双库各 14 个入口统一生成到 `graph_action`、`graph_edit`、`graph_query` 三个 RPC（opcode 80～82）。共享 CNDrv adapter 直接维护 native DAG；查询结果映射为 GraphID/NodeID，拓扑边与查询方向由本机 SDK 决定。`scripts/array_codegen.py` 生成有界 trailing u64 arrays、逐元素网络序编码、精确长度检查和 caller-owned 输出；客户端 ABI 句柄仅保存 opaque ID。

GraphID/NodeID 复用 session owner/generation/type registry，NodeID 带 parent GraphID。输入依赖逐个检查父图；clone 预留并登记全部新 NodeIDs，重复查询和 FindInClone 返回稳定身份。DestroyNode 先由 native 删除节点/边再撤销 ID；graph destroy/reset/EOF 撤销全部 child IDs。native graph 清理失败保留 session/context ownership 并 quarantine，不能假定 context destroy 自动释放这些 host-side 对象。

每 session 最多 1024 个 graph、所有 graph 合计 4096 个 node，并受共享 8192-object registry 限制。单 RPC 最多 65536 个 u64 identities；node 查询 capacity 上限 65536，edge 查询两组数组合计上限相同，因此 capacity 上限 32768。count-only、截断、零容量和 NULL tail 保留 native 实测语义；零条 Add/RemoveDependencies 成功。

本机两库 `NodeGetDependencies` 返回 outgoing 后继，`NodeGetDependentNodes` 返回 incoming 前驱；Remote 直接调用 native 后映射身份，不按名字反转。stale/destroyed handle 的 native 行为不能作为安全契约，Remote 的 stale/reset 拒绝单列为安全校验。G1 初始边界为 empty nodes；G2 增加的 memset、child 和 entity 见下一节。

[G1 证据](../evidence/graph-g1-20261107/README.md) 包含独立 Server/Client、两库 Native/replacement/preload/隔离客户端、owner/type/parent/clone/数组边界、reset/EOF 和 clean drain，专项 14/14。

## G2：memset / D2D 与 Host memcpy / kernel / child / entity（边界内 44/44）

双库各 15 个入口由 Schema 生成 wrapper，共享 `graph_memset`、`graph_entity`、`graph_child`、`graph_memcpy` RPC（83～86）。memset 使用 AllocationID+offset 与有界依赖数组；按本 SDK 仅认证 height=1、elementSize=1/2/4。GetParams 反向恢复客户端 fake VA，已退役地址使用仍保留的 tombstone。诊断 buffer 上限 65536 bytes；当前 instantiate 失败节点映射覆盖顶层，嵌套失败诊断尚未认证。

D2D memcpy 首批支持 1..1048576 bytes、extent height=depth=1、两端 pitch=width/ysize=1。`src`/`dst` 各自编码为 owned live AllocationID+offset，分别校验 range；Host 扩展见下节，capture 见 G3，peer 仍受单设备范围限制。SDK 忽略的 position、ptr aliases、xsize 规范化；`dir` 保留 Native 判定（本机 0/4/5 成功，1/2/3/6 返回无效参数），不能用 CNRT 枚举名称推断该图入口的实际行为。GetParams 恢复两端客户端 VA，公开 free 后仍能使用 graph payload 持有的 tombstone。每个 payload 各持有源、目标一个 lease；即使同一 allocation 占据两端也分别增减，clone/child/entity/invocation 复用相同引用机制。

Native AddChildTopoNode 复制源图；embedded 图随 child node 所有，GetTopo 返回独立 GraphID/NodeID 树，修改它会影响该 child。clone 递归通过 Native NodeFindInClone 建立新身份，保留 immutable payload；源图销毁不影响 child 或 entity。所有 embedded graph 计入原有 1024 graph / 4096 node 配额，递归深度小于 64。Remote 拒绝直接 destroy node-owned borrowed 图；删除 child node 或销毁父图递归撤销 IDs，由 native 所有者统一释放。

Instantiate 先 native clone 独立快照，再建立 EntityID；每 session 的 entity snapshot、revision view 和 invocation view 共用 4096 snapshot-node 配额（含嵌套 child）。EntityUpdate 先分配 replacement refs，native 成功才切换，失败保留旧 revision 和 update result/error node。setter 保持最初 instantiate 图的顶层 node 对应关系；不会把新图 GetNodes 的枚举位置当作 native update 配对证据。整个当前 revision 的 allocation refs 与各 setter override 分开保留；被 setter 替换的 base payload 可能保守保留到下一次完整 update 或 entity destroy。

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

### Host memcpy：每次 invoke 的输入与完成写回

8 个 memcpy Add/Get/Set/EntitySet 入口扩展到 1D H2D/D2H；单节点 1..1048576 bytes、height=depth=1、pitch=width、ysize=1，单 invocation 的 Host endpoint 长度总和不超过 16 MiB。Host token 只用于客户端识别地址，Server 从不解引用。Native 检查方向；本机 H2D 为 0/2、D2H 为 0/3，其他所测方向拒绝。GetParams 恢复原 Host token 和 device VA。

生成的 EntityInvoke wrapper 通过 `graph_host`（opcode 89）执行 describe → 上传当前 H2D bytes → submit；客户端用同一 session 锁覆盖描述、输入读取、提交和 pending publication 注册。Execute 回显有序 token/length/direction descriptor，Server 在执行前逐项核对，拒绝旧 descriptor。构图/instantiate 不读取客户端 Host bytes。H2D 输入在每次 invoke 时取样，后续 invocation 不覆盖已提交的输入；调用者需先完成会向下一次输入写入数据的先前异步操作。

每次 Host invoke 独立 clone 当前 revision 和 setter overrides，改写 Host 端到本次 pinned staging，再 instantiate 私有 native entity。通过 `NodeFindInClone` 映射顶层和 embedded child，不依赖两个无关图的 GetNodes 顺序。一次 invocation 中相交的 Host 地址区间共享 staging，以保持图内 D2H→H2D 反馈及有序重叠输出；不同 invocation 独立保留。占位 Host payload 和实际 pinned bytes 都计入 STAGING，view nodes 计入共享 snapshot 配额。全部 Host 节点被 D2D override 替换时支持零 Host bytes 路径。

每次提交绑定 TransferID，D2H 在完成时统一 fetch，再按各目标 scatter。复用普通 Host async 的 pending 队列、同队列/目标重叠检查和 Queue/Notifier/Context 等成功完成边界；不会在 instantiate 或提交时提前写回，也没有后台写回。图内多个目标各自持有 Host registry lease；pageable 内存仍由调用者维持有效。reset/context close 先完成并写回，EOF 只清理 Server 资源。私有 native entity、view、allocation/module leases 和 pinned bytes 保留到对应完成及输出 fetch/release；native 释放失败保留 ownership/配额，成功 Context sync 重试。completion record 创建失败且 Queue wait 失败时保留 orphan，直到确认 Context 完成。

EntityUpdate 保持最初 instantiate-time 的 setter NodeID。Host 重放中的 setter 路由只接受经节点类型、边结构和唯一结构角色证明的对应关系，最多 512 个顶层节点；对称或不明确的图仍可更新和回放，但会影响 Host 重放的 entity setter 返回 unsupported。不会用枚举位置猜测映射。这是明确的 Remote 保守边界。

[Host 重放证据](../evidence/graph-host-20261107/README.md) 包含两库 Native/replacement/preload/隔离客户端、pageable/pinned、当前输入、clone/update/原 setter 身份、nested child/replacement、Host alias、default Queue、pending destroy、Host retirement/reset/EOF、协议/配额/故障回滚及资源归零。kernel 回归另验证 H2D→kernel→D2H 与普通异步 copy 的重叠目标写回顺序；提前 Host free 与多个在途 invocation 间修改输入单列 Remote 安全策略，不称为 Native 相同调用顺序差分。

## G3 已实现：Capture、依赖前沿与重放

双库各 5 个入口由 Schema 生成，共享 `graph_capture` RPC（opcode 90，capability `MLU_CAP_GRAPH_CAPTURE`）。Server session 持有 capture DAG、root Queue、mode、逻辑客户端线程、invalid 状态、参与 Queue 与 Notifier 的 frontier。所有元数据计入 staging 配额，DAG/NodeID 继续使用 G1 registry；捕获中的 GraphID 是借用句柄，不能直接 Destroy。End 成功将图转交给调用者，reset/close/EOF 统一清理。

Server worker TLS 不能代表应用线程。客户端进程分配稳定逻辑线程 ID，在三个模式下按 Native 状态转换操作；共享 Global count/epoch 也覆盖同一 Driver 进程的多个 Context/session。另一个客户端 session 的操作不影响本 session。客户端在同一锁内完成 capture 判断与普通 Host 采样/submit，服务端 dispatch guard 拒绝绕过 capture RPC 的普通入队请求。

声明边界为显式 owned Queue、1D async memset（1/2/4 bytes）、H2D/D2H/D2D memcpy，以及已有 metadata 支持的 generic kernel（含 nested struct/array pointer relocation）。捕获只创建 immutable DAG payload，不提交这些操作；H2D 只保留客户端地址，既不读取也不上传 bytes，D2H 不创建普通 pending writeback。End 后复用 G2 private graph/每次 invocation staging、当前 Host 输入、allocation/module/Host leases 和成功完成边界的 publication。单 copy <=1 MiB，Host endpoint 总和 <=16 MiB；pageable 地址仍须由调用者保持有效。2D/3D capture 和 callback nodes 尚无 adapter，明确拒绝，未纳入当前认证。

Notifier record 保存 Queue frontier，wait 将其他 Queue 加入同一 capture 并合并依赖；独立 capture 的 merge 返回 100363 且两边仍 ACTIVE。UpdateCaptureDependencies 的 flags=0 ADD 保留现有 frontier，flags=1 SET 替换，逐个校验同图 NodeID。下一个 captured node 依赖完整 frontier。End 从 root frontier 沿本 SDK 的 GetDependentNodes（前驱）遍历，允许分支 join 后 root 继续前进，未汇合分支返回 100364。

本机 Native 严格差分保留以下状态，不按其他 SDK 推断：

| 操作 | Native 与 Remote 行为 |
|---|---|
| Begin / IsCapturing / GetCaptureInfo | NONE → ACTIVE；NONE/INVALID 只写 status，其他输出保留；ACTIVE 返回稳定 graph/node 身份和 borrowed frontier |
| Global 异线程 End | 100362，保留 graph 输出并结束整个 capture；原线程再 End 返回 100015；mode 1/2 允许异线程 End |
| captured QueueSync / Query | 100360 并转 INVALID，End 返回 100361 并清理 |
| Global 中 malloc、其他/default QueueSync | 100360 并失效；包括异线程、既有其他 Driver Context。mode 1/2 的这些操作正常执行 |
| ContextSync / SyncDevice | 三种 mode 均成功，capture 保持 ACTIVE，即使已有 captured nodes |
| EntityInvoke | 正常执行既有 entity，不加入 capture 图；Native 数值测试同时确认执行值与零新增节点 |
| captured Queue wait 外部普通 Notifier | 100365，capture 保持 ACTIVE |
| 从参与分支 Queue End | 100366，保留输出并将整个 capture 置 INVALID；root End 返回 100361 |
| End 配额/native 查询失败 | 保留 capture 可重试；空图无需遍历临时缓冲即可 End |

Frontier 返回存储由客户端 capture session 持有，至 End/reset/disconnect 释放；其他 capture 的 cache 独立。捕获 payload 的持久配额、module/allocation leases、clone/snapshot 和 invocation 回收沿用 G2，失败 Native destroy 保留 registry ownership 待 cleanup 重试。Reset/close 先同步并 publish 普通 pending Host 输出，再丢弃 active capture；EOF 仅清理服务端资源。

[G3 证据](../evidence/graph-capture-20261107/README.md) 专项 97/97，含双库 Native/replacement/preload/隐藏 SDK 与设备的客户端、不可读 H2D 源页、两次当前输入 kernel 回放、参数 identity/snapshot、Notifier fork/join 与 ADD/SET、模式/线程/Context、raw dispatch owner/type、两档配额及 reset/EOF。G1/G2、Host async、功能族与完整核心回归记录保存在同目录。

## G4 已实现：客户端 callback、user object 与文件路径

双库各 10 个 Host node Add/Get/Set/EntitySet、user object Create/Acquire/Release、graph Acquire/Release 和 DOT wrapper 由 Schema 生成，共享 `graph_control` RPC（opcode 92、capability `MLU_CAP_GRAPH_CALLBACKS=0x20000`）。Host 函数及 userData 始终留在客户端 immutable identity 表；Server native 源图只保存不可执行的占位 Host node。getter 恢复原函数/userData，clone、instantiate、child、EntityUpdate 和 entity setter 复用 G2 snapshot/override。更新后的 setter 仍需唯一结构角色映射。

主 session 建立 callback service 后返回 128-bit 随机配对 secret。客户端连接同一 peer，以首帧 `CALLBACK_ATTACH=91` 绑定独立 callback channel；重复绑定拒绝，副通道不创建 native Context。事件序号从 2 递增，32-byte 网络序头保存 kind/run-cookie/token/bytes；仅 D2H 事件带输出 bytes，仅 H2D ACK 带输入 bytes。CALL、H2D、D2H、DONE、DESTROY、CLOSE 分别为 1～6。副通道占一个普通 worker/连接额度，无事件时允许长期空闲，事件及 ACK 受 session RPC deadline 约束；随机 secret 仅用于 session 配对，未增加生产认证或加密。

回放先 describe 返回有序 24-byte token/bytes/kind 计划，客户端登记 callback/Host endpoint whitelist、run cookie 和 pinned Host registry leases，再完整回显计划提交。Server 逐项核对，在私有 native clone 中用 thunk 替换 callback，并给每个 Host copy 插入 native Host 边界。H2D 在原前驱完成后请求当前客户端 bytes，再执行 copy；D2H 完成后立即在客户端 scatter、ACK，然后才允许原后继执行。图内 D2H→callback→H2D 因此能观察回调修改的数据，无须等待整个图结束。各 endpoint 独立 pinned staging，不沿用无 callback 图的区间 alias 合并；顺序由 DAG 和逐 copy ACK 保持。

顶层 DONE 依赖所有顶层节点（含 child 和辅助 Host nodes）。客户端先移除 run、释放 Host leases，再 ACK DONE，避免 QueueSync 成功时仍占有已完成 lease。Server Transfer 绑定 native completion，客户端立即请求 release，真正完成前仍保留私有图/entity、callback args、allocation/module refs 和 pinned bytes。describe/执行共用 snapshot 配额；helper nodes 也计入 4096 node 上限，pinned staging 计入 STAGING。单 copy <=1 MiB，Host endpoint 总和 <=16 MiB；客户端最多 4096 个 immutable identity、1024 个在途 run，Server 最多 8192 个 pending events。pageable 地址由调用者保活；pinned 提前 free 的延后退页属于 Remote 安全策略。

每个 session 的客户端 reader 串行执行回调。回调必须返回，且一次事件须在 RPC deadline 内完成；不能强制取消用户代码。Host callback 和 user destructor 中调用 Runtime/Driver API 在 shim 锁前返回 NotSupported，避免主 RPC 等待 callback ACK 时发生重入死锁。没有 callback 的图直接继续原 Host/non-Host 路径；旧 raw entity 和 Host replay 路径拒绝含 callback 的图及 child override，不能误执行占位节点。callback 图提交前先完成已有普通 deferred D2H，保证回调侧读取能看到之前的输出。

UserObjectID 使用独立 session/type registry；Native `cnUserObject*` 管理 caller、graph、clone、entity revision 和在途引用。支持非零 count <= INT_MAX、创建 flag 1、graph acquire flags 0/MOVE；MOVE 相应扣减 caller refs。native 最终 destructor 仅异步入队一次 DESTROY，不阻塞 native 析构线程；service ref 独立于 session，客户端在专用线程调用原始 destructor，移除一次性 identity。不关联图的最后 release 同样自行推进，后续无 API 调用也会执行。cleanup 释放图和 caller refs 后，等待 native destructor 与排队事件完成，再握手 CLOSE。

channel EOF、坏事件/ACK 或 deadline 失败使主 session 失效，不能假报完成或自动重放。提交发生断线时本地 run/leases 保留至 executor join；不读取空 RPC response。Server 提交后 completion 建立失败且 QueueSync 也失败时先 poison channel，保留 orphan 到确认 Context 完成。reset/close 正常 drain，EOF 仅做安全回收；源 graph/entity 在 callback 阻塞期间销毁，invocation 仍持有对应 user object 和 backing。

DOT 使用 Server 临时文件取得 native 内容，有界读回至客户端，再写入调用者本地路径，最大 1 MiB。flags 由 Native 解释；实测 0/1/2/4/64/255/256 成功，缺失目录为 Driver 100007 / Runtime 632015。没有把同名 Server 路径视为客户端路径。

[G4 证据](../evidence/graph-callbacks-20261107/README.md) 专项 20/20：两库 Native/replacement/preload/隔离客户端、数值顺序/PID、clone/setter/update/nested child、无 wait 推进、在途销毁/user refs、DOT、重入/reset/EOF/Host retirement、raw plan/owner/type、staging 配额回滚和提交断线。既有 Capture、G1/G2、Host async、功能族和核心回归记录保存在同目录。

## I1 已实现：memory IPC broker

Native 的独立进程共享成功，同一进程跨 Context 的 IPC open 失败。当前 Server 是一个进程中的多个 session workers，不能直接照搬 native handle open。后续 [Context 探针](../evidence/ipc-context-20261107/README.md) 7/7、202 个检查通过：另一 Context 可以直接用 owner allocation 地址进行显式 H2D/D2H/D2D、D32 memset，并在自己的 queue/module 中执行混用 owner/local 指针的 kernel；另线程新 Context 也成功。owner allocation/Context 全程存活，importer 销毁后 owner 数值仍正确。

I1 使用进程级 device backing broker：导入会话在 Server 内引用同一 native backing，通过自己的 Context 执行实测路径，无须 native IPC open。`server/ipc_lifetime.inc` / `ipc_control.inc` 管理 backing 与源 Context；两库 IPC adapter 共享导入身份和 copy/kernel/Graph lease 路径。该执行依据限本设备/SDK 的已验证操作。

实现将以下身份分开：

- IpcExportToken：8-byte opaque handle；device token 绑定 server instance、backing 与 kind，不泄露 native 地址；Host token 在客户端域解析。不能与 64-byte notifier handle 混用。
- Backing record：实际 native storage、拥有它的 Context/执行器、allocation size/kind、export/open/operation refs，以及计量 owner。
- ImportedAllocationID：importer session 内独立 identity 与 fake VA，借用 backing lease；其 offset/range 检查复用现有共享内存路径。

普通未导出的 allocation 继续受原 session owner 限制；只有合法 broker token 才能建立 imported mapping。close 撤销该 importer 的公开 mapping，完成它自己的在途操作后减 ref，不可对 exporter storage 调普通 free，也不能影响其他 importer。

device source free/reset/EOF 后撤销新 opens，同时由既有 importer、Graph 和在途操作 refs 延后 backing 及源 Context 的释放，最终一个 ref 释放后回收。Graph 可在 imported identity 已关闭后继续持有 backing。Native 文档对 import 未关闭就 free 源内存的行为未定义；这里的保留是 Remote 内部安全回收策略。正常兼容性验收使用 close-before-free，提前退役另列 Remote 生命周期验证。

Native 探针证明混合本地与共享指针的有限 kernel 可以在 importer Context 执行；没有认证跨 Context Queue/Notifier handles。broker 保留原 backing Context，将 source session 公开身份退役与 native Context 最终销毁分开。跨 Queue ordering 仍遵守应用的同步边界。

并发 open/revoke/ref 更新受 broker 锁保护。device bytes 对 backing 只计一次；importer 使用独立 mapping/object identity，不重复计物理分配。源分配退役将计量转给 detached backing；最后一个 ref 才归还额度。双库各以 4096-byte global/session 上限、3 个 importer 验证存活期间无法多分配、源 free 后额度保留、最后 close 后可重新分配完整 4096 bytes。Server 最终 resources/workers/cleanup errors 归零。

### CNRT Host IPC 的客户端域

本机 Native HostMalloc IPC 是两个进程的 CPU 共享内存。`client/host_memory.c` 使用 memfd、MAP_SHARED 和 mlock，为同客户端主机的进程建立共同 OS backing，保留原 guard/tombstone/pending lease。抽象 Unix socket broker 校验 SO_PEERCRED 的 UID，以 SCM_RIGHTS 传 FD；两端直接 CPU 读写可见同一内容，未经过一次性 copy 快照。

Host IPC 限同客户端主机、UID 与 Unix namespace；不同客户端机器没有裸 CPU load/store 一致性。进程级 Host allocation 不属于某一 Context，Context reset 不撤销仍存活的 Host 源；free/进程退出撤销新 opens，既有映射可继续使用直至 unmap。tombstone 使用 private PROT_NONE 替换旧共享映射，使退役地址不可访问并释放 backing 引用。重复 export 保留 Native 100102 / Runtime 632015。

## I2 已实现：VMM / OS descriptor

I2 与 memory family 的 11 个 VMM API 已完成。generic handle ref、VA reservation、mapping 和 exported descriptor ref 分开；释放 generic handle 不销毁已有映射，backing 等待最后引用才回收。native granularity/access descriptors、aligned guarded 客户端 reservation、offset-zero aliases、retain/release/remap 与 Context cleanup 已差分。非零 backing offset 在本 SDK 为 Native 100050，Remote 保留该结果。

本机 `cnMemExportToShareableHandle(void *)` 在 POSIX FD 模式向输出位置写入 **8 bytes**。探针用前后 guard 与预填 `0xa5a5a5a5a5a5a5a5` 的 uint64 槽观察到 `000000000000000a`，高 4 bytes 也被写零；仅分配 4-byte int 会覆盖邻近对象。Remote adapter 按该宽度写入。Import 的 `void *osHandle` 按头文件接口传递 FD 数值，不解引用为输出槽。

`client/ipc_descriptor.inc` 返回真实可传递的客户端 TCP socket FD，连接 Server 的 descriptor broker；其中未消费的 8-byte capability 通过 MSG_PEEK 读取。dup/SCM_RIGHTS 共享 socket 生命周期，最后一个 close 让 Server 释放 Native FD/backing lease，源 Context 同时保活。Server 重启断开 descriptor，旧 capability 失效。由于 Native 同进程 FD import 拒绝，broker 在源 backing 上临时 map+RetainAllocationHandle 建立可用引用。独立 importer、源 handle/map/Context 退役后经 dup FD 导入与数值访问已通过，客户端无本地 Driver 依赖。

## 分批验收

| 批次 | 本族 API 数 | 共享能力与升级条件 |
|---|---:|---|
| G1 | 28 | 已完成；owned DAG、node identities、父图校验、clone remap、有界 node/edge arrays；两库 Native/Remote 查询、编辑、reset/EOF 和隔离客户端 |
| G2 | 44 | 边界内 44/44：memset/D2D/H2D/D2H memcpy/kernel/child/entity snapshot/update、allocation/module/Host invocation leases 与完成写回 |
| G3 | 10 | 已完成；逻辑线程/三种 mode、跨 Context Global invalidation、Notifier fork/join、ADD/SET frontier、当前 Host 输入与完成写回 |
| G4 | 20 | 已完成；客户端 executor、逐 copy Host 顺序、callback snapshots、native user refs/唯一析构、客户端 DOT、reset/EOF 与失败回滚 |
| I1 | 6 | 已完成；跨 session device backing/Context refs、imported identities、一次物理计量，以及同客户端主机/UID/namespace 的 Host CPU 共享 |
| I2 | 2 | 已完成；VMM backing/VA/handle/FD refs、真实客户端 descriptor/dup/SCM_RIGHTS；11 个 memory family 前提入口另计 |
| I3 | 4 | 当前 BLOCKED；先换至 IPCNOTIFIER_SUPPORTED=1 且合法创建成功的设备/SDK，再做跨进程 record/wait 基线和 Remote refs |

G1→G2→G3→G4、I1→I2 均已完成。最新 IPC 正常四模式 24/24、生命周期 12/12、配额 2/2 通过，见 [证据与复现](../evidence/remaining-ipc-20261107/README.md)。普通队列 Host callback 另保证此前普通/Graph deferred D2H 在回调前发布，见 [剩余功能族证据](../evidence/remaining-closure-20261107/README.md)。I3 需要可成功创建 interprocess notifier 的硬件/SDK 后继续；7 个 Peer API 因缺第二设备单独 BLOCKED。
