# Host memory / deferred D2H，2026-11-07

基线 `49504c8`，MLU370-S4 单设备、Neuware 3.5.2、CNRT 6.5.2、CNDrv 2.5.2、AArch64→AArch64。Server 和 Client 为独立 TCP 进程，Native、replacement、preload 使用同一个 CNCC executable；namespace 用例隐藏 SDK 和设备节点。源码和测试二进制哈希见 [provenance](provenance.json)，逐项变更见 [change-counts](change-counts.json)，生成物见 [artifact-manifest](artifact-manifest.json)。

## 共享能力和状态变化

- 两库共享进程级 Client Host registry：mmap/mlock、interior range、guard/tombstone、跨 Context 和交叉 free。CNRT Host alloc/attributes/free 初始化前、离线可用；CNDrv alloc/free 需要 init，但无需当前 Context，属性查询仍需当前 Context。Host allocation 不随 Device reset 或 Context destroy 失效。
- 异步 D2H 在 Server `cnMallocHost` 页中原生入队，submit 只返回 TransferID；生成的有界 `size_from` response buffer 支持后续精确长度 fetch。Host 指针不传给 Server，旧同步 D2H wire 继续接受。
- Queue/Notifier/Transfer 成功 wait/query、Context sync、free、module unload 查询完成项并写回；reset/close 在旧 ID 失效前 drain。没有后台 publication；pageable 目标由调用者保持有效至完成，registered Host 通过 pending lease 延后退页。较晚的同 Queue 或重叠写回不能越过较早未完成项，独立目标仍能前进；新同步 D2H 输出不会被旧结果覆盖。
- Server 的 pinned D2H 与 H2D snapshot 共用 staging quota。D2H 完成后仍计费，fetch/release 或 cleanup 才释放；submit/Notifier/wait 失败保留可达 ownership，释放失败重试，无法确认释放则 quarantine。EOF 只清理 Server 自有缓冲，不补写失联客户端。

5 个旧 PARTIAL 收口：`cnrtMemcpyAsync`、`cnMemcpyAsync`、`cnrtPointerGetAttributes`、`cnGetMemAttribute`、`cnGetMemAttributes`。3 个原拒绝 stub 实现：CNRT Host alloc/free、`cnMemcpyDtoHAsync`；2 个原无导出入口新增：CNDrv Host alloc/free。总数 **138 IMPLEMENTED / 7 PARTIAL / 7 BLOCKED / 2 OUT_OF_SCOPE / 1 UNSUPPORTED / 155 NOT_IMPLEMENTED**。typed RPC 43→45，公共 RPC wrapper 仍为 45。

## 验证

```sh
./scripts/build.sh
python3 scripts/verify-host-async.py
python3 scripts/verify-api-contracts.py
python3 scripts/verify-api-families.py --out evidence/host-async-20261107/api-families --port 50229
python3 scripts/verify-copy-closure.py --out evidence/host-async-20261107/copy-closure --port 50230
python3 scripts/verify-transparent.py --out evidence/host-async-20261107/full-suite --port 50231
```

Host 专项 [results.json](results.json) 15/15：离线四模式、两库各四模式、D2H wire ownership/fetch/EOF 和两个 backend failure/cleanup 用例。两库显式/default Queue、65539-byte pinned/pageable 目标、interior 属性和准确输出宽度、跨 Queue wait、reset/unload/销毁、在途 Host free 均覆盖。Backend 注入覆盖 pinned allocation/submission/Notifier/queue wait/free 失败，验证 quota 拒绝发生在提交前、完成后 buffer 仍计费、失败 buffer 保留和 Context 销毁后的释放/隔离。

功能族 [15/15](api-families/results.json)，copy/error [16/16](copy-closure/results.json)，完整核心回归 [76/76](full-suite/results.json) 通过。其中 client fault 用例验证生成 buffer 的截短/额外回复不写输出、完成查询竞态下的顺序和独立进展、较新的同步 D2H 输出。Server 专项与完整回归最终 `RESOURCE_TOTAL device=0 staging=0 image=0`，无 cleanup error。构建与契约/冻结审计检查日志分别为 [build.log](build.log)、[contracts.log](contracts.log)。

## Native 观察和完成边界

Native 与 Remote 的正常 copy/wait/query/attribute 结果一致；Native 不保证释放无关 allocation 会完成另一 Queue，因此该用例随后明确 sync。此 SDK 的 pending Queue destroy、Runtime reset、Driver module unload 可能取消 D2H。数值基线在 Queue destroy/reset/module unload/Context destroy 前预等待，Remote 在仍 pending 时执行对应生命周期操作并验证 drain/writeback，**这一部分不是相同调用顺序的差分**。Queue destroy 观察沿用 [前批记录](../queue-retirement-20261107/README.md)。本批 [native-observations.json](native-observations.json) 两次均 exit 3：Runtime 在 frontier 8 的 reset 后数值断言失败，Driver 在 frontier 8 的 unload 后数值断言失败；这些独立观察不计为应通过的回归，也未继续执行 Driver frontier 9。初始化前/无当前 Context 属性差异的探针源码、输出及哈希另见 [native-probes](native-probes/results.json)。

`MLU_TEST_SHIM` 仅控制测试基线是否提前等待及 Remote-only 的 freed-range 生命周期断言；实现不读取该变量。`MLU_HOST_OBSERVE_NATIVE=1` 则让 Native 在 reset/unload 前保持 pending，用于记录 SDK 现象。等待完成前读取/释放普通 pageable 目标不在支持契约内；并发无依赖 Queue 写同一地址不提供额外排序保证。映射/cache coherency、callback、Graph/IPC 仍需各自状态机，不能由本批 Host allocation 成功推导完成。

冻结的 closure baseline/review 保持原样；早期 `backend-retirement-*` 日志为开发记录，最终验收以本批 results 和上列日志为准。剩余 7 个 PARTIAL 为两个 cast、Runtime kernel memory usage、symbol size，以及三个 kernel metadata/relocation 入口，详见 [family plan](../../compat/family-plan.json)。
