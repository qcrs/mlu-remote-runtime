# 单设备 closure audit，2026-11-07

基线 `dbf958a302d80fdc3319353f26459ceb6f8d51bb`，MLU370-S4 / Neuware 3.5.2 / CNRT 6.5.2 / CNDrv 2.5.2 / AArch64→AArch64 / 单设备。

`baseline.json` 冻结全部 97 个原 PARTIAL，`review.json` 保存逐项审查决定。77 IMPLEMENTED、18 PARTIAL、2 OUT_OF_SCOPE。额外发现原 IMPLEMENTED 的 `cnrtQueueDestroy` 同步等待而违反立即返回契约，降为 PARTIAL；7 个 Peer 改为 BLOCKED。审计后 121/19/7/2/4/157，依次为完整/部分/阻塞/范围外/拒绝/未实现。

逐项提升复用 Schema 引用的历史 native/remote 日志，审计不把日志存在当成语义证明；已对照对应实现及 SDK 描述。已完成状态保留具体边界（当前工具链 image、owned resources、单设备、native NULL queue），真实缺口保留 gap/fix/acceptance。`cnMemcpyAsync` 的 D2D 子集经复核继续 PARTIAL。

本批运行：

```sh
./scripts/build.sh
python3 scripts/verify-api-families.py --out evidence/closure-20261107/family --port 50203
python3 scripts/verify-query-boundaries.py --out evidence/closure-20261107/query --port 50213
python3 scripts/verify-api-contracts.py
python3 scripts/verify-closure-audit.py
```

[family/results.json](family/results.json)：15/15；[query/results.json](query/results.json)：11/11。每个 runner 启动独立 Server 进程并用客户端进程走 TCP，同时保留 Native baseline；replacement、preload、隐藏 SDK/设备的 isolated 模式结果一致。协议、owner 和 metadata 必要回归保留于同目录。build/schema/ledger/contracts/audit 均通过。审计只修改分类和验证机制，不修改 Runtime/backend，未重复全量 suite。

当前状态与后续修复参见 [PROJECT_STATE](../../docs/PROJECT_STATE.md)；本目录是初始审计快照，后续变化通过 Schema `closure_resolution` 和新增 evidence 追踪。
