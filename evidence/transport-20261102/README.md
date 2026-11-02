# 连接超时、握手隔离和服务端重启证据

2026-11-02，AArch64 / MLU370-S4 / Neuware 3.5.2。本批保留旧 TCP/Session/六 preset/通用 image/共享 module；新增有限 socket 时间预算和 session 生命周期加固。详细语义见 [连接生命周期](../../docs/TRANSPORT_LIFECYCLE.md)。

## 最终验证

[final-suite/results.json](final-suite/results.json) **42/42 返回期望值**，包含原生/替换/preload、隐藏 SDK/设备节点的三个客户端、共享 globals/DSO、1200 launch/copy、协议/所有权、六 preset 和 parser ASan/UBSan 6000 mutations。CNNL 项仍只验证明确拒绝。

```sh
./scripts/build.sh
python3 scripts/verify-transparent.py --out evidence/transport-20261102/final-suite --port 52343
```

[build-complete.log](build-complete.log) 是最终完整构建；[final-suite-run.log](final-suite-run.log) 为运行索引。已有 warning 保留，未宣称零 warning。

[client-core-build.log](client-core-build.log) 和 [哈希对照](client-core-build.json) 验证 `CLIENT_CORE_ONLY=1 NEUWARE_HOME=/nonexistent` 重建通过，重建后的全部记录产物哈希与 final-suite 运行时相同。

| 能力 | 测试与结果 |
|---|---|
| Client 整次 RPC deadline | [14 组](final-suite/client-transport-deadlines.log)：非法配置、HELLO/RPC 黑洞/半包/慢滴、header+body 总预算、发送 backpressure、真实本机 listen backlog connect 超时、EINTR；200ms 配置按期返回，输出 sentinel 不变，后续不发送/重放 |
| Server 首包和收发 deadline | [21 组](final-suite/server-transport-deadlines.log)：首包 ALLOC 拒绝、坏 version/flags/length/status/sequence/device、重复 HELLO、legacy HELLO、250ms 首包/350ms established 空闲及半包、D2H backpressure、另一 native-backed 客户端持续工作；[server 原始日志](final-suite/server-transport/server.log) |
| 暂停/kill/restart | [3 组](final-suite/server-restart.log)：只操作自建 server；SIGSTOP 时 250ms RPC 超时，SIGCONT 后新 session 可用，SIGKILL 后旧 context 失效，restart 后旧 context 不重连/重放、旧 AllocationID 拒绝 |
| 对象身份与熵失败 | [object-identity](final-suite/object-identity.log)：getrandom EAGAIN 注入时失败关闭；四线程、同 owner 重初始化共 2048 ID 不重复；owner/type/stale/generation 耗尽检查 |
| 旧协议负例真实性 | [protocol-error-test](final-suite/protocol-error-test.log)：实际读取超长 HELLO 的 BAD_LENGTH+EOF 和截断 HELLO 的无成功 EOF，不再仅 send/close 后打印 PASS |
| 客户端依赖隔离 | [isolated-client](final-suite/isolated-client.log)、[shared-module](final-suite/isolated-shared-module.log)、[DSO](final-suite/isolated-module-dso.log)、[strace](final-suite/client.strace)、[dependency](final-suite/dependencies.log) |

## 暴露的问题与修复

[restart-before.log](restart-before.log) 真实复现旧实现重启前后第一个 AllocationID 均为 `4294968297`。该测试先完成暂停/kill 两项，再在 ID 相等断言失败；不是成功证据。原始两个 server 日志保存在 [restart-before](restart-before/results.json) 的同目录。

对象注册表改为进程内不复用 sequence XOR 内核随机启动 nonce；所有 session 共用序列，owner/type/live 验证保留。跨重启碰撞为随机 64-bit 空间的概率事件，不作为认证或持久唯一 ID 承诺。[restart-after.log](restart-after.log) 和最终 suite 记录实际新旧 ID、旧句柄拒绝及新 allocation 可释放。

[attempt-1-test-expectation/server-deadlines.log](attempt-1-test-expectation/server-deadlines.log) 保留首轮测试错误：把本机 CNDrv InvalidDevice 预期写成 100006；SDK 实际为 100005，已更正测试。[build-identity.log](build-identity.log) 保留新增熵失败路径误用不存在 `CN_ERROR_SYSTEM` 的编译失败，更正为 SDK 的 `CN_ERROR_OPERATING_SYSTEM` 后重建通过。

[suite/results.json](suite/results.json) 是 identity 修复前的 40 项阶段回归；当前实现的完整结论以 42 项 final-suite 为准。旧批证据没有覆盖重写。

## 证据边界

- 所有 native server 和 namespace 客户端在同一物理机器，通过真实 loopback TCP 通信；没有作为第二物理主机或公网网络故障验收。
- Client connect 的预算从 DNS 完成后开始，RPC 从获得 context lock 后开始。Server socket deadline 不约束 native CNDrv 执行和 cleanup；超时不等于 kernel 取消。
- server 收发测试的“另一客户端继续工作”是实际 native allocation/free/query 成功，不是只看连接仍打开。
- 首包不创建 context 和 cleanup 完成是代码与日志路径证据；尚未独立测量 native 资源计数归零。
- 本批 SIGSTOP/SIGKILL 时未在执行 device kernel，不能当作在途 kernel 强制取消或 SIGTERM worker drain 的证据。
- M4.1 部分完成；认证/TLS、配额、DNS/native 取消、应用恢复、worker drain 和最终跨物理主机验收仍待做。API 台账总量保持 CNRT 130 / CNDrv 180；本次没有增加原生 API 空实现。

[provenance.json](provenance.json) 记录未提交源码快照、产物和原始证据 SHA-256；[final-checks.log](final-checks.log) 保存最终生成器、语法、链接、统计和哈希检查。
