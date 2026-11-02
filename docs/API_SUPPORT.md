# API 与能力支持入口

[兼容性台账 CSV](../compat/api-ledger.csv) 是逐 API 的当前记录；[Schema](../schema/mlu_api_schema.json) 保存人工审查，[annotations](../compat/annotations.json) 是生成视图，不能仅因出现导出就算已实现。[summary](../compat/summary.json) 提供头文件哈希及统计。

当前 SDK 的 130 个 CNRT 公共入口中：20 已实现、28 部分实现、3 明确 NotSupport、79 未提供导出。180 个 CNDrv 公共入口中：25 已实现、45 部分实现、1 明确 NotSupport、109 未提供导出；合计 45/73/4/188。服务端使用标记与客户端导出分别核对。6 个私有 BANG hooks 和 legacy cnrtInit 不计入公共 API 分母。新增 [查询功能族](QUERY_BOUNDARIES.md) 已接通 native backend。

台账的 IMPLEMENTED 限于行内的单设备会话、当前 SDK/CPU 等测试边界。未实现入口可能在 loader 阶段失败；部分实现不能视为原生所有语义完整兼容；明确拒绝也不能视为原生硬件不支持。

真实 image upload 和通用 launch 已实现，支持当前实测 CNCC/CNFB/ELF 及 primitive 参数，不再受六 preset 限制。一个 registration 现在持有一个 module 和多个 KernelID，已验证未初始化 device globals 的 Kernel 内部共享、reset/reload 和 DSO 生命周期；详见 [共享模块证据](../evidence/shared-module-20261102/README.md)。Host symbol API、初始化/constant globals、复杂参数、真异步 D2H、默认 queue/多设备尚有缺口。

按工程能力统计的 20 项矩阵、证据、阻塞和不支持原因在 [PROJECT_STATE](PROJECT_STATE.md)；各 API 的 native_test/remote_test 见 CSV。CNNL 无 SDK、第二物理主机无连接配置属于能力阻塞，不能混进 CNRT/CNDrv API 分母。[M1～M4 路线](../design/MILESTONES.md) 给出每项依赖与验收标准。

新增 [Driver Shim](DRIVER_SHIM.md) 已覆盖独立 native context/TLS、module 文件与内存上传、通用 launch、显式/NULL queue、基础 memory/notifier；context close 有 native cleanup ACK。异步 D2H、NULL queue 异步 copy、复杂 kernel 类型、其余 context/queue 模式仍有明确边界。不能据此声称 vendor CNRT 的全部内部 Driver 调用均可接管。
