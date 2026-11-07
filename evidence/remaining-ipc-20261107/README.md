# Memory / Host / VMM IPC，2026-11-07

I1 六项与 I2 两项已在声明边界内实现：cnrtAcquireMemHandle、cnrtMapMemHandle、cnrtUnMapMemHandle，cnIpcGetMemHandle、cnIpcOpenMemHandle、cnIpcCloseMemHandle，cnMemExportToShareableHandle、cnMemImportFromShareableHandle。环境为 MLU370-S4 / Neuware 3.5.2，AArch64 LP64。

## 正式证据与结果

| 目录 | 模式/语义 | 通过数 |
|---|---|---:|
| [native](native/results.json) | vendor 库，独立 exec exporter/importer | 6/6 |
| [remote](remote/results.json) | replacement，独立 TCP Server/Client | 6/6 |
| [preload](preload/results.json) | 原生链接 executable + LD_PRELOAD | 6/6 |
| [isolated](isolated/results.json) | 客户端隐藏 SDK/设备节点 | 6/6 |
| [lifetime](lifetime/results.json) | 多 importer、源 free/reset/EOF、Graph refs、FD retire | 12/12 |
| [quota](quota/results.json) | 两库各 3 个 importer，只计一份物理 backing | 2/2 |

每个正常模式为四种 CNRT/CNDrv device 跨库组合、CNRT Host CPU 共享、VMM POSIX FD。exporter/importer 是不同 PID，读取初值、offset 修改、边缘 guard、close 与 exporter 最终数值均检查。每个模式的 provenance.json 记录 remote/mode、SDK、命令与探针 binary/header/source hashes。

**目录根部较早的同名 jsonl/results/provenance 是被替代的探索运行。**其中 provenance 的 remote=false 没有正确记录替换模式，不能用来证明 Remote。当前完成声明仅使用上表四个正常子目录和 lifetime/quota；旧记录保留以免改写历史。较早 Native Graph/IPC 与跨 Context kernel 基线分别见 [原生契约](../graph-ipc-contracts-20261107/README.md)、[Context 探针](../ipc-context-20261107/README.md)。

## 实现和生命周期

Device：8-byte 随机 token 在 Server instance 内解析，绑定 backing/kind；importer 获得自己的 AllocationID/fake VA。broker 保留实际 storage 及源 Context，普通 copy/memset/kernel/Graph 复用现有 lease 路径。公开 free imported identity 拒绝，close 只撤销本 importer 并 drain 它自己的工作。重复 export 保留 Native 的 100102 / CNRT 632015。

源 device allocation free/reset/EOF 撤销新 opens，已有 importer、Graph 与在途操作保持 backing/源 Context，最后引用释放才回收。这是 **Remote 安全回收策略**；Native 文档对先 free 源再使用 import 未定义，不能将 lifetime 用例称为 Native 相同顺序差分。正常四模式采用 close-before-free。lifetime 包含双库各 free/reset/EOF、Host free/reset/EOF、双库 Graph 在 close imported identity/source free 后仍正确回放，以及源 VMM Context/handle/map 退役后经 dup FD 导入访问。

Host：memfd/MAP_SHARED/mlock 与同 UID 抽象 Unix socket broker，使用 SO_PEERCRED、SCM_RIGHTS；两个进程直接 CPU 读写观察同一 backing。范围限 **同客户端主机、UID 和 Unix namespace**，没有跨客户端机器的裸 CPU coherence。Host source 为进程域，Context reset 不撤销仍存活的 Host allocation；free/退出阻止新 opens，既有映射可用至 unmap。guard/tombstone 用 private PROT_NONE 退役旧共享映射，避免继续占用 backing。

VMM：Native generic handle、mapping、reservation 与 descriptor refs 独立；client 持有真实 TCP socket FD，可 dup、SCM_RIGHTS 和跨 exec。未消费的 capability 通过 MSG_PEEK 取得；最后 descriptor close 触发 Server 对 Native FD/backing lease 的释放，源 Context 保活。Native 同进程 FD import 拒绝时，Server 临时 map+RetainAllocationHandle 获取引用。export 输出按本 SDK 实测 ABI 写 **8 bytes**，不是将 Server FD 数字伪装为客户端 FD。server 重启后旧连接/capability 失效。

Quota：global/session device cap 均 4096 bytes，exporter 分配 4096 后三个 importer 全部成功；此时额外 1 byte 失败，证明 import 不重复占物理额度。源 free、两个 importer close 后最后 importer 仍可读但新增 1 byte 仍失败，最终 close 后完整 4096 bytes 再分配成功。[quota/server.log](quota/server.log) 与 [server.log](server.log) 均以 `DRAIN_COMPLETE workers=0 cleanup_errors=0`、`RESOURCE_TOTAL device=0 staging=0 image=0` 结束。

## 复现与来源

先在独立 Server 终端运行：

```sh
LD_LIBRARY_PATH=/usr/local/neuware/lib64 MLU_REMOTE_BIND=127.0.0.1 MLU_REMOTE_PORT=50342 ./build/mlu-remote-server
```

在 Client 终端运行四个正常模式与生命周期：

```sh
python3 scripts/verify-memory-ipc.py --mode native --port 50342 --out evidence/remaining-ipc-20261107/native
python3 scripts/verify-memory-ipc.py --mode remote --port 50342 --out evidence/remaining-ipc-20261107/remote
python3 scripts/verify-memory-ipc.py --mode preload --port 50342 --out evidence/remaining-ipc-20261107/preload
python3 scripts/verify-memory-ipc.py --mode isolated --port 50342 --out evidence/remaining-ipc-20261107/isolated
python3 scripts/verify-ipc-lifetime.py --port 50342
python3 scripts/verify-ipc-quota.py --port 50346
```

quota runner 自管专用限额 Server。完成后给正常 Server 发送 TERM 并保留完整 drain 日志。

lifetime/provenance.json 保留原 12 场景执行时的 hash；其后 tests/native_ipc_lifetime.c 仅扩展 quota 入口并重编译，原 lifetime 源哈希因此与当前不同，未覆盖历史 provenance。quota/provenance.json 记录 quota 运行制品与最终测试源；[final-state.json](../remaining-closure-20261107/final-state.json) 记录最终实现/SDK/二进制快照。

正常 remote 模式运行时的 probe_graph_ipc.c 是本批基线 HEAD 版本（SHA-256 `8a9d15292df0550cbbd18e210e910d73e69193d93836a8e1e8de3c86694b0f03`）；随后仅新增 VMM exporter 的提前 retire 分支用于 lifetime 测试，未带 retire 参数的正常流程保持。native/preload/isolated 三个正常模式记录的是扩展后的源码（SHA-256 `223636c63e2f560393a77270ac1d783a444cf0807043963769b87aef84caa4b1`）。这一区别以及对应 Driver probe binary hash 保留在各自 provenance 中，不以当前重编译制品覆盖早期运行记录。

I3 的四个 notifier IPC 入口仍 BLOCKED：Native 支持属性为 0、合法 interprocess notifier 创建 NotSupported。未将未运行的成功路径算作完成。
