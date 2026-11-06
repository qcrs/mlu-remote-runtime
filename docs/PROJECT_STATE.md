# 当前工程状态

更新：2026-11-03。主线已经从预置算子 RPC 扩展到原生 CNRT ABI、CNCC 注册 hooks、真实 image 上传和通用 launch。旧 TCP、Session、对象表及六 preset 保留。

本阶段已交付 [API 自动化与功能族扩展](API_AUTOMATION.md)：新增 30 个公共 API（15 完整、15 部分），将 5 个原拒绝入口接入 native backend（3 完整、2 部分），并补齐已有 queue/notifier 创建选项。完整支持判定限定在当前 SDK、单设备会话和台账边界。新增功能族 15 项、原有完整回归 59 项均通过；[证据与增量计数](../evidence/api-expansion-20261102/README.md) 可复核。

续批已交付 [Context/PCI、内存属性与 Queue 边界](QUERY_BOUNDARIES.md)：在 `8dbd336` 上新增 24 个公共 API（9 完整、15 部分），无旧入口状态提升。新增对照 11/11、原功能族 15/15、完整回归 59/59 通过；[增量和证据](../evidence/query-boundaries-20261102/README.md) 可复核。本批继续补齐 Context flags、默认 Queue 和内存边界。续批前台账为 45 完整、76 部分、4 拒绝、185 未实现。

2026-11-03 增加 CNDrv `cnGetErrorName` / `cnGetErrorString` typed RPC 和生成 wrapper。Server 按 CNDrv native out-string ABI 查询并复制有界文本，client 返回线程局部副本；当前只验证 `CN_SUCCESS`，因此两项标 PARTIAL。当前台账为 45 完整、78 部分、4 拒绝、183 未实现。typed RPC 为 40、公共 wrapper 为 46、对象 RPC 仍为 10 对；Native/Remote 证据见 [本批记录](../evidence/api-expansion-20261103/README.md)。

MLU370-S4 / AArch64 / Neuware 3.5.2 真机运行通过；同一 CNCC binary 的 native、replacement、LD_PRELOAD 均通过。独立 namespace 隐藏 SDK 和 `/dev` 后客户端仍通过。按用户后续要求，本轮采用本机双终端/namespace 模拟无 NPU 客户端，另已通过普通 kernel 和完整 ABI 程序，见 [本机模拟证据](../evidence/driverless-local-20261102/README.md)。**第二物理主机的部署验证仍是后续独立项，最终跨主机目标尚未全部完成。**

## API 统计

分母为当前 SDK 预处理后的公共声明，含传递 deprecated 声明，宏别名按链接符号归一；详见 [逐 API 台账](../compat/api-ledger.csv)。

| 层 | 已实现 | 部分实现 | 明确不支持 | 未实现 | 合计 |
|---|---:|---:|---:|---:|---:|
| CNRT client ABI | 20 | 30 | 3 | 77 | 130 |
| CNDrv client ABI | 25 | 48 | 1 | 106 | 180 |

另有 6 个私有 `__bang*` hooks；legacy `cnrtInit` 不在当前公共头文件分母内。CNDrv 的 backend_used 与 client_export 分别核对，只有人工审查与原生/远程证据支持的入口才提升状态。未实现入口可能 loader 失败；明确不支持入口返回 NotSupport。

## 能力统计

下面固定 20 个工程能力项：已实现 7、部分实现 9、阻塞 2、明确不支持 2、未实现 0。这里的工程能力与上面的 API 数量是不同分母；已实现仍限定在列明测试范围。

