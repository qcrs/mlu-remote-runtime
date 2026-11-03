# CNDrv error string API batch

基线 `e8a2401`：CNDrv 25 IMPLEMENTED / 46 PARTIAL / 1 UNSUPPORTED / 108 NOT_IMPLEMENTED。当前为 25 / 48 / 1 / 106；CNRT 未变化，为 20 / 30 / 3 / 77。总计从 45 / 76 / 4 / 185 变为 45 / 78 / 4 / 183。

增加 `cnGetErrorName` 与 `cnGetErrorString`，共享一个 typed RPC opcode `API_DRIVER_ERROR_TEXT=72`。Server 调用 CNDrv native 的输出参数 ABI，并将文本限制在 256 字节；client 将文本复制到线程局部缓冲区。两个 API 只验证 `CN_SUCCESS`，未知错误和 SDK 版本差异保留 PARTIAL。

自动化从 39 到 40 typed RPC、44 到 46 public wrappers，10 对 simple object RPC 不变。Native/Remote 输出中两者均报告 `CN_SUCCESS: "no error"`。

验证：L0 build、`generate_rpc.py --check`、compat generation、API contract checker；L1 API family native/remote；L2 replacement、LD_PRELOAD、isolated client 与 metadata/protocol smoke，全部 15 个 family suite 项返回 0。L3 未运行：本批没有修改 Session、transport framing、fake VA、Object ownership、module/kernel registration 或 Host completion 核心。
