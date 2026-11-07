# 剩余 49 项 API 收口，2026-11-07

范围：MLU370-S4、Neuware 3.5.2、CNRT 6.5.2、CNDrv 2.5.2、AArch64→AArch64、单设备。Server/Client 为独立进程和地址空间，经 TCP 通信；Native 直接使用 vendor 库。isolated 模式隐藏客户端 SDK/设备节点。没有第二物理主机验收声明。

## 分类结果

| 原 49 项的归属 | 已实现 | 新增阻塞 | 新增范围外 |
|---|---:|---:|---:|
| Shared Context | 5 | 0 | 0 |
| Memory：VMM / merge / setters | 11 | 3 | 0 |
| Host：InvokeHostFunc / mapping / cache | 2 | 8 | 0 |
| 普通 memory/Host/VMM IPC | 8 | 0 | 0 |
| Symbol 地址及拷贝 | 6 | 0 | 0 |
| Queue callback / atomic | 2 | 0 | 0 |
| 其他工具：atomic / reshape / private export | 2 | 1 | 1 |
| 合计 | **36** | **12** | **1** |

当前全部 310 项：CNRT 119 IMPLEMENTED / 10 BLOCKED / 1 UNSUPPORTED，CNDrv 164 IMPLEMENTED / 13 BLOCKED / 3 OUT_OF_SCOPE。合计 **283 IMPLEMENTED、23 BLOCKED、3 OUT_OF_SCOPE、1 UNSUPPORTED、0 PARTIAL、0 NOT_IMPLEMENTED**。所有状态来自逐 API 人工边界及证据，未由导出或测试数量推导。

新增阻塞的原生依据见 [Native README](../remaining-native-20261107/README.md)；原 Peer 7、notifier IPC 4 仍阻塞。私有 cnGetExportFunction 范围外的原因是 SDK Impl Private 的异构扩展 ABI；不是因 Native 查找失败。

## 已验证语义

- Shared Context：同进程/设备稳定身份、计数 acquire、inactive flags、最后 release/reset、TLS current identity 与重新 acquire；shared/shared-life 四模式 stdout 与 Native 一致。
- VMM：generic handle、reservation、mapping 与 backing refs 分开；granularity/properties/access、offset-zero aliases、retain/release/remap、Context cleanup。非零 backing offset 在本 SDK 返回 Native 100050，未声称支持该路径。
- Symbol：module-owned AllocationID/fake VA，Runtime 四个 sync/async copy、Runtime/Driver 地址查询；H2D/D2H/D2D、byte offsets、默认/显式队列和真实 kernel 观察更新。reset/unload 撤销公开身份。
- 普通队列 Host/callback：原函数与 userData 在客户端执行，PID/顺序一致；此前普通和 Graph deferred D2H 在 Host callback 前发布。Graph Host 测试在 QSYNC 之前由 callback 检查整段输出。
- Atomics：aligned pinned Host 64-bit cell 映射到 Native pinned cell，支持 Native compare/request/read、跨队列唤醒、每 Context 独立侧通道及 40 cells。绑定后 CPU 更新须经 atomic API；镜像在 atomic read/Queue/Context completion 发布。raw CPU load/store 跨主机一致性和同指针跨 Context 不在声明范围。
- 未满足 atomic compare 的 abrupt EOF 及被 kill 的阻塞同步触发 ATOMIC_EOF_CANCEL，后续独立客户端仍可成功运行，Server 清理不挂起。内部 cnQueueAbort ABI 的原生探针见 [记录](../remaining-native-20261107/queue-abort.log)。
- IPC 的普通/跨库路径、Host 域、真实客户端 FD、源 Context/backing 生命周期、Graph refs 与配额见 [IPC README](../remaining-ipc-20261107/README.md)。源提前 free/reset/EOF 保留 backing 是 Remote 安全策略，不是 Native 未定义流程的差分声明。

callback 必须在 deadline 内返回，Runtime/Driver 重入拒绝。Host IPC 限同客户端主机、UID 与 Unix namespace。现有 Graph copy 大小、Host 目标 lifetime、image/参数 ABI 等边界继续以 Schema 和 [设计](../../docs/GRAPH_IPC.md) 为准。

## 验证结果

| 验证 | 结果 | 证据 |
|---|---:|---|
| Shared/VMM/Symbol/普通队列/Graph Host 四模式及 atomic EOF | 39/39 | [results.json](results.json)、[server.log](server.log) |
| IPC 正常四模式 | 24/24 | [IPC README](../remaining-ipc-20261107/README.md) |
| IPC 生命周期 / backing 配额 | 12/12 + 2/2 | [lifetime](../remaining-ipc-20261107/lifetime/results.json)、[quota](../remaining-ipc-20261107/quota/results.json) |
| Graph callback 回归 | 20/20 | [results](graph-regression/results.json) |
| Queue retirement 回归 | 12/12 | [results](queue-regression/results.json) |
| 完整核心回归 | 76/76 | [results](core/results.json)、[runner](core/runner.log) |
| 8 个真实 workload、19 组输入 | 57/57 | [results](workloads/results.json) |
| 全量 build | PASS | [build.log](build.log) |
| codegen / ledger / contracts / closure audit | 见最终检查 | [verification.log](verification.log) |

39 项包含 36 个四模式执行、abrupt、blocked-sync kill 和 after-eof；blocked-sync 的退出码 -9 是预期的 SIGKILL，runner 明确标 passed=true。probe 进程返回 0 与 API 返回成功分开判断。Workload 全部逐元素 CPU reference 与 guard 通过；GEMM/row_center 最大绝对误差 9.53674316e-7，heat diffusion 5.96046448e-8。

上述 Server 日志均记录 `DRAIN_COMPLETE workers=0 cleanup_errors=0` 与 `RESOURCE_TOTAL device=0 staging=0 image=0`。最新核心回归亦包括 transport/restart、资源 rollback/quarantine、parser 和生成契约检查。

复现命令（各 runner 自管 Server，IPC 正常/lifetime 使用单独运行的 Server）：

```sh
./scripts/build.sh
python3 scripts/verify-remaining.py --port 50341
python3 scripts/verify-graph-callbacks.py --out evidence/remaining-closure-20261107/graph-regression --port 50343
python3 scripts/verify-queue-retirement.py --out evidence/remaining-closure-20261107/queue-regression --port 50344
python3 scripts/verify-workloads.py --out evidence/remaining-closure-20261107/workloads --port 50345
python3 scripts/verify-transparent.py --out evidence/remaining-closure-20261107/core --port 50347
python3 scripts/generate_rpc.py --check
python3 scripts/generate_compat.py --check
python3 scripts/verify-api-contracts.py
python3 scripts/verify-closure-audit.py
git diff --check
```

[provenance.json](provenance.json) 为 39 项运行时的源码/二进制哈希；这些被记录文件在最终整理时仍相同。[final-state.json](final-state.json) 另记录整理时实现、Schema、测试、二进制和 SDK 头文件的哈希，不用它覆盖原运行来源。IPC lifetime 测试在原运行后增加 quota 分支，原哈希保留，区别见 IPC README。根目录 native_symbols-* 编译制品和较早的 remaining-* 探索证据保留，正式完成声明以本目录及 IPC/Native README 指定的结果为准。
