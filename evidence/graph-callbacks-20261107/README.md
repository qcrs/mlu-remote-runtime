# G4 客户端 callback、user object 与 DOT

2026-11-07，AArch64 / MLU370-S4 / Neuware 3.5.2 / CNRT 6.5.2 / CNDrv 2.5.2。双库各 10 个 API 已按 Schema boundary 实现：HostNode Add/Get/Set/EntitySet、DebugDotPrint、UserObject Create/Acquire/Release、Topo AcquireUserObject/ReleaseUserObject。Graph G1～G4 合计 102/102，项目为 247 IMPLEMENTED、0 PARTIAL、11 BLOCKED、2 OUT_OF_SCOPE、1 UNSUPPORTED、49 NOT_IMPLEMENTED；IPC 不计为完成。

## 验证结果

| 批次 | 场景 | 结果 |
|---|---:|---|
| G4 专项 | 20 | 全部通过 |
| Capture | 97 | 全部通过 |
| Host graph | 18 | 全部通过 |
| Kernel graph | 18 | 全部通过 |
| Entity execution | 16 | 全部通过 |
| G1 DAG | 14 | 全部通过 |
| 功能族 | 15 | 全部通过 |
| Host async | 15 | 全部通过 |
| 核心透明运行 | 76 | 全部通过 |
| 合计 | 289 | 全部通过 |

`results.json` 为 G4 的 20 项，每个回归子目录另有 results/stdout/stderr/Server/provenance；`verification.log` 保存回归命令及输出，`checks.log` 保存收口检查。表中不重复累加 core 内嵌的 transport/resource 子场景。Server 与 Client 始终是独立进程；Native 直接链接 vendor 库，replacement/preload 使用相同测试程序。isolated 用例确认 vendor SDK 路径和设备节点被隐藏。

## 语义证据

- 两库 Native/replacement/preload/isolated 共 8 项：callback PID 是客户端 PID，D2H→callback→H2D 顺序产生 42/73；clone、instantiate、Node/Entity setter、nested child 和 child replacement 正确。EntityUpdate 替换 callback snapshot 后，原始 NodeID 仍可用于 setter。
- 两库各 reentry/reset/abrupt/lease 共 8 项：callback/destructor 重入在锁前拒绝，reset/Context close 正常 drain，Runtime reset 后可重新配对，abrupt EOF 回收；pinned Host 提前 free 的 invocation lease 保活到完成。
- `protocol`：配对、owner/type、重复 attach、完整计划白名单与事件序号；旧 raw replay 拒绝占位 callback，EOF poison 主 session。
- `submit-disconnect`：真实 TCP 代理在 action 13 提交响应时断线；客户端返回 API error，未读取 NULL reply 或发生 SIGSEGV，清理后可重新接入。
- `after-cleanup`：新进程再次完成数值与生命周期验证，Server 准入正常。
- `quota`：192-byte staging 上限下两次准入失败，原 descriptor 不变；随后无 copy 的 callback replay 成功。Server 与 quota Server 均报告 `RESOURCE_TOTAL device=0 staging=0 image=0`、`DRAIN_COMPLETE workers=0 cleanup_errors=0`。

主数值程序另验证无需 wait 也能推进、12 次重复 invoke、callback 阻塞期间销毁源图/entity，以及 caller/acquire/MOVE/graph release/clone/entity/inflight 引用的唯一析构；未关联图的最终 user release 也能自行析构。DOT flags 0/1/2/4/64/255/256 成功；缺目录返回 Driver 100007 / Runtime 632015。DOT 来自 native 内容并写至客户端路径，文件不要求与 Native 字节完全相同。

## 实现与边界

`graph_control` 生成 RPC 共用 paired callback service。Server 只接受客户端登记并完整回显的计划，私有 native DAG 插入逐 copy Host 边界：H2D 取当前输入，D2H scatter 后 ACK，后继才执行。DONE 先释放客户端 run/Host leases 再 ACK；Server Transfer 保留资源至 native completion。Native destructor 只排事件，原始函数在客户端执行，service ref 独立于 session。

callback 每 session 串行，须在 RPC deadline 内返回；不能强制取消应用代码。Runtime/Driver 重入返回 NotSupported。单 copy <=1 MiB、Host endpoint 总和 <=16 MiB，pageable 内存由调用者保活，pinned 提前 free 延后回收属于 Remote 安全策略。EntityUpdate 后结构角色不唯一时，相关 setter 保守拒绝。普通 Host async 仍在成功完成 API 后发布，callback 图为逐节点发布的专用路径。随机配对 secret 不等于生产认证或 TLS。

## 复现与来源

```sh
./scripts/build.sh
python3 scripts/verify-graph-callbacks.py --port 50267 --out evidence/graph-callbacks-recheck
python3 scripts/generate_rpc.py --check
python3 scripts/generate_compat.py --check
python3 scripts/verify-api-contracts.py
git diff --check
```

其余回归的完整命令见 `verification.log`，产物写至独立目录以保留本轮记录。`provenance.json` 保留专项执行时哈希，各子目录保留各自运行时哈希；`final-artifacts.json` 记录收口后的源码、二进制、SDK 和生成文件哈希。专项后只更新 Schema 证据元数据及文档，runtime 二进制未改变；最终 C 测试源码已重新编译后运行专项。契约检查证明结构一致性，数值/生命周期依据为独立执行日志。
