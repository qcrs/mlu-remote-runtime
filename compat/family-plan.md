# API Census / 功能族计划

由 `scripts/generate_compat.py` 从当前 SDK 声明及 Schema 生成。完整、部分、拒绝、未实现是不同状态；剩余项的逐 API 依赖、阻塞和下一步见 `family-plan.json`。

| 功能族 | 公共声明 | 完整 | 部分 | 拒绝 | 未实现 | 优先级 |
|---|---:|---:|---:|---:|---:|---:|
| device_context | 45 | 23 | 13 | 0 | 9 | P0 |
| error | 6 | 2 | 2 | 0 | 2 | P0 |
| graph | 102 | 0 | 0 | 0 | 102 | P5 |
| host_memory | 14 | 0 | 0 | 2 | 12 | P2 |
| host_utilities | 9 | 0 | 0 | 0 | 9 | P3 |
| ipc | 12 | 0 | 0 | 0 | 12 | P5 |
| memcpy | 20 | 0 | 13 | 1 | 6 | P1 |
| memory | 42 | 9 | 11 | 1 | 21 | P1 |
| module_kernel | 13 | 1 | 6 | 0 | 6 | P2 |
| peer | 7 | 0 | 0 | 0 | 7 | P4 |
| queue_notifier | 40 | 10 | 28 | 0 | 2 | P1 |

P0：设备/context/版本基础查询；P1：owned memory、stride copy、queue/notifier；P2：Host completion 与 kernel/module ABI；P3：Host 数值工具；P4：多设备 Peer；P5：Graph/IPC。具体 API 可在同一功能族中按依赖再排序。
