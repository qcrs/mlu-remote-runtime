# 本机双终端：模拟无 NPU 客户端

本轮按用户要求使用本机模拟：一个终端运行能访问真实 MLU 的服务端，另一个终端在独立 user/mount namespace 中运行客户端。客户端视图隐藏当前 Neuware SDK 和整个 `/dev`，设备操作仍通过普通 TCP 交给服务端。

新开终端只隔离交互界面，Driver 和设备仍然可见；下面的脚本会实际改变客户端的挂载视图。它不卸载本机 Driver，不修改宿主机挂载；客户端退出后隔离视图消失。

先在项目目录构建一次：

```sh
cd /home/lvtong/mlu-remote-runtime
./scripts/build.sh
```

终端 A，保留真实 MLU 环境：

```sh
cd /home/lvtong/mlu-remote-runtime
MLU_REMOTE_BIND=127.0.0.1 MLU_REMOTE_PORT=50051 ./scripts/run-server.sh
```

终端 B，模拟无 NPU、无 SDK 客户端：

```sh
cd /home/lvtong/mlu-remote-runtime
MLU_REMOTE_HOST=127.0.0.1 MLU_REMOTE_PORT=50051 ./scripts/run-isolated-client.sh ./build/native_bang_generic
```

脚本会打印 user/mount/network namespace、SDK/设备隐藏检查、TCP endpoint，以及：

```text
VERIFIED vendor SDK path and /dev hidden
PASS generic_add_one BANG C numerical check
```

再运行完整 ABI/资源压力程序：

```sh
MLU_REMOTE_HOST=127.0.0.1 MLU_REMOTE_PORT=50051 ./scripts/run-isolated-client.sh ./build/native_bang_abi
```

没有参数时也默认运行 ABI 程序。其 aggregate/extern C 的 unsupported 日志是预期拒绝用例；最后应打印两行 PASS，包含 1200 launches、1200 async copies、reset/reload、stale handles/VA。

同样可以运行多 Kernel 共享设备状态，以及动态库注册/卸载用例：

```sh
MLU_REMOTE_HOST=127.0.0.1 MLU_REMOTE_PORT=50051 ./scripts/run-isolated-client.sh ./build/native_bang_module
MLU_REMOTE_HOST=127.0.0.1 MLU_REMOTE_PORT=50051 ./scripts/run-isolated-client.sh ./build/native_bang_module_dso
```

新增原生 Driver 程序可使用 `MLU_REMOTE_SHIM=driver` 选择 `build/driver-shim/libcndrv.so`，隔离机制相同，完整命令见 [Driver Shim](DRIVER_SHIM.md)。默认仍选择 CNRT Shim。

前者应输出两个 Kernel 共享状态及 reset 后重载的 PASS；后者应输出 20 次 DSO reload、保留 loader 引用、pending-work unload 的 PASS。它们均使用原生 CNRT/CNCC 接口，不插入自定义 RPC 调用。新增 [共享模块验证证据](../evidence/shared-module-20261102/README.md) 已覆盖这两条隔离命令。

两个进程的 network namespace 相同，使 loopback TCP 可达；user/mount namespace 不同，使客户端看不到 SDK 和设备节点。终端 A 可以用 Ctrl+C 停止自己启动的服务端。脚本要求 Linux 允许 unprivileged user namespace/mount；本机已验证可用。

这项演示验收业务源码不改、原始 CNCC executable 不改、无 Driver 客户端运行和真实 TCP→MLU 执行。它没有模拟物理网络延迟、不同 CPU/发行版、重启故障，也不作为 hostile-client 安全沙箱。跨物理主机测试仍是后续独立部署项，不阻塞本轮本机模拟交付。

本轮实测输出和两端 namespace/binary 哈希见 [本机模拟证据](../evidence/driverless-local-20261102/README.md)；完整范围见 [主验证证据](../evidence/transparent-20261102/README.md)。

最新 [58 项回归](../evidence/driver-shim-20261102/README.md) 已通过四个隔离客户端，新增 Driver 原生/替换/preload、NULL queue 和故障检查，并覆盖 socket deadline、首包握手、重启失效、资源配额和 pending kernel/H2D 的 SIGTERM drain。默认超时及长任务配置见 [连接生命周期](TRANSPORT_LIFECYCLE.md)，配额与关闭边界见 [资源生命周期](RESOURCE_LIFECYCLE.md)。
