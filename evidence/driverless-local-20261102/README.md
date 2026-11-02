# 本机模拟无 NPU 客户端：实测结果

2026-11-02，按用户要求把本机作为两个独立运行环境：宿主机服务端可访问真实 MLU，客户端进入独立 user/mount namespace，隐藏当前 Neuware SDK 和整个 `/dev`。两个进程通过 loopback TCP 通信。

[results.json](results.json) 保存服务器 PID、namespace、地址、每个客户端实际命令/环境、退出码和 binary SHA-256。3 次运行均通过，binary 前后哈希相同：

- [generic-native.log](generic-native.log)：普通 CNCC 程序在本机原生环境运行并验证数值。
- [native_bang_generic-isolated.log](native_bang_generic-isolated.log)：同一 executable 在隐藏 SDK、Driver 用户态库和设备节点后运行，通过 TCP 执行 generic_add_one。
- [native_bang_abi-isolated.log](native_bang_abi-isolated.log)：同样隔离条件下，混合标量、offset/NULL、xyz、3线程×400 launch、1200 async H2D、Queue/Notifier、reset/reload、stale 句柄/地址均通过。

[server.log](server.log) 证明独立真实 CNDrv server 接受会话。两端 user/mount namespace 不同、network namespace 相同；客户端日志中的 `VERIFIED vendor SDK path and /dev hidden` 来自真实文件可见性检查。脚本内部强制使用项目 Shim/transport 动态库，不依赖调用终端预先设置 LD_LIBRARY_PATH。

这项证据验收本机模拟下的客户端 Driver 解耦与透明执行；不声称已经测量第二物理主机的网络/部署行为。完整 API/ABI 限制与故障证据见 [主证据目录](../transparent-20261102/README.md)。两个终端的复现命令见 [演示说明](../../docs/DRIVERLESS_LOCAL_DEMO.md)。
