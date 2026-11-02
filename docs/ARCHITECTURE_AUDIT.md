# 架构与环境审计

审计日期：2026-11-02。项目目录为 `/home/lvtong/mlu-remote-runtime`。用户给出的 `~/x-remote-runtime` 不存在，实际 CoreX 仓库为 `/home/lvtong/corex-remote-runtime`；Cricket 为 `/home/lvtong/cricket`。参考 commit 在 evidence provenance 中留存；CoreX 工作树还有未提交实现，审计以实际文件为准。

## 环境实证

| 项目 | 结果 |
|---|---|
| CPU/OS | AArch64 Linux / Ubuntu，见 environment.log |
| 硬件 | 一块 MLU370-S4，mtp_372.41，24 GiB |
| Driver/Firmware | 5.10.35 / 1.1.6 |
| Neuware | /usr/local/neuware → neuware-3.5.2 |
| CNCC | 4.5.1，clang 11.1.0 |
| CNRT/CNDrv | 6.5.2 / 2.5.2 |
| CNNL | 无 cnnl.h/libcnnl.so；CNNL 真机调用阻塞 |
| 第二台设备服务器 | 本轮无可用地址/访问配置，跨主机最终验收待完成 |

SDK 头文件哈希在 `compat/summary.json` 和 environment.log。所有通过的 remote 用例都通过独立 TCP server process；没有把进程内 backend 单测当作远程成功。

## 原仓库资产与问题

保留：MLUR framing、RuntimeContext mutex、ServerSession、owner/generation 对象表、AllocationID+offset、H2D staging、Queue/Notifier/TransferID、reset、EOF cleanup、六个 BANG C preset 和生成的 manifests。没有移植另一套 RPC 或资源系统。

原始 operator-only 路径只能加载 server presets。工作树已有 Shim/image-upload 草稿，但缺少严格 ABI 验证，存在按名称走 preset、native header 依赖、launch configuration 签名错误等风险。当前改为真实注册 image + 参数 metadata，移除 operator 名称替换；同名不同实现的数值测试已通过。

源码审计发现并已修复：旧 RPC 可能把 short success reply 当作成功，返回未赋值的对象句柄；断线或损坏回复后还可能继续使用同一 socket。现在 Client 统一校验每个成功回复的形状、D2H 数据长度和 HELLO version；失败即关闭连接，后续调用明确报错，不自动重放。`client-response-faults.log` 保存 28 个 host-only 故障注入/健康错误场景。

审计时发现的缺口包括：每连接一线程且 session 结构/上下文较重、对象表线性查找、无连接数/内存总配额、未强制首包 HELLO、socket 无 timeout、无显式 reconnect 恢复策略、SIGTERM 不等待 worker drain。后续已完成 socket deadline、强制首包及延迟 native context 创建，并修复真实重启探针发现的确定性 ID 重复；见 [连接生命周期](TRANSPORT_LIFECYCLE.md)。再后续已实现连接/字节/transfer 配额、native 失败回滚与 quarantine、worker drain，并用纯 native CNDrv 和 cnmon 对照验证所列内存序列；见 [资源生命周期](RESOURCE_LIFECYCLE.md) 和 [48 项回归](../evidence/resources-20261102/README.md)。native 取消、物理硬隔离、长期计量、公平性、认证/TLS 和应用恢复仍列为 M4 缺口，尚未生产验收。

后续 [Driver Shim 增量](DRIVER_SHIM.md) 已为普通 CNDrv binary 提供独立客户端 ABI，覆盖 context/TLS、文件与内存 module、通用 launch 和真实 NULL queue 的初步语义。仍只支持台账列明的入口和参数格式，不能将其作为完整 vendor CNRT 底层替代。

## Cricket：可取方法与不适用部分

- `cpu/cpu-client.c` 的 __cudaRegisterFatBinary / Function / Var 获取编译器产物并注册，`cpu/cpu-elf2.c` 从 CUDA `.nv.info` 读参数大小/offset。该方法启发 BANG 私有 hooks 和 `.params.text`，格式不能复用 CUDA parser。
- `cpu/cpu-client-runtime.c` 以 kernel metadata 打包 launch 参数；`cpu/cpu-client-driver.c` 对 module/driver launch 走 RPC；`cpu/generate_dispatch.sh` 生成重复分派。
- `cpu/resource-mg.c/.h` 维护 client-address/native-address 映射，包含 bypass、恢复资源映射。MLU 继续用 session ID/owner/generation，不能把未找到句柄直接当 native 地址传下去。
- `cpu/cpu-libwrap.c` 的 dlopen/真实库 fallback 需要本地 vendor 依赖，不满足无 Driver 客户端；本实现无此 fallback。
- Cricket 的 CUDA dim3、parameter metadata、registration 结构、默认 stream 规则不能直接解释 CNRT task grid/KernelClass。

本轮只做源码参考，未运行 Cricket CUDA 集成测试，也不宣称其所有资源语义可用。

## CoreX：复用工程方法

- `src/corex_fatbin_runtime.c` 的可读 PT_LOAD 限界、metadata 判定和拒绝未知格式，适用于 MLU 的解析信任边界；二进制格式仍按 CNCC 实测重写。
- `schema/corex_api_schema.json`、`scripts/generate-api-schema.py`、`compat/README.md` 将 API census、人工状态、schema plumbing、evidence 分开。MLU 采用相同职责边界，生成 10 对简单对象 RPC，关键生命周期手写。
- fake VA / session object registry / staging / completion token 是现有 MLU 资产可继续承载的机制，避免引入第二套资源模型。
- 测试按能力族组织：同一 native binary 的原生/替换/preload；协议负例、资源隔离、生命周期和证据目录。CoreX API 参数和 CUDA 同步语义没有复制到 MLU。

本轮没有修改参考仓库。兼容台账以 MLU 头文件为分母，不能用 CoreX 支持率估计 MLU 支持率。

## 真实验证发现

1. PushCallConfiguration 的 cnrtDim3_t 按值传递，AArch64 中 x/y 打包在 x0，z 在 x1；草稿按指针/错误签名会丢任务维度。
2. CNCC 部分 stub 的 InvokeKernel reserved 栈槽没有初始化；native 忽略，该字段不应参与 RPC。
3. CNFB 压缩记录 +20 是 64-byte padded span，+24 才是实际解压大小。小 image 恰好对齐不能覆盖一般情况；ABI 多 Kernel image 暴露了此差异。
4. TCP_NODELAY 移除原有偶然延迟后，旧测试的立即 query 可合法返回 NotReady；已接受 pending，sync 后再验证完成。
5. native cnrtMalloc(0)→success/NULL、cnrtFree(NULL)→success；负设备号为 ArgsInvalid，成功 API 不清除旧 error。Shim 依真实探针修正。
6. native 多线程 cnrtSyncDevice 与其他线程 queue teardown 竞态曾返回 CN_QUEUE_ERROR_INVALID；保留失败日志。正常压力测试改为每线程同步所属 queue；不把这一变化解释为已修复 vendor runtime。
7. 当前 CNCC 对最小 double 参数 load/store 崩溃（exit 254）；失败重现源在 probes/compiler_double.mlu。double 的端到端支持不算已验证。
