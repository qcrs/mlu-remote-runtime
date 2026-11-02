# CNDrv Shim 与无 Driver 客户端验证

2026-11-02，server310p / AArch64 / MLU370-S4 / Neuware 3.5.2 / CNCC 4.5.1。新增独立 `libcndrv.so` Shim，复用已有 TCP、Session、对象表、内存、Queue/Notifier、共享 module 和配额管理；CNRT 与旧六 preset 路径继续回归。实现语义和限制见 [Driver 说明](../../docs/DRIVER_SHIM.md)。

## 最终结果

[final-suite/results.json](final-suite/results.json) **58/58 返回期望值**；[verify.log](verify.log) 保存运行索引，[build-final.log](build-final.log) 保存完整构建。suite 新增 10 项 Driver 检查，包含 4 个隐藏 SDK/设备的客户端场景。CNNL 项仅验证明确拒绝，不能计为 CNNL 原生执行成功。

```sh
./scripts/build.sh
python3 scripts/generate_compat.py
python3 scripts/verify-transparent.py --out evidence/driver-shim-20261102/final-suite
```

suite 启动自己的 loopback TCP server，默认端口 50192；故障/配额检查使用各自的临时端口，只关闭测试自己启动的进程。本批未使用第二台物理主机。

| 能力 | 证据与验收范围 |
|---|---|
| 原生 Driver executable | 同一个普通 C/SDK binary 的 [native](final-suite/driver-native.log)、[replacement](final-suite/driver-replacement.log)、[LD_PRELOAD](final-suite/driver-preload.log)；文件/fatbinary 上传、共享 globals、两个 context 隔离、线程 TLS、混合标量/offset/NULL/xyz、1200 launch 和 1200 async H2D 均通过 |
| 原生语义对照 | [native](final-suite/driver-semantics-native.log) 与 [remote](final-suite/driver-semantics-remote.log) 除命令/环境头外逐行一致；覆盖初始化、零大小、错误输出保留、缺失 symbol 清零、current context、四种调度标志及低 32 位 device ordinal |
| 原生 NULL queue | 三种 Driver 运行均实测显式/NULL queue 的有限 kernel 在卸载前为 NOT_READY=1；module unload 后数值正确，notifier 可等待；没有用固定成功代替 query |
| Driver 协议和所有权 | [driver-protocol.log](final-suite/driver-protocol.log)：合法/非法 HELLO16 flags、坏 binary 长度/格式、context sync/close 尾部数据、共享 module、跨 session 句柄拒绝；pending NULL queue close 返回 cleanup ACK 和 EOF，另一客户端继续工作 |
| 无 SDK/设备视图 | [isolated-driver.log](final-suite/isolated-driver.log) 隐藏当前 Neuware SDK 和整个 `/dev`，Driver 数值及生命周期通过；原有 [CNRT ABI](final-suite/isolated-client.log)、[共享 module](final-suite/isolated-shared-module.log)、[DSO](final-suite/isolated-module-dso.log) 也通过 |
| 动态依赖及设备访问 | [driver-dependencies.log](final-suite/driver-dependencies.log) 的 readelf/ldd 表明实际解析到项目 Shim/core 和系统库；[driver.strace](final-suite/driver.strace) 与 [访问审计](final-suite/driver-access-driver.json) 无成功 vendor SDK open 或 MLU 设备访问；原有 CNRT 的 [依赖](final-suite/dependencies.log) 与 [访问审计](final-suite/driver-access.json) 同样通过 |
| Driver 故障语义 | [driver-client-faults.log](final-suite/driver-client-faults.log)：5 组配置/坏 close 回复/EOF/坏 sync 回复/旧 capability；不重放，destroy 后旧 context 不可绑定，独立 control session 保持可用 |
| 故障、资源与关闭回归 | [21 组服务端传输](final-suite/server-transport/results.json)、[3 组暂停/kill/restart](final-suite/server-restart/results.json)、[19 组资源/关闭](final-suite/server-resources/results.json)、[backend 回滚](final-suite/backend-resource-rollback.log) 与 [quarantine](final-suite/backend-resource-quarantine.log) 均通过 |
| CNRT 与旧执行能力 | 六组 CNCC 程序的 native/replacement/preload、协议负例、同名不同实现、自定义 image、共享 globals/DSO、1200 launch/copy、reset/stale、旧六 preset 全部回归，详见 results 中对应日志 |
| Parser 和生成 | [parser-bounds.log](final-suite/parser-bounds.log)：ASan/UBSan 下 kernel parser 与两种 variable image 各 2000 次 mutation；[RPC 生成检查](final-suite/generated.log) 与 [台账检查](final-suite/compatibility-ledger.log) 通过 |

