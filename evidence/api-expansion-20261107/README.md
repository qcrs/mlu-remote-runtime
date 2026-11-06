# Driver context configuration expansion — 2026-11-07

MLU370-S4 / Neuware 3.5.2 / AArch64，单设备。接入 `cnGetCtxConfigParam_pt` 与 `cnSetCtxConfigParam_pt`，使用 typed `ctx_config` RPC；只传 context-owned 的 type/value/set 标量，server 调用对应 CNDrv native API，union 不跨线传输。

Native/Remote driver family 输出逐字一致，覆盖：

- `CN_CTX_CONFIG_VISIBLE_CLUSTER` get：63
- `CN_CTX_CONFIG_VISIBLE_CLUSTER_NUM` get：6
- `CN_CTX_CONFIG_UNION_LIMIT` get：16
- union limit set=3 后 get：3

验证结果见 [Native](context-config-native.log) 与 [Remote](context-config-remote.log)。当前只验证单设备 context 和 union limit setter；visible-cluster setter、shared TLS、多设备语义未宣称完成，两个 API 保持 PARTIAL。

本批只运行 context/driver family 对照、L0 contracts/codegen 和既有 core family smoke；未运行 transport fault、quota、restart、DSO、fuzz 或 Full Regression。
