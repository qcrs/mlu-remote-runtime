# 资源配额与服务端关闭

范围：2026-11-02，2026-11-07 补充 Queue retirement、Host/deferred D2H、Graph leases、普通队列 callback/atomic、VMM/Symbol 和 IPC backing；AArch64 / Neuware 3.5.2 / MLU370-S4。保留原有 TCP、对象所有权和 native backend；新增配额不会把原生资源错误改成成功。实现位于 `server/resource_limits.c`、`server/mlu_backend.c` 及其 Graph/VMM/Symbol/IPC/queue Host adapters、`server/callback_bridge.c`、`client/host_memory.c` 和 `server/mlu_remote_server.c`。

## 配置与计量口径

启动时读取配置，只接受正十进制整数；空值、零、负数、尾随空白、溢出和超范围均在监听前失败。所有 byte 配额的范围为 1..UINT64_MAX，可大于物理设备容量；实际分配仍须通过 native CNDrv。MiB = 1024² bytes。

| 环境变量 | 默认值 | 计量 |
|---|---:|---|
| MLU_REMOTE_MAX_CONNECTIONS | 32，最大 1024 | 已接受且尚未完成清理的 worker，包括未发送 HELLO、partial frame、callback/atomic 侧通道、IPC descriptor、发送阻塞及 native cleanup 中的连接 |
| MLU_REMOTE_SESSION_DEVICE_BYTES | 1 GiB | 单 session 的成功设备分配、VMM backing 请求字节及在途预留；IPC 导入不重复计量 |
| MLU_REMOTE_TOTAL_DEVICE_BYTES | 8 GiB | 全进程上述预留与存活 backing，包括源 session 退役后仍由 IPC refs 保活的字节 |
| MLU_REMOTE_SESSION_STAGING_BYTES | 64 MiB | 单 session 持有的 H2D snapshot、pinned D2H/Graph Host buffer、普通 callback publication 副本、每个 atomic cell 的 8 bytes 与持久 Graph kernel record/wire/aligned 参数存储 |
| MLU_REMOTE_TOTAL_STAGING_BYTES | 256 MiB | 全进程上述 Host staging、atomic cells 与 Graph kernel 参数 |
| MLU_REMOTE_SESSION_IMAGE_BYTES | 64 MiB | 单 session 保留的上传 image 原始字节及解析/加载时预留 |
| MLU_REMOTE_TOTAL_IMAGE_BYTES | 256 MiB | 全进程上述上传 image 字节 |
| MLU_REMOTE_SESSION_MAX_TRANSFERS | 1024，最大 1024 | 尚未回收的 TransferID；包括已完成但未 release，以及 release 后 native completion 尚未到达的项 |

会话预留和全进程预留在同一 mutex 内检查、增加；使用减法检查余量，拒绝 UINT64_MAX 等超额请求时不会整数回绕。不同 worker 不能同时抢占同一份剩余额度。单 worker 串行处理本 session；global lock 不跨 native 调用持有。

连接满额时立即关闭新 socket，已有连接可继续使用。此时没有可用 session，不发送虚构 HELLO 成功。其他配额拒绝返回 `MLU_REMOTE_RESOURCE_LIMIT=0x80000007`、错误名 `REMOTE_RESOURCE_LIMIT`；CNRT Shim 映射为 `cnrtErrorNoMem`。该错误保留连接，不会 poison。错误表示本次设备分配、image load、copy 或 launch 尚未提交；调用者可在释放资源后主动重试。

Transfer 容量在 copy/launch 的 native 提交前检查。不能先改变内存或 kernel 状态，再把可预知的额度不足当作失败返回。创建 native notifier 等后续原生失败仍可能发生在提交之后；这条旧语义没有变成事务式回滚，不能任意自动重放。

## 释放与失败路径

设备分配 native 失败会退回预留；成功 free 退回对应字节。异步 H2D 只在确认 queue completion 后释放 snapshot 和额度；拒绝、host malloc 或 native enqueue 失败退回本次预留。不同 queue 的 snapshot 不互相释放。D2H 与 H2D 共用 staging byte 配额，D2H 的 buffer 在完成之后仍属于 Transfer，直到 fetch 后 release 或 session cleanup 才释放；单纯 Queue sync/destroy 不能退还这部分额度。

