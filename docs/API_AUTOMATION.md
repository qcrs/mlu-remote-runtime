# API 泛化与编译器元数据

本工作包保留 TCP v1、Session、ObjectID、fake VA、TransferID 和六 preset。当前共有 48 个 typed RPC，通过 `MLU_CAP_TYPED_API` 协商支持，生成 72 个公共 RPC 包装和 4 个本地 error API；普通查询不再逐项手写协议。完整的 310 个公共声明进入 Schema，未审查的参数保留 `unknown/review_required`，不能直接进入自动生成路径。

2026-11-07 error closure：四个 CNRT/CNDrv error API 改为 `generated_local`。显式 SDK-builder 命令 `capture-error-catalog.py` 从当前头文件枚举并集捕获 139 个不同错误值的原生文本；reviewed JSON 纳入版本控制，普通 build 只消费 JSON。生成器输出 immutable Host-local 查询、枚举静态断言和仅含输入值的 native probe，不依赖客户端 Driver/设备/Server。已验证初始化前、远端 port 1、replacement/preload/namespace 的全目录与八个未知值，区分 CNRT 在 199999 以上的回退。旧 typed error RPC 保留 wire 兼容，公共入口不再使用线程共享可覆盖文本。Native Driver 的 NULL 输出崩溃，Shim 安全返回 INVALID_VALUE。

## 实际参考与 MLU 取舍

实际阅读本机 `/home/lvtong/corex-remote-runtime` 的 `schema/corex_api_schema.json`、`scripts/generate-api-schema.py`、`scripts/census-corex-runtime-api.py`、`compat/cuda-api-ledger.csv`、`scripts/verify-api-contracts.py`。采用单一 Schema、预处理 SDK Census、状态与证据分离、生成物/Protocol/Export/Ledger 契约检查的组织方式。

实际阅读 `/home/lvtong/cricket/cpu/cpu-client.c` 的编译器注册 hooks、`cpu-elf2.c` 的 ELF 参数 metadata、`cpu-client-runtime.c` 的通用打包、`cpu-server-runtime.c` 的资源重定位、`resource-mg.c` 与 `generate_dispatch.sh`。采用 registration→module→多个 kernel、参数布局与类型分离、allocation+offset 重定位和生成 dispatch 的机制。

MLU 使用 CNCC 的 `__bangRegisterFatBinary/Function/Var`、CNFB v2/LZ4、设备 ELF `.params.text.<symbol>` 与 CNDrv `cnInvokeKernel`。不能使用 CUDA 的枚举、参数段格式、stream flags 或注册 wrapper 布局。CNRT/CNDrv 同名概念分别绑定对应 native 函数，例如设备属性枚举直接传给各自 API；结构体按命名字段展开；CNRT NotifierDuration 的微秒和 Driver execution timing 的毫秒明确转换。

## Schema 和生成边界

`schema/mlu_api_schema.json` 是签名、功能族、优先级、状态、边界及证据的单一来源。`compat/annotations.json` 改为生成的兼容视图，修改结论应编辑 Schema。Schema 包含：

- 310 个公共链接符号的返回类型、完整签名、参数类型/方向/长度/映射、依赖和 blocker。Scalar、Enum、Pointer、Buffer、Struct、Object Handle 有不同描述。
- `typed_rpc` 的 opcode、request/response 固定宽度字段、可选 `response_buffer` 或有界 `request_array`/`response_array`、backend binding。公共 wrapper 显式指定输入映射、必需/可选输出、句柄解析和输出写入。
- `bindings` 将本轮 API 对应到 wrapper、请求/响应、dispatch handler 与 backend；手工入口用 `rpc_operations` 关联相同链路。
- `simple_rpc` 继续生成原来的 10 对对象包装/dispatch，`extra_exports` 单列 6 个私有 BANG hooks 与 legacy `cnrtInit`。

