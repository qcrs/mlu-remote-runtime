# Queue retirement 与 SyncDevice 修复（2026-11-07）

目标：MLU370-S4 单设备，Neuware 3.5.2 / CNRT 6.5.2 / CNDrv 2.5.2，AArch64→AArch64。本机独立 Server/Client 进程通过 TCP 运行；隔离客户端隐藏 SDK 和整个 `/dev`，保留网络。

```sh
./scripts/build.sh
python3 scripts/verify-queue-retirement.py --port 50227
python3 scripts/verify-api-contracts.py
python3 scripts/verify-transparent.py --out evidence/queue-retirement-20261107/full --port 50228
```

专项 [results.json](results.json) 为 12/12 通过：两库各一次 Native pending-destroy 观测，加各四种 Native/replacement/preload/isolated 运行，以及 backend rollback/quarantine 两项。完整核心回归 [full/results.json](full/results.json) 为 67/67 通过，已将八项 retirement 用例接入常规 runner。构建和 codegen/ledger/closure 契约检查均通过，见 [build.log](build.log)、[contracts.log](contracts.log)。[full/environment.log](full/environment.log) 保留系统、cnmon、编译器版本和 SDK header 哈希；[provenance.json](provenance.json) 保留本批源文件、binary、替换库及 driver image 哈希。

## 执行断言

- CNRT 五种完成路径：Notifier wait、SyncDevice、free、Notifier query 轮询、另一队列等待该 Notifier 后 sync。CNDrv 另加 module unload，共六种。
- 每次提交有限 recurrence kernel、async H2D、async D2D 和末尾 Notifier。Remote destroy 返回后 Notifier 必须仍为 NOT_READY；旧 queue query/destroy 必须拒绝。随后改写客户端 Host 源，最终三项数据必须为 `2515503399,305419896,305419896`。CPU reference 独立计算 recurrence，H2D/D2D 检查服务端 snapshot 生命周期。
- 两库各运行 1200 次 create/destroy，超过服务端 queue 表的 1024 项，检查完成队列可再次收集，旧身份不复用；另有带 pending kernel 的 CNRT reset / CNDrv context teardown。
- Backend 使用真实 CNDrv 成功路径，仅注入失败返回：destroy 不调用 queue sync，退役时 snapshot/额度仍保留；完成后的第一次 native destroy 失败可见，句柄保留；后续 transfer wait 重试成功才释放该 native queue。原有 sync/context destroy 双失败的 staging quarantine 验证继续通过。

Remote 延迟日志只作观测，不使用绝对毫秒阈值判定。通过条件是 destroy 后仍处于 NOT_READY，以及各自完成路径后的精确数值；详见两库 `*-retirement-remote.stderr`。`cnrtSyncDevice` 原先只遍历客户端 live queues，会遗漏已退役队列；现在同步 Server native context。context 完成前不再提前释放资源。

## Native pending-destroy 差异

本机 SDK 的独立观测见 [CNRT](runtime-native-pending-observation.log)、[CNDrv](driver-native-pending-observation.log)：销毁前 Notifier 为 NOT_READY；销毁后可变为 SUCCESS，wait 也成功，但三项输出仍为初始化值 `2779096485`（`0xa5a5a5a5`），未得到预期 kernel/copy 数据。

因此 Native 数值基线在 destroy 前显式等待 Notifier；Remote 三种运行方式则保持 pending destroy 并检查最终数据。stdout 一致代表数值与 CPU/Native 已完成基线一致，不能描述为完全相同调用顺序的差分。实现遵循 SDK 声明的立即返回、待完成后释放契约，不复现这里观测到的丢数据行为，也不把这一观测外推到其他 SDK。

## 回收边界

公开 QueueID 退役后，底层句柄仍归 session 所有。创建队列、成功的 queue/Notifier/Transfer 完成操作、context sync、free/module unload 或 session cleanup 触发收集；仅确认整个 retired queue 完成后释放 H2D staging 并销毁 native queue。查询/销毁失败保留句柄并上报；尚未完成时继续保留。

专项 [server.log](server.log) 和完整回归 [full/server.log](full/server.log) 均为 `DRAIN_COMPLETE workers=0 cleanup_errors=0`、`RESOURCE_TOTAL device=0 staging=0 image=0`。这些是逻辑额度及会话清理证据，没有新增物理驻留/长期泄漏测量。没有后台 collector；空闲 session 可保留已完成的 retired queue，直到下一次相关 API/teardown。没有有界 native 取消保证。

`cnrtQueueDestroy`、`cnDestroyQueue` 在上述范围内升级 IMPLEMENTED，当前台账为 128 完整、12 部分、7 阻塞、2 范围外、4 明确拒绝、157 未实现。其余 Host registry、异步 D2H/deferred writeback、module/symbol/参数 metadata 与 cast 缺口继续保留。

仅清理 cnmon 日志的行末空白，返回码和数值内容保留。