Device IPC 的 source allocation、imported views、Graph/invocation refs 与导出 descriptor 共享 backing，保留源 native Context；每个 importer 仍有独立的公开 AllocationID/fake VA。源 free/reset/EOF 撤销新 opens，已有引用继续保活，最后引用释放才回收 storage 和源 Context。这是 Remote 安全回收策略，Native 对源提前 free 的使用未定义。物理 backing 只向源分配计一次 DEVICE 字节；源身份/session 退役时，尚存字节移入独立的 detached 计量，继续占用 global quota，直至实际释放。不会把源 session 已结束误算为物理 backing 已释放。

VMM 的 generic handle 引用、backing、VA reservation 和 mapped view 分别登记；release handle 不提前释放仍映射的 storage，unmap/Graph lease 释放后再回收，最后 address free 释放 reservation。VMM IPC 导出给客户端的是可 dup/SCM_RIGHTS 的真实 socket FD；Server 保留 native FD/backing lease，最后 descriptor close 产生 EOF 后回收。descriptor worker 也计连接额度，Server 关闭时统一 shutdown。Symbol view 属于 module，公开 unload/reset 撤销身份，已有 Graph/invocation leases 延迟底层 module 回收；不把 Symbol 大小重复计为用户 DEVICE allocation。细节及生命周期/配额实测见 [IPC 证据](../evidence/remaining-ipc-20261107/README.md)。

Queue destroy 先失效公开 ID，返回时不调用 native sync/destroy；已提交的 kernel、copy、Notifier 和 H2D snapshot 继续归 session 所有。后续创建 queue、成功的 queue/Notifier/Transfer query 或 wait、context sync、free 和 module unload 会非阻塞查询已退役队列，仅在整个队列完成后释放 snapshot 并销毁底层句柄。未完成或查询失败时保留资源；底层 destroy 失败会返回错误并保留句柄供后续重试/teardown。没有独立后台收集线程，空闲 session 的资源可保留到下一次这些操作或 session cleanup。每类 1024 项的 queue 表包含尚未回收的退役队列。

`cnrtSyncDevice` 同步整个 Server native context，包含已经没有客户端句柄的退役队列。显式/default queue sync 仍遵守各自 native 完成边界，收集器只查询其他退役队列，不等待它们。Free、module unload 与 session cleanup 仍等待所有存活底层队列，包括退役队列；reset/EOF 也不能提前释放在途 staging。真机用例与 SDK pending-destroy 差异见 [Queue retirement](../evidence/queue-retirement-20261107/README.md)。

image 在解析前预留；坏 image、分配失败、native load 失败退回预留。成功 unload 释放 image。失败回滚中的 native unload 如果也失败，保留镜像及额度，交由 session cleanup/context destruction 处理。预置 module 不保留上传 image，因此不计入上传 image 字节。

Graph kernel record 在 native Add/Set/EntitySet 前预留 STAGING，持有 canonical wire、aligned 参数存储、module 和每个参数 pointer 的 allocation lease。clone/child/entity/invocation 共享 immutable record，引用共享不重复计量；setter 先建立新 record，native 成功后替换，失败退回新额度与 refs。属性副本另持有对应 allocation。公开 allocation free/module unload 立即退役身份，backing 保留到所有 graph/entity/invocation refs 释放。最后引用消失后若 native unload 失败，IMAGE 额度和 image/module 保持存活，后续成功 Context sync 重试；不能先清空记录或额度。reset/EOF/context close 按已有图/在途清理顺序释放。详见 [18 场景与失败回滚证据](../evidence/graph-g2-kernel-20261107/README.md)；提前 free/unload 的保活是 Remote 安全策略。

异步 D2H 提交前预留额度，以 `cnMallocHost` 分配独立 pinned buffer，并先登记 staging ownership，再调用 native `cnMemcpyDtoHAsync` 和记录完成 Notifier。成功后 ownership 移交 Transfer；submit/Notifier 失败时尝试同步，若同步也失败则保留 staging 和额度至后续成功同步或 Context 销毁。fetch 必须匹配 owner、TransferID 和精确长度，且 Notifier 已完成。`cnFreeHost` 失败保留指针和额度，可在后续回收时重试；Context 已销毁后仍不能丢失释放失败的 pinned buffer，最终进入 quarantine。旧同步 D2H 请求继续先等待显式 Queue，再同步复制到回复缓冲。

