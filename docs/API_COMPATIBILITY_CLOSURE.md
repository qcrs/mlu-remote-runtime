# 单设备 API 兼容性收口

范围：MLU370-S4 单设备、Neuware 3.5.2、CNRT 6.5.2、CNDrv 2.5.2、AArch64→AArch64。`IMPLEMENTED` 限于逐 API boundary，不代表其他架构、SDK 或未声明的 API。

冻结基线 `dbf958a` 的 97 个 PARTIAL，逐项对照 SDK、实现和既有 Native/Remote 证据：77 项提升为 IMPLEMENTED、18 项保留 PARTIAL、2 项 OUT_OF_SCOPE。另将原先错误标完整的 `cnrtQueueDestroy` 降为 PARTIAL，7 个 Peer 入口由 NOT_IMPLEMENTED 改为 BLOCKED。审计提交时总数：121 完整、19 部分、7 阻塞、2 范围外、4 明确不支持、157 未实现。

[baseline.json](../evidence/closure-20261107/baseline.json) 保存原条目及源 commit，[review.json](../evidence/closure-20261107/review.json) 保存每项初始决策；Schema 的 `closure_review` 保留旧 boundary 和判定。后续修复用 `closure_resolution` 记录新的证据，不改写审计历史。`verify-closure-audit.py` 对照 git 中原始 Schema，检查无遗漏、重复、理由漂移和无证据提升。`api_codegen.validate` 强制每个 PARTIAL 有 gap/fix/acceptance，BLOCKED/OOS 有理由。

收口依据包括：默认 Queue 已经转发 native NULL，不再按旧文档认定为合成 Queue；Notifier flags、priority、跨 Queue wait 和 timing 已有真实对照；内存 node/zero-fill/kind、Context TLS/flags 和当前 CNCC image/module 生命周期的证据也已齐备。无需穷举的负例、多设备和其他 SDK 格式不再模糊阻塞当前单设备入口。

审计明确了统一异步 copy、立即 Queue destroy、Host 属性、模块计量和参数 relocation 的具体缺口。后续批次已按下面的 resolution 收口；两个 cast 入口在 Host cast 续批按本机 SDK 支持矩阵收口。

## 审计时的待修复项

下面为审计快照；最新状态、完整 gap/fix/acceptance 见 [ledger](../compat/api-ledger.csv) 和 [family plan](../compat/family-plan.json)。

| API | 缺口 | 修复与升级验收 |
|---|---|---|
| cnrtGetErrorName / cnrtGetErrorStr | 名字不全，描述错误地返回名字 | 生成当前 SDK 的本地完整目录；所有声明值及未知值 Native/replacement/preload 一致 |
| cnGetErrorName / cnGetErrorString | 只验证 CN_SUCCESS，未知值输出语义待核对 | 全 CNresult 目录对照返回码、文本及失败输出 |
| cnrtMemcpy | 缺 NoDirection 推断 | 共享 Host/device 地址分类；四方向、offset 和 stale ownership 对照 |
| cnrtMemcpyAsync / cnMemcpyAsync | 缺推断，D2H 阻塞或拒绝 | 共享 submit/completion/fetch，queue/notifier/context 完成时 deferred writeback；有限 pending kernel 证明提交先返回 |
| cnrtQueueDestroy / cnDestroyQueue | 回收前同步违反立即返回约定 | 公开 ID 先退役，保留 native queue/staging 至完成；pending copy/kernel + notifier/context 验收 |
| cnrtPointerGetAttributes / cnGetMemAttribute / cnGetMemAttributes | 缺 Host registry 与属性 | Host/device/interior/batch 属性和释放生命周期对照 |
| cnrtDeviceQueryKernelMemoryUsage | CNRT 计量与 CNDrv 模块归属未对齐 | 比较 load/launch/unload/reset 前后 native usage，再接 owner 计量 |
| cnrtGetSymbolSize | initialized/constant 注册被拒 | 扩展共享变量 metadata；普通 CNCC globals 的 size 与 kernel 数值对照 |
| cnrtInvokeKernel | 嵌套 by-value pointer 未重定位 | 编译器提供字段 offsets，共享 AllocationID relocation；NULL/offset/nested kernel 数值验收 |
| cnModuleGetKernel / cnInvokeKernel | Driver extern-C/aggregate 缺可信 metadata | 复用编译器类型 metadata 和 relocation；extern-C/aggregate launch 与 unload ownership 对照 |
| cnrtCastDataType / cnrtCastDataType_V2 | by-channel、INT4/fixed 与量化矩阵缺项 | 扩展共享 Host conversion；类型/量化/rounding/saturation 数值对照 |

