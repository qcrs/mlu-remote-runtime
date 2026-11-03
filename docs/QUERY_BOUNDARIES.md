# Context、PCI、内存属性和 Queue 扩展

2026-11-02，基于 `8dbd336`。本包新增 24 个公共 API，9 完整、15 部分；没有把原有部分入口自动提升。310 项合计为 **45 完整、73 部分、4 明确拒绝、188 未实现**。完整表示当前 Neuware 3.5.2 / AArch64 / 单 MLU370-S4 已测范围，不表示替代整个 vendor runtime。

## 实现范围

| 功能族 | 新增 | 完整 | 部分及边界 |
|---|---:|---|---|
| PCI | 4 | `cnDeviceGetByPCIBusId`、`cnrtDeviceGetByPCIBusId` | 两个 GetPCIBusId 要求 len≥13；原生 len=12 仍写 13 字节，remote 拒绝；输入容量 64 字节，完整形式和无 domain 形式已测 |
| Context/config/cache | 8 | Driver GetApiVersion、GetConfig、SetConfig、GetCtxMaxParallelUnionTasks、ResetPersistingL2Cache | Runtime GetConfig/SetConfig/ResetPersistingL2Cache 按拥有的远程 Context 工作；shared-context/多线程 interop 未验收 |
| 内存 | 5 | `cnMemGetNodeInfo` | GetAddressRange、GetMemAttribute(s)、cnrtPointerGetAttributes 只支持 owned device allocations；Host/UVA/VMM 未实现 |
| Queue | 7 | `cnQueueGetContext` | 两套 Get/Set/CopyAttributes 显式 owned queues；NULL 属性调用拒绝；非零 persisting window 在本硬件不可用，故仍部分 |

[增量 JSON](../evidence/query-boundaries-20261102/change-counts.json) 列出全部 24 项。四组均真正经过 Client→codec→TCP→dispatch→native backend，未以导出数量替代执行证据。

## 自动生成与人工边界

Typed RPC 从 20 增至 **39**，公共 wrapper 从 21 增至 **44**。保留原 10 对 ObjectID RPC。23 个新增公共 wrapper 可从 Schema 生成；批量 `cnGetMemAttributes` 手写组合单项 typed RPC。新的19个 RPC 使用 opcode 53–71，没有改旧 opcode 或 framing。

生成器新增固定容量 `cstring`：Client 通过 `strnlen` 限长，Server 检查请求的 NUL，Client 在发布输出前检查成功回复的 NUL。固定长度但缺终止符的坏回复会 poison 连接，输出不变；PCI wrapper 另核对调用者的 `len`，拒绝已终止但超出输出容量的回复。数组输入也由生成器复制。`wrapper.session` 让普通查询按已解析 owner 选择会话，不修改客户端 TLS current。

默认仍只在成功时发布输出。Driver 的 invalid PCI 查询实际会将 device 写成 -1，CNRT 则保留原输出；前者使用显式 `native_status/u32` 结果字段，在有效 RPC envelope 内保留原生状态与输出。没有把失败的 transport 当作可发布结果。

编解码、响应形状、dispatch、固定 binding、导出 map、manifest、测试 plan/template 和兼容性台账均复用已有生成器。重复字段、opcode 和长度检查无需再跨 Client/Server 手写；人工工作集中到 SDK 输出宽度、状态、owner 路由、地址重定位和语义断言。生成代码行数见 artifact manifest，只用于复核产物，不折算人时。

`server/api_queries.inc` 负责内存属性与 Queue union 的 native 语义适配；`shim/driver_queries.inc` 负责 owner 查找和结果发布。它们复用现有 Context、ObjectID、allocation 表，不建立另一套资源系统。Census 的 backend_used 会扫描这些适配文件中的真实 native 调用。

## 地址、句柄与生命周期

内存查询需要 bound current context，但可以查询另一 live context 的 allocation：Client 由 fake VA 找 owner，向 owner session 发 AllocationID+offset。Context API 和 QueueGetContext 同样使用明确对象 owner；unbound 时 QueueGetContext(explicit queue) 仍成功，memory query 则返回原生 ContextInvalid。两个 context 的配置隔离已测。

内存属性中 device pointer、range start 和 context 不传 native 值：Server 验证其与 owned allocation/context 一致，返回 offset 或身份标记；Client 发布 fake VA/Context token。`CN_MEM_ATTRIBUTE_MAPPED` 在该 SDK 只写 **1 字节**；普通 enum/int 写 4 字节，地址/size/context 写 8 字节。测试检查每种宽度后的哨兵，避免 `void *pi` 被一律写 8 字节。