Client 两库共享进程级 Host registry；分配使用 memfd/MAP_SHARED、guard 和 mlock，free 立即失效公开登记，pending D2H/atomic lease 则延后退页。最终用 private PROT_NONE mapping 替换旧共享映射，既保留不可访问的 VA tombstone，也释放旧 backing 引用。Host IPC 通过同 UID 抽象 Unix socket broker、SO_PEERCRED 和 SCM_RIGHTS 传递 memfd；范围限同客户端主机、UID 与 Unix namespace。源 free/退出阻止新 opens，已有映射保持至 unmap。Host 内存为进程域，跨 Context，reset/Context destroy 不销毁仍存活的用户 Host 分配或撤销其 Host IPC export；普通 pageable 目标不被 registry 保活，调用者必须保持其有效至完成。普通 Transfer copy 的客户端 Host 地址留在本地；Graph Host endpoint 的地址值作为 opaque token 上传，Server 不解引用它，EOF 清理也不访问客户端地址。

Client 在成功 Queue/Notifier/Transfer wait/query、Context sync、free、module unload 后查询已提交 Transfer，fetch 后写回；query 遇 NotReady 不阻塞。较晚的同 Queue 或地址重叠写回必须等较早写回发布，独立目标可继续；同步 D2H 的新回复写入前先发布旧的已完成结果。reset/close 在 Server ID 失效前同步并写回；连接失败/断开时只释放本地 lease，不向应用目标写入或重放。普通 Host async 没有后台写回，未调用这些完成边界时数据可以保持未发布。Native pending destroy/reset/unload 的取消观察和数值基线预等待差异见 [Host 证据](../evidence/host-async-20261107/README.md)。

G4 callback 图使用独立配对通道和客户端 executor：每个 H2D 节点在原前驱完成后取当前输入，D2H 完成后立即写回并 ACK，后继 callback 因而能观察结果。此路径无须应用调用 wait/query，且提交前先发布已有普通 deferred D2H。每次 invocation 持有自己的 Host leases、私有 native 图/entity、allocation/module refs 与 pinned staging；DONE 先释放客户端 run/Host leases 再 ACK，Server 仍等 native completion 才回收 Transfer。source graph/entity 在途销毁不影响已提交回放。snapshot/helper 计入图配额，endpoint pinned bytes 计入 STAGING，准入失败退回新 refs/bytes 而不改源图。

UserObjectID 的 caller、graph、clone、entity 和在途 refs 由 native user-object 引用维护；最终 native destructor 只向 service 入队一次，原始 destructor 在客户端执行。service refs 独立于 session，正常 reset/close 等待销毁事件及 CLOSE；EOF/坏 ACK/deadline 使主 session 失效，不重放。callback/destructor 重入 Runtime/Driver 在 shim 锁前返回 NotSupported。用户 callback 必须在 RPC deadline 内返回；不能强制取消用户代码。提交回复断线时客户端 run 保留至 executor join，Server 无法确认 completion 时保留 orphan 至 Context 完成。详见 [G4 证据](../evidence/graph-callbacks-20261107/README.md)。

普通队列 Host function/callback 复用客户端 executor，执行前发布同队列此前普通及 Graph D2H，原始函数和 userData 留在客户端。atomic 入口将 aligned pinned Host 64-bit cell 镜像为 Server native pinned cell，每 cell 预留 8 bytes STAGING，保留客户端 Host lease；每 Context 共用一个独立 RPC 侧通道，使主连接等待 queue compare 时仍可提交 atomic 更新。绑定后 CPU 更新须经 atomic API，镜像在 atomic read/Queue/Context completion 发布；不提供 raw CPU load/store 跨主机一致性或同 cell 跨 Context 共享。Context retirement 释放侧通道和本地 lease，Server 完成/取消相关队列后释放 native cell 与额度。39 场景差分和 EOF 清理见 [剩余项收口证据](../evidence/remaining-closure-20261107/README.md)。

