# 构建、部署与验证

## 构建边界

完整构建需要 AArch64 Linux、GCC/Python3、Neuware 头文件、CNCC、服务端 CNDrv 库；真机执行需要 Driver/MLU。当前 target 为 `mtp_372`。解析器运行需要系统 `liblz4.so.1`，Shim 参数分类需要 `libstdc++.so.6`；这两项不属于 vendor runtime。

```sh
./scripts/build.sh
python3 scripts/generate_compat.py
```

产物：

- `build/shim/libcnrt.so`：原生 CNRT ABI + BANG hooks；SONAME 为 libcnrt.so。
- `build/libremote_cnrt.so`：纯 host transport；无 SDK 动态库依赖。
- `build/libremote_cnnl.so`：保留原有 CNNL seam，当前返回不支持。
- `build/mlu-remote-server`：Native CNDrv backend。
- `build/driver-shim/libcndrv.so`：部分原生 Driver ABI，只有系统库与 client core 依赖；与 CNRT Shim 分目录选择。
- `build/native_driver`、`build/probes/probe_driver_semantics`：普通 CNDrv binary 的模块/launch/TLS/生命周期及原生语义探针；[运行方式](DRIVER_SHIM.md)。
- `build/native_bang_{remote,generic,abi,module,drain}`：普通 CNCC 编译的原生程序。
- `build/libnative_bang_module.so`、`build/native_bang_module_dso`：CNCC 生成的共享库及原生 CNRT/动态 loader 测试程序。
- `build/audit/`：ABI 用例的 `-save-temps` host/device 产物。
- `build/module-audit/`：共享 module/globals 用例的 host/device 产物。
- `build/generated/` 和 `modules/*.manifest`：生成的 RPC/registry/manifest。
- `modules/*.fatbin`：原有六个 BANG C preset。

仅构建纯 host Client，不安装 SDK：

```sh
CLIENT_CORE_ONLY=1 NEUWARE_HOME=/nonexistent ./scripts/build-client.sh
```

构建 Shim 时需 SDK 头文件：`NEUWARE_HOME=/usr/local/neuware ./scripts/build-client.sh`。**运行客户端不需要 SDK/Driver**，这一点已用 namespace 验证；不能把无需运行依赖解释为 CNCC 编译也无需 SDK。

## 原生与透明运行

服务端：

```sh
MLU_REMOTE_BIND=127.0.0.1 MLU_REMOTE_PORT=50051 ./scripts/run-server.sh
```

同一个 executable，先 native，再 replacement/preload：

```sh
LD_LIBRARY_PATH=/usr/local/neuware/lib64 ./build/native_bang_generic
MLU_REMOTE_HOST=127.0.0.1 MLU_REMOTE_PORT=50051 LD_LIBRARY_PATH="$PWD/build/shim:$PWD/build" ./build/native_bang_generic
MLU_REMOTE_HOST=127.0.0.1 MLU_REMOTE_PORT=50051 LD_PRELOAD="$PWD/build/shim/libcnrt.so" ./build/native_bang_generic
```

无需修改业务 `.mlu`，也不需要按 kernel 名称修改 server registry。当前 image/参数支持边界见 [执行链路](BANG_EXECUTION_CHAIN.md)。静态 CNRT、其他 SONAME/symbol version、RTLD_DEEPBIND 等不在已验证范围。

按用户要求的本机双终端模拟，可直接使用 [本地演示说明](DRIVERLESS_LOCAL_DEMO.md)，无需第二台服务器。

## 独立无 Driver 客户端部署

从构建机复制 `build/shim/libcnrt.so`、`build/libremote_cnrt.so` 和正常 CNCC 编译的 executable；保留目录结构。Shim 的 RUNPATH 为 `$ORIGIN/..`。当前产物为 AArch64，需要客户端具备兼容的 glibc、pthread/dl、libstdc++ 和 liblz4；未验证其他 CPU 的交叉构建或 scalar ABI。

在受控网络的 MLU 服务器启动：

```sh
MLU_REMOTE_BIND=0.0.0.0 MLU_REMOTE_PORT=50051 ./scripts/run-server.sh
```

客户端：

```sh
MLU_REMOTE_HOST=SERVER_IP MLU_REMOTE_PORT=50051 LD_LIBRARY_PATH="$PWD/build/shim:$PWD/build" ./build/native_bang_generic
```

`SERVER_IP` 替换为实际地址。socket deadline 和 HELLO 首包策略已实现，见 [连接配置](TRANSPORT_LIFECYCLE.md)；连接/字节配额和 worker drain 见 [资源生命周期](RESOURCE_LIFECYCLE.md)；认证、TLS、native 取消尚未完成。跨第二台物理主机尚缺地址/访问配置。

## 自动化证据

```sh
python3 scripts/verify-transparent.py --out evidence/your-batch
```

runner 创建一个仅监听 loopback 的独立 server，finally 只终止自己启动的进程；每个命令及相关环境写入日志。默认证据目录为 `evidence/transparent-20261102`，重复运行会覆盖该批日志，建议新批使用独立目录。

已有远端 server 时：

```sh
python3 scripts/verify-transparent.py --host SERVER_IP --port 50051 --out evidence/cross-host-batch
```

