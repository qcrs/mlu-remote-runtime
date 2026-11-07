# Module 注册计量、globals 与参数 relocation（2026-11-07）

范围：MLU370-S4 单设备，Neuware 3.5.2 / CNRT 6.5.2 / CNDrv 2.5.2 / CNCC 4.5.1，AArch64 LP64 little endian，mtp_372 单 device image。本机独立 Server/Client 进程通过 loopback TCP 运行；隔离客户端隐藏 SDK 和整个 `/dev`，保留网络。[环境记录](full/environment.log) 包含设备、系统、编译器版本和 SDK header 哈希。跨第二台物理主机尚未验收。

## 复现与结果

```sh
./scripts/build.sh
python3 scripts/verify-module-accounting.py
python3 scripts/verify-api-families.py --out evidence/module-accounting-20261107/families
python3 scripts/verify-transparent.py --out evidence/module-accounting-20261107/full
python3 scripts/verify-api-contracts.py
git diff --check
```

| 验证 | 结果 | 记录 |
|---|---:|---|
| 完整构建 | 通过 | [build.log](build.log) |
| module/globals/relocation 专项 | 19/19 | [results.json](results.json) |
| API 族回归 | 15/15 | [families/results.json](families/results.json) |
| 完整核心回归 | 76/76 | [full/results.json](full/results.json) |
| codegen、closure、导出与台账契约 | 通过 | [full/api-contracts.log](full/api-contracts.log) |

专项对 CNRT 注册计量和 CNDrv 独立加载各执行 Native、动态库替换、LD_PRELOAD、隔离客户端四种方式，再以同样四种方式运行聚合参数用例，另加 wire、metadata 和 parser 验证，共 19 项。计量 stdout 逐行对照 Native；relocation 的共同数值断言使用同一 binary，stale/foreign pointer 与失效句柄负例仅在 Shim 模式执行，不声称 Native 对无效指针有相同拒绝行为。

完整构建日志生成后，修正了 parser 测试中 const 符号的 mangled name，并单独重建 `build/bang-image-bounds`；后续专项使用重建后的测试程序。[compiler-manifest.json](compiler-manifest.json) 保存聚合布局及 image 指纹，[compiler-types.ll](compiler-types.ll) 保存对应 device LLVM IR，[provenance.json](provenance.json) 保存本批源文件、编译产物和替换库 SHA-256；不提交 binary。

## 注册计量与 globals

[Native CNRT](runtime-native.log) 在 DSO 注册 image 后、首次 launch 前即计入 image estimate；增加同一 DSO 的 loader 引用不重复计量，保留一个引用时仍计量，最后 `dlclose` 触发 unregister 后扣除。两种 image 分别独立计量。reset 保留 registration 与计数，下次 launch 重新加载 module，并验证初始化 globals 参与的计算结果。

| 观测点 | Native 与三种 Remote 模式计数（bytes） |
|---|---:|
| 尚未注册 | 0 |
| 注册第一个 image | 74752 |
| 增加/释放第二个 loader 引用，保留原引用 | 74752 |
| 注册两个 image | 149504 |
| launch、reset、重新 launch | 149504 |
| 注销一个 image | 74752 |
| 全部注销 | 0 |
| 再次注册同一 image / 最后注销 | 74752 / 0 |

[Native CNDrv](driver-native.log) 独立 load、launch、unload 时，`cnrtDeviceQueryKernelMemoryUsage` 均为 0。Server 通过 CNDrv 执行，直接读取其 CNRT counter 会漏掉客户端注册；因此公共 CNRT adapter 对本进程存活 registration 求和，并缓存 Server 原生 `cnModuleQueryFatBinaryMemoryUsage` 返回的 image estimate。表中值是当前产物的观测，测试通过原生 `cnModuleQueryMemoryUsage` 获取期望值，不在实现中硬编码。该 API 表达注册 image 的计量，不是物理显存驻留测量。

旧 typed `API_KERNEL_MEMORY_USAGE` opcode 保留，但 Server 缺少客户端注册表，明确返回 `CN_OPS_ERROR_NOT_SUPPORTED`（100050）和空 body；公共入口使用 registration adapter。[typed-protocol.log](families/typed-protocol.log) 验证旧 wire 入口的拒绝行为。

`native_module_globals.mlu` 包含未初始化 `int[5]`、已初始化 `int[3]`、`__mlu_device__ const int[2]`，`cnrtGetSymbolSize` 分别返回 host registration 大小 20、12、8 bytes；设备 OBJECT size 按 64 bytes 对齐。当前编译器三者的 ext/constant/global 注册 flags 均为 0，const 符号为 `_ZL9constants`。parser 接受有界 `.data.gdram_uninit.<symbol>` 和 `.data.gdram_init.<symbol>`，要求 `st_value == 0`，已初始化 section 文件长度与对齐后的 symbol size 相等。存储和初始化由 native module 管理，kernel 数值依次为 37、41，reset 后为 38，再注册后为 39。这里不包含 host symbol address/copy API、external globals，也不声明当前 CNCC 未识别的 `__mlu_constant__` 支持。

