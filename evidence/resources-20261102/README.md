# 资源配额、原生清理和关闭证据

2026-11-02，AArch64 / MLU370-S4 / Neuware 3.5.2。沿用 TCP、Session、对象表、CNRT Shim、用户 image 和共享 module，保留旧六 preset。新增资源配额、失败回滚、清理失败隔离和 SIGINT/SIGTERM worker drain；语义及默认配置见 [资源生命周期](../../docs/RESOURCE_LIFECYCLE.md)。

## 最终验证

[final-suite/results.json](final-suite/results.json) **48/48 返回期望值**。[build-final.log](build-final.log) 是包含本轮最终生产代码及新 BANG 程序的完整构建；[final-suite-run.log](final-suite-run.log) 是运行索引。

```sh
./scripts/build.sh
python3 scripts/generate_rpc.py --check
python3 scripts/generate_compat.py
python3 scripts/verify-transparent.py --out evidence/resources-20261102/final-suite
```

默认 suite 端口为 50192；资源/关闭测试自行选择临时端口且只停止自己启动的 server。本批没有第二台物理主机。

六组普通 CNCC 程序分别 native/replacement/preload 成功，包含新 [有限 kernel 原生对照](final-suite/native_bang_drain-native.log)；三个隐藏 SDK/设备的隔离客户端、共享 globals/DSO、1200 次 launch/copy、所有权/重启、故障注入、六 preset 及 parser ASan/UBSan 6000 次 mutation 均回归通过。CNNL 项验证的是 SDK 缺失下的明确拒绝，不算原生 CNNL 成功。

| 能力 | 原始证据及结果 |
|---|---|
| 配额、原生对照与关闭 | [19 组 results](final-suite/server-resources/results.json)、[运行日志](final-suite/server-resources.log)：6 个非法配置在监听前失败；session/进程设备、staging、image、transfer、连接准入与释放恢复 |
| 并发全局额度 | [concurrent-reservations.log](final-suite/server-resources/concurrent-reservations.log)：四个 session 各申请 40 MiB，96 MiB 总额下恰为两成功、两 RESOURCE_LIMIT，释放后恢复 |
| 拒绝前不提交 | [quotas.log](final-suite/server-resources/quotas.log)：第 5 个 transfer 的 kernel/copy 被拒；设备状态/读回证明未执行；UINT64_MAX 无整数回绕；坏 image 重复回滚与 unload/reset/EOF 恢复 |
| pending H2D snapshot | [staging-total.log](final-suite/server-resources/staging-total.log)：两个真实忙 queue 竞争全局 staging，完成前额度与缓冲保持，完成后可再次使用 |
| pending kernel/H2D 的 SIGTERM | [pending-drain.log](final-suite/server-resources/pending-drain.log)：signal 前 queue 实测 CN_ERROR_NOT_READY=1，等待有限 kernel 和后续 H2D 完成，约 1.7659 秒退出；随后 cnmon 为 0 MiB |
| socket 关闭与 fd 复用 | [admission.log](final-suite/server-resources/admission.log) 及同目录 drain-0..5.log：pre-HELLO 纳入准入额度；idle/partial frame/发送 backpressure 在 SIGTERM 后约 0.0073 秒完成；6 次 SIGINT 关闭通过 |
| Native 失败回滚与 D2H | [backend-resource-rollback.log](final-suite/backend-resource-rollback.log)：注入 cnMalloc、H2D enqueue、module load 和 queue wait 失败；成功调用实际走 CNDrv；D2H wait 失败保持 host output，pending staging 保留；context 成功销毁后退回额度 |
| Cleanup 双失败隔离 | [backend-resource-quarantine.log](final-suite/backend-resource-quarantine.log)：注入 queue sync/context destroy 双失败，保留 16-byte staging 及额度至进程退出；[failed-context-cleanup.log](final-suite/server-resources/failed-context-cleanup.log)：server 停止准入、报告原生失败、exit 1 |
| 正常总量归还 | [suite server.log](final-suite/server.log) 末尾 DRAIN_COMPLETE workers=0 cleanup_errors=0，RESOURCE_TOTAL 三类均为 0；这是逻辑计量，物理观察另见下文 |
| 无 SDK 客户端 | [isolated-client.log](final-suite/isolated-client.log)、[shared-module](final-suite/isolated-shared-module.log)、[DSO](final-suite/isolated-module-dso.log)、[依赖](final-suite/dependencies.log)、[strace](final-suite/client.strace) |

