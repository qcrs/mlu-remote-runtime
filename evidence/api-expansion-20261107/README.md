# Driver context configuration expansion — 2026-11-07

MLU370-S4 / Neuware 3.5.2 / AArch64，单设备。接入 `cnGetCtxConfigParam_pt` 与 `cnSetCtxConfigParam_pt`，使用 typed `ctx_config` RPC；只传 context-owned 的 type/value/set 标量，server 调用对应 CNDrv native API，union 不跨线传输。

Native/Remote driver family 输出逐字一致，覆盖：

- `CN_CTX_CONFIG_VISIBLE_CLUSTER` get：63
- `CN_CTX_CONFIG_VISIBLE_CLUSTER_NUM` get：6
- `CN_CTX_CONFIG_UNION_LIMIT` get：16
- union limit set=3 后 get：3

验证结果见 [Native](context-config-native.log) 与 [Remote](context-config-remote.log)。当前只验证单设备 context 和 union limit setter；visible-cluster setter、shared TLS、多设备语义未宣称完成，两个 API 保持 PARTIAL。

本批只运行 context/driver family 对照、L0 contracts/codegen 和既有 core family smoke；未运行 transport fault、quota、restart、DSO、fuzz 或 Full Regression。

## Module memory query expansion

同一批次又接入了 `cnModuleQueryFatBinaryMemoryUsage` 和
`cnModuleQueryMemoryUsage`。Driver shim 将可读的 CNFB image（或客户端
path 读取出的 image）通过 `MODULE_QUERY_FATBINARY_MEMORY` 上传，server 在
当前 native context 中调用 `cnModuleQueryFatBinaryMemoryUsage`，只返回
`size_t` 结果；native 地址、文件路径和 vendor handle 不跨线。

`driver-native.log`、`driver-remote.log`、`driver-preload.log` 和隔离模式
输出见 [module-memory-family](module-memory-family/)。本机 MLU370-S4 输出
`path=110592 fatbin=110592`，四种 Driver 运行模式逐字一致。当前边界是
单个 ELF64 LE/CNFBv2 image；非 CNFB 格式、完整 malformed-input 矩阵和
不同 SDK 的错误码仍保持 PARTIAL。

本轮 L0 为 build、Schema/RPC/compatibility/API-contract 检查；L1 为
Driver module family 的 Native/Remote/Preload/isolated 对照及两个边界；L2
为 [`core-smoke`](module-memory-family/core-smoke/)。未重复运行 transport
fault、quota、restart、DSO stress、parser fuzz 或 Full Regression。
