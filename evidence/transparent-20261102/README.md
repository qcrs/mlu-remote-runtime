# 透明 BANG C Remote Runtime 验证证据

日期：2026-11-02。MLU370-S4 / AArch64 / Neuware 3.5.2 / CNCC 4.5.1 / CNRT 6.5.2 / CNDrv 2.5.2。版本和硬件输出见 [environment.log](environment.log)。源码、SDK 头、编译产物和参考仓库哈希见 [provenance.json](provenance.json)。当前工作树未提交，不能仅用基线 commit 复现本批修改。

## 结果与范围

[results.json](results.json) 的 **29 项全部返回期望值**。其中 CNNL 用例通过表示明确拒绝，不能解释为 CNNL 执行成功。另在完整 suite 后扩展内存探针，补跑 native/remote H2D→D2D→D2H 和纯 host Client 构建；见 [supplemental-results.json](supplemental-results.json)。

所有 remote 设备用例经过独立 Client process→TCP→ServerSession→原生 CNDrv→真实 MLU，主 suite server 监听 `127.0.0.1:50192`。没有第二台物理服务器；**这不是跨物理主机验收通过报告**。namespace 验证也使用同主机网络连接，只隔离客户端 SDK/设备可见性。

## 构建与复现

```sh
./scripts/build.sh
python3 scripts/generate_compat.py
python3 scripts/verify-transparent.py --out evidence/new-batch
CLIENT_CORE_ONLY=1 NEUWARE_HOME=/nonexistent ./scripts/build-client.sh
python3 scripts/inspect_bang.py
```

完整编译输出见 [build.log](build.log)，包含现有 misleading-indentation/signedness warnings；没有宣称零 warning。`semantics-build.log` 记录扩展内存探针的单独编译。`client-core-build.log` 记录 `NEUWARE_HOME=/nonexistent`、`CLIENT_CORE_ONLY=1` 和 exit 0；编译器不使用 SDK include/lib 参数。

每个 runner `.log` 开头记录实际 COMMAND 和相关 ENV。`build/audit/` 大型产物由构建重建，不复制进 evidence；`host-stub.txt` 保留有行号和 SHA-256 的短汇编片段。

## 能力族对应证据

| 能力族 | 原生证据 | 远程或独立检查 |
|---|---|---|
| 普通 CNCC vector_add | `native_bang_remote-native.log` | 同前缀 replacement/preload 日志 |
| 非 preset kernel | `native_bang_generic-native.log` | replacement/preload：上传原始 generic_add_one，输出逐项 +1 |
| 同名不同实现、参数和并发 | `native_bang_abi-native.log` | replacement/preload：vector_add 实际减法+17，uchar/short/int/long long/float，pointer offset/NULL，xyz 2×2×2，3线程×400 launch，1200 async H2D |
| Queue / Notifier / 生命周期 | ABI native | ABI remote 额外验证 reset/reload、stale queue/VA、不可信 token、aggregate/extern C 拒绝、last-error；跨 session/reset/unload 另有专门日志 |
| 内存与错误语义 | `api-semantics-native.log` | `api-semantics-remote.log`、`api-semantics-compare.json`；零分配、Free(NULL)、负 ordinal、成功不清 sticky error、H2D→D2D→D2H、完成 query |
| 原六算子与对象功能 | 原有 BANG C preset 真机路径 | `client-test.log`、`operator-test.log`、`custom-kernel-test.log`、`session-isolation-test.log`、`device-select-test.log`、`session-reset-test.log` |
| 通用协议防错 | 非 native API 等价项 | `generic-protocol.log`：10 个坏参数/image/session 用例、拒绝后有效 launch、unload 后 stale ID |
| 坏服务端回复 | host-only transport 检查，不算 native MLU 用例 | `client-response-faults.log`：28 个故障/健康错误场景；输出不被写入，坏连接不继续发送或重放 |
| 无 Driver 客户端 | native 对照用 namespace 外 MLU | `dependencies.log`、`client.strace`、`driver-access.json`、`isolated-client.log`、`client-core-build.log` |
| image 和 parser | 实际 CNCC 编译产物 | `image-audit.json`：LZ4 输出与 device object 字节一致；`host-stub.txt`；`parser-bounds.log`：ASan/UBSan、截断/section/NOBITS/2000 mutation |
| codegen / ledger | 已安装公开头文件 census | `generated.log`、`compatibility-ledger.log`；310 个公共入口和 10 对对象 RPC |
| 异常断开 | 原生 cleanup 路径 | `abrupt.log` 和 `server.log`，以及补测的 `semantics-server.log` |

真异步 D2H、共享 globals、复杂类型、默认 queue 隐式排序、Union、多设备不是本批已验收能力。native/remote 使用同一 executable 路径且不重新编译；最终 artifact 哈希记录在 provenance。

## 无 Driver 证据的解释

`dependencies.log`：普通 CNCC executable 的 DT_NEEDED 包含 libcnrt.so；Shim 通过同 SONAME 替换。Client transport 不依赖 SDK 动态库；Shim 使用系统 libstdc++/liblz4 解析数据。

`driver-access.json` 的 forbidden_access 为空：对 replacement ABI Client 的 strace 未发现 libcndrv/libcnbin/cambricon device 访问。单靠这一点不能排除环境内已有文件；所以额外运行 user/mount namespace，把 `/usr/local/neuware-3.5.2` 和整个 `/dev` 用 tmpfs 覆盖，并确认 vendor libcnrt 和设备节点不可见，ABI 程序仍通过。

namespace 保留 host network，使用外部 server 的真实设备。它证明当前客户端进程无运行时 Driver/SDK 依赖，未证明别的 CPU/发行版或跨物理网络已完成验收。

## 观察到的原生失败和修正

- [compiler-double.log](compiler-double.log)、[compiler-double.json](compiler-double.json)：`probes/compiler_double.mlu` 的最小 double load/store 触发 CNCC frontend segmentation fault，exit 254；不是远程 runtime 返回的错误。未通过 double 端到端验收。
- [observed-native-context-sync-race.log](observed-native-context-sync-race.log)：原生多线程测试中 cnrtSyncDevice 与其他线程 queue teardown 交错曾得到 CN_QUEUE_ERROR_INVALID。当前压力用例让各 worker 同步所属 queue，再销毁；没有声称已修复 vendor context-sync 问题。此日志对应早期测试版本，当前源已改为 QueueSync，不能按旧 line 30 理解当前文件。
- CNFB 记录中 padded span 与 actual expanded size 不同，按小 image 假定二者相等会失败；多 kernel ABI image 暴露后修正。`image-audit.json` 记录实际字段。
- 原生 cnrtMalloc(0)/Free(NULL)/负 ordinal/sticky error 的探针结果用于修正 Shim；对照结果保留。
- TCP_NODELAY 去掉原先的偶然延迟后，立即 transfer query 可返回 NotReady；旧 operator 测试改为接受合法 pending，sync 后必须成功。
- 旧 Client 的短成功回复会返回成功但不赋值输出；现在统一检查结果长度和 D2H 声明长度。坏帧/截断/断线关闭连接，后续调用报 context poisoned；正常 native API 错误不会破坏连接。

`SESSION_CLEANUP_EMPTY` 是清理函数返回后的代码标记，尚未独立测量 native allocation/module/queue 全部归零；不能用该字样宣称无资源泄漏。长稳、配额、deadline、worker drain 和独立进程隔离依 [M4](../../design/MILESTONES.md) 验收。