| 能力 | 状态 | 范围及证据 |
|---|---|---|
| TCP framing / session | 已实现 | 独立进程、序号及长度验证；首包强制 HELLO，完整合法后才创建 native context，重复握手拒绝；`server-transport-deadlines.log` |
| Client 原生依赖解耦 | 已实现 | 纯 core 无 SDK 构建；运行不依赖 Driver；`client-core-build.log`、`isolated-client.log` |
| 原生 CNRT ABI | 部分实现 | 48 个有实现的公共入口及 3 个明确拒绝入口；详见 API 台账 |
| 原生 CNDrv client ABI | 部分实现 | 独立 libcndrv.so；context/TLS、立即 module load、通用 launch、显式/NULL queue、基础 memory/notifier、Driver error text；[范围和证据](DRIVER_SHIM.md)，仍非完整 vendor CNRT 底层替代 |
| Kernel 注册及远程加载 | 部分实现 | 当前 CNCC wrapper / ELF64 LE / CNFB v2 单 image；一个 registration/module→多个 KernelID；DSO 20 次重载、pending-work unload、reset/reload 已测 |
| 通用 kernel launch | 部分实现 | 新增 compiler IR→host ELF metadata；C/enum/template/zero args、scalar/fake VA、offset/NULL 已测；原多 kernel/globals 保留；pointer-free byval/nested struct/array 已测；嵌入 pointer/pointer graph/Host symbol 未完成 |
| 内存与 fake VA | 已实现 | owned allocation+offset；free/reset 失效；VA tombstone 防旧指针复用；`native_bang_abi-replacement.log` |
| H2D staging / D2D enqueue | 已实现 | native/remote 对照、1200 async H2D；staging 完成前保持存活 |
| 真异步 D2H | 部分实现 | 有正确数据回传，但回复前阻塞同步；pinned/writeback 未实现 |
| Queue / Notifier | 部分实现 | 真实 priority/range、flags、跨 queue wait、timing；新增 owner context 与属性 union 双向重定位；NULL 属性明确拒绝，非零 window/default ordering 仍待验收 |
| 所有权、生命周期和异常清理 | 部分实现 | 跨 session/stale/reset/unload/EOF/restart；配额预留/回滚、pending H2D、native 错误逐项报告、失败 quarantine、worker drain 已测；长期计量及 native 取消待做 |
| 台账与 RPC codegen | 已实现 | 310 个声明及功能族依赖；10 对对象 RPC + 40 typed RPC + 46 公共 wrapper；Protocol/Export/Ledger/evidence 契约检查 |
| 回复校验与故障连接失效 | 已实现 | 短成功回复、错误形状、截断、序号错误、socket 超时/kill/restart 拒绝且不重放；`client-response-faults.log`、`client-transport-deadlines.log`、`server-restart.log` |
| 原有六 preset | 已实现 | 六组数值、TransferID、manifest 验证；`operator-test.log` |
| pinned/Host symbol/复杂 kernel ABI | 明确不支持 | pinned、复杂 launch ABI 显式拒绝；Host symbol API 尚无导出，加载或调用时失败；initialized/constant/external globals 注册拒绝 |
| CNNL 原生执行 | 阻塞 | 当前 SDK 无 cnnl.h/libcnnl.so；已有 seam 明确拒绝，`cnnl-test.log` 不算 CNNL 成功 |
| 多设备语义 | 部分实现 | 初始 ordinal/select/reset 有实现，仅一物理设备；TLS device 尚未实现 |
| 跨第二物理主机验收 | 阻塞 | 无第二主机地址/连接配置；本机独立 TCP 和 namespace 证据不替代此项 |
| 生产连接与部署管理 | 部分实现 | 已有 connect/RPC/首包/收发 deadline、连接/设备字节/staging/image/transfer 配额、SIGINT/SIGTERM drain；DNS/native 取消、认证/TLS、租户公平隔离、显式应用恢复仍待做 |
| 异构 CPU / 未知 SDK ABI | 明确不支持 | scalar LP64 LE，只验收 AArch64→AArch64；新 ABI 先做 M1 |

## 测试结论与依赖