`scripts/generate_rpc.py` / `api_codegen.py` 自动生成 SDK 无关 C 请求/响应声明、网络字节序编码、f32 位编码、成功回复长度校验、Client RPC、Server Dispatch、Native Binding 函数、公共 ABI wrapper、链接导出 map、清单、协议测试 plan 与可编辑测试模板。`include/generated` 的协议头入 Git；`build/generated` 每次重建且受 `--check` 校验。

Binding 与 wrapper 的局部 C 语句仍须作者根据 SDK 审查填写；这不是从任意 C 原型自动推导远程语义的工具。生成路径拒绝 unknown 参数、缺少请求映射、重号 opcode、未定义 RPC。结构体禁止直接用 native `sizeof` 上线；例如设备 properties 的数组和每个整数均独立编码。输出仅在成功且回复结构校验通过后发布，`cnrtGetLibVersion` 的可空输出已按 native 探针处理。构建把不兼容指针类型警告设为错误，防止将 CNDrv 的 `cn_int64_t *` 错作 32 位结果。

`response_buffer` 用 request 中 scalar u64 的 `size_from` 指定长度，生成器检查 `max_bytes`、caller-owned 输出、Server 回复分配及成功回复的精确字节数。上限为协议 payload 减固定回复字段及 128-byte envelope 余量；Native binding 仍负责业务范围及 ownership 校验。`wrapper.local_prepare` 位于必需参数校验后、CNRT `CONNECT()` 前，CNDrv 则在 `INIT()`/`CURRENT()` 后，用于从共享 Host registry 返回本地属性，保持初始化和离线语义。

`request_array` / `response_array` 的字段为 `name`、`type: u64`、`size_from`、`max_count`；`size_from` 引用 request 的 scalar u64，wire 为固定字段后紧跟逐元素网络序数组。请求借用调用者数组；响应须由调用者预分配，生成器校验成功回复恰好为固定长度加 `8*count`，校验通过后才发布。Server 使用独立解码数组并统一释放；业务 adapter 负责 identity 类型、parent/owner 和容量语义。`wrapper.response_prepare` 绑定 caller-owned 输出，`cleanup` 回收调用临时数组。数组与原 byte-buffer 生成路径互斥。

复杂创建/销毁、context/TLS、对象回滚、异步 staging、Memset 的 owned range、2D/3D span、Kernel ABI 与重定位仍手写。统一 helper 复用原对象表和配额，不另造 RPC 框架。2D/3D 的 owned allocation、显式 pitch/ysize 边界及审计结论见 [closure audit](API_COMPATIBILITY_CLOSURE.md)，不再因其他 allocation family 的缺口笼统挂 PARTIAL。

## 自动化前后的工作量

之前仅有 10 个 ObjectID RPC 能生成。普通 API 需要分别增加 header、client 编解码、server 长度检查/dispatch、backend、导出与台账；各处重复字段和 opcode。现在新增一个已审查的普通 API，主要填写 Schema 中的字段/映射/native binding，再补功能族语义断言；其余重复路径一次生成。

首批 20 个 RPC、21 个公共 wrapper 对应 1,089 行生成的 C 包装/编解码/dispatch/binding（含声明）/成功形状检查代码，后续查询批次扩至 39 个 RPC、44 个 wrapper；当前为 48/72，另有 4 个本地 error API，最新制品受 `generate_rpc.py --check` 和 `verify-api-contracts.py` 校验。行数不能当作人时节省或正确性指标。台账状态不会因为生成代码或导出符号而自动提升。

## 2026-11-07 G1 Graph arrays 与生命周期

28 个公开 Graph wrapper 全部由 Schema 生成，三个 action/edit/query RPC 复用有界数组生成器。双库共享 native CNDrv DAG adapter 与 GraphID/NodeID registry；clone 预留并映射独立节点身份、编辑校验 parent、destroy/reset/EOF 撤销并回收。SDK 的实际节点查询方向直接保留；typed payload/entity/capture 和 IPC 尚待后续阶段。详见 [设计](GRAPH_IPC.md) 与 [14 场景证据](../evidence/graph-g1-20261107/README.md)。

