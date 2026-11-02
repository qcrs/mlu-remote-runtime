# API Compatibility Ledger

`api-ledger.csv` 是当前安装 SDK 的公开 API 台账。`annotations.json` 保存人工审查结论；生成器不会根据函数名或导出存在自动提高支持等级。`summary.json` 给出准确分母、头文件 SHA-256、状态统计和独立统计的 6 个私有 BANG hooks。

```sh
python3 scripts/generate_compat.py
python3 scripts/generate_compat.py --check
```

生成器预处理 `cnrt.h` 和 `cn_api.h`（包括传递引入的 deprecated 声明），检查全部匹配声明，去重后生成 CSV，并分别核对 `build/shim/libcnrt.so` 与 `build/driver-shim/libcndrv.so` 的真实动态导出。CNRT 宏别名归一为预处理后的链接符号。CNDrv 行的 `backend_used=true` 仅表示服务端调用；独立 `client_export` 和人工状态才描述 Driver Shim 能力。

| 状态 | 含义 |
|---|---|
| IMPLEMENTED | 已实现，并在行内限定的单设备会话范围验证；不代表全部 SDK/架构认证 |
| PARTIAL_IMPLEMENTED | 有真实实现，但存在参数、异步、ABI 或验证缺口 |
| UNSUPPORTED | 当前 Shim 明确返回 NotSupport；不等于原生硬件不支持 |
| NOT_IMPLEMENTED | 未提供客户端导出；应用可能在动态加载/符号解析时失败 |
| GROUND_TRUTH_BLOCKED | 原生依赖或硬件缺失；本轮公开 CNRT/CNDrv 台账不滥用此分类 |

`native_test` / `remote_test` 中不带目录的 `.log` 相对 `evidence/transparent-20261102/`；带批次前缀（例如 `shared-module-20261102/suite/`）则相对 `evidence/`。`not_run` 明确表示尚无对应证据，尤其不能把拒绝函数计为已实现。CNNL 和第二台主机的阻塞记录在总体能力矩阵中，二者不属于这两个头文件的 API 分母。

RPC 重复封装由 `schema/mlu_api_schema.json` 的 `simple_rpc` 生成，目前 10 对客户端包装/服务端分派。对象归属、异步 staging、模块存活和参数重定位仍手写。旧六算子 registry/manifests 继续由原生成器维护。