## 物理驻留与逻辑释放的区别

`tests/server_resources.py` 使用 ctypes 直接调用 CNDrv，在三个原生线程/context 上建立与远程相同的分配序列，独立运行 cnmon；这组对照不依赖 RPC 或 backend byte counter。

| 观察点 | 纯 native MiB | 远程 MiB |
|---|---:|---:|
| 开始 | 0 | 0 |
| 分配 40+40 MiB | 80 | 80 |
| 先 free A，C 再分配 20，最后全部 free | 112 | 112 |
| 三个 context reset/destroy | 32 | 32 |
| 三个 worker 退出 | 0 | 0 |

原始 cnmon：同目录 [native-free](final-suite/server-resources/native-free-cnmon.log)、[native-reset](final-suite/server-resources/native-reset-cnmon.log)、[native-worker-exit](final-suite/server-resources/native-destroy-cnmon.log)、[remote-free](final-suite/server-resources/after-free-cnmon.log)、[remote-reset](final-suite/server-resources/after-reset-cnmon.log)、[remote-worker-exit](final-suite/server-resources/after-quota-server-exit-cnmon.log)。不能据此推断所有 SDK 有固定大小缓存；这里只证明本批环境的逻辑 free 不等于物理立即归零，且所列序列的线程退出后驻留归零。

## 保留的失败与阶段记录

- [attempt-1-run.log](attempt-1-run.log) 和 [attempt-2-run.log](attempt-2-run.log)：测试分别错误地要求 free、context reset 后物理为零，暴露 112/32 MiB 驻留；补充纯 native 对照后更正验收口径。
- [attempt-3-run.log](attempt-3-run.log)：测试将 native NOT_READY 预期误写为 100051，原生实际为 1，已更正。
- [build-attempt-4.log](build-attempt-4.log)：新程序把 cnrtQueueQuery 误拼为 cnrtQueryQueue，构建失败。[attempt-4](attempt-4/results.json) 虽通过资源测试，但使用旧 drain 产物，不能作为最终新程序构建证据。
- [resource-validation](resource-validation/results.json) 是最后 D2H 错误路径修复前的 19 组阶段检查；[backend-probe-first.log](backend-probe-first.log) 为早期 backend 探针。当前结论以最终 48 项 suite 为准。

## 尚未验收

设备配额按申请字节计量，不包含 Driver rounding/cache、context/queue/module 的内部设备内存；host frame/response、parser 解压临时内存和线程栈另受既有协议/parser/对象/连接上限约束。没有物理硬隔离、租户身份/份额、公平调度、跨进程/多设备分池或逐类长期泄漏证明。

drain 等待 native 返回，未实现 native 执行截止时间或取消；长 kernel/卡住的 CNDrv 可使关闭等待。D2H 仍为回复前阻塞读回，真异步 D2H/pinned、复杂参数 ABI、CNDrv client Shim、认证/TLS 和跨物理主机最终验收均未完成。M4.2/M4.3 基础已验证，工程能力分类仍为部分实现。

[client-core-build.log](client-core-build.log) 和 [哈希对照](client-core-build.json) 记录无 SDK 路径重建；[provenance.json](provenance.json) 记录当前未提交源码、产物和证据 SHA-256；[final-checks.log](final-checks.log) 记录生成器、语法、链接、统计及哈希检查。基线 commit 不代表本轮未提交实现。