地址范围严格拒绝 one-past、stale、其他 session 的 AllocationID。可选 base/size 输出按 native 支持；节点内存查询也支持可选输出，动态 free memory 只比较不变量。

SDK 的 `cnGetMemAttributes` 会尝试全部条目，返回**最后一个条目的状态**，失败条目之后的成功输出仍会写入。手工批量 wrapper 保留此行为，每个 entry 复用单项的 native 查询/重定位；目前没有原子批量快照保证，也没有压缩成一个 RPC。

Queue accessPolicyWindow 按 base AllocationID、offset、bytes、ratio/f32、hit/miss enum 分字段编码。Get 返回窗口先在 Server 的 live allocation 表反查，再在 Client 反查 fake VA；释放后的窗口地址不会泄漏原生 VA，查询明确失败。Copy 只允许同 session 的两个 live queue。

本机 `cnQueueGetAttribute(NULL,…)` 发生 SIGSEGV；remote 在两端明确拒绝该路径。普通 default queue sync/query/notifier/launch 保留原实现。原生只接受 zero-size accessPolicyWindow，nonzero window 和非零 persisting cache 配置返回 InvalidValue；本包只把原生错误传回，未声称实际 cache policy 有效。零大小的带 offset base 窗口 Set→Copy→Get 仍可验证双向地址映射。

## 验证与后续

新增对照 11/11、原功能族 15/15、完整回归 59/59；包括 replacement、LD_PRELOAD 和隐藏 SDK/设备的独立 namespace。协议负例覆盖 cross-session、overflow、stale、one-past、错误 union tag、NULL queue、坏 string 和全部39个 typed RPC 的回复形状。复现命令、二进制哈希、原生探针和状态见 [证据](../evidence/query-boundaries-20261102/README.md)。

剩余按 [功能族计划](../compat/family-plan.md) 推进：

1. P0/P1：Context flags、线程配置、TLS device；Queue default ordering、nonzero window 在支持硬件上的数值/生命周期对照；memory mapped/VMM/pool 和更广 2D/3D。
2. P2：Host Memory 必须同时实现 client pinned ownership、server staging、TransferID completion/fetch、延后 writeback。Queue sync/query、Notifier wait/query、context sync/destroy/reset、host free 都必须规定写回时点和失败后的 ownership；不能以本地 malloc 或阻塞 D2H 宣称真异步。本包未改变 HostMalloc/FreeHost/D2HAsync 状态。
3. P2：复杂 Kernel ABI/Symbol 仍需 aggregate/indirect pointer 类型、initialized/constant/external globals、symbol 地址和 module 生命周期；保留已有 compiler metadata 与 BANG 数值回归。
4. P3/P4/P5：host 数值工具后置；Peer 缺双设备验证；Graph 缺 node/exec/异步生命周期，IPC 缺跨进程 ownership 和远程共享语义。CNNL 缺本机库。第二物理主机、TLS、公网认证、多租户和复杂 Fuzz 不作为本阶段门槛。


## 2026-11-03 续批：Context flags、默认 Queue 与边界

本批没有把查询 API 的成功误记为 Host Memory 或复杂 ABI 完成。CNRT 新增 `cnrtGetDeviceFlag` / `cnrtSetDeviceFlag` 的真实 wrapper：合法 flags 在无活动资源时接受，非法值和活动资源修改返回 native 错误；MLU370-S4 的 native 查询对合法 setter 保持 SPIN，因此 Schema 状态为部分实现。CNDrv 原有 TLS current Context 保持线程局部，并新增跨线程 Context 绑定、NULL Queue 异步复制对照。

复制 wire 在原 56-byte 请求后增加可选 async 标志。服务端同时接受旧请求，只有长度和 host payload 能唯一匹配 60-byte 形式时才解析该标志。QueueID 0 直接映射 native NULL Queue；默认队列的 H2D staging 绑定到默认队列 completion，队列同步后才回收。`cnMemcpyAsync`、`cnMemcpyHtoDAsync` 和 `cnMemcpyDtoDAsync` 均支持显式和 NULL Queue 的 D2D/H2D 范围；D2H 仍等待后写回复缓冲。

边界用例新增 CNRT/CNDrv 零长度分配、zero-byte one-past、非零越界、owned offset 和 default Queue 可见性。HostMalloc/FreeHost、真正异步 D2H writeback、pinned ownership、initialized/constant/external globals、aggregate/pointer graph 与 Host Symbol 仍保持原有部分或拒绝状态。

对照命令和逐项结果见 [续批结果](../evidence/query-boundaries-next-20261102/README.md) 与 [完整回归](../evidence/context-default-20261102-final/README.md)。
