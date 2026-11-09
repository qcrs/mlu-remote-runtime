# 原生 CNDrv 客户端 ABI

当前增量提供独立 `build/driver-shim/libcndrv.so`，复用原有 TCP、对象表、配额、内存、staging、Queue/Notifier 和用户 image 执行。普通 Driver 程序在 builder 用 SDK 头文件和原生 libcndrv 编译；运行时用库替换或 LD_PRELOAD 选择远程执行。同一 binary 不插入 remote API。CNCC host stub 的 CNRT Shim 路径继续保留。

范围：Neuware 3.5.2、CNDrv 2.5.2、CNCC 4.5.1、AArch64 LP64 little endian、MLU370-S4。验证记录见 [本轮证据](../evidence/driver-shim-20261102/README.md)，逐 API 状态见 [台账](../compat/api-ledger.csv)。它没有覆盖 vendor CNRT 所需的全部内部 Driver 调用，不能用这套部分 Driver Shim 承接任意未替换的 vendor CNRT。

## Context、所有权和并发

- `cnInit` 建立一条额外 control session，供设备查询使用，不绑定客户端当前 context。它占用一个服务端连接及 native context；默认 32 个连接额度也包括它。无可用设备的 server 初始化不是已验收场景。
- 每个 `cnCtxCreate` 建立独立 TCP session / native context，HELLO16 传递设备 ordinal 与原生调度标志 0..3。创建成功绑定调用线程；线程初始未绑定，`cnCtxSetCurrent(NULL)` 解绑，销毁当前 context 后不恢复以前的 context。
- TLS 保存当前 token，全局 mutex 保护客户端 context、对象、fake VA 和 RPC。多个线程的调用目前串行进入 RPC；服务端不同 session 独立 worker。它验证正确性，不代表多线程吞吐已经优化。
- Queue/Notifier/Module/Kernel/Allocation 都属于 context，跨 context、stale、已卸载父 module 的操作拒绝。opaque token 单调增加；已释放设备地址保留 PROT_NONE 映射至进程退出，避免旧地址被新分配复用。长时间大量分配的客户端 VA/元数据成本仍待优化。
- `cnCtxDestroy` 使用 SESSION_CLOSE，服务端先完成 native cleanup 再响应并关连接，不像 reset 那样重建 context。若回复丢失或损坏，客户端仍失效该 context 和全部子资源，不重发、不自动重连。此时远端是否完成销毁不能仅凭本地状态判断。
- 析构时断开仍存活的 session，服务端走 EOF cleanup。native 卡住时没有有界取消保证，继承现有 shutdown/quarantine 策略。

这个 SDK 的公共 context API 使用 current binding 模型，没有 CUDA 式 push/pop 接口。后续扩展应继续依据原生探针。

## Module 和通用 launch

`cnModuleLoad` 读取客户端路径，将文件内容上传，而不要求服务端存在相同路径。`cnModuleLoadFatBinary` 在 Linux `/proc/self/maps` 的可读范围内确定 image span，再执行有界 ELF/CNFB 检查。这是对正常应用参数的适配，不是任意无效指针安全验证或 hostile-client 沙箱。

MODULE_LOAD_BINARY 在加载时立即调用原生 `cnModuleLoadFatBinary`，无须预先提供第一个 kernel 名。服务端复制并保留 image，通过 ModuleID 管理其 native module；`cnModuleGetKernel` 随后查询原生符号，再建立多个 child KernelID。两个 BANG kernel 共享 module globals，分别加载相同 image 的 context 保持独立状态。

客户端和服务端复用 `.params.text.*` 的参数大小/偏移解析。`generate_bang_metadata.py --attach-images` 将同次 CNCC device LLVM IR 生成的 MLUABI02 写入 Driver image，以 `.cn_fatbin` payload 长度/FNV64 和精确符号关联参数类型、自然 LP64 聚合布局及 pointer 字段 offsets；指纹只关联产物，不提供认证。CNRT/CNDrv 共享参数打包，将顶层 pointer 和按值 struct/嵌套 struct/固定数组中的 pointer 字段转为 AllocationID+offset，保留标量字节。Server 校验字段范围、对齐、顺序/重叠、NULL 和 allocation owner 后填入 native 地址；最多 512 个字段，旧 peer 未协商 relocation 能力时本地拒绝。缺少 metadata 时仅保留已验证的 primitive scalar 与单层 primitive/void pointer demangle fallback；无法确定类别时拒绝，不能根据参数值猜指针。MLUABI01 reader 保留，device pointer graph、packed/opaque/未知地址空间 ABI 不在当前支持范围。见 [本批 metadata/relocation 证据](../evidence/module-accounting-20261107/README.md)。

当前 image 只支持已测 ELF64 LE / CNFB v2 单 device image，要求有可验证的参数 section；其他合法 native 格式也可能被拒绝。原生符号不存在时返回 `CN_ERROR_NOT_FOUND` 并清空 kernel 输出句柄；本轮探针确认文件不存在时则保留 module 输出值。对于其他不支持格式，不承诺逐错误码与 native 相同。

`cnInvokeKernel` 支持 xyz、primitive 混合参数、offset/NULL pointer、带编译器 metadata 的 extern-C/自然布局按值聚合参数和零参数 kernel，以及显式 queue 和真实 native NULL queue；reserved 必须为 0，extra 必须为 NULL。嵌套 struct/array、跨 context pointer 拒绝和 module unload 后 Kernel 失效已有真机数值/错误验证。Block 数值已验收，Union 系列仅转发，尚未验收其完整语义。

