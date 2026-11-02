# 查询与 Queue 边界验证

基线 `8dbd336aa5aa6b494cf63f592906ae158400e999`；设备为 MLU370-S4，AArch64，Neuware 3.5.2。新增 24 项公共 API（9 完整、15 部分），没有提升既有 API 状态。Census 保持 310 项，总计 45 完整、73 部分、4 拒绝、188 未实现。逐项差异在 `change-counts.json`，可执行制品和生成代码行数/哈希在 `artifact-manifest.json`。

Context/PCI、owned allocation 属性与 range、Queue 属性 union 共新增 19 typed RPC 和 23 generated wrapper；`cnGetMemAttributes` 为手写批量组合。累计 39 typed RPC、44 wrapper。复用既有 session、Context、ObjectID、fake VA 与 AllocationID+offset。人工 adapter 保留 CN SDK 字段宽度、owner routing、批量状态和反向重定位语义。逐 API 支持范围、自动/手写边界与下一优先级见 [`docs/QUERY_BOUNDARIES.md`](../../docs/QUERY_BOUNDARIES.md)。

## 测试

- `results.json`：11/11 成功，包括 CNRT/CNDrv native、replacement、LD_PRELOAD 和隐藏 SDK/设备的 isolated namespace；另有协议、ownership、所有 typed RPC 坏回复测试。
- `family-regression/results.json`：原 API 功能族 15/15。
- `full-suite/results.json`：保留回归 59/59，包括六个 preset、BANG C、Driver、资源与故障测试。
- `contracts.log`：310 项 schema/protocol/backend/export/ledger/evidence 一致性检查通过。
- `build.log`：构建输出。
- `native-probes.json`：常规 SDK 语义探针成功。`native-null-attribute.log` 的 `-11` 是独立原生进程中调用 `cnQueueGetAttribute(NULL, ...)` 的预期 SIGSEGV；该调用单独隔离，remote 不崩溃而是返回错误。

重跑命令：

```sh
./scripts/build.sh
python3 scripts/verify-query-boundaries.py
python3 scripts/verify-api-families.py --out evidence/query-boundaries-20261102/family-regression
python3 scripts/verify-transparent.py --out evidence/query-boundaries-20261102/full-suite
python3 scripts/generate_compat.py
python3 scripts/verify-api-contracts.py
python3 scripts/summarize-api-change.py --baseline 8dbd336 --out evidence/query-boundaries-20261102
```

测试在本机原生 SDK/设备、两个进程 TCP、preload 和独立 namespace 下完成，无第二物理主机。本机属性：`CN_MEM_ATTRIBUTE_MAPPED` 输出 1 字节，其他枚举/整数 4 字节，地址/size/context 8 字节；SDK 的 `cnGetMemAttributes` 返回最后一项状态且逐项尝试；PCI bus ID 原生 len=12 会写 13 字节，因此 remote 要求至少 13 字节并验证调用者容量。所有状态与限制按此测试环境记录，生成/导出本身不提升完整状态。

日志保留诊断文本和数值，规范化了工具输出中无语义的行尾空格，以便通过 Git whitespace 检查。
