# 单设备 API 兼容性收口

范围：MLU370-S4 单设备、Neuware 3.5.2、CNRT 6.5.2、CNDrv 2.5.2、AArch64→AArch64。`IMPLEMENTED` 限于逐 API boundary，不代表其他架构、SDK 或未声明的 API。

冻结基线 `dbf958a` 的 97 个 PARTIAL，逐项对照 SDK、实现和既有 Native/Remote 证据：77 项提升为 IMPLEMENTED、18 项保留 PARTIAL、2 项 OUT_OF_SCOPE。另将原先错误标完整的 `cnrtQueueDestroy` 降为 PARTIAL，7 个 Peer 入口由 NOT_IMPLEMENTED 改为 BLOCKED。审计提交时总数：121 完整、19 部分、7 阻塞、2 范围外、4 明确不支持、157 未实现。

[baseline.json](../evidence/closure-20261107/baseline.json) 保存原条目及源 commit，[review.json](../evidence/closure-20261107/review.json) 保存每项初始决策；Schema 的 `closure_review` 保留旧 boundary 和判定。后续修复用 `closure_resolution` 记录新的证据，不改写审计历史。`verify-closure-audit.py` 对照 git 中原始 Schema，检查无遗漏、重复、理由漂移和无证据提升。`api_codegen.validate` 强制每个 PARTIAL 有 gap/fix/acceptance，BLOCKED/OOS 有理由。

收口依据包括：默认 Queue 已经转发 native NULL，不再按旧文档认定为合成 Queue；Notifier flags、priority、跨 Queue wait 和 timing 已有真实对照；内存 node/zero-fill/kind、Context TLS/flags 和当前 CNCC image/module 生命周期的证据也已齐备。无需穷举的负例、多设备和其他 SDK 格式不再模糊阻塞当前单设备入口。

审计明确了统一异步 copy、立即 Queue destroy、Host 属性、模块计量和参数 relocation 的具体缺口。后续 Queue retirement 和 Host/deferred D2H 已按下面的 resolution 收口；模块计量及参数 relocation 等剩余项继续保留明确验收条件。

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

真正剩余 PARTIAL 为 casts 两项、`cnrtDeviceQueryKernelMemoryUsage`、`cnrtGetSymbolSize`、`cnrtInvokeKernel`、`cnInvokeKernel`、`cnModuleGetKernel`。Graph/IPC 的共享状态机尚待核心阶段后推进；Peer 的 7 项仍需第二设备。