## 外部条件与下一阶段

7 个 Peer API 需要第二物理设备才能证明访问、copy 和 ordering；self-device 结果不算 Peer 完成。`cnMallocSecurity` 与 `cnMallocFrameBuffer` 的分配/普通 copy 已有证据，但 security/codec 消费语义不在当前普通 BANG 目标，标 OUT_OF_SCOPE。CNNL 库缺失是独立工程阻塞，不在这 310 个 CNRT/CNDrv 声明分母内。第二物理主机和生产认证/TLS 不作为本阶段门槛。

先修核心语义，再做 Graph/IPC。Graph 为 CNRT capture / CNDrv TaskTopo，需共享 capture、node、instantiate/update/launch 状态及句柄 ownership；IPC 需跨独立 session/process 的可导出身份、导入引用、关闭与源资源存活。当前两族未实现，不能靠同一 session 内转发伪造完成。

审计回归：[功能族 15 项和查询 11 项](../evidence/closure-20261107/README.md) 均通过，包括独立 TCP Server/Client、Native、替换库、preload、隐藏 SDK 和设备节点的 namespace。契约检查证明结构一致性，数值和语义证明来自差分运行。

## 2026-11-07 error/copy resolution

四个 error API 与 `cnrtMemcpy` 已收口，该批为 126 IMPLEMENTED、14 PARTIAL。错误目录生成覆盖两套枚举的 139 个不同数值及八个未知输入，支持离线初始化前调用；同步 NoDirection 支持 H2H/H2D/D2H/D2D。共享 fake-VA range helper 保留 tombstone/guard/owner 拒绝；CNDrv unified async 补入 H2D 与 staging snapshot。两库异步拷贝仍因 D2H completion/writeback 保持 PARTIAL。证据与完整回归见 [core-copy](../evidence/core-copy-20261107/README.md)。

## 2026-11-07 Queue retirement resolution

`cnrtQueueDestroy` 与 `cnDestroyQueue` 已收口，该批为 128 IMPLEMENTED、12 PARTIAL。公开 ID 立即失效，底层队列/staging 保留到完成；后续完成操作或创建队列触发收集，失败保留底层句柄并返回错误。CNRT `SyncDevice` 改为 Server context sync，包含已退役队列。

证据见 [Queue retirement](../evidence/queue-retirement-20261107/README.md)：两库 pending kernel/H2D/D2D、Notifier wait/query、context/free/module/cross-queue 完成，1200 次退役、pending reset/teardown 与 destroy 失败重试。Native pending destroy 在本 SDK 上可能使 Notifier 成功而输出仍是填充值；保留独立观测，Native 数值基线先等待，Remote 仍在 pending 状态调用 destroy 并验证最终数值。收口依据为 SDK 声明的延后释放契约及这些执行结果，不宣称复现该 SDK 的丢数据行为。收集发生在后续 API/teardown，没有后台回收时限或 native 取消保证。

## 2026-11-07 Host / deferred D2H resolution

5 个旧 PARTIAL（两库 unified async copy 和三个 pointer attribute 入口）已收口；新增完整实现 CNRT/CNDrv Host alloc/free 及 `cnMemcpyDtoHAsync`，当前为 138 IMPLEMENTED、7 PARTIAL、7 BLOCKED、2 OUT_OF_SCOPE、1 UNSUPPORTED、155 NOT_IMPLEMENTED。原审计 snapshot 不变，Schema 中逐项增加 `closure_resolution` 和本批证据。

共享 Host registry/lease、Server pinned Transfer ownership 和两个生成 RPC 提供真实 submit/completion/fetch：pending kernel 后的 D2H 提交先返回，成功完成 API 发布数据；default/显式 Queue、interior/pageable Host、跨 Context 和两库交叉 free、动态 buffer、释放与失败配额均已验证。没有后台 Host publication，pageable 地址由调用者保持存活；mapped/VMM/cache coherence 属于其他未实现 family，不能推导为完成。Native 数值基线对 SDK 可能取消 D2H 的生命周期操作先等待，Remote 则验证 pending drain；两种顺序明确分开记录。详见 [Host evidence](../evidence/host-async-20261107/README.md)。

