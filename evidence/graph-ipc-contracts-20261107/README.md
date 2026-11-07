# Graph / IPC 原生契约（2026-11-07）

MLU370-S4 单设备、Neuware 3.5.2、CNRT 6.5.2、CNDrv 2.5.2、AArch64 LP64。13/13 场景、74 个检查通过；本批只运行 Native，没有 Remote 数值测试或新增客户端导出。

## 复现与来源

```sh
python3 scripts/probe-graph-ipc.py
python3 scripts/generate_rpc.py
python3 scripts/generate_compat.py
python3 scripts/generate_rpc.py --check
python3 scripts/generate_compat.py --check
python3 scripts/verify-api-contracts.py
python3 scripts/verify-closure-audit.py
git diff --check
```

[runner](../../scripts/probe-graph-ipc.py) 将 [C probe](../../probes/probe_graph_ipc.c) 分别以 `gcc -std=c11 -O2 -Wall -Wextra -Werror` 编译，直接链接 vendor CNRT/CNDrv。显式设置 Native SDK lib64 并移除 LD_PRELOAD；不启动 Remote Server。runner 验证每个进程退出码、所有 check 及 IPC 两端 PID 不同。失败或超时会 kill/reap exporter，避免残留等待进程。

[results.json](results.json) 为汇总；[provenance.json](provenance.json) 保存时间、SDK 路径、基础 commit、C/runner/header/binary SHA-256 和编译命令。二进制保存在忽略的 `build/probes/`；[CNRT build](cnrt-build.log) 与 [CNDrv build](cndrv-build.log) 均无编译诊断。各 JSONL 包含 library version、PID、8-byte memory handle、调用状态与断言；配对 `.stderr` 保留 SDK 输出。

[verification.log](verification.log) 记录生成物检查、台账核对、310 项 API 契约、原 97 项 PARTIAL 审计及 diff 检查，均返回 0。I2 的 `prerequisite_apis` 同时列出 11 个 memory family 前提；本次实际调用的 8 个 VMM memory 入口只补充 Native 证据，状态仍为 NOT_IMPLEMENTED。

## 结果

| 场景 | 数量 / checks | 证据与实际观察 |
|---|---:|---|
| 数值 DAG | 2 / 24 | [CNRT](cnrt-graph.jsonl)、[CNDrv](cndrv-graph.jsonl)：3 nodes / 2 edges、查询与 dependency edits、clone 参数独立、instantiate snapshot；销毁两图后四次执行的 4096 bytes 为 17/51/34/17 |
| Relaxed capture | 2 / 14 | [CNRT](cnrt-capture.jsonl)、[CNDrv](cndrv-capture.jsonl)：NONE→ACTIVE→NONE，捕获 pinned H2D/D2H 不执行；销毁捕获图后，回放读取当前 source 42/43 |
| Notifier 原生前提 | 2 / 3 | [CNRT](cnrt-notifier.jsonl)、[CNDrv](cndrv-notifier.jsonl)：支持属性 0，合法 flags 12 创建返回 100050，未获得有效 handle |
| 同进程跨 Context IPC | 1 / 1 | [CNDrv](cndrv-ipc-same-process.jsonl)：export 成功，另一 Context 的 open 返回 100102 |
| 独立进程 Device IPC | 4 / 20 | CNRT→CNRT、CNDrv→CNDrv 及两种跨库组合；import/offset write/close 后 exporter 确认中间 85、两端各 64 bytes 保持 17 |
| 独立进程 Host IPC | 1 / 5 | [exporter](ipc-cnrt-to-cnrt-host-exporter.jsonl)、[importer](ipc-cnrt-to-cnrt-host-importer.jsonl)：CNRT HostMalloc 两端直接 CPU memcpy/memset 共享成功 |
| VMM POSIX FD | 1 / 7 | [exporter](vmm-cndrv-posix-fd-exporter.jsonl)、[importer](vmm-cndrv-posix-fd-importer.jsonl)：32 MiB granularity、create/map/access/export、SCM_RIGHTS、独立 exec import/map/write、unmap/release，数值及 guard 通过 |

Device IPC 日志按双方分别保存：

- CNRT→CNRT：[exporter](ipc-cnrt-to-cnrt-device-exporter.jsonl)、[importer](ipc-cnrt-to-cnrt-device-importer.jsonl)。
- CNDrv→CNDrv：[exporter](ipc-cndrv-to-cndrv-device-exporter.jsonl)、[importer](ipc-cndrv-to-cndrv-device-importer.jsonl)。
- CNRT→CNDrv：[exporter](ipc-cnrt-to-cndrv-device-exporter.jsonl)、[importer](ipc-cnrt-to-cndrv-device-importer.jsonl)。
- CNDrv→CNRT：[exporter](ipc-cndrv-to-cnrt-device-exporter.jsonl)、[importer](ipc-cndrv-to-cnrt-device-importer.jsonl)。

## SDK 特例与结论边界

Native 第二次 export 同 allocation 返回 CNDrv 100102 / CNRT 632015，第一次 handle 仍完成共享。头文件称重复导出无害，本批按实际 SDK 观察记录差异。普通 memory IPC handle 为 8-byte opaque pointer typedef；notifier IPC handle 在头文件中为 64-byte struct，二者不能混用。

VMM `cnMemExportToShareableHandle(void *)` 即使导出 POSIX FD 也写 **8 bytes**。探针预填 uint64 槽为 `a5a5a5a5a5a5a5a5`，实际得到 `000000000000000a`，前后 guards 保持；高 4 bytes 也被写零。不能以普通 4-byte int 输出槽封装此 SDK。FD 是通过 Unix socket `SCM_RIGHTS` 传递，不能将 Server FD 数字经 TCP 发送就当作客户端 FD。

普通 memory/Host/VMM IPC 已有 Native 正例，因此继续列为 NOT_IMPLEMENTED。4 个 notifier IPC get/open API 则因原生支持前提缺失标 BLOCKED；对应 native_test 指向的是 prerequisite probe，没有运行成功 get/open。解除条件为支持属性 1、合法 notifier 创建成功，随后验证独立进程 record/wait。

未测 Kernel/Host/child nodes、entity update、pending entity destroy、其他 capture 模式/线程交互、user objects、多个 importer 或源资源提前退出。正常 IPC 都由 importer close 后 exporter free，不能从这些成功结果推断 Native 在源 free/EOF 后仍安全。原生返回码与数值测试的区别保留在设计中。

本批 [Graph/IPC 设计](../../docs/GRAPH_IPC.md) 和 Schema 引入 G1～G4、I1～I3 分批契约；Graph/node/entity/capture、跨 session broker 与 VMM refs 均为待实现模型。Remote 仍需解决同进程 native IPC open 失败、Host CPU 共享域和客户端 FD transport，不能简单逐 API 转发。

状态为 145 IMPLEMENTED、0 PARTIAL、11 BLOCKED（7 Peer + 4 notifier）、2 OUT_OF_SCOPE、1 UNSUPPORTED、151 NOT_IMPLEMENTED。runtime 源码未变；最近完整核心回归 76/76 和功能族 15/15 属于提交 `58b7e6c` 的 [Host cast 批次](../host-cast-20261107/README.md)，不算作本批 Graph/IPC Remote 结果。