此完整 runner 仍在调用端运行 native/compiler/parser 对照，因此需从有 SDK/MLU 的验证主机运行；它不会启动或终止指定的远端 server。无 Driver 客户端单独执行前面的透明运行命令，额外记录两端 hostname、依赖、binary 哈希和数值结果。

验证族：

| 用例 | 核心检查 |
|---|---|
| native_driver / probe_driver_semantics | 同一 Driver binary 的 native/replacement/preload、隔离执行；context/TLS、四种 flags、立即文件/fatbinary 加载、共享 globals、混合参数、1200 launch/copy；原生语义日志逐行对照 |
| driver_protocol.py / driver_client_faults.py | 真机 HELLO16、binary 上传、NULL queue、cleanup ACK/EOF、跨 session 拒绝；host-only 配置/坏回复/EOF/旧 capability 注入，不重放且 control session 独立 |
| native_bang_remote | 普通 native vector_add，native/replacement/preload |
| native_bang_generic | 不存在于 preset registry 的 kernel，数值 +1 |
| native_bang_abi | 同名不同实现、混合标量、offset/NULL、xyz、线程、1200 launch/copy、notifier；remote 额外测 reset/stale/拒绝 ABI |
| native_bang_module | 同一 image 的两个 Kernel 共享未初始化设备变量；reset 保留注册并重新加载一个 module |
| native_bang_module_dso | 20 次 dlopen/dlclose、保留 loader 引用、pending-work unload、两批注册间 reset；native/replacement/preload/隔离客户端 |
| probe_semantics | 零分配、Free(NULL)、负设备、sticky error、H2D→D2D→D2H 和完成 query 的 native/remote 对照 |
| generic_protocol.py | 真 TCP module/launch、错误 arg count/size、跨 session module/allocation、offset/dim/class、坏 image、unload stale |
| shared_module_protocol.py | 多个稳定子 KernelID、重复 lookup、独立 image 状态、错误 parent、跨 session、unload/reset 级联失效；与旧协议用例共用 mlur_wire.py |
| client_response_faults.py | host-only 假服务端给真实 Client 注入坏回复；不输出句柄/数据、不重放；健康 API 错误后连接可用 |
| transport_deadlines.py | SDK-free Client 的真实 TCP 黑洞、半包、慢滴、总预算、backpressure、connect backlog 和 EINTR；超时后不发送/重放 |
| server_transport.py | 自启 loopback 真机 server；首包/重复 HELLO、延迟 context 创建、空闲/收发 deadline、另一客户端持续工作 |
| native_bang_drain | 有限 kernel 数值 native/replacement/preload；queue NOT_READY 为关闭测试提供原生对照 |
| server_resources.py / backend_resources.c | 自建真机 server 的配额、并发预留、pending kernel/H2D、socket drain、cnmon 计量；原生失败注入验证回滚/隔离 |
| server_restart.py | 仅暂停/kill 自建 server；断线/超时 context 不重放，重启后旧对象 ID 不误命中新 allocation |
| bang_image_bounds | ASan/UBSan：真实 image、截断、越界 section、NOBITS、变量 OBJECT/大小；kernel parser 与两种 variable image 各 2000 次 mutation |
| run-isolated-client.sh | user/mount namespace 隐藏当前 SDK 和整个 /dev，保留网络，连接 namespace 外的真机 server |
| 原有 client / operator / custom-kernel / isolation / reset / device / abrupt / CNNL | 六 preset 及内存/对象生命周期回归；CNNL 验证明确拒绝 |

namespace 测试要求系统允许 unprivileged user namespace 和 mount；否则应记录环境阻塞，不能把跳过算成通过。`client.strace` 检查 vendor lib/device 访问，namespace 才进一步证明当前客户端在隐藏 SDK/设备节点后可运行。两者都不替代跨物理主机证据。

## 生成流程与维护

```sh
python3 scripts/generate_operator_registry.py
python3 scripts/generate_rpc.py
python3 scripts/generate_rpc.py --check
python3 scripts/generate_compat.py
python3 scripts/generate_compat.py --check
```

`schema/mlu_api_schema.json` 保留 operator registry，并新增 `simple_rpc`；目前生成 10 对单对象 ID 包装/分派，同时校验 opcode 与协议头一致。关键的内存/异步/注册/归属语义仍手写。不要通过添加成功空函数提高兼容数字。

`compat/annotations.json` 是人工语义审查；生成器遍历安装头文件、核对真实导出，输出 310 行公共 API。更新 SDK 后先核对 diff、原生语义与证据，再调整状态。新增能力按 [M1～M4](../design/MILESTONES.md) 的交付门槛更新。

旧 preset 加载仍需同目录 manifest，server 验证 module、kernel symbol、架构、dtype/rank/count/scalar ABI。`MLU_REMOTE_MODULE_DIR` 可覆盖目录；`run-server.sh` 默认用项目绝对目录。透明 image upload 路径不依赖该目录。

EOF cleanup 路径由 `server.log` 留证，但 `SESSION_CLEANUP_EMPTY` 是代码标记，不能独立证明 native 资源计量归零。Client 断线/坏回复/超时后拒绝继续发送请求，不自动创建新 session 或重放；[连接生命周期](TRANSPORT_LIFECYCLE.md) 记录配置和已实现边界，已实现的连接/字节配额、worker drain 和独立 cnmon 对照见 [资源生命周期](RESOURCE_LIFECYCLE.md)；native 取消、应用恢复及其余验收仍见 M4。
