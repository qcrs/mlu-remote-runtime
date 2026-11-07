# Host cast 收口（2026-11-07）

范围为本机 MLU370-S4 / Neuware 3.5.2 / CNRT 6.5.2，AArch64 LP64 little endian。cast 本身为纯 Host 运算，本批远端 endpoint 固定 `127.0.0.1:1`（不可达）；Native、replacement、LD_PRELOAD 和隔离客户端分别运行 Python ctypes 进程，未调用设备初始化。

## 复现

```sh
./scripts/build-client.sh
python3 scripts/verify-host-cast.py
python3 scripts/verify-api-families.py --out evidence/host-cast-20261107/families --port 50230
python3 scripts/verify-transparent.py --out evidence/host-cast-20261107/full --port 50231
python3 scripts/verify-closure-audit.py
python3 scripts/verify-api-contracts.py
gcc -std=c11 -O1 -g -frounding-math -fsanitize=undefined,float-cast-overflow -fPIC -shared -I/usr/local/neuware/include common/host_cast.c -o /tmp/libhostcast-ubsan.so -lm
UBSAN_OPTIONS=halt_on_error=1 python3 tests/host_cast.py /tmp/libhostcast-ubsan.so > /tmp/mlu-cast-ubsan.json
git diff --check
```

| 验证 | 结果 | 证据 |
|---|---:|---|
| Host cast 专项 | 4/4 模式；每种 7,507 观察 | [results.json](results.json) |
| 类型、状态、数值与源/目标 guard | 三种客户端均逐字节匹配 Native | [native.json](native.json)、[remote.json](remote.json)、[preload.json](preload.json)、[isolated.json](isolated.json) |
| 隐藏 vendor SDK、整个 `/dev` | 通过；远端不可达 | [isolation.log](isolation.log) |
| UBSan / float-cast-overflow | 7,507 cases，无诊断 | [ubsan.log](ubsan.log)、[provenance.json](provenance.json) |
| 功能族回归 | 15/15 | [families/results.json](families/results.json) |
| 完整核心回归 | 76/76 | [full/results.json](full/results.json) |
| codegen / ledger / closure / export contracts | 通过 | [full/api-contracts.log](full/api-contracts.log) |

矩阵涵盖 legacy 全 15 enum 的 225 对、V2 全 13 enum 的 169 对；plain/scalar/by-channel 三种参数、V2 六种 rounding、两组有限样本共 7,434 观察，另有 45 个 wide integer 及 28 个负 pos 观察。By-channel 使用 `[2,3,2]`、axis=1，创建后改写原 poses/scales/offsets/shape 数组；返回参数的 owned 内容保持不变。测试缓冲区显式指定精确长度，没有 `create_string_buffer` 默认额外 NUL 导致的 guard 误判。

Native 接受 legacy plain/scalar 的 24/13 对、V2 的 53/17 对，by-channel 两版本各两对（f32→i8/i16）；其余返回 ArgsInvalid。日志保留 Native 对拒绝组合输出的 stderr。V2 的 f64/i8→bf16 scalar quant 成功但不写目标，目标 poison 保留也逐字节比较。

数值语义包括 half/bfloat、rounding/saturation、legacy f32→i64 的 ±2³² clamp、half 窄化 wrap、quant offset 位解释、负 pos，以及 rz 改变线程浮点环境造成的数组后续元素差异。详细组合和数值特例见 [HOST_CAST](../../docs/HOST_CAST.md)。实现与原生 API 在普通有限样本中逐字节一致，不将此描述为全部 IEEE special values、double rounding 临界点或任意重叠内存的认证。

[Native boundary](native-boundary.json) 记录 legacy `int16(-32768)→half` 在独立进程中超过 2 秒仍未返回。此 SDK 缺陷不复制；矩阵 signed 最小值使用 min+1。按通道入口当前要求 count 等于 shape 总元素数，调用者提供有效 Host buffer 和存活参数；不新增 stale/free 后的 opaque Host handle 安全保证。

完整核心回归仍使用独立 TCP Server/Client，包括两库 Host deferred D2H、Queue retirement、模块/launch、资源失败回滚、连接与 namespace 隔离。结束为 `DRAIN_COMPLETE workers=0 cleanup_errors=0`、`RESOURCE_TOTAL device=0 staging=0 image=0`。[构建日志](build.log) 和 [源/二进制哈希](provenance.json) 保存本批来源；build 产物不提交。

本批两个 PARTIAL 升级，保留原审计快照：145 IMPLEMENTED、0 PARTIAL、7 BLOCKED、2 OUT_OF_SCOPE、1 UNSUPPORTED、155 NOT_IMPLEMENTED。Graph/IPC 为后续共享状态机阶段；不因此标记整体目标完成。