## 2026-11-07 Host memory / deferred D2H

两库 Host 分配/free 共享 `libremote_cnrt.so` 内的进程级 registry、guard/tombstone 和 pending lease，不发送真实客户端指针。新增 `API_D2H_SUBMIT` / `API_TRANSFER_FETCH` 与 `MLU_CAP_DEFERRED_D2H`：submit 只返回 TransferID，Server 的 native pinned buffer 独立存活；成功 Queue/Notifier/Transfer 完成、Context sync/free/module unload 触发非阻塞 fetch 和 Host 写回，reset/close 则在旧 ID 失效前 drain。查询遇 NotReady 不等待，较晚同 Queue 或重叠目标不能越过较早未发布的写回。旧同步 D2H wire 保留；同步回复写入目标前先发布已完成的旧异步结果。没有后台 publication，pageable 目标寿命由调用者负责。证据见 [Host 批次](../evidence/host-async-20261107/README.md)。

## 2026-11-13 V2 conversion

`cnrtCastDataType_V2` 的 FLOAT/INT/UCHAR host-local 子集复用 checked conversion helper，显式处理六种 rounding mode；half/bfloat 已加入，未验证的 quantized 类型继续保留 PARTIAL。

## 2026-11-07 Driver module memory query

`cnModuleQueryFatBinaryMemoryUsage` 与 `cnModuleQueryMemoryUsage` 共用一个手写可变长度 image RPC。Driver shim 在客户端读取并验证 CNFB image，再上传给 server 当前 native context 调用 `cnModuleQueryFatBinaryMemoryUsage`；只返回 `size_t` 使用量，路径和 native address 不跨线。MLU370-S4 上 Native、Remote、Preload 和隔离客户端均返回 `path=110592 fatbin=110592`，缺失路径和 malformed image 保持错误。当前仍是单 ELF64 LE/CNFBv2 image 与单设备范围，其他格式和完整错误码矩阵保留 PARTIAL。详见 [批次记录](../evidence/api-expansion-20261107/README.md)。

## 2026-11-12 Parameterized INT8 cast

`cnrtCastDataType` 的已验证参数路径在 Shim 内执行 FLOAT32↔INT8 scalar 转换；参数布局和数值边界写入证据，反向 INT8→FLOAT32 的 scalar 路径已验证，未观测/未验证类型保持 PARTIAL。

## 2026-11-11 By-channel quantization lifecycle

`cnrtCreateQuantizedParamByChannel` 在 Shim 内校验 channel 维度并复制参数数组，销毁时释放副本；量化计算仍保留 PARTIAL。

## 2026-11-10 Quantization parameter lifecycle

`cnrtQuantizedParam_t` 的基础 create/destroy 在 Shim 内管理 opaque 参数和 magic 校验；量化参数实际参与 cast、by-channel 配置和完整类型矩阵仍保留 PARTIAL。

## 2026-11-09 Host utility conversion

`cnrtCastDataType` 的已验证子集在 Shim 内执行，不产生 Remote RPC；Schema/ledger 记录支持的类型与数值边界，未验证的量化/V2 类型继续保留 PARTIAL。

## 2026-11-08 Symbol metadata

`cnrtGetSymbolSize` 直接读取 `__bangRegisterVar` 的 host registration metadata，返回经过 device ELF 校验的普通未初始化、初始化和 const device variable 大小，不把 host pointer 或 symbol 数值送入 RPC。地址/拷贝和 external globals 不包含在此入口；2026-11-07 收口证据见 [module 批次](../evidence/module-accounting-20261107/README.md)。

## 2026-11-07 Context configuration

