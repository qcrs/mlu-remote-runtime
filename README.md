# MLU Remote Runtime

原生 BANG C 程序由 CNCC 编译，通过 CNRT Shim 把设备操作和真实 Kernel image 转发到另一进程的 CNDrv/MLU 执行。业务源码和 CNCC 生成的 executable 均无需修改；用户在运行时选择远程动态库。

另提供部分原生 [CNDrv Shim](docs/DRIVER_SHIM.md)：普通 Driver 程序可通过 `build/driver-shim/libcndrv.so` 使用独立 context、文件/fatbinary module 和通用 launch，客户端同样无需 vendor Driver。

```text
BANG C Application / CNCC host stub
  → libcnrt.so Shim → libremote_cnrt.so → TCP
  → ServerSession → Native CNDrv → MLU
```

当前在 MLU370-S4 / Neuware 3.5.2 真机上验证了原生、动态库替换、LD_PRELOAD、隐藏 SDK 和设备节点的客户端运行。真实注册 image、pointer-free 按值 struct/嵌套 struct/固定数组（[证据](evidence/aggregate-20261103/README.md)）、混合标量、指针偏移/NULL、三维 task grid、1200 次 launch、1200 次异步 H2D、线程、资源回收和旧六算子均有证据。**跨第二台物理主机的最终验收尚未完成。**

```sh
./scripts/build.sh
python3 scripts/generate_compat.py
python3 scripts/verify-transparent.py
```

服务端在具有 SDK/Driver/MLU 的服务器启动：

```sh
MLU_REMOTE_BIND=0.0.0.0 MLU_REMOTE_PORT=50051 ./scripts/run-server.sh
```

客户端部署 `build/shim/libcnrt.so` 和 `build/libremote_cnrt.so`，以及 CNCC 编译好的程序；运行不依赖原生 MLU Driver 或 vendor runtime。当前程序和库是 AArch64 构建。客户端仍需系统 glibc、pthread、dl、libstdc++、liblz4；编译 Shim 需要 SDK 头文件。

```sh
MLU_REMOTE_HOST=SERVER_IP MLU_REMOTE_PORT=50051 \
LD_LIBRARY_PATH="$PWD/build/shim:$PWD/build" ./build/native_bang_generic
```

当前默认只监听 localhost。已实现 socket deadline、首包 HELLO 和故障连接失效；配置与恢复边界见 [连接生命周期](docs/TRANSPORT_LIFECYCLE.md)。认证、TLS 和应用恢复尚未完成，跨网络部署目前限可信、受控网络。

- [总体设计与执行边界](design/REMOTE_RUNTIME.md)
- [架构审计与参考项目](docs/ARCHITECTURE_AUDIT.md)
- [BANG C 编译/注册/执行链路](docs/BANG_EXECUTION_CHAIN.md)
- [完整 API 台账](compat/api-ledger.csv)、[状态解释](compat/README.md)
- [M1–M4 路线与验收](design/MILESTONES.md)
- [当前状态](docs/PROJECT_STATE.md)、[构建与测试](docs/DEVELOPMENT.md)
- [真机证据及未验收项](evidence/transparent-20261102/README.md)
- [本机双终端模拟无 NPU 客户端](docs/DRIVERLESS_LOCAL_DEMO.md)
- [多 Kernel 共享 Module、设备状态和 DSO 生命周期证据](evidence/shared-module-20261102/README.md)
- [连接超时、首包隔离和重启证据](evidence/transport-20261102/README.md)
- [资源配额、原生清理和 SIGTERM drain](docs/RESOURCE_LIFECYCLE.md)
- [资源隔离与关闭验证证据](evidence/resources-20261102/README.md)
- [原生 CNDrv Shim、context/module/NULL queue 语义](docs/DRIVER_SHIM.md)
- [Driver native/replacement/preload 与隔离验证证据](evidence/driver-shim-20261102/README.md)

## API 自动化扩展（2026-11-02）

首个 API 扩展批次新增 30 个 CNRT/CNDrv 公共 API，接通 Client→Protocol→Server→Native Backend；另修复 5 个原拒绝入口。后续查询边界批次新增 24 个 API，2026-11-03 增加 2 个部分实现的 Driver error string API。Schema 覆盖 310 个公共声明，当前生成 40 个 typed RPC、46 个公共 wrapper，并维护导出/台账/契约检查。当前为 45 完整、78 部分、4 拒绝、183 未实现。详情见 [设计和扩展流程](docs/API_AUTOMATION.md)、[查询边界设计](docs/QUERY_BOUNDARIES.md)、[功能族计划](compat/family-plan.md)、[本批证据](evidence/api-expansion-20261103/README.md)。