[final-suite/server.log](final-suite/server.log) 末尾为 `DRAIN_COMPLETE workers=0 cleanup_errors=0`、`RESOURCE_TOTAL device=0 staging=0 image=0`。物理驻留的独立 native/remote cnmon 对照仍是 free 后 112 MiB、context reset 后 32 MiB、worker 退出后 0 MiB；逻辑配额不能解释为物理硬隔离。

[client-core-build.log](client-core-build.log) 记录 `CLIENT_CORE_ONLY=1 NEUWARE_HOME=/nonexistent` 成功重建；[产物哈希](client-core-build.json) 确认重建前后所有现有 build 文件完全相同。Shim 编译仍需 SDK 头文件，运行不需 vendor Driver/runtime；不能将运行解耦解释为 CNCC 编译无需 SDK。

## 台账与未完成边界

公共 API 分母为当前 SDK 头文件的 310 个声明，私有 BANG hooks 单列。当前 [台账](../../compat/api-ledger.csv) 为：

| 层 | 已实现 | 部分实现 | 明确不支持 | 未实现 | 合计 |
|---|---:|---:|---:|---:|---:|
| CNRT client ABI | 12 | 14 | 8 | 96 | 130 |
| CNDrv client ABI | 6 | 27 | 1 | 146 | 180 |

Driver 共导出 34 个公共入口，其中 1 个明确拒绝。真实 NULL queue 的 kernel/notifier/sync/query 已实现，但 MEMCPY wire 的 queue=0 仍表示同步，非零字节 NULL queue async copy 明确拒绝；整个 `cnMemcpyDtoHAsync` 也明确拒绝。pinned/writeback、复杂 kernel ABI、完整默认队列隐式排序、更多 context/device API、完整 vendor CNRT 底层替换、长期资源计量及跨物理主机仍待验收。生产认证/TLS 和 native 卡住时的有界取消尚未实现。

本机新开终端需要配合 [隔离脚本](../../scripts/run-isolated-client.sh) 才能模拟无 NPU 客户端；仅开终端仍共享本机设备。两端共享 network namespace，通过 loopback TCP 通信；客户端单独隐藏 SDK/设备，服务端保持真实 CNDrv/MLU。复现命令见 [双终端说明](../../docs/DRIVERLESS_LOCAL_DEMO.md)。

## 保留的失败证据

- [driver-native-first.log](driver-native-first.log)：测试误以为缺失 kernel 时保留输出，实测 native 将它清零。探针和 Shim 已按原生行为修正。
- [driver-native-second.log](driver-native-second.log)：循环 async H2D 重用主机源缓冲，未遵守原生完成前的生命周期。测试现为 1200 次上传各保留独立源元素，完成后再释放；native/remote 均通过。
- [first-suite](first-suite/results.json) 已执行的命令均返回 0，但随后依赖审计误把 executable RUNPATH 下查找系统 `libc.so.6` 的 ENOENT 记录判为 vendor 依赖泄漏，故该批不算完整通过。原始 [访问记录](first-suite/driver-access-driver.json) 保留。检查现区分失败查找与成功打开；最终 [driver-access-driver.json](final-suite/driver-access-driver.json) 同时保留失败搜索和空的 forbidden_access，namespace 隔离另有独立验证。

本轮构建和回归命令索引见 [verify.log](verify.log)，58 个用例的逐项退出码见 [results.json](final-suite/results.json)。SDK-free 客户端 core 重建的前后产物 SHA-256 见 [client-core-build.json](client-core-build.json)。完整构建日志中有少量原有 compiler/style warning；上述链接状态检查、RPC 生成器和 310 项台账校验均通过。源码尚未提交，基线 commit 不代表当前工作树。
