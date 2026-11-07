# G2 子集：memset、child graph 与可执行 entity

环境：2026-11-07，单卡 MLU370-S4，Neuware 3.5.2 / CNRT 6.5.2 / CNDrv 2.5.2，AArch64 LP64。相对 G1 提交 `eb5212d` 新增双库各 11 项，共 22 项 API。整个 G2 为 44 项，本批完成其中 22 项，未宣称完整 G2。

## API 与共享能力

两库各有 AddMemsetNode、MemsetNodeGetParams、MemsetNodeSetParams、EntityMemsetNodeSetParams、AddChildTopoNode、ChildTopoNodeGetTopo、EntityChildTopoNodeSetParams、Instantiate、EntityInvoke、EntityUpdate、EntityDestroy。全部由 Schema 生成公共 wrapper，共享三个 typed RPC（83～85）、有界依赖数组与 diagnostic response buffer。完整逐项名称、状态、库与前后计数见 [change-counts.json](change-counts.json)。

Server 以 Native DAG 为拓扑权威，immutable payload 保留 AllocationID backing lease。clone、child node-owned embedded graph 和 instantiate snapshot 各自有独立生命周期。EntityUpdate 只有 native 成功才切换引用；setter 始终使用最初 instantiate 顶层节点对应，资源保留整个 revision，不用 GetNodes 枚举位置猜测 native pairing。每次 invoke 入队前预分配 completion record，并持有本次 base/override 资源直到 Transfer 完成；pending EntityDestroy 后仍可完成写入。

公开 allocation free 撤销 ID，仍被图、entity 或 invocation 持有的 backing 延迟 native free/配额释放。同步失败的清理保留在途引用至确认 Context 销毁；native graph 释放失败保留 context/ownership 并 quarantine，允许重试。失败 clone/child adoption 的 native rollback 失败也保留 hidden native graph/node，供所属 parent/session 统一清理。

## 复现

```bash
./scripts/build.sh
python3 scripts/verify-graph-execution.py
python3 scripts/verify-graph.py --out evidence/graph-g2-execution-20261107/g1 --port 50252
python3 scripts/generate_compat.py
python3 scripts/verify-api-contracts.py
python3 scripts/verify-api-families.py --out evidence/graph-g2-execution-20261107/families --port 50251
python3 scripts/verify-transparent.py --out evidence/graph-g2-execution-20261107/core --port 50250
python3 scripts/summarize-api-change.py --baseline eb5212d --out evidence/graph-g2-execution-20261107
```

## 专项 16/16

[results.json](results.json) 保存每个独立进程命令与退出码；两库各有 Native、replacement、preload、隐藏本地 SDK 和设备节点的 namespace 客户端、Remote allocation lease、nested reset、abrupt EOF，另有 wire/ownership 与清理后新 session。Server 与 Client 是不同进程和地址空间。stdout 仅比较 `PASS ` 和 `update_failure=` 行，CNRT native 的预期 update 失败会额外打印带时间戳诊断，原文保留。

数值路径包括三种 elementSize、分支 DAG、GetParams 字段、clone 独立、instantiate 后源图修改不影响 entity、entity setter、update 成功、拓扑变化失败后旧 revision 可回放、update 后仍用最初节点身份 setter、源图销毁后回放，以及 pending EntityDestroy 完成。child 验证源图与 embedded 图独立、NodeFindInClone、GetTopo 内编辑、递归 clone、entity child replacement、所有源图销毁后回放、三层 snapshot 和 nested lease/reset/EOF。

协议检查覆盖三条新 RPC 的有界数组/诊断 buffer、精确长度、allocation/entity/queue owner、node-owned borrowed graph 禁止直接 destroy、reset 退役。正常 Server 清理无 quarantine；[server.log](server.log) 最终为 `RESOURCE_TOTAL device=0 staging=0 image=0`。

## 回归与失败清理

[G1 14/14](g1/results.json)、[功能族 15/15](families/results.json)、[核心 76/76](core/results.json) 通过；构建、codegen/ledger、API contracts、frozen closure audit 与 whitespace 检查见 [verification.log](verification.log)。

核心 [backend-resource-rollback.log](core/backend-resource-rollback.log) 与 [stderr](core/backend-resource-rollback.stderr) 使用真实 CNDrv，只有指定失败点由 linker wrap 返回错误。它在 native clone/add 已成功后使 identity adoption 和 rollback 失败，确认 hidden ownership 被保留；第一次 session cleanup 故障导致 graph/context/64-byte quota 保留，第二次真实 native cleanup 成功后归零。失败返回是注入场景，不是正常专项 Server 的 quarantine。既有 staging/context 失败隔离回归也通过。

[provenance.json](provenance.json) 记录当前源、生成头、SDK 头和专项二进制哈希；[artifact-manifest.json](artifact-manifest.json) 记录生成代码与构建制品。本目录日志入库时只整理行尾空格，内容及数值保留。

## 完成边界与剩余

memset 按本 SDK 只认证 height=1、elementSize=1/2/4。每 session 最多 1024 graph（含 embedded）、4096 registered nodes；另有 4096 snapshot nodes 总额；嵌套深度 <64；每次 typed-node dependencies <=4096，diagnostic buffer <=65536 bytes。instantiate 失败节点映射覆盖顶层，嵌套失败诊断未认证。setter 替换的 base allocation 可能保守保留至完整 update 或 entity destroy。同 entity 跨 Queue 并发 invoke 未另行认证。

提前 public free 和 stale/borrowed/reset 拒绝属于 Remote 安全契约；没有把 Native 未定义的提前 free 作为差分正例。模块、Host 回调或 IPC 没有借本批升级状态。

当前计数：195 IMPLEMENTED、0 PARTIAL、11 BLOCKED、2 OUT_OF_SCOPE、1 UNSUPPORTED、101 NOT_IMPLEMENTED。G2 剩余 kernel/attributes 14 项、memcpy 8 项；还需 module leases、客户端 kernel getter 参数快照、每次 Host 输入及 D2H publication。G3 capture 10 项、G4 callback/user refs/DOT 20 项与普通 memory/VMM IPC 8 项仍未实现。11 个 BLOCKED 仍是 7 个 Peer 缺第二设备和 4 个 notifier IPC 缺 Native 支持前提。详见 [设计](../../docs/GRAPH_IPC.md)。
