# API 泛化与编译器元数据

本工作包保留 TCP v1、Session、ObjectID、fake VA、TransferID 和六 preset。当前共有 39 个固定字段 RPC，通过 `MLU_CAP_TYPED_API` 协商支持，生成 44 个公共包装；普通查询不再逐项手写协议。完整的 310 个公共声明进入 Schema，未审查的参数保留 `unknown/review_required`，不能直接进入自动生成路径。

## 实际参考与 MLU 取舍

实际阅读本机 `/home/lvtong/corex-remote-runtime` 的 `schema/corex_api_schema.json`、`scripts/generate-api-schema.py`、`scripts/census-corex-runtime-api.py`、`compat/cuda-api-ledger.csv`、`scripts/verify-api-contracts.py`。采用单一 Schema、预处理 SDK Census、状态与证据分离、生成物/Protocol/Export/Ledger 契约检查的组织方式。

实际阅读 `/home/lvtong/cricket/cpu/cpu-client.c` 的编译器注册 hooks、`cpu-elf2.c` 的 ELF 参数 metadata、`cpu-client-runtime.c` 的通用打包、`cpu-server-runtime.c` 的资源重定位、`resource-mg.c` 与 `generate_dispatch.sh`。采用 registration→module→多个 kernel、参数布局与类型分离、allocation+offset 重定位和生成 dispatch 的机制。

MLU 使用 CNCC 的 `__bangRegisterFatBinary/Function/Var`、CNFB v2/LZ4、设备 ELF `.params.text.<symbol>` 与 CNDrv `cnInvokeKernel`。不能使用 CUDA 的枚举、参数段格式、stream flags 或注册 wrapper 布局。CNRT/CNDrv 同名概念分别绑定对应 native 函数，例如设备属性枚举直接传给各自 API；结构体按命名字段展开；CNRT NotifierDuration 的微秒和 Driver execution timing 的毫秒明确转换。

## Schema 和生成边界

`schema/mlu_api_schema.json` 是签名、功能族、优先级、状态、边界及证据的单一来源。`compat/annotations.json` 改为生成的兼容视图，修改结论应编辑 Schema。Schema 包含：

- 310 个公共链接符号的返回类型、完整签名、参数类型/方向/长度/映射、依赖和 blocker。Scalar、Enum、Pointer、Buffer、Struct、Object Handle 有不同描述。
- `typed_rpc` 的 opcode、request/response 固定宽度字段、backend binding。公共 wrapper 显式指定输入映射、必需/可选输出、句柄解析和输出写入。
- `bindings` 将本轮 API 对应到 wrapper、请求/响应、dispatch handler 与 backend；手工入口用 `rpc_operations` 关联相同链路。
- `simple_rpc` 继续生成原来的 10 对对象包装/dispatch，`extra_exports` 单列 6 个私有 BANG hooks 与 legacy `cnrtInit`。

`scripts/generate_rpc.py` / `api_codegen.py` 自动生成 SDK 无关 C 请求/响应声明、网络字节序编码、f32 位编码、成功回复长度校验、Client RPC、Server Dispatch、Native Binding 函数、公共 ABI wrapper、链接导出 map、清单、协议测试 plan 与可编辑测试模板。`include/generated` 的协议头入 Git；`build/generated` 每次重建且受 `--check` 校验。

Binding 与 wrapper 的局部 C 语句仍须作者根据 SDK 审查填写；这不是从任意 C 原型自动推导远程语义的工具。生成路径拒绝 unknown 参数、缺少请求映射、重号 opcode、未定义 RPC。结构体禁止直接用 native `sizeof` 上线；例如设备 properties 的数组和每个整数均独立编码。输出仅在成功且回复结构校验通过后发布，`cnrtGetLibVersion` 的可空输出已按 native 探针处理。构建把不兼容指针类型警告设为错误，防止将 CNDrv 的 `cn_int64_t *` 错作 32 位结果。

复杂创建/销毁、context/TLS、对象回滚、异步 staging、Memset 的 owned range、2D/3D span、Kernel ABI 与重定位仍手写。统一 helper 复用原对象表和配额，不另造 RPC 框架。2D/3D 当前只支持 owned device allocations；3D 要求显式非零 pitch/ysize，别名和零 pitch 的完整边缘矩阵未验收，因此六个 stride-copy API 均保留部分实现。

## 自动化前后的工作量

之前仅有 10 个 ObjectID RPC 能生成。普通 API 需要分别增加 header、client 编解码、server 长度检查/dispatch、backend、导出与台账；各处重复字段和 opcode。现在新增一个已审查的普通 API，主要填写 Schema 中的字段/映射/native binding，再补功能族语义断言；其余重复路径一次生成。