该批结束时剩余 7 个 PARTIAL；其中五个 module/kernel 入口在下面的续批收口。

## 2026-11-07 Module / symbol / argument resolution

`cnrtDeviceQueryKernelMemoryUsage`、`cnrtGetSymbolSize`、`cnrtInvokeKernel`、`cnModuleGetKernel`、`cnInvokeKernel` 已按当前 CNCC 单设备 ABI 收口，当前为 143 IMPLEMENTED、2 PARTIAL、7 BLOCKED、2 OUT_OF_SCOPE、1 UNSUPPORTED、155 NOT_IMPLEMENTED。每项保留原 `closure_review`，新增 `closure_resolution` 和真机证据。

原生 CNRT 在注册时计入 image，reset 后仍计量，独立 CNDrv load/launch 不计入；因此改为客户端存活 registration 求和，缓存 native image estimate。DSO 引用、两 image、最终 unregister 与重新注册均已差分。普通 `__mlu_device__` 未初始化/初始化/const globals 返回 20/12/8 bytes，并在 reset/reload 前后取得相同 kernel 数值。

MLUABI02 明确提供自然 LP64 聚合参数的 pointer 字段 offset；两库共享 AllocationID+offset 打包，Server 校验 owner/range/NULL/alignment/order 后重建参数。Driver image 也可携带该 metadata，支持 extern-C/aggregate/zero args；v1 reader 和保守 primitive fallback 保留。嵌套 struct/array 数值、guard、scalar 位值、reset/reload、foreign/stale owner 与损坏 metadata/wire 均有验证。指纹只关联构建产物；device pointer graph、packed/opaque/未知 ABI 不包含在当前声明。详见 [批次证据](../evidence/module-accounting-20261107/README.md)。

剩余 PARTIAL 为 `cnrtCastDataType` 和 `cnrtCastDataType_V2`。Graph/IPC 的共享状态机尚待核心阶段后推进；Peer 的 7 项仍需第二设备。

## 2026-11-07 Host cast resolution

`cnrtCastDataType`、`cnrtCastDataType_V2` 已收口：145 IMPLEMENTED、0 PARTIAL、7 BLOCKED、2 OUT_OF_SCOPE、1 UNSUPPORTED、155 NOT_IMPLEMENTED。旧 `closure_review` 保留，新增实际组合、数值 boundary 和 `closure_resolution`。

共享 Host engine 覆盖 legacy plain/scalar 24/13 对、V2 plain/scalar 53/17 对，以及两版本 full-shape by-channel f32→i8/i16；参数持有自己的 shape/数组。7,507 个观察在 Native、replacement、preload、隐藏 SDK/设备客户端中逐字节一致，远端均不可达；支持/拒绝状态与源/目标 guards 一同校验。数值实现保留原生 rounding、offset 位解释、窄化和成功但不写目标的特例；未把 INT4/fix8/quant8 等 enum 声明当成 Native 支持。Native legacy i16 -32768→half 挂起缺陷不复制，有限数值认证边界见 [HOST_CAST](HOST_CAST.md)。

专项、UBSan、功能族 15/15 和核心 76/76 证据见 [host-cast](../evidence/host-cast-20261107/README.md)。核心 PARTIAL 已清零；继续推进 Graph/IPC 的共享状态机与跨进程 lifetime，不能据此宣称整个项目完成。

## 2026-11-07 Graph / IPC 原生契约阶段

新增双库 Native conformance harness，13/13 场景、74 个检查通过。覆盖数值 DAG、dependency/clone、instantiate snapshot、源图销毁后回放、relaxed capture 当次 Host source，以及独立 exec 的四种 device IPC 组合、CNRT Host CPU 共享和 VMM POSIX FD import/map/write。具体日志和边界见 [本批证据](../evidence/graph-ipc-contracts-20261107/README.md)。这批没有 Graph/IPC Remote 数值测试，runtime 实现与客户端导出不变，既有核心 76/76 属于上一提交 `58b7e6c`。

