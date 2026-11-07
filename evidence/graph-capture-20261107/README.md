# G3 Capture：逻辑线程、依赖前沿与数值回放

日期：2026-11-07。设备/SDK：MLU370-S4、Neuware 3.5.2、CNRT 6.5.2、CNDrv 2.5.2、AArch64 LP64，单设备。G3 双库各 5 项 API 已按声明边界实现，Graph 从 72/102 提升至 82/102；全量状态为 227 IMPLEMENTED / 69 NOT_IMPLEMENTED / 0 PARTIAL / 11 BLOCKED / 2 OUT_OF_SCOPE / 1 UNSUPPORTED。

## 验证

`results.json` 保留 Capture 专项 97/97 个独立进程运行。Native 使用 vendor 库；Remote Server 与各 Client 是独立进程，replacement、preload 及 namespace 客户端均使用原测试程序。隔离日志明确显示 vendor SDK 和整个 `/dev` 被隐藏，网络仍可到独立 Server。

- 双库 18 个契约场景逐条 JSON 对照，CNDrv 另有两项跨 Context 场景：Begin/End/Info、三种 mode、异线程 End、QueueSync/Query、ContextSync、Notifier fork/join/unjoined/wrong-root/merge/external wait、Global unsafe 及跨线程/Context 失效、ADD/SET dependencies。忽略的差异仅 PID、版本显示和捕获 ID 的数值；其余状态/计数/输出保留行为必须完全一致。关键 Native 错误码另有显式断言。
- 数值测试将 H2D 源页设为 `PROT_NONE`，捕获 H2D → nested struct/array pointer relocation kernel → D2H，End 后恢复页面。捕获期间既不读 Host source，也不执行 device work；getter 核对原始函数/Kernel 身份与参数快照。修改原参数后，两次回放仍使用捕获参数、分别读取当前 Host 输入；Remote D2H 在成功 QueueSync 前不写目标。
- 既有 EntityInvoke 在 Native capture 内正常执行且捕获图零节点，Remote 保留该行为。Notifier 分支 join 后 root 继续前进，仍可成功 End 并数值回放。
- Remote reset/Context close 使普通 pending Host 输出与 active capture 共存，先完成 publication，再释放 capture；abrupt exit 仅校验服务端回收。
- `protocol.log` 校验 session owner/type、借用 graph 不可 Destroy、错误 dependencies 不修改 frontier、普通 raw MEMSET/MEMCPY/D2H_SUBMIT 无法绕过 capture、逻辑线程、跨客户端隔离、reset 与 active EOF。
- 两档 staging 配额：1024 bytes 下 Begin 回滚；40000 bytes 下 Notifier 元数据准入失败保留 ACTIVE，空图 End 成功。三个 Server 都无 quarantine，最终 `RESOURCE_TOTAL device=0 staging=0 image=0`，`DRAIN_COMPLETE workers=0 cleanup_errors=0`。

受影响回归保存在 `host/`、`kernel/`、`execution/`、`g1/`、`families/`、`host-async/`、`core/`；各自的 `results.json`、完整 stdout/stderr/Server 日志与 provenance 可直接复核。回归分别为 18/18、18/18、16/16、14/14、15/15、15/15、76/76；连同 Capture 合计 269/269。完整 build、codegen/compat、API contracts 与 closure audit 通过，总验证记录见 `verification.log`。

## Native 特例与声明边界

Global 异线程 End 返回 100362，保留输出并结束 capture；mode 1/2 可异线程 End。captured QueueSync/Query 返回 100360 并失效；ContextSync/SyncDevice 在三个 mode 下均成功且保持 ACTIVE。Global malloc/其他或默认 QueueSync 可跨线程、跨既有 Driver Context 使 capture 失效。独立 capture merge 返回 100363、两边保持 ACTIVE；外部普通 Notifier wait 返回 100365；分支 Queue End 返回 100366、保留输出并使 root INVALID。NONE/INVALID GetCaptureInfo 只写 status。

支持 owned explicit Queue、三种 SDK mode、1D async memset/H2D/D2H/D2D、现有 metadata 支持的 generic kernel、Notifier fork/join 与 ADD/SET frontier。单 copy <=1 MiB，Host endpoint 总和 <=16 MiB，graph/node/registry 配额沿用 G1/G2；捕获元数据受 staging 配额约束。pageable Host 地址由调用者保持有效。2D/3D capture、Host callback nodes 不在当前认证范围。G4 callback/user objects/DOT 和普通 memory/VMM IPC 仍为 NOT_IMPLEMENTED；未将架构缺口列为硬件 BLOCKED。

Capture 复用 G2 的 immutable graph payload、allocation/module leases、snapshot/update 与逐次 invocation Host replay/publication。Server 维护 session-owned capture 状态；客户端逻辑线程与 process-wide Global epoch 覆盖多个 Driver Context。copy 的 capture 判断与 Host 采样/普通 submit 共用同一锁，Server dispatch guard 再防止 raw RPC 直接执行。frontier cache 由 capture 持有到 End/reset/disconnect。

## 复现与来源

```bash
bash scripts/build.sh
python3 scripts/verify-graph-capture.py --port 50259 --out evidence/graph-capture-20261107
python3 scripts/verify-graph-host.py --port 50260 --out evidence/graph-capture-20261107/host
python3 scripts/verify-graph-kernel.py --port 50261 --out evidence/graph-capture-20261107/kernel
python3 scripts/verify-graph-execution.py --port 50262 --out evidence/graph-capture-20261107/execution
python3 scripts/verify-graph.py --port 50263 --out evidence/graph-capture-20261107/g1
python3 scripts/verify-api-families.py --port 50264 --out evidence/graph-capture-20261107/families
python3 scripts/verify-host-async.py --port 50265 --out evidence/graph-capture-20261107/host-async
python3 scripts/verify-transparent.py --port 50266 --out evidence/graph-capture-20261107/core
python3 scripts/verify-api-contracts.py
```

`provenance.json` 为最终 Capture 矩阵对应源码/二进制/SDK 头文件 SHA-256；runtime/driver metadata JSON 来自实际 CNCC 产物。较早回归的 schema hash 保留其运行时状态，后续仅补充状态/证据/绑定声明元数据，未改变已验证的 runtime code 或二进制。`final-artifacts.json` 单列提交前当前文件哈希，不覆盖原始运行 provenance。此前探索性 Capture 日志已移至 `/tmp`，不计入正式通过结果。