EOF/reset/关闭时逐项记录 native cleanup 错误，包括操作名、对象 ID 和原生状态；不再无条件打印清理成功。queue sync 失败时，H2D snapshot 与 D2H pinned buffer 保持存活，直到成功销毁 native context。若 context 销毁也失败，或销毁后 pinned buffer 仍释放失败，保留尚未确认可释放的 Host 内存和额度，报告 `CLEANUP_QUARANTINED`，停止接受新连接并 drain 其他 worker，最终非零退出。不能通过重复断开连接无限逃逸配额。进程退出是这种异常的隔离边界；没有实现 native context 修复。

## SIGTERM / SIGINT

信号处理只设置 lock-free stop flag，并向非阻塞 self-pipe 写通知。主线程拥有 listener 的唯一 close；避免从信号处理器和主线程重复关闭已被系统复用的 fd。

主线程停止 accept 后，在 worker registry 锁内对现有 socket 执行 `shutdown(SHUT_RDWR)`，唤醒 header/body 接收、空闲和回复发送等待。socket 的关闭和 registry 移除使用同一锁，避免 shutdown 已被复用的 socket。已开始的 native 工作按已有 queue 同步和 context 清理路径完成。主线程等所有 worker 清理结束，再报告 `DRAIN_COMPLETE` 和 `RESOURCE_TOTAL`，正常退出返回 0；观测到 native cleanup 错误返回 1。

使用 atomic cell 的 session 在 queue/Notifier 等待时查询 native 状态并轮询主连接；EOF 可通过已单独验证的本地 `CNresult cnQueueAbort(CNqueue)` 扩展取消未满足的 compare，然后完成清理。该扩展只在 Server 内部使用，不代表公共 `cnGetExportFunction` 通用转发已实现。

这仍没有任意 native 执行的截止时间。卡住的 CNDrv、长 kernel 或不可返回的 cleanup 仍可使 drain 等待；atomic compare 的取消不能外推为任意 kernel 可取消。没有通过 pthread cancel、强制释放在途 host buffer 或设备 reset 冒充取消。SIGKILL 的验证仅覆盖客户端连接失效；相应 Server 随后的 TERM/drain 结果单独记录。

## 真机证据与限制

原资源测试见 [证据](../evidence/resources-20261102/README.md)。测试包括原生三线程 CNDrv 对照、真实 TCP 会话/进程字节配额、并发抢占、跨 session 对象拒绝、transfer 拒绝前数值不变、坏 image/native 失败回滚、pending H2D 跨 queue 的额度回收、EOF/reset、连接准入、partial frame/回复 backpressure、有限 kernel 在 queue 明确 NOT_READY 时 SIGTERM，以及 native cleanup 失败注入。最新 [核心回归](../evidence/remaining-closure-20261107/core/results.json) 76/76、[IPC 生命周期](../evidence/remaining-ipc-20261107/lifetime/results.json) 12/12、[一次 backing 计量](../evidence/remaining-ipc-20261107/quota/results.json) 2/2 通过；相关 Server 日志均记录资源和 workers 清零。

独立 `cnmon` 实测：原生三线程分配 40+40 MiB、free/再分配 20 MiB 的序列，初始物理使用 0 MiB，80 MiB 分配后为 80 MiB，全部 free 后仍为 112 MiB，reset 后 32 MiB，worker 退出后为 0。远程同序列复现相同值。数字属于该批设备环境，不能外推为所有 SDK 的固定缓存大小或缓存实现；证据仅证明逻辑 free 不等于物理驻留立即归零。首次测试错误地要求 free/reset 后物理为零，失败记录保留。

设备配额计请求字节，**不是 native 物理内存硬隔离**。Driver rounding/cache、context/queue/module 的内部设备内存、其他进程、host frame/response 缓冲、parser 解压临时内存和线程栈不在 byte counters 中；它们分别受协议长度、parser 上限、对象表及连接上限约束。只测量了所列序列的物理内存回收，没有逐类原生对象计数器或长期泄漏证明。

配额按 session 和当前 server 进程生效，IPC backing 在源 session 结束后仍计全局额度。没有认证身份、租户保留份额、跨进程/多设备分池、公平调度或单一客户端多连接约束；总额可被先来的合法 session 占满。每类 1024 个对象的既有硬上限保留，transfer 全局数量还受连接数×session 上限约束。M4.2/M4.3 的上述基础已实现并验证；完整生产资源隔离与任意 native 执行的有界取消不在当前单卡阶段的完成声明中。