最新 [查询边界 11 项](../evidence/query-boundaries-20261102/results.json)、[原功能族 15 项](../evidence/query-boundaries-20261102/family-regression/results.json) 和 [完整 59 项](../evidence/query-boundaries-20261102/full-suite/results.json) 均通过。此前 [API 扩展 15 项](../evidence/api-expansion-20261102/results.json) 与 [完整回归 59 项](../evidence/api-expansion-20261102/full-suite/results.json) 均返回期望值；保留之前 [58 项 Driver 批次](../evidence/driver-shim-20261102/final-suite/results.json)、[48 项资源批次](../evidence/resources-20261102/final-suite/results.json)、[42 项连接加固批次](../evidence/transport-20261102/final-suite/results.json)、[38 项共享模块批次](../evidence/shared-module-20261102/suite/results.json) 和 [29 项基线](../evidence/transparent-20261102/results.json)。CNNL 项验证的是明确拒绝。六组 CNCC 程序与普通 Driver 程序的 native/replacement/preload、隔离客户端、ABI/线程/1200 launch/copy、共享 module/globals、DSO 生命周期、协议与故障注入、六 preset、ASan/UBSan parser 均通过。

新一轮 [Driver Shim 回归](../evidence/driver-shim-20261102/README.md) 在此前基础上新增普通 Driver 程序的 native/replacement/preload、语义逐行比较、隔离客户端、Driver 协议、坏回复/EOF/configuration 故障和依赖检查。真实 CNDrv NULL queue 的 kernel/notifier/sync/query 已接入，pending NULL queue 在 module unload/context close 时完成清理；同步 copy 与 NULL queue 异步 copy 已由可选 wire async 位区分；真异步 D2H/pinned writeback 仍明确拒绝。

新增 14 组 Client deadline/配置故障测试、21 组真机 server 握手/收发/另一客户端继续工作检查、3 组暂停/kill/restart 检查，以及 entropy 失败/2048 并发 registry 身份检查均通过。首次 restart 测试实际复现了跨进程旧 AllocationID 重复，修复前后证据均保留。配置、时间保证范围与未完成项见 [连接生命周期](TRANSPORT_LIFECYCLE.md)。

资源与关闭新增 [19 组真机集成检查](../evidence/resources-20261102/final-suite/server-resources/results.json) 及两个 backend 失败注入模式：并发抢占全局配额不超额、拒绝前不改变 kernel/内存状态、坏 image/native 失败回滚、reset/EOF 恢复、pending kernel/H2D 和 socket backpressure 的关闭等待、context 清理失败停止准入并非零退出。D2H wait 失败不会留下向回复缓冲写入的异步任务。纯 native 三线程与远程 `cnmon` 对照同样观察到 free 后 112 MiB、context reset 后 32 MiB、worker 退出后 0；配额计请求字节，不能当作物理硬隔离或长期无泄漏证明。见 [资源生命周期](RESOURCE_LIFECYCLE.md) 和 [本轮证据](../evidence/resources-20261102/README.md)。

编译器 double 参数崩溃与 native 多线程 context-sync/queue-teardown 竞态保留了独立失败日志；没有把它们隐藏成通过。详见 [证据索引](../evidence/transparent-20261102/README.md)。

共享 module→多个 kernel、typed CNDrv Shim 和 compiler 类型 metadata 基础已落地，M3.1/M3.5 仍有并发卸载、跨 DSO image 引用、更广 globals/context/default-queue 语义待验收。Context flags/TLS、QueueID 0 默认队列顺序、零长度/one-past/owned allocation 边界已补入 [续批证据](../evidence/query-boundaries-next-20261102/README.md)，并通过完整透明回归 [证据](../evidence/context-default-20261102-final/README.md)。CNRT flags 和 default Queue 仍按单设备/当前 SDK 语义保留部分实现；下一阶段推进 Host memory/completion/writeback、嵌入 pointer aggregate 与 symbol ABI。Graph/IPC/Peer 按依赖推进；第二物理主机及生产安全体系不作为本阶段门槛。

## 2026-11-06 Memory allocation expansion

