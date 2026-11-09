# 2026-11-08 Workload 扩展验证

本批增加 `cndrv_gemm`、`llm_transformer` 和 `runtime_driver_interop`。三者均由 CNCC
成功编译；两个 CNDrv workload 还生成并附加了 compiler-derived Driver image ABI metadata。
构建命令、可执行文件哈希和源码哈希见 [artifacts.json](artifacts.json)，两份 Driver
image 与 ABI manifest 分别为 `cndrv_gemm.cnfatbin`、`cndrv_gemm-abi.json`、
`runtime_driver_interop.cnfatbin` 和 `runtime_driver_interop-abi.json`。

运行验证未开始。自动启动的服务端尝试监听 `127.0.0.1:50221`，但初始化时因本机没有可用
MLU 设备而退出：`No useful mlu device (100004)`。因此 [results.json](results.json) 的
`results` 与 `core_smoke` 均为空；本批没有 workload、隔离客户端或 core smoke 结果，不能
据此声称运行通过。服务端诊断保存在 [server.log](server.log)。

在有可用 MLU driver/device 的主机上，运行全部 11 个 workload 并生成独立证据目录：

```sh
python3 scripts/verify-workloads.py --out evidence/workloads-20261108-full
```

全量矩阵共 22 组输入；自动服务端模式含 68 次 workload 执行和一次 core smoke。使用
`--external-server` 时不执行两个 Driver 隔离客户端，共 66 次 workload 执行。默认隔离客户端脚本
隐藏 SDK 路径与 `/dev` 设备节点，但保留宿主网络命名空间，项目文件也仍可见；本批因服务端
提前退出，没有执行 workload 隔离客户端。之后通过独立的拓扑自检验证 sibling network namespace、
veth/TCP、client chroot 和 Driver image payload 传输，结果见
[isolation topology evidence](../isolation-topology-20261108/README.md)。这项不包含 MLU server、
runtime protocol 或 workload 数值验证。
