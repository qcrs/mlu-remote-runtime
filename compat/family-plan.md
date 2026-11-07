# API Census / 功能族计划

由 `scripts/generate_compat.py` 从当前 SDK 声明及 Schema 生成。完整、部分、阻塞、范围外、拒绝、未实现是不同状态；剩余项的逐 API 依赖、阻塞和下一步见 `family-plan.json`。

| 功能族 | 公共声明 | 完整 | 部分 | 阻塞 | 范围外 | 拒绝 | 未实现 | 优先级 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| device_context | 45 | 40 | 0 | 0 | 0 | 0 | 5 | P0 |
| error | 6 | 6 | 0 | 0 | 0 | 0 | 0 | P0 |
| graph | 102 | 0 | 0 | 0 | 0 | 0 | 102 | P5 |
| host_memory | 14 | 4 | 0 | 0 | 0 | 0 | 10 | P2 |
| host_utilities | 9 | 5 | 0 | 0 | 0 | 0 | 4 | P3 |
| ipc | 12 | 0 | 0 | 4 | 0 | 0 | 8 | P5 |
| memcpy | 20 | 16 | 0 | 0 | 0 | 0 | 4 | P1 |
| memory | 42 | 25 | 0 | 0 | 2 | 1 | 14 | P1 |
| module_kernel | 13 | 11 | 0 | 0 | 0 | 0 | 2 | P2 |
| peer | 7 | 0 | 0 | 7 | 0 | 0 | 0 | P4 |
| queue_notifier | 40 | 38 | 0 | 0 | 0 | 0 | 2 | P1 |

P0：设备/context/版本基础查询；P1：owned memory、stride copy、queue/notifier；P2：Host completion 与 kernel/module ABI；P3：Host 数值工具；P4：多设备 Peer；P5：Graph/IPC。具体 API 可在同一功能族中按依赖再排序。

## Graph / IPC 分批实现契约

原生契约已验证，Remote 仍待实现；共享状态机、SDK 特例和验收见 [设计](../docs/GRAPH_IPC.md) 与 [证据](../evidence/graph-ipc-contracts-20261107/README.md)。native_test 中的 Notifier 证据证明创建前提缺失，并非成功导出/导入。

| 批次 | 能力 | API 数 | 依赖 |
|---|---|---:|---|
| G1 | Owned DAG and node identities | 28 | 无 |
| G2 | Typed nodes and executable snapshots | 44 | G1 |
| G3 | Capture and replay | 10 | G1, G2 |
| G4 | Client callbacks user objects and output files | 20 | G1, G2, G3 |
| I1 | Process-wide memory sharing broker | 6 | 无 |
| I2 | VMM backing reservation and OS-handle transport | 2 | I1 |
| I3 | IPC notifier native prerequisite | 4 | 无 |

每批具体缺口及验收见 `family-plan.json` 的 `implementation_stages`；该计划不新增 wire opcode、客户端导出或 IMPLEMENTED 声明。
