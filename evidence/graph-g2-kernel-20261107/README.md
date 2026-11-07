# G2：owned kernel 参数与 module leases

环境：2026-11-07，MLU370-S4 单设备、Neuware 3.5.2 / CNRT 6.5.2 / CNDrv 2.5.2、AArch64 LP64。相对 `d72dd36` 新增双库各 7 项 Graph kernel API，共 14 项。Graph 从 58/102 增至 72/102，G2 在逐 API 声明边界内为 44/44；Host graph memcpy staging/publication 仍未实现，未宣称整个 Graph/IPC 完成。

## API 与共享实现

新增 AddKernelNode、KernelNodeGetParams、KernelNodeSetParams、EntityKernelNodeSetParams、KernelNodeGetAttribute、KernelNodeSetAttribute、KernelNodeCopyAttributes。14 个公共 wrapper 均由 Schema 生成，共享两个 typed RPC（87～88）；当前累计 54 个 typed RPC、116 个公共 RPC wrapper，另有 4 个本地 error API。逐项变化见 [change-counts.json](change-counts.json)。

`scripts/buffer_codegen.py` 新增有界 borrowed request buffer，可与 caller-owned response buffer 组合，生成精确动态长度、容量和成功回复检查，Server 输出先清零。Graph adapter 将依赖 ID 与参数放在最多 2 MiB 的请求中；canonical wire 每参数使用 32-byte header、scalar bytes 及每字段 24-byte relocation，最多 256 args、512 nested pointer relocations，每参数最多 4096 bytes。只认证 `kernelParams`、`reserve=0`、`extra=NULL`；自然 LP64 aggregate 类型与指针字段复用编译器 metadata，保留既有 primitive fallback。pointer slots 清零后仅传 AllocationID+offset，scalar 位值不被猜成指针。

Server immutable kernel record 拥有 canonical wire、aligned 参数存储、module 和 allocation leases；clone/child/entity snapshot、setter override 和每次 invocation 共享引用。持久 record/wire/aligned storage 计入 STAGING，native 操作前准入；失败释放本次 refs/charge。GetParams 在客户端建立 node-owned 参数快照，使用原始函数身份、offset/NULL 与退役 VA tombstone；祖先链用于 child/graph 销毁时清理 cache。reset/context close/析构清理缓存。

公开 module unload 使 module/kernel ID 失效，graph/entity/invocation 保留 native module/image；最后引用消失才 unload 和释放 IMAGE 额度。延迟 unload 失败保留 backing 和额度，成功 Context sync 重试回收。公开 allocation free 同样只退役身份，持有的参数记录继续保活 backing。提前 free/unload 是 Remote 安全策略，单列验证；Native 数值基线使用存活资源，未把不同调用顺序称为相同语义差分。

属性只认证 AccessPolicyWindow（ID 1），baseAddr 为 owned allocation+offset 或 NULL，其余值由 native 判定；CopyAttributes 限同图 kernel nodes。Native 默认 Get、零值 Set/Copy 成功。所测非空 64-byte window、hitRatio=0.5 返回 100006，Remote 相同；没有声称非空属性成功。

## 复现

```bash
./scripts/build.sh
python3 scripts/verify-graph-kernel.py --out evidence/graph-g2-kernel-20261107 --port 50254
python3 scripts/verify-graph-execution.py --out evidence/graph-g2-kernel-20261107/execution --port 50253
python3 scripts/verify-graph.py --out evidence/graph-g2-kernel-20261107/g1 --port 50252
python3 scripts/generate_rpc.py
python3 scripts/generate_compat.py
python3 scripts/verify-api-contracts.py
python3 scripts/verify-closure-audit.py
python3 scripts/verify-api-families.py --out evidence/graph-g2-kernel-20261107/families --port 50251
python3 scripts/verify-transparent.py --out evidence/graph-g2-kernel-20261107/regression --port 50250
python3 scripts/summarize-api-change.py --baseline d72dd36 --out evidence/graph-g2-kernel-20261107
git diff --check
```

Server 与客户端为独立进程/地址空间，两库分别运行 Native、replacement、preload 和隐藏本地 SDK/设备节点的 namespace 客户端。数值差分比较 PASS 与 `attribute_status=` 行，原始 SDK stderr 单独保留。

