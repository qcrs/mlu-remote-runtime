# G1：owned DAG 与节点身份

环境：2026-11-07，MLU370-S4 单设备，Neuware 3.5.2 / CNRT 6.5.2 / CNDrv 2.5.2，AArch64 LP64。此前 Native Graph/IPC 契约基线为提交 `c6271ca`；本批增加 Remote G1 实现，不包含 typed graph 执行或 IPC。

## 实现范围

双库各 14 个 `TaskTopo` API：Create、Destroy、Clone、NodeFindInClone、AddEmptyNode、AddDependencies、RemoveDependencies、GetNodes、GetEdges、GetRootNodes、NodeGetType、DestroyNode、NodeGetDependencies、NodeGetDependentNodes。28 项全部通过 Schema 生成 ABI wrapper；三个共享 RPC 为 action/edit/query（80～82）。

共享 array codegen 对有界 trailing u64 identities 逐元素编解码网络序；请求数组在同步调用内借用，响应数组由调用者持有。native CNDrv DAG 是拓扑权威，Server 映射 GraphID/NodeID 并验证 session/generation/type/parent。Clone 先预留完整节点集合，再登记新的稳定身份；图销毁撤销全部 child IDs。Reset/EOF 清理使用同一路径；native graph 清理失败保留 session/context ownership，避免丢失 host-side 对象。

限制：每 session 1024 graphs、所有 graphs 合计 4096 nodes，同时受共享 8192-object registry 限制。每 RPC 最多 65536 u64 identities；node query capacity 最多 65536，双数组 edge query capacity 最多 32768。当前仅 empty nodes 与 DAG 查询/编辑；typed payload/entity/capture/callback/user refs 属于 G2～G4。

## 复现

```bash
./scripts/build.sh
python3 scripts/verify-graph.py
python3 scripts/generate_rpc.py
python3 scripts/generate_compat.py
python3 scripts/verify-api-contracts.py
python3 scripts/verify-closure-audit.py
python3 scripts/verify-api-families.py --out evidence/graph-g1-20261107/families --port 50251
python3 scripts/verify-transparent.py --out evidence/graph-g1-20261107/core --port 50250
```

## 专项结果：14/14

[results.json](results.json) 列出命令与退出码；每个场景保留 stdout/stderr，独立 Server 输出见 [server.log](server.log)，源码与 SDK 头文件哈希见 [provenance.json](provenance.json)。`verification.log` 汇总本批命令及退出码。

入库时移除 Native cnmon/stderr 日志的行尾空格；内容和数值保持原样，测试中的 stdout 比较在此格式整理之前完成。

- CNRT 和 CNDrv 各运行 Native、replacement、LD_PRELOAD、隐藏本地 SDK 和设备的 namespace 客户端，正常语义输出完全一致。覆盖四节点 diamond、四条边、count-only/truncation/NULL tail、两个方向查询、clone remap、删除/编辑以及源图销毁后 clone 继续可查。零条 Add/RemoveDependencies 成功。
- 每库独立运行 reset 与 abrupt `_exit`，保留活图/节点让 Server 清理；随后新 session 正常运行。图清理日志无 quarantine，Server drain 时 `RESOURCE_TOTAL device=0 staging=0 image=0`。
- `protocol.log` 检查跨 session/type/parent 拒绝、clone 稳定身份、destroy/reset stale IDs、数组精确长度/上限和零容量查询。客户端与 Server 分进程、分地址空间运行。

本机 SDK 的 `NodeGetDependencies` 实测返回 outgoing 后继，`NodeGetDependentNodes` 返回 incoming 前驱。Remote 直接调用 native 后映射 IDs，未按名字重建或反转边。Native 对销毁后的 graph/node 的行为不能作安全契约；stale/reset 拒绝仅计为 Remote 安全检查。不能将这些拒绝称为 Native 差分，也不能从 G1 推导 entity 数值执行或 IPC 已实现。

## 本批回归

完整构建成功（[build.log](build.log)）；功能族 [15/15](families/results.json)、完整核心 [76/76](core/results.json) 通过，API contracts、codegen/ledger 与 frozen closure audit 检查均通过。[verification.log](verification.log) 记录命令和退出码。核心覆盖 BANG/Module、两库 Host async 与 Queue retirement、Context/ownership、隔离客户端、客户端 Driver 访问检查、协议/连接、配额回滚与 worker drain。最终复核 provenance 中的源文件、生成头、SDK 头和二进制哈希均与当前制品一致。

[change-counts.json](change-counts.json) 记录相对 `c6271ca` 的 28 项新增实现及前后计数；[artifact-manifest.json](artifact-manifest.json) 记录公共生成路径和构建产物。G1 原始日志与哈希以本目录专项记录为准。

本批 API 计数：173 IMPLEMENTED、0 PARTIAL、11 BLOCKED、2 OUT_OF_SCOPE、1 UNSUPPORTED、123 NOT_IMPLEMENTED。11 个 BLOCKED 仍为 7 个 Peer 缺第二设备与 4 个 notifier IPC 缺 Native 支持前提。后续 G2 为 typed nodes / entity snapshot / leases，完整计划见 [GRAPH_IPC](../../docs/GRAPH_IPC.md) 和 [family plan](../../compat/family-plan.md)。
