# API Census / 功能族计划

由 `scripts/generate_compat.py` 从当前 SDK 声明及 Schema 生成。完整、部分、阻塞、范围外、拒绝、未实现是不同状态；剩余项的逐 API 依赖、阻塞和下一步见 `family-plan.json`。

| 功能族 | 公共声明 | 完整 | 部分 | 阻塞 | 范围外 | 拒绝 | 未实现 | 优先级 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| device_context | 45 | 45 | 0 | 0 | 0 | 0 | 0 | P0 |
| error | 6 | 6 | 0 | 0 | 0 | 0 | 0 | P0 |
| graph | 102 | 102 | 0 | 0 | 0 | 0 | 0 | P5 |
| host_memory | 14 | 6 | 0 | 8 | 0 | 0 | 0 | P2 |
| host_utilities | 9 | 7 | 0 | 1 | 1 | 0 | 0 | P3 |
| ipc | 12 | 8 | 0 | 4 | 0 | 0 | 0 | P5 |
| memcpy | 20 | 20 | 0 | 0 | 0 | 0 | 0 | P1 |
| memory | 42 | 36 | 0 | 3 | 2 | 1 | 0 | P1 |
| module_kernel | 13 | 13 | 0 | 0 | 0 | 0 | 0 | P2 |
| peer | 7 | 0 | 0 | 7 | 0 | 0 | 0 | P4 |
| queue_notifier | 40 | 40 | 0 | 0 | 0 | 0 | 0 | P1 |

P0：设备/context/版本基础查询；P1：owned memory、stride copy、queue/notifier；P2：Host completion 与 kernel/module ABI；P3：Host 数值工具；P4：多设备 Peer；P5：Graph/IPC。具体 API 可在同一功能族中按依赖再排序。

## Graph / IPC 分批实现契约

原生契约与逐批 Remote 状态见下表；共享状态机、SDK 特例和验收见 [设计](../docs/GRAPH_IPC.md) 与 [原生证据](../evidence/remaining-ipc-20261107/README.md)。G1 Remote 证据见 [owned DAG](../evidence/graph-g1-20261107/README.md)。native_test 中的 Notifier 证据证明创建前提缺失，并非成功导出/导入。

| 批次 | 能力 | API 数 | 已实现 | 依赖 |
|---|---|---:|---:|---|
| G1 | Owned DAG and node identities | 28 | 28 | 无 |
| G2 | Typed nodes and executable snapshots | 44 | 44 | G1 |
| G3 | Capture and replay | 10 | 10 | G1, G2 |
| G4 | Client callbacks user objects and output files | 20 | 20 | G1, G2, G3 |
| I1 | Process-wide memory sharing broker | 6 | 6 | 无 |
| I2 | VMM backing reservation and OS-handle transport | 2 | 2 | I1 |
| I3 | IPC notifier native prerequisite | 4 | 0 | 无 |

每批具体缺口及验收见 `family-plan.json` 的 `implementation_stages`；完成声明以逐 API 状态、boundary 和 Native/Remote 证据为准。
