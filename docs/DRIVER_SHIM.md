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

客户端和服务端复用 `.params.text.*` 的参数大小/偏移解析；客户端额外 demangle 参数类型，仅接受当前验证的 primitive scalar 与单层 primitive/void pointer。参数通过 AllocationID+offset 重定位，标量原样传输。缺少可靠类型信息的 extern C、aggregate、嵌套 pointer 等明确拒绝；不能根据参数值猜指针。

当前 image 只支持已测 ELF64 LE / CNFB v2 单 device image，要求有可验证的参数 section；其他合法 native 格式也可能被拒绝。原生符号不存在时返回 `CN_ERROR_NOT_FOUND` 并清空 kernel 输出句柄；本轮探针确认文件不存在时则保留 module 输出值。对于其他不支持格式，不承诺逐错误码与 native 相同。

`cnInvokeKernel` 支持 xyz、primitive 混合参数、offset/NULL pointer、显式 queue 和真实 native NULL queue；reserved 必须为 0，extra 必须为 NULL。Block 数值已验收，Union 系列仅转发，尚未验收其完整语义。

## NULL queue、异步和清理

真机确认 `cnInvokeKernel(..., NULL, ...)`、`cnPlaceNotifier(..., NULL)`、`cnQueueSync(NULL)`、`cnQueryQueue(NULL)` 可用。因此 Driver Shim 直接传递 native NULL queue，不把 sync 变成 context barrier，也不把 query 写成固定成功。

NULL queue launch 与显式 queue 一样创建真实 transfer notifier，release 只表达客户端放弃观察，不代表设备已完成。server 在记录 NULL queue 工作后，free、module unload、session cleanup 也等待该队列，防止尚未完成的工作失去资源。已实测两种 queue 均在有限 kernel 执行期间返回 NOT_READY，卸载后数值正确；context close 也覆盖 pending NULL queue。

同步 H2D/D2H/D2D 使用原有 bounded RPC；显式 queue 的异步 H2D 用服务端 staging 保持源字节，异步 D2D 原生入队。原生对照程序为每个异步 H2D 保留独立主机源存储，直到完成，遵守 native host-buffer 生命周期约束。

当前 MEMCPY wire 的 queue=0 表示同步操作，无法区分 native NULL queue 的异步 copy 意图；`cnMemcpyHtoDAsync/DtoDAsync` 的非零字节 NULL queue 调用明确 NotSupported，不能伪装同步为已实现的异步。`cnMemcpyDtoHAsync` 整个入口明确 NotSupported，等待 pinned/host completion/writeback 设计。

Queue/Notifier 创建仅 flags=0。queue destroy、free 和 module unload 使用保守同步，可能比 native 等待更久；因此这些族仍为部分实现。完整 default queue 隐式跨队列排序、priority、附加 notifier flags、context attach/configuration、完整 Driver 内部调用面仍待实现。

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

完整自动验证由 `scripts/verify-transparent.py` 执行，包括 native/replacement/preload、语义逐行对照、隔离客户端、strace、Driver 协议/故障注入、原有 CNRT 和六 preset 回归、有界 parser ASan/UBSan。构建 Shim 需要 SDK 头文件，运行只依赖系统库和 client core。必须保持调用库的 SONAME、动态解析及当前 CPU/ABI 相容；静态链接、绝对 dlopen、deep binding、自定义 loader namespace 不在已验收边界。

跨第二物理主机仍未验收。当前双终端/namespace 证明客户端路径不加载 vendor SDK 或访问设备节点，不能证明独立机器部署、物理网络或异构 CPU 已兼容。
