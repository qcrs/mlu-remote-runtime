# API Compatibility Ledger

`api-ledger.csv` 是当前安装 SDK 的公开 API 台账。`schema/mlu_api_schema.json` 保存人工审查结论，`annotations.json` 是生成的兼容视图；生成器不会根据函数名或导出存在自动提高支持等级。`summary.json` 给出准确分母、头文件 SHA-256、状态统计和独立统计的 6 个私有 BANG hooks。[功能族计划](family-plan.md) 与 `family-plan.json` 列出全部剩余 API 的优先级、依赖及 blocker。

```sh
python3 scripts/generate_compat.py
python3 scripts/generate_compat.py --check
python3 scripts/verify-api-contracts.py
python3 scripts/verify-closure-audit.py
```

生成器预处理 `cnrt.h` 和 `cn_api.h`（包括传递引入的 deprecated 声明），检查全部匹配声明，去重后生成 CSV，并分别核对 `build/shim/libcnrt.so` 与 `build/driver-shim/libcndrv.so` 的真实动态导出。CNRT 宏别名归一为预处理后的链接符号。CNDrv 行的 `backend_used=true` 仅表示服务端调用；独立 `client_export` 和人工状态才描述 Driver Shim 能力。

| 状态 | 含义 |
|---|---|
| IMPLEMENTED | 已实现，并在行内限定的单设备会话范围验证；不代表全部 SDK/架构认证 |
| PARTIAL_IMPLEMENTED | 当前范围内可继续修复的真实缺口；必须记录 gap、fix、acceptance |
| BLOCKED | 缺少实际硬件或 SDK 前提；逐行记录解除条件 |
| OUT_OF_SCOPE | 当前普通 BANG 单设备计算目标不需要的能力；逐行记录原因 |
| UNSUPPORTED | 当前 Shim 明确返回 NotSupport；不等于原生硬件不支持 |
| NOT_IMPLEMENTED | 尚无满足声明边界的 Remote 实现；导出存在也不能据此提升状态 |

`native_test` / `remote_test` 中不带目录的 `.log` 相对 `evidence/transparent-20261102/`；带批次前缀（例如 `shared-module-20261102/suite/`）则相对 `evidence/`。`not_run` 明确表示尚无对应证据，尤其不能把拒绝函数计为已实现。CNNL 和第二台主机的阻塞记录在总体能力矩阵中，二者不属于这两个头文件的 API 分母。

RPC 重复封装由 Schema 生成：原有 10 对对象包装/分派，加上当前 61 个 typed RPC 的请求/响应、编解码、成功形状、分派与 backend binding，以及 146 个公共 RPC ABI wrapper 和 4 个本地 error API、导出 map、清单和测试 plan。对象归属、异步 staging、模块存活、查询 owner 路由和参数重定位仍有手写部分。旧六算子 registry/manifests 继续由原生成器维护。详见 [自动化设计](../docs/API_AUTOMATION.md)、[查询边界设计](../docs/QUERY_BOUNDARIES.md) 和 [Host completion 证据](../evidence/host-async-20261107/README.md)。

当前收口以 MLU370-S4、Neuware 3.5.2、CNRT 6.5.2、CNDrv 2.5.2、AArch64→AArch64、单设备为范围。[closure audit](../docs/API_COMPATIBILITY_CLOSURE.md) 冻结原有 97 个 PARTIAL 并保存逐 API 决策；状态不由测试数量或导出符号自动推导。后续修复须附 Native/Remote resolution 证据。

当前为 **283 IMPLEMENTED、0 PARTIAL、23 BLOCKED、3 OUT_OF_SCOPE、1 UNSUPPORTED、0 NOT_IMPLEMENTED**。原剩余 49 项中，36 项完成，12 项有 Native 硬件/SDK 前提阻塞，1 项私有扩展查询范围外。Shared Context、VMM、Symbol、普通队列 Host/callback/atomics 与普通 memory/Host/VMM IPC 已有 [本批差分与生命周期证据](../evidence/remaining-closure-20261107/README.md)；Graph G1～G4 保持 102/102。

[IPC 证据](../evidence/remaining-ipc-20261107/README.md) 覆盖独立进程、两库交叉、替换库/preload/隔离客户端、源退役、Graph 引用、真实 FD 和单份 backing 配额。Host IPC 限同客户端主机、UID 与 Unix namespace；原子单元限经 API 更新的 pinned Host 64-bit cells；VMM 非零 backing offset 在本 SDK 返回 Native NotSupported。23 个阻塞为 Peer 7、notifier IPC 4、CPU mapping/cache 8、merge 1、属性设置 2、reshape 1。详见 [阻塞证据](../evidence/remaining-native-20261107/README.md)、[Graph/IPC 设计](../docs/GRAPH_IPC.md) 和逐 API boundary。