4 个 notifier IPC get/open API 由 NOT_IMPLEMENTED 改为 BLOCKED：本机 IPCNOTIFIER_SUPPORTED=0，两库合法 interprocess/disable_timing_all 创建返回 100050 / NotSupported，无法建立有效原生 handle。native_test 仅记录此创建前提，未把 get/open 拒绝或未运行算成成功实现。加上原有 7 个 Peer，当前为 145 IMPLEMENTED、0 PARTIAL、11 BLOCKED、2 OUT_OF_SCOPE、1 UNSUPPORTED、151 NOT_IMPLEMENTED；历史审计与 resolution 计数保持原样。

普通 memory/Host/VMM IPC 已有 Native 正例，仍为 NOT_IMPLEMENTED。当前 SDK 重复 export 同 allocation 失败，同进程跨 Context 的 IPC open 也失败；多线程 Server 因此需要 broker 及兼容 Context 的共享执行模型，Host CPU 共享和客户端 FD 传递亦需额外架构。VMM 的 void* FD 输出实测写 8 bytes，后续 adapter 必须保留本机 ABI 行为。

Schema 的 `graph_ipc_plan` 定义 GraphID/NodeID/EntityID/CaptureID 与 export/import/VMM refs 的计划模型，将两族逐 API 分为 G1～G4、I1～I3；生成器校验阶段唯一性、有序依赖和 family 归属，输出 [family plan](../compat/family-plan.md) 的分批缺口与验收。这些是待实现契约，不是已完成的共享 runtime 基础。下一阶段先建立 owned DAG 与 entity payload/lease，再接 capture replay、客户端 callback/user refs；IPC 先解决 process-wide backing/执行路由，再推进 VMM/OS handle。详见 [Graph/IPC 设计](GRAPH_IPC.md)。

## 2026-11-07 G1 owned DAG 收口

两库各 14 个 Graph API 已按 G1 boundary 升级：173 IMPLEMENTED、0 PARTIAL、11 BLOCKED、2 OUT_OF_SCOPE、1 UNSUPPORTED、123 NOT_IMPLEMENTED。Graph 余 74 项、普通 memory/VMM IPC 余 8 项；原 97 项 closure audit 与历史 resolution 不变。

新增共享有界 u64 arrays codegen、三个 Graph RPC 和全部 28 个生成式 wrapper；native DAG adapter 管理 GraphID/NodeID、父图校验、clone remap、查询和 reset/EOF 回收。14/14 专项及本批核心复跑见 [G1 证据](../evidence/graph-g1-20261107/README.md)。两库实际查询方向与名字相反，由 native 权威返回；stale 拒绝属于 Remote 安全边界。下一阶段为 G2 typed payload/entity snapshot/leases，G3/G4 和 IPC broker/VMM 仍待实现。

## 2026-11-07 G2～G4 Graph 收口

G2 的 44 个 typed payload/entity 入口、G3 的 10 个 capture 入口和 G4 的 20 个 callback/user-object/DOT 入口已按逐 API boundary 升级，Graph 合计 102/102。当前为 247 IMPLEMENTED、0 PARTIAL、11 BLOCKED、2 OUT_OF_SCOPE、1 UNSUPPORTED、49 NOT_IMPLEMENTED；历史审计与各批次计数保持原样。

共享能力包括 recursive child ownership、immutable kernel/Host/callback snapshots、allocation/module/invocation leases、逻辑线程 capture/frontier，以及客户端执行的 callback/destructor。callback 图在每个 Host copy 边界采样或写回再放行后继；native user refs 保活至最后引用并触发唯一析构。DOT 在客户端路径写入。callback 每 session 串行、须在 RPC deadline 内返回，Runtime/Driver 重入拒绝；歧义 entity setter、Host size 和 pageable lifetime 等边界见 [Graph/IPC](GRAPH_IPC.md)。

[G4 专项](../evidence/graph-callbacks-20261107/README.md) 20/20 与既有 269/269 回归通过，包含两库 Native/replacement/preload/隐藏 SDK 和设备的客户端、顺序/数值/PID、clone/update/child、无 wait 推进、refs/在途销毁、reset/EOF、配额回滚及提交断线。Server resources/workers/cleanup errors 清零，codegen、ledger、API contracts 和 closure audit 通过。普通 memory/Host/VMM IPC 仍属 NOT_IMPLEMENTED，需完成跨 session backing/Context 执行、同 client-host 域 CPU 共享和真实客户端 descriptor transport；11 个 BLOCKED 原因不变。