新增 CNDrv allocation family 的七个入口：Node、zero-fill、constant、security 和 framebuffer 变体统一进入现有 AllocationID/fake VA/owner/range/free 路径；`cnMemcpy` 增加由 registry 推断 H2D/D2H/D2D/H2H 的同步适配。当前设备 Native/Remote 证据覆盖 zero-byte 输出保持、zero-fill、offset、owner、invalid node、stale free 和 unified copy。七个入口与 `cnMemcpy` 仍标 PARTIAL：node>0、多设备、VMM/shareable handle、security/codec 专用语义和 foreign mapping 未验收。`cnrtDeviceQueryKernelMemoryUsage` 已接 typed RPC，但按空上下文查询证据保留 PARTIAL。

## 2026-11-07 Context configuration expansion

新增 CNDrv `cnGetCtxConfigParam_pt` / `cnSetCtxConfigParam_pt` 的 typed context RPC。Native/Remote 单设备对照覆盖 visible cluster、visible cluster number、union limit 查询，以及 union limit set/get；共享 TLS、visible-cluster setter 和多设备配置仍为 PARTIAL。证据见 [context config](../evidence/api-expansion-20261107/README.md)。

## 2026-11-08 BANG symbol size

`cnrtGetSymbolSize` 已接入现有 `__bangRegisterVar` metadata，支持当前 CNCC 注册的未初始化 `__mlu_device__` 变量；Native/Remote/preload 对同一 Module 的 `module_state[4]` 返回 16。Host symbol address、symbol copy、initialized/constant/external globals 和 pointer relocation 仍保持 PARTIAL/未实现。见 [symbol evidence](../evidence/api-expansion-20261108/README.md)。

## 2026-11-09 Host datatype conversion

`cnrtCastDataType` 已补充 host-local 的 FLOAT32/INT32/UINT8 子集：identity、signed nearest 和 UINT8 clamp/truncate，Native 与 remote shim 数值一致。量化参数、V2、half/int4/fix/quantized 类型和 rounding mode 继续保持 PARTIAL。见 [cast evidence](../evidence/api-expansion-20261109/README.md)。

## 2026-11-10 Quantization parameter lifecycle

补充 `cnrtCreateQuantizedParam` / `cnrtDestroyQuantizedParam` 的 host-local opaque 生命周期管理，并保持 `cnrtCastDataType` 已验证数值子集。带参数 quantized cast、by-channel 参数和更多数据类型仍为 PARTIAL。见 [quant evidence](../evidence/api-expansion-20261110/README.md)。

## 2026-11-11 By-channel quantization lifecycle

补充 `cnrtCreateQuantizedParamByChannel` 的 host-local 参数复制和销毁校验，Native/Remote shim 对照通过。实际 by-channel cast 数值语义和完整 SDK 类型矩阵继续保持 PARTIAL。见 [by-channel evidence](../evidence/api-expansion-20261111/README.md)。

## 2026-11-12 Parameterized INT8 cast

`cnrtCastDataType` 增加已观测的 FLOAT32↔INT8 scalar quantization 子集：`scale / 2^pos`、signed nearest rounding、INT8 clamp；Native/Remote shim 对照通过。offset 当前 SDK 路径未观察到作用，V2/half/int4/fix/by-channel cast 和更广反量化语义仍为 PARTIAL。见 [parameterized cast evidence](../evidence/api-expansion-20261112/README.md)。

## 2026-11-13 V2 datatype conversion

`cnrtCastDataType_V2` 已补充 host-local FLOAT/INT/UCHAR 子集，支持 round-nearest、toward-zero、down、up、round-off-zero、round-math 六种模式；Native/Remote shim 对照通过。half/bfloat/64-bit/quantized 继续为 PARTIAL。见 [V2 cast evidence](../evidence/api-expansion-20261113/README.md)。

## Application Workloads

已验证 Vector/Tensor elementwise、Matrix Add、Naive GEMM、Image pointwise（threshold）、Image neighborhood（3×3 blur）。四个新增应用均使用普通 BANG C Kernel、CNRT Memory 与 Queue，CPU reference 对照，Native/replacement/preload 运行同一 executable。运行方式和两个终端模拟本地/远程见 [workloads](../workloads/README.md)，数值结果见 [证据](../evidence/workloads-20261103/README.md)。本机独立进程经 TCP 验证，不代表第二物理主机验收。