首批 20 个 RPC、21 个公共 wrapper 对应 1,089 行生成的 C 包装/编解码/dispatch/binding（含声明）/成功形状检查代码，后续查询批次扩至 39 个 RPC、44 个 wrapper；最新行数及哈希见 [制品清单](../evidence/query-boundaries-20261102/artifact-manifest.json)，另有协议头、导出 map 和测试模板。行数不能当作人时节省或正确性指标。台账状态不会因为生成代码或导出符号而自动提升。

## BANG C 编译器类型 metadata

`scripts/generate_bang_metadata.py` 接受同一构建的 device LLVM IR 与实际嵌入的 `.cnfatbin`，提取 `mlisa_kernel` 参数，写入宿主 executable/DSO 的非加载 ELF section `.mlu.remote.abi`。构建命令见 `scripts/build.sh` 的 `metadata-audit` 段；也可重复传 `--pair LL:IMAGE` 附带多个 image 的记录。无需额外导出 helper，无运行时 SDK、环境 sidecar 路径或业务源码分支。

记录包含 image 字节数、FNV64 指纹、精确符号、参数数量、大小和 scalar/pointer 分类。指纹用于发现过期构建产物，不提供密码学认证。客户端通过注册的 host stub 定位宿主 ELF，独立读取设备 ELF 大小/offset，再交叉核对类型；不匹配或损坏会拒绝。无 metadata 的已有 binary 继续用原保守 demangler。

已通过 C 链接符号、枚举、模板、零参数、一个 module 的三个 kernel、NULL/offset pointer 和与 fake VA 位值相同的 scalar 数值验证。重定位复用现有 allocation ID+offset→native address 机制，完全不按 kernel 名称、参数个数或 scalar 位值猜类型。四类 stale/损坏 metadata 均被拒绝；旧 aggregate/C-symbol 无 metadata 拒绝测试保留。

本机 CNCC 在 LLVM IR 中给出 i64，却在设备 MLISA 中发出 `.s48`；Native tagged VA 作为 scalar 的高位会被截断，`unsigned long long` 也不能消除这个观察。测试保留实际 input/output 到 stderr，并按原生 low48 语义断言；没有宣称任意 64 位数值都能跨 ABI 保真。IR 类型有助于区分 pointer，但不能单独证明设备算术语义。

byval aggregate、嵌套 pointer graph、未知 IR 类型、复杂地址空间仍显式阻塞。Device Globals 沿用已有 Var 注册校验和 module 内未初始化 globals 共享测试；Host symbol 地址/拷贝、initialized/constant/external globals、Driver module 的 compiler 类型关联仍需实现，不能由 kernel 元数据成功推导完成。

## 扩展流程与回归

```sh
./scripts/build.sh
python3 scripts/generate_compat.py
python3 scripts/verify-api-contracts.py
python3 scripts/verify-api-families.py
python3 scripts/verify-transparent.py --out evidence/<batch>/full-suite
```

`generate_compat.py` 对 SDK 预处理 Census、头文件哈希、Schema 精确签名、真实链接导出和人工状态做校验，生成逐 API ledger、summary、功能族统计和剩余 API 的依赖计划。`verify-api-contracts.py` 再核对生成结果、opcode、codec/dispatch/binding、导出 allowlist、ledger 和完整状态的证据文件。

新增功能族测试比较稳定 stdout；实时 free memory 只比不变量，时间值只比非负及 disabled-timing 错误，stderr 单独留存 native 诊断。生成 test plan 为全部 39 个 typed RPC 提供坏长度与坏成功回复测试，另加跨 session、overflow、stale、alignment、string 和元数据负例。生成模板不会被当作通过测试；语义断言仍由作者编写。

剩余 310 项分类及逐项 blocker 见 [功能族计划](../compat/family-plan.md) 和 `compat/family-plan.json`。Context/config/PCI、allocation attribute/range 和 queue 属性基础已接入；后续补 Context flags/TLS、default queue ordering 和更广内存边界，实现 client host pages/completion/writeback，扩展 module/symbol 与 aggregate metadata；Graph/IPC/Peer 等等待相应依赖。CNNL 不在这 310 项分母，当前 SDK 缺库仍阻塞。本阶段无需第二物理主机和新增生产安全体系。

## 查询边界续批

Context/PCI、memory range/attributes、Queue union 本轮新增 19 个 typed RPC 与 23 个公共 wrapper，累计 39/44。新增 `cstring` 的请求/回复 NUL 检查、owner session 选择、显式 `native_status` 错误输出；语义 adapter 和批量 wrapper 仍手写。详见 [设计与边界](QUERY_BOUNDARIES.md)。`summarize-api-change.py --baseline <commit> --out <evidence-dir>` 机械生成逐项 delta 和制品哈希；完整状态仍需人工证据审查。
