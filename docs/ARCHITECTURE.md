# 架构入口

当前主链路：

```text
原始 BANG C / CNCC host stub
  → 原生 libcnrt.so Shim（CNRT ABI + __bang 注册 hooks）
  → libremote_cnrt.so / 原有 MLUR TCP
  → 每连接 ServerSession / 对象注册表
  → Native CNDrv / MLU
```

Shim 保存真实 CNCC 注册 image，解析 device 参数 metadata，在首个 launch 时上传 image；服务端调用 `cnModuleLoadFatBinary`、`cnModuleGetKernel`、`cnInvokeKernel`。host function 地址仅在客户端作注册 key；内存通过 AllocationID+offset 重定位，native pointer/handle 不作为设备地址跨 wire 传递。

原有六个 preset/operator registry、manifest、TCP、Session、Queue/Notifier/TransferID、staging 和 reset 路径保留。通用 image 路径已实现，不依赖 preset 名称；它目前限制单 image、primitive 参数、单层指针、当前工具链/CPU ABI。

原生 Driver 程序另经 `libcndrv.so Shim → client core → TCP` 接入同一个 backend。每个 Driver context 独立 session，TLS current binding，module 在 load 时立即上传，NULL queue 交给 native 并用真实 notifier 跟踪完成；销毁使用带 cleanup ACK 的 SESSION_CLOSE。Driver ABI 已部分实现，详见 [范围与证据](DRIVER_SHIM.md)。它不是完整 vendor CNRT 底层替代层。

客户端只依赖系统库，运行无需 vendor SDK/Driver；完整构建中的 CNCC 与服务端仍需原生环境。每连接有独立 context 和 owner/generation 对象注册；异步 H2D staging 保持到完成，D2H 目前回复前同步，free/unload/reset 使用保守同步。

权威设计、协议和信任边界见 [REMOTE_RUNTIME](../design/REMOTE_RUNTIME.md)；原始编译/注册证据见 [BANG_EXECUTION_CHAIN](BANG_EXECUTION_CHAIN.md)；当前能力和缺口见 [PROJECT_STATE](PROJECT_STATE.md)；后续依赖/验收见 [MILESTONES](../design/MILESTONES.md)。
