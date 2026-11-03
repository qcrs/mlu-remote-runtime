# Application workloads — 2026-11-03

MLU370-S4 / Neuware 3.5.2 / AArch64；同机服务端与客户端独立进程，经 localhost TCP 模拟两个终端。不是第二物理主机验收。普通 CNCC executable，不调用 remote helper，无 preset Kernel RPC 或名称特判。每个 workload 包含 CPU reference。

| Workload | Native | Replacement | Preload | MAX_ABS_ERROR | MISMATCH |
|---|---|---|---|---|---|
| matrix_add | PASS | PASS | PASS | 0 | 0 |
| naive_gemm | PASS | PASS | PASS | 0 | 0 |
| image_threshold | PASS | PASS | PASS | 0 | 0 |
| image_blur | PASS | PASS | PASS | 0 | 0 |

每种模式的 stdout/stderr 见同目录日志；[results.json](results.json) 保存返回码及 executable SHA256，同一程序三种模式 SHA256 相同。MLU_CHECKSUM 的命名不依赖运行模式。运行及两个终端命令见 [workloads README](../../workloads/README.md)。

发现并小幅修复 Runtime 缺口：CNRT fake VA 映射恰好连续且分配大小页对齐时，某个分配起点可能与另一分配 one-past 相同，inclusive lookup 会选错 allocation/offset，导致 Matrix Add 的合法 D2H 返回 cnrtErrorArgsInvalid。大 Host malloc 邻接映射也可能受同一歧义影响。每个 fake VA 映射尾部增加一个 PROT_NONE 保护页，保留既有 one-past/range/tombstone 语义，不改变协议或架构。原始 256×256 Matrix Add（普通 malloc Host 缓冲区）本身提供回归覆盖，修复后完整通过。没有扩展 API 或 Host completion/Symbol 能力。

实际验证：

- L0：[build](build.log)、[schema/codegen/contracts](l0.log)、git diff --check。
- L1：四个应用 × Native/replacement/preload，共 12 次，CPU 逐元素 reference；另因小幅 fake VA 修复运行 [受影响 Memory/query 边界](memory-boundaries/results.json)，11/11（含 owned allocation、offset、zero/one-past/stale、ownership、namespace 对照）。
- L2：一次现有 generic CNCC [core smoke](core-smoke.log)，malloc/H2D/generic launch/sync/D2H 数值通过。
- 按本轮明确要求未运行无关 transport fault、resource quota、restart、DSO stress、parser fuzz/full historical suite。

API Schema 和实现状态数量未变化。该批无性能优化或性能结论。