## 验证

[专项 18/18](results.json)：两库各 Native/replacement/preload/isolated/lease/reset/abrupt 共 14 项，加协议、清理后新 session、真实 backend 失败回滚与 1024-byte staging 配额场景。数值检查 nested struct/array 内不同 offset pointers、NULL、与 fake VA 相同位值的 scalar、三项结果及整段输出 guard，另含零参数 kernel、getter snapshot、clone 独立、entity setter/update、embedded child getter 和源图销毁后执行。pending EntityDestroy/QueueDestroy 后 Context sync 检查数值；Remote lease 模式在提前 free 后回放，Driver 同时提前卸载 module 并拒绝旧公开 kernel，getter 仍恢复原身份。Runtime reset、Driver context close 和 `_exit`/EOF 时保留源图/entity 以验证清理。

[协议](protocol.log) 验证精确动态长度、2 MiB 上限、依赖/参数数量、pointer slots、nested relocation order/overlap/alignment/range、foreign graph/module/kernel/allocation、node type、entity membership、attribute ownership、退役 getter 和 reset stale IDs。[quota.log](quota.log) 验证参数额度不足在 native Add 前返回 RESOURCE_LIMIT，图仍为空且 session 健康。

[backend 回滚](backend-resource-rollback.log) 使用真实 CNDrv，仅 linker wrap 指定 native 调用失败，不伪造成功。Add/Set/EntitySet 失败平衡本次 STAGING/module/allocation refs，保留旧参数；成功 Set 回收公开退役旧 allocation。clone/entity 共享 record，不重复计费；entity override 单独保留。最后引用消失时注入 unload 失败，确认 IMAGE 仍占用，随后同步重试释放。既有 graph/child rollback、copy refs、D2H 和 queue cleanup 注入也通过。

正常 [server.log](server.log) 与 [quota-server.log](quota-server.log) 均无 quarantine，结束为 `RESOURCE_TOTAL device=0 staging=0 image=0`、`DRAIN_COMPLETE workers=0 cleanup_errors=0`。注入失败的 stderr 是预期证据，不能据此宣称正常 server 清理失败。

[既有 G2 16/16](execution/results.json)、[G1 14/14](g1/results.json)、[功能族 15/15](families/results.json) 和 [核心 76/76](regression/results.json) 通过。[build.log](build.log) 保留完整构建，[verification.log](verification.log) 保留 codegen/ledger/contracts/closure 与 whitespace 检查。初次核心执行遇新 Schema 缺少 blocker 台账字段，补全元数据并刷新 ledger 后复验；功能测试未因此修改。

[provenance.json](provenance.json) 保留 source/schema/SDK headers 与二进制 SHA256，[runtime-metadata.json](runtime-metadata.json)、[driver-metadata.json](driver-metadata.json) 保留编译器布局，`GraphPayload` 参数 56 bytes，pointer fields 为 offsets 0/8/24/32。[artifact-manifest.json](artifact-manifest.json) 记录生成制品；最后仅恢复 Graph/IPC 计划的原始 Native 证据链接并刷新 Schema 哈希，未改变 wire、wrapper 或 backend；日志入库时只整理行尾空白。

## 状态与剩余

217 IMPLEMENTED、79 NOT_IMPLEMENTED、0 PARTIAL、11 BLOCKED、2 OUT_OF_SCOPE、1 UNSUPPORTED，公共分母为 310。CNRT 95 implemented / 29 not，CNDrv 122 / 50。BLOCKED 仍为 7 个 Peer 缺第二物理设备，以及 4 个 IPC notifier 缺本机 Native 支持前提。

剩余 Host graph copy 每次 invocation staging/publication、G3 capture 10 项、G4 callback/user refs/DOT 20 项、普通 memory/VMM IPC 8 项。Capture 需逻辑客户端线程状态与 Host 完成顺序；IPC 需 backing broker、可行的 native Context 执行模型及真实客户端 OS handle transport。非当前 SDK/image ABI、device pointer graph、packed/opaque aggregate 和同 entity 跨 Queue 并发仍未认证。详见 [设计](../../docs/GRAPH_IPC.md)。
