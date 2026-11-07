# Host memcpy graph replay

环境：2026-11-07，MLU370-S4 单设备，Neuware 3.5.2 / CNRT 6.5.2 / CNDrv 2.5.2，AArch64 LP64。基于 `bb8de3c` 补齐 G2 声明边界内的 1D H2D/D2H 显式图重放；公共 API 数量保持 217 IMPLEMENTED、Graph 72/102。新增共享 typed RPC `graph_host`（89），累计 55 个 typed RPC、116 个公共 RPC wrapper。

## 行为与边界

构图和 instantiate 只保存 Host 地址 token。每次 EntityInvoke 在客户端同一 session 锁内完成 describe、读取当前 H2D 输入、execute 和 D2H publication 注册；Server 不解引用客户端地址。每次提交独立 clone 当前 entity revision，应用 setter overrides，分配 pinned staging，改写 Host 节点并 instantiate 私有 native entity。顶层及 embedded child 使用 `NodeFindInClone` 递归配对。一次 invocation 内相交的 Host 区间共享 staging，保持 D2H→H2D 图内反馈和有序重叠输出；不同 invocation 各自保留输入与资源。

D2H 输出绑定 TransferID；成功完成 API 边界统一 fetch 后按目标 scatter，复用普通 Host async 的同队列/重叠目标 publication 顺序。Queue、Notifier、Context、reset/context close 均有对应验证。没有后台写回；EOF 只清理 Server 资源。每个 pinned Host 输出目标持有客户端 registry lease，pageable 内存仍须由调用者保持有效。会写入下一次图输入的先前异步操作，须先完成再 invoke，因为 H2D 在 invoke 时取样。

支持单节点 1..1048576 bytes、extent height=depth=1、pitch=width、ysize=1。一次 invocation 的 Host endpoint 长度总和最多 16 MiB；占位 Host payload 与 pinned staging 计入 STAGING，entity snapshot、revision view 和 invocation view 共用 4096 snapshot-node 配额。全部 Host 节点被 D2D override 替换的零 Host endpoint 路径也可执行。

EntityUpdate 保留最初 instantiate-time setter NodeID。涉及 Host 重放的 setter 只接受经节点类型和边结构证明唯一的对应关系，最多 512 个顶层节点；对称或不明确的更新图仍可重放，其 setter 返回 Remote unsupported。未使用无关图的节点枚举位置猜测对应关系。多维/peer graph copy、capture、客户端 callback/user objects、IPC 及同 entity 跨 Queue 并发不在本次认证范围。

## 复现

```bash
./scripts/build.sh
python3 scripts/verify-graph-host.py --port 50258 --out evidence/graph-host-20261107
python3 scripts/verify-graph-kernel.py --port 50259 --out evidence/graph-host-20261107/kernel
python3 scripts/verify-graph-execution.py --port 50260 --out evidence/graph-host-20261107/execution
python3 scripts/verify-graph.py --port 50261 --out evidence/graph-host-20261107/g1
python3 scripts/verify-host-async.py --port 50262 --out evidence/graph-host-20261107/host-async
python3 scripts/verify-api-families.py --port 50263 --out evidence/graph-host-20261107/families
python3 scripts/verify-transparent.py --port 50264 --out evidence/graph-host-20261107/core
python3 scripts/verify-api-contracts.py
git diff --check
```

Server 与客户端为独立进程和地址空间。两库分别执行 Native、replacement、preload、隐藏本地 SDK 与设备节点的隔离客户端；数值差分比较专项脚本提取的 PASS 行，SDK stderr 单独保留。Native 数值场景在修改输入前等待上一轮完成。提前 Host free、reset pending drain，以及多个在途 invocation 之间修改输入是单列 Remote 安全策略测试，不称为 Native 相同调用顺序的差分。

## 结果

| 测试 | 通过 | 证据 |
| --- | --- | --- |
| Host graph 专项 | 18/18 | [results.json](results.json) |
| Kernel 与 Host/普通 async 混排 | 18/18 | [kernel/results.json](kernel/results.json) |
| 既有 G2 执行 | 16/16 | [execution/results.json](execution/results.json) |
| G1 图查询与编辑 | 14/14 | [g1/results.json](g1/results.json) |
| API 功能族 | 15/15 | [families/results.json](families/results.json) |
| 普通 Host async | 15/15 | [host-async/results.json](host-async/results.json) |
| 完整核心回归 | 76/76 | [core/results.json](core/results.json) |

Host 专项的两库 Native/replacement/preload/isolated/lease/reset/abrupt 共 14 项，加协议、清理后新 session、真实 backend 失败回滚和低 STAGING 配额场景。覆盖 pageable/pinned、getter/clone/instantiate snapshot、当前输入、entity setter/update 与原 NodeID、nested child/replacement、Host alias、default Queue、pending EntityDestroy、Host retirement/reset/EOF。整段输出与 guard 均检查。Kernel 专项增加 H2D→kernel→D2H，并在图前后混排普通 H2D/D2H，验证同目标后写覆盖顺序。

[协议](protocol.log) 覆盖精确动态长度、capacity、owner/type、foreign Queue、opaque tokens、旧 descriptor 拒绝、transfer fetch/release/reset、移除全部 Host endpoint，以及对称 EntityUpdate 后 setter 的精确 unsupported 状态与继续重放。[quota.log](quota.log) 以 192-byte STAGING 额度验证占位 payload 可创建而 invocation staging 准入失败，临时 views 释放且旧 descriptor 保留。

[失败注入](backend-resource-rollback.log) 在真实 CNDrv 调用上以 linker wrap 注入 Add、clone、Host alloc、memcpy rewrite、instantiate、invoke 和 completion record 失败，检查引用与配额回滚。completion record 与 Queue wait 同时失败时保留 orphan，成功 Context sync 后清理。私有 entity destroy、pinned free、view destroy 失败均保留所有权和相应额度，并经同步重试回收。未用伪造 native 成功代替执行。

正常 [server.log](server.log) 与 [quota-server.log](quota-server.log) 最终均为 `RESOURCE_TOTAL device=0 staging=0 image=0`、`DRAIN_COMPLETE workers=0 cleanup_errors=0`，无 quarantine；故障注入 stderr 为预期负例。[build.log](build.log) 保留最终构建，[verification.log](verification.log) 记录专项/回归汇总及 codegen、台账、API contracts、closure 检查。

[provenance.json](provenance.json) 与 kernel/execution/G1 下的同名文件记录各次运行的源码、Schema、SDK headers 和二进制 SHA256；[artifact-manifest.json](artifact-manifest.json) 记录最终源码与生成制品。最后仅补充相关 Entity setter 的 Schema 边界说明并刷新台账，Host/kernel 专项随后复验；既有 G2/G1 与核心回归的二进制和实现源码未变。入库时仅整理 cnmon 等日志的行尾空白，不改变观察值。最初方向与 FindInClone 探索日志另保留于 [contracts](../graph-host-contracts-20261107/README.md)，不计入专项通过数量。

后续工作是 G3 capture 状态、操作拦截和依赖前沿，再接 G4 callback/user refs/DOT 与普通 memory/VMM IPC。详见 [Graph/IPC 设计](../../docs/GRAPH_IPC.md)。