`cnGetCtxConfigParam_pt` / `cnSetCtxConfigParam_pt` 使用固定字段 typed RPC 传 context-owned type/value/set，server 在 session context 上调用 CNDrv native API；`CNctxConfigParam` union 不直接跨线。当前证据覆盖三类 getter 和 union-limit setter，setter/TLS/多设备边界保持 PARTIAL。

## 2026-11-06 Allocation family

CNDrv `cnMallocNode`、`cnZmalloc`、`cnZmallocNode`、`cnMallocConstant`、`cnMallocNodeConstant`、`cnMallocSecurity`、`cnMallocFrameBuffer` 共享一个手写 semantic adapter：Schema/typed RPC 只传 bytes/kind/node，server 根据 kind 调用对应 native allocator，结果仍注册为 AllocationID；client 只生成 guarded fake VA。`cnMemcpy` 通过 registry ownership 推断 H2D/D2H/D2D/H2H，native device address 不穿过 wire。node 0、zero-fill、offset/range、owner 和 stale free 已对照；多设备/VMM/security consumer 保持 PARTIAL。

## BANG C 编译器类型 metadata

`scripts/generate_bang_metadata.py` 接受同一构建的 device LLVM IR 与实际嵌入的 `.cnfatbin`，提取 `mlisa_kernel` 参数，写入宿主 executable/DSO 的非加载 ELF section `.mlu.remote.abi`。构建命令见 `scripts/build.sh` 的 `metadata-audit` 段；也可重复传 `--pair LL:IMAGE` 附带多个 image 的记录。无需额外导出 helper，无运行时 SDK、环境 sidecar 路径或业务源码分支。

MLUABI02 的可变长度记录包含 `.cn_fatbin` payload 字节数、FNV64 指纹、精确符号、参数数量/大小、scalar/pointer/aggregate 分类，以及聚合 pointer 字段的 (argument, offset) 列表。MLUABI01 reader 仍保留。指纹用于发现过期构建产物，不提供密码学认证。客户端通过注册的 host stub 定位宿主 ELF，独立读取设备 ELF 大小/offset，再交叉核对类型；不匹配或损坏会拒绝。无 metadata 的已有 binary 继续用原保守 demangler。`--attach-images` 可把同一 metadata 写入 Driver 加载的 image，保留独立 CNDrv binary 的 extern-C/aggregate 类型关联。

已通过 C 链接符号、枚举、模板、零参数、一个 module 的三个 kernel、NULL/offset pointer 和与 fake VA 位值相同的 scalar 数值验证。重定位复用现有 allocation ID+offset→native address 机制，完全不按 kernel 名称、参数个数或 scalar 位值猜类型。四类 stale/损坏 metadata 均被拒绝；旧 aggregate/C-symbol 无 metadata 拒绝测试保留。

本机 CNCC 在 LLVM IR 中给出 i64，却在设备 MLISA 中发出 `.s48`；Native tagged VA 作为 scalar 的高位会被截断，`unsigned long long` 也不能消除这个观察。测试保留实际 input/output 到 stderr，并按原生 low48 语义断言；没有宣称任意 64 位数值都能跨 ABI 保真。IR 类型有助于区分 pointer，但不能单独证明设备算术语义。

自然布局 byval struct、嵌套 struct 和固定数组按 CNCC datalayout 计算成员 offset、alignment 和 padding，构建 manifest 保留布局；设备 ELF 参数大小再交叉验证。早期无 pointer 用例输出 `21 / 11 / -123`，覆盖 signed/float 字段，见 [aggregate 证据](../evidence/aggregate-20261103/README.md)。续批增加 pointer 字段 offset：共享 `bang_pack_args` 只解析编译器标记的字段，将 AllocationID+offset 传给 Server，scalar 位值保持原样。`MLU_CAP_ARGUMENT_RELOCATIONS` 控制 kind 3；wire 清零 pointer 字节并附带最多 512 个 24-byte 网络序字段记录。Server 在 owner/range/NULL/alignment/order/overlap 校验后填入 native 地址。

