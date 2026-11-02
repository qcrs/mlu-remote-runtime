# API 自动化与功能族扩展验收

日期：2026-11-02。基线 commit：`6280171819335924f7a4b37195271aa7cc7e74ff`。环境：AArch64、MLU370-S4、Neuware 3.5.2、CNCC 4.5.1；运行时 CNRT 6.5.2、Driver library 2.5.2、kernel driver 5.10.35。完整环境及 SDK 头文件哈希见 [environment.log](full-suite/environment.log)。

## 数量和判定边界

本轮新增 **30 个公共入口，其中 15 完整、15 部分实现**；另将 **5 个已有拒绝入口接通 native backend，其中 3 完整、2 部分**，并补齐已有 queue/notifier 创建选项。新增入口均有 Client→RPC→Server→Native Backend 和实际数值/语义测试。完整状态限定当前 SDK、单设备会话和台账列明的所有权边界；不表示所有 SDK/硬件组合均已验收。

| 统计 | 基线完整 | 基线部分 | 当前完整 | 当前部分 | 当前拒绝 | 当前未实现 | 分母 |
|---|---:|---:|---:|---:|---:|---:|---:|
| CNRT | 12 | 14 | 19 | 20 | 3 | 88 | 130 |
| CNDrv | 6 | 27 | 17 | 38 | 1 | 124 | 180 |
| 合计 | 18 | 41 | **36** | **58** | **4** | **212** | **310** |

逐 API 的增量在 [change-counts.json](change-counts.json)，完整兼容边界在 [api-ledger.csv](../../compat/api-ledger.csv)。6 个私有 BANG hooks 与 legacy `cnrtInit` 不计入 310 项。CNNL 也不在该分母；当前测试只证明明确拒绝。

## 自动生成与人工部分

实际阅读了 CoreX 的 Schema、生成器、Census、Compatibility Ledger、Contract Checker，以及 Cricket 的注册 hooks、ELF metadata、参数打包、资源映射与 dispatch 生成。具体路径及 MLU 取舍见 [设计说明](../../docs/API_AUTOMATION.md)。

原有 10 对 ObjectID RPC 保留；新增 **20 个 typed RPC、21 个公共 API wrapper**，自动生成固定字段 request/response、字节序编解码、回复形状检查、Client RPC、Server dispatch、native binding 函数、链接导出 map、manifest、协议测试 plan 和语义测试模板。310 项签名、参数类型/方向/长度/映射、功能族、状态和阻塞均进入 Schema；尚未审查的参数保留 unknown，禁止进入生成路径。Ledger 与功能族计划也从 Schema 生成，SDK 原型和真实链接导出参与一致性检查。

自动化前，普通 API 要重复修改公共包装、客户端编解码、协议、服务端 dispatch、backend、导出与台账；现在主要填写已审查的字段、输入输出映射和 native binding，再编写功能族语义断言。本轮对应 1,089 行生成 C 代码，数量可由 [artifact-manifest.json](artifact-manifest.json) 复核，不折算人时。局部 binding/prepare/output C 语句仍须人工审查；模板本身不算测试成功。

生命周期、context/TLS、资源回滚、异步 staging/writeback、owned allocation 范围、2D/3D span、Kernel 参数打包/重定位继续人工实现并复用现有 helper。

## 可复用实现模式和实际测试

| 功能族 | 实现模式 | 验证内容与当前边界 |
|---|---|---|
| Device / Runtime / Driver 查询 | 固定字段 RPC；枚举透传给对应 native API；struct 按字段展开 | 版本、CNRT/CNDrv 属性、全部 properties 字段、priority range、memory info；稳定输出逐行相同，动态 free memory 只检查不变量 |
| Memory / Memset | allocation ID + offset、元素宽度与数量、native queue | CNRT 字节截断，Driver D8/D16/D32，非零 offset、数值和 async fill；同步 fills 标记完整，异步保持部分 |
| Memcpy 2D/3D | descriptor 逐字段编码；提交前检查 pitch/xyz/depth 及溢出 | 不同 pitch、非零 xyz、depth、padding、Driver aliases；仅 owned device allocations，3D 显式非零 pitch/ysize，六入口均部分实现 |
| Queue / Notifier | 复用对象注册/回滚；普通查询与 wait/timing 生成 | native priority clamping、创建 flags、跨 queue wait、两类 elapsed 和 disabled timing 错误；默认 queue 隐式排序/更广 flags 仍待验收 |
| Module / Kernel ABI | 注册→module→多个 KernelID；compiler types + ELF size/offset 交叉核对 | Kernel 五种属性，C/enum/template/zero args、offset/NULL pointer、与 fake VA 相同的 scalar；原 module/globals/DSO 测试保留 |