## NULL queue、异步和清理

真机确认 `cnInvokeKernel(..., NULL, ...)`、`cnPlaceNotifier(..., NULL)`、`cnQueueSync(NULL)`、`cnQueryQueue(NULL)` 可用。因此 Driver Shim 直接传递 native NULL queue，不把 sync 变成 context barrier，也不把 query 写成固定成功。

NULL queue launch 与显式 queue 一样创建真实 transfer notifier，release 只表达客户端放弃观察，不代表设备已完成。server 在记录 NULL queue 工作后，free、module unload、session cleanup 也等待该队列，防止尚未完成的工作失去资源。已实测两种 queue 均在有限 kernel 执行期间返回 NOT_READY，卸载后数值正确；context close 也覆盖 pending NULL queue。

同步 H2D/D2H/D2D 使用原有 bounded RPC；显式 queue 的异步 H2D 用服务端 staging 保持源字节，异步 D2D 原生入队。原生对照程序为每个异步 H2D 保留独立主机源存储，直到完成，遵守 native host-buffer 生命周期约束。

当前 MEMCPY wire 的 queue=0 表示同步操作，无法区分 native NULL queue 的异步 copy 意图；`cnMemcpyHtoDAsync/DtoDAsync` 的非零字节 NULL queue 调用明确 NotSupported，不能伪装同步为已实现的异步。显式 Queue 的 `cnMemcpyDtoHAsync` 已通过 Server pinned staging、TransferID completion fetch 和成功边界写回实现；pageable 目标由调用者保持到完成，NULL queue 的非零异步路径仍按台账边界拒绝。

Queue destroy 立即退役公开 ID，底层句柄和 staging 保留到队列完成再回收；Notifier wait/query、context sync、free、module unload 和 cross-queue wait 均保留在途数据。Free 和 module unload 仍使用保守同步。Queue/Notifier flags、priority、timing 与 cross-queue wait 的当前支持范围以台账为准；QueueID 0 的常用 H2D/D2D async 和 completion 已接入。退役/失败重试与 SDK pending-destroy 差异见 [Queue retirement](../evidence/queue-retirement-20261107/README.md)。

## 原生细节与配置

`probe_driver_semantics` 对照以下可观察行为：初始化前报 NOT_INITIALIZED；无当前 context 的 malloc 报 CONTEXT_INVALID；`cnMalloc(0)` 成功但保留输出；NULL free/零字节同步 copy 成功；线程绑定/解绑/销毁行为；四种 context flags；缺失符号/文件的输出；NULL queue invoke/notifier/sync/query。

此 SDK 声明 `CNdev` 为 64 位无符号类型，但实测 native 在设备查询和 context create 中使用低 32 位 ordinal：`1ULL<<32` 对应 device 0，低 32 位全 1 则无效。Shim 按此已测行为转换；这不构成其他 SDK 的 ABI 承诺。

配置复用 `MLU_REMOTE_HOST/PORT`、connect/RPC deadlines。非法端口或空 host 返回 INVALID_VALUE；连接/协议故障映射为操作系统错误。合法 native 错误保留，配额错误映射为进程内存上限错误。更详细的 remote 诊断仍在 core error 和 server 日志；尚未提供完整 Driver error-string API。

## 使用与验收

先完整构建并在终端 A 启动 `./scripts/run-server.sh`，终端 B：

```sh
module=build/module-audit/native_bang_module-bang-mlisa-cambricon-bang.cnfatbin
abi=build/audit/native_bang_abi-bang-mlisa-cambricon-bang.cnfatbin
drain=build/drain-audit/native_bang_drain-bang-mlisa-cambricon-bang.cnfatbin
LD_LIBRARY_PATH="$PWD/build/driver-shim:$PWD/build" \
  ./build/native_driver "$module" "$abi" "$drain"
```

模拟无 SDK/设备客户端：

```sh
MLU_REMOTE_SHIM=driver ./scripts/run-isolated-client.sh \
  ./build/native_driver "$module" "$abi" "$drain"
```

`run-isolated-client.sh` 保留宿主网络并只隐藏 SDK 和 `/dev`。更严格的本地模拟由
`scripts/run-isolated-pair.sh` 提供：Server/Client 进入 sibling network namespace，使用
veth/TCP；Client 在独立 chroot 中只看到 executable、shim、系统动态库及显式文件参数。
用 `bash scripts/test-isolated-topology.sh` 可在没有 MLU 的机器上验证 TCP payload
传输和 SHA-256；在 MLU 主机上可用 `python3 scripts/verify-workloads.py
--realistic-isolation --workload cndrv_gemm` 运行 Driver workload。该拓扑自检不等于
MLU runtime 或第二物理主机验收。

完整自动验证由 `scripts/verify-transparent.py` 执行，包括 native/replacement/preload、语义逐行对照、隔离客户端、strace、Driver 协议/故障注入、原有 CNRT 和六 preset 回归、有界 parser ASan/UBSan。构建 Shim 需要 SDK 头文件，运行只依赖系统库和 client core。必须保持调用库的 SONAME、动态解析及当前 CPU/ABI 相容；静态链接、绝对 dlopen、deep binding、自定义 loader namespace 不在已验收边界。

跨第二物理主机仍未验收。当前双终端/namespace 证明客户端路径不加载 vendor SDK 或访问设备节点，不能证明独立机器部署、物理网络或异构 CPU 已兼容。