[module 收口证据](../evidence/module-accounting-20261107/README.md) 覆盖 Runtime/Driver 同 binary 的 nested struct/array pointer、NULL、offset、scalar bits、guard、reset/reload 和 stale/foreign ownership。Driver 的 extern-C/zero-arg lookup/launch、v1 兼容、坏 metadata 和畸形 relocation 均有验证。Device pointer graph、packed/opaque/递归布局、未知 IR/地址空间与多架构 image 继续拒绝；普通初始化/const globals 已支持，Host symbol address/copy 与 external globals 不在此入口范围。

## 2026-11-07 CNRT registered-image accounting

`cnrtDeviceQueryKernelMemoryUsage` 改为手写 registration adapter：逐个存活注册查询并缓存 native image memory estimate，注册后首 launch 前即计入、reset 保留、最终 unregister 扣除。原生 CNDrv module 不参与 CNRT 计数，Server 无客户端注册表，旧 `API_KERNEL_MEMORY_USAGE` opcode 保留并明确返回 NOT_SUPPORTED；公共入口使用已有 image memory query RPC，避免返回误导性的零。该批完成时为 45 个 typed RPC、44 个生成公共 RPC wrapper 和 4 个本地 error API。

## 扩展流程与回归

```sh
./scripts/build.sh
python3 scripts/generate_compat.py
python3 scripts/verify-api-contracts.py
python3 scripts/verify-api-families.py
python3 scripts/verify-transparent.py --out evidence/<batch>/full-suite
```

`generate_compat.py` 对 SDK 预处理 Census、头文件哈希、Schema 精确签名、真实链接导出和人工状态做校验，生成逐 API ledger、summary、功能族统计和剩余 API 的依赖计划。`verify-api-contracts.py` 再核对生成结果、opcode、codec/dispatch/binding、导出 allowlist、ledger 和完整状态的证据文件。

新增功能族测试比较稳定 stdout；实时 free memory 只比不变量，时间值只比非负及 disabled-timing 错误，stderr 单独留存 native 诊断。生成 test plan 为全部 48 个 typed RPC 提供坏长度与坏成功回复测试，buffer/array 另验证动态长度，已有跨 session、overflow、stale、alignment、string 和元数据负例保留。生成模板不会被当作通过测试；语义断言仍由作者编写。

2026-11-03 续批为 memcpy wire 增加可选 async 标志。旧 56-byte 请求继续由 server 接受，60-byte 扩展只在 host payload 长度唯一匹配时启用；因此默认 Queue 的 QueueID 0 语义可以扩展而不破坏已有协议负例。该字段由手工 memcpy adapter 使用，typed RPC 生成器负责固定字段及有界 response buffer 的长度和成功回复校验。

310 项分类及逐项 blocker 见 [功能族计划](../compat/family-plan.md) 和 `compat/family-plan.json`。Context/config/PCI、allocation attribute/range、Context flags、QueueID 0 default ordering、Host pages/completion/writeback 已接入；module/symbol、聚合 pointer 字段 metadata 与 Host conversion 已按当前范围收口。G1 完成后继续 G2～G4 与 IPC broker/VMM。Peer 仍受单卡条件阻塞。CNNL 不在这 310 项分母，当前 SDK 缺库仍阻塞。本阶段无需第二物理主机和新增生产安全体系。

## 查询边界续批

Context/PCI、memory range/attributes、Queue union 本轮新增 19 个 typed RPC 与 23 个公共 wrapper，累计 39/44。新增 `cstring` 的请求/回复 NUL 检查、owner session 选择、显式 `native_status` 错误输出；语义 adapter 和批量 wrapper 仍手写。详见 [设计与边界](QUERY_BOUNDARIES.md)。`summarize-api-change.py --baseline <commit> --out <evidence-dir>` 机械生成逐项 delta 和制品哈希；完整状态仍需人工证据审查。
