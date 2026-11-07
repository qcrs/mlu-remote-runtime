# G2：owned 1D D2D memcpy 节点

环境：2026-11-07，MLU370-S4 单卡，Neuware 3.5.2 / CNRT 6.5.2 / CNDrv 2.5.2，AArch64 LP64。相对 `4beeb96` 新增双库各 4 个 Graph API，共 8 项。G2 从 22/44 增至 30/44，尚余 14 个 kernel 参数/属性入口，未宣称 G2 完成。

## API 与边界

两库各新增 AddMemcpyNode、MemcpyNodeGetParams、MemcpyNodeSetParams、EntityMemcpyNodeSetParams。公共 wrapper 全部由 Schema 生成，共享 opcode 86 `graph_memcpy`；当前共 52 个 typed RPC、102 个公共 RPC wrapper。逐项 API 与前后计数见 [change-counts.json](change-counts.json)。

支持 owned live device allocation 之间的 1D copy：1..1048576 bytes，两端 pitch=width/ysize=1，extent height=depth=1。源、目标分别以 AllocationID+offset 传输，逐端校验 owner/range；不传 native 地址。SDK 忽略的 position、ptr aliases、xsize 规范化。Host/peer、capture、每次回放的 Host staging/D2H publication 不在本批边界。

`dir` 原值交由 Native 判定，不能按 CNRT enum 名称自行限制为 DevToDev=1。当前 [CNDrv Native 方向探针](native-copy-direction.log) 中 0/4/5 的 Add、Set、EntitySet 成功，1/2/3/6 的 Add 返回 100006；主数值测试使用默认 0 及 5，协议测试验证失败的 dir=1 setter 不改变原参数。[探针源码](native-copy-direction.c) 随证据保存。

每个 immutable payload 对源、目标各持有一个 allocation lease，同一 allocation 占据两端时也各增减一次。clone、embedded child、entity snapshot/update、setter override 和每次 pending invocation 复用既有引用机制。公开 free 撤销 allocation ID，backing 延迟回收；GetParams 使用 tombstone 恢复两端客户端 VA。提前 free 属于 Remote 安全契约，不是 Native 的相同顺序差分正例。setter 的 base revision 仍可能保守保留到完整 update 或 entity destroy。

## 复现

```bash
./scripts/build.sh
python3 scripts/verify-graph-execution.py --out evidence/graph-g2-memcpy-20261107 --port 50253
python3 scripts/verify-graph.py --out evidence/graph-g2-memcpy-20261107/g1 --port 50252
python3 scripts/generate_compat.py
python3 scripts/verify-api-contracts.py
python3 scripts/verify-api-families.py --out evidence/graph-g2-memcpy-20261107/families --port 50251
python3 scripts/verify-transparent.py --out evidence/graph-g2-memcpy-20261107/core --port 50250
python3 scripts/summarize-api-change.py --baseline 4beeb96 --out evidence/graph-g2-memcpy-20261107
```

方向探针可用 `gcc -I/usr/local/neuware/include evidence/graph-g2-memcpy-20261107/native-copy-direction.c -L/usr/local/neuware/lib64 -Wl,-rpath,/usr/local/neuware/lib64 -lcndrv -o /tmp/mlur_graph_copy_direction` 编译后独立运行。

## 验证结果

[Graph 专项 16/16](results.json)：两库各有 Native、replacement、preload、隐藏本地 SDK/设备节点的 namespace 客户端，以及 Remote lease/reset/abrupt EOF。另有 wire/ownership 与清理后新 session。Server/Client 为独立进程、独立地址空间；比较 stdout 的 PASS 和 update_failure 行，保留原始 SDK stderr。

数值测试覆盖两端不同 offsets、GetParams 有效字段、clone 独立、instantiate 后修改不影响既有 snapshot、entity setter、完整 EntityUpdate、update 后沿用最初 instantiate 节点身份、每次回放读取当前 device source bytes、源图销毁后回放和 pending EntityDestroy。每次读取完整 512-byte 目标，检查 copy 区与其余区域/guard。child copy 验证 embedded GetTopo/FindInClone、源图销毁后 snapshot、同一 allocation 的非重叠源/目标。Remote 单独验证两端 public free 后原图/clone GetParams、继续 instantiate/replay、pending destroy 和正常回收。reset/EOF 的嵌套 DAG 同时含 memset 和 memcpy。

[协议测试](protocol.log) 覆盖精确请求/响应长度、截短/多余 bytes、依赖数组数量、两端各自 foreign/type/NULL/out-of-bounds/overflow、跨 session graph/node/entity、同 session 不同 parent dependency、错误 node type、entity 的 instantiate-time node membership、失效 dir setter 后原参数、两端退役 getter 与 reset stale IDs。正常 [Server 日志](server.log) 无 quarantine，最终 `RESOURCE_TOTAL device=0 staging=0 image=0`，worker 全部 drain。

[失败回滚](backend-resource-rollback.log) 与 [stderr](backend-resource-rollback.stderr) 使用真实 CNDrv，仅 linker wrap 指定调用返回失败，不伪造成功。Add/Set/EntitySet 失败释放本次两个 endpoint refs，保留旧 backing；成功 replacement 回收已退役旧 endpoints，entity 销毁归还 base/override quota；同一 allocation 的两份引用正确平衡。既有 clone/child adoption rollback 故障加入 memcpy payload，确认 native rollback/cleanup 失败时 hidden ownership 与 quota 保留，重试后释放。

[G1 14/14](g1/results.json)、[功能族 15/15](families/results.json)、[核心 76/76](core/results.json) 通过。核心包含更新后的 backend rollback/quarantine、Host completion、parser bounds、object identity、transport 和隔离客户端回归。[build.log](build.log) 与 [verification.log](verification.log) 保存构建、codegen/ledger/contracts、closure audit 和 whitespace 验证。完整构建通过后，仅强化了数值测试中的整段目标/guard 检查；随后使用构建脚本的相同参数单独重编译两库 Graph 测试二进制，并重新跑完 Graph 16/16。

[provenance.json](provenance.json) 包含共享 memcpy shim、backend、schema、测试、SDK 头与专项二进制哈希；[artifact-manifest.json](artifact-manifest.json) 记录生成代码与构建制品。日志入库时只整理行尾空格。

## 当前状态与剩余

共 203 IMPLEMENTED、0 PARTIAL、93 NOT_IMPLEMENTED、11 BLOCKED、2 OUT_OF_SCOPE、1 UNSUPPORTED，公共分母仍为 310。CNRT 为 88 implemented / 36 not implemented；CNDrv 为 115 / 57。Graph 合计 58/102；G2 为 30/44，剩余 kernel 14 项。module leases、客户端 kernel GetParams 参数快照、Host graph replay staging/publication 尚未认证；G3 capture 10 项、G4 callback/user refs/DOT 20 项和普通 memory/VMM IPC 8 项仍未实现。BLOCKED 仍为 Peer 缺第二设备及 notifier IPC 缺 Native 前提。详见 [设计](../../docs/GRAPH_IPC.md)。