[results.json](results.json) 记录新增功能族 **15/15** 通过：8 项 runtime/Driver native、replacement、preload、独立 namespace；4 项 compiler metadata 同类对照；typed protocol、typed client fault、metadata validation 各 1 项。namespace 隐藏 SDK 与整个 `/dev`，server 是 namespace 外独立进程，使用 TCP。

[full-suite/results.json](full-suite/results.json) 记录原有完整回归 **59/59** 通过，保留六 preset、核心 BANG 程序、通用参数、共享 globals、DSO 生命周期、Driver、协议/资源/断线、ASan/UBSan parser 等检查。每项日志保留命令和相关环境。CNNL 用例只验收拒绝结果。

协议测试由生成 plan 驱动，全部 20 个 typed RPC 测坏请求长度和坏成功回复；另测跨 session、overflow、alignment、stale、错误 queue、2D/3D 越界。在坏成功回复后，输出保持哨兵且失效连接不再发请求。

## 编译器 metadata 与原生 ABI 观察

`generate_bang_metadata.py` 从 CNCC device IR 提取 primitive scalar/direct primitive pointer，在宿主 ELF 添加 `.mlu.remote.abi`。每条记录按 image 长度/FNV64、精确 symbol、参数数量/大小/类型关联，运行时再与设备 `.params.text` 布局核对。支持同一宿主多个 image 的记录；同名 symbol 的其他 image 记录不会遮蔽正确匹配。不存在针对 kernel 名称、参数数量或 scalar 数值的专用分支。

[compiler-metadata.json](compiler-metadata.json) 保存本次三个 kernel 的类型，[compiler-abi-excerpts.txt](compiler-abi-excerpts.txt) 保存 IR/MLISA 片段。过期 image、错误 size/kind、截断记录以及不支持的 aggregate/vector/indirect pointer 都有拒绝测试，见 [metadata-validation.log](metadata-validation.log)。无 metadata 的旧 binary 沿用保守 demangler，原拒绝用例保留。

本机 CNCC 的 LLVM i64 参数会生成 MLISA `.s48`，`unsigned long long` 亦如此。Native tagged pointer 当作 scalar 的高位发生截断，实际 input/output 见 [metadata-native.stderr](metadata-native.stderr)；remote 用与 fake VA 完全相同的 scalar，验证不误重定位，见 [metadata-remote.stderr](metadata-remote.stderr)。仅按观测到的 native low48 语义断言，不承诺任意 i64 数值保真。

## 剩余项、优先级与阻塞

尚有 **58 部分、4 明确拒绝、212 未实现**。全部逐项依赖、blocker 和下一步在 [family-plan.json](../../compat/family-plan.json)，统计在 [family-plan.md](../../compat/family-plan.md)。

- P0：Device/Context 余 21 项未实现、Error 余 2 项；优先 context/config/PCI，需保持 Driver TLS/current 与 Runtime device 语义。
- P1：Memory 余 26、Memcpy 余 6、Queue/Notifier 余 9 项未实现；先 allocation 属性/range、queue 查询，再扩展 stride/async/default-queue 边界。
- P2：Host Memory 12 项未实现、2 项拒绝，Module/Kernel 6 项未实现；Host ownership/pinned pages、completion/writeback、compiler aggregate 类型、Host symbol 与 initialized/constant/external globals 是实际依赖。
- P3：9 项 Host utilities；需逐项评估客户端本地实现及数值对照。
- P4：7 项 Peer；需要多设备和跨 context 地址/顺序语义，目前只有一物理设备。
- P5：102 项 Graph、12 项 IPC；依赖稳定 capture/lifecycle 与跨进程资源/地址语义，本轮不按数量强行实现。

CNNL 缺少当前 SDK 头文件和库。第二物理主机、TLS/公网认证、多租户安全、复杂 Fuzz 不作为本阶段门槛。

## 复现

```sh
./scripts/build.sh > evidence/api-expansion-20261102/build.log 2>&1
python3 scripts/generate_compat.py
python3 scripts/verify-api-contracts.py
python3 scripts/verify-api-families.py
python3 scripts/verify-transparent.py --out evidence/api-expansion-20261102/full-suite
git diff --check
```

两个 runner 各自启动和清理自己的 loopback server；应顺序运行，避免证据目录互相覆盖。归档文本仅去除行尾空白。[artifact-manifest.json](artifact-manifest.json) 记录最终 binary、生成物及 SDK 头文件 SHA-256，便于核对这批测试对应的构建。