## 聚合参数与所有权

MLUABI02 从同次 CNCC device LLVM IR 生成，记录 `.cn_fatbin` payload 长度/FNV64、精确符号、参数 kind/size 和 `(argument, pointer field offset)`，并与设备 `.params.text.*` sizes 交叉核对。宿主 executable/DSO 和 Driver image 均可携带 `.mlu.remote.abi`；FNV 仅关联编译产物，不提供认证。MLUABI01 reader 和无 metadata 时保守的顶层 primitive demangle fallback 保留。

`Payload` 为 72 bytes，含两个 `Leaf` 的固定数组、output pointer 和 uint64 scalar；五个 pointer 字段 offsets 为 `[8, 16, 32, 40, 56]`。CNRT/CNDrv 共享打包逻辑，把字段转为 AllocationID+allocation offset；wire 中原 pointer 槽八字节清零。协商 `MLU_CAP_ARGUMENT_RELOCATIONS` 后使用 kind 3，每个字段为 24-byte 网络序记录：field offset、reserved、AllocationID、allocation offset；每次 launch 最多 512 个字段。Server 在 native launch 前校验所属 session、allocation 范围、NULL offset、字段范围/对齐/顺序/重叠、reserved 和清零槽，再填入 native 地址。

[Runtime](relocation-runtime-remote.log) 和 [Driver](relocation-driver-remote.log) 数值验证包含嵌套 struct/array pointer、allocation 内偏移、NULL、zero args 和 extern-C。output 两侧 guard 保持 -77，数据为 12、30 及 scalar 的低 16 bits；scalar 故意取与设备地址相同的数值，确认它不按地址猜测重定位。当前 CNCC 对 i64 使用 `.s48`，此用例只声明实际检查的低 16 bits，不外推为完整 uint64 设备算术验证。Runtime reset 后 metadata 保留且 module 可重新加载；已释放及 reset 前 pointer 被拒绝。Driver 重复 lookup 返回同一 Kernel，foreign-context pointer 被拒绝，module unload 后旧 Kernel 返回 `CN_ERROR_INVALID_KERNEL`。

当前支持自然 LP64 按值 struct、嵌套 struct 和固定数组中的明确 pointer 字段；不遍历设备内存中的 pointer graph，不包含 packed、opaque、递归/未知类型、未知地址空间、跨架构 ABI 或 multi-arch fatbin。缺少可靠 metadata 的 aggregate/extern-C 无法凭参数大小恢复类型；按既有边界拒绝。

## 拒绝路径、兼容与回收

- [relocation-protocol.log](relocation-protocol.log)：13 种畸形 wire，包括 foreign allocation、越界、非法 NULL offset、未对齐/重叠/乱序字段、reserved、非零 pointer 槽、缺字段、scalar 带字段、超过上限和截断；之后合法 launch 仍成功，stale module 拒绝。
- [metadata-validation.log](metadata-validation.log)：旧 v1 metadata 数值执行；同名符号按 image 关联；过期指纹、错误 size/kind、截断拒绝；argument/range/alignment/overlap/kind/count 六类字段损坏分别通过 host 与 Driver image 路径验证。IR parser 另拒绝未知 aggregate/vector、过宽 scalar、间接 pointer、递归/opaque/packed 布局。
- [full/client-response-faults.log](full/client-response-faults.log)：旧 peer 未提供 relocation capability 时客户端本地拒绝，不发送 launch，session 后续仍可用。
- [parser-bounds.log](parser-bounds.log)：ASan/UBSan 下有界 ELF/CNFB、截断/extent/NOBITS，以及各 device global 的大小、截断和每类 2000 次 mutation 验证。

专项 [server.log](server.log) 和完整回归 [full/server.log](full/server.log) 结束均为 `DRAIN_COMPLETE workers=0 cleanup_errors=0`、`RESOURCE_TOTAL device=0 staging=0 image=0`。这验证会话清理及逻辑资源额度归零，不新增物理驻留或长期泄漏测量。

本批将 `cnrtDeviceQueryKernelMemoryUsage`、`cnrtGetSymbolSize`、`cnrtInvokeKernel`、`cnModuleGetKernel`、`cnInvokeKernel` 五项按上述边界升级 IMPLEMENTED，保留原 closure 审计快照并新增 resolution。当前 310 项台账为 143 IMPLEMENTED、2 PARTIAL、7 BLOCKED、2 OUT_OF_SCOPE、1 UNSUPPORTED、155 NOT_IMPLEMENTED；剩余 PARTIAL 为 `cnrtCastDataType` 与 `cnrtCastDataType_V2`。

归档仅移除原生日志的行末空格；返回码、时间戳和数值保留。
