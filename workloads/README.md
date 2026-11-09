# Application Workloads

普通 CNCC/BANG C 应用：Host 生成确定性输入，CPU reference 逐元素对照；Runtime 程序
使用 `cnrtMalloc/cnrtMemcpy/kernel<<<>>>/sync`，Driver 程序使用相应 CNDrv API。源码
没有模式判断、remote helper 或 preset operator RPC。共 11 个程序，优先展示正确性，不提供性能结论；原有八个程序和新增
`cndrv_gemm`、`llm_transformer`、`runtime_driver_interop` 均已有宿主 MLU 的 Native→Remote 运行证据。

| 程序 | 默认尺寸 | 计算与覆盖能力 | 绝对误差容限 |
|---|---|---|---:|
| matrix_add | 256×256 FP32 | A+B；显式 Queue | 0 |
| naive_gemm | M=N=K=64 FP32 | M/N/K 三重循环；CPU double reference，非二进制分数输入 | 1e-4 |
| image_threshold | 512×512 灰度 | >127 输出255，否则0；输入含126/127/128 | 0 |
| image_blur | 512×512 灰度 | 3×3 均值；四周边界复制输入 | 1e-4 |
| image_sobel | 512×512 灰度 | 3×3 Sobel，min(abs(Gx)+abs(Gy),255)；四周边界置零 | 0 |
| queue_notifier | 4096 FP32 | async H2D → Notifier → 跨 Queue affine kernel | 1e-6 |
| row_center | 65×127 FP32 | 行均值归约 → `(x-mean)*1.25-0.5`，nested struct/array 按值参数 | 1e-4 |
| heat_diffusion | 193×129 FP32，32 步 | 五点 Jacobi 热扩散；同 Queue 连续 launch、双缓冲/offset/guard | 1e-5 |
| cndrv_gemm | 19×17×23 FP32 | CNDrv 异步 H2D、跨 Queue Notifier 依赖、GEMM、行归约和 D2H | 1e-4 |
| llm_transformer | 7 tokens × 8 width × 12 hidden | RMSNorm、QKV、RoPE、KV cache、causal attention/softmax、SwiGLU 和 residual；逐阶段 CPU reference | 7e-4 |
| runtime_driver_interop | 37 FP32 × 4 phases | 同进程 CNRT/CNDrv 数值对照，并跨 CNRT reset 与 Driver/Runtime 资源重建 | 1e-4 |

图像以 float 存储 0..255 灰度，无需图片库。Kernel 使用 `taskId/taskDim` 分配
4 个 Block task。`row_center` 的两个 Kernel 在同一 image 内，使用已有编译器
metadata 机制，不要求修改应用源码；没有 embedded pointer 或 pointer graph。
它还验证默认 Queue、async memset、context sync、pitched D2D、allocation offset，
并读回 padding/首尾 guard 与中间均值，检查越界写入及中间结果。
`heat_diffusion` 使用 `u_next = u + 0.125*(上+下+左+右-4*u)`，四周固定为
初始值；每步交换两个设备缓冲，仅在所有 Kernel 提交后 QueueSync。
CPU 使用独立 double precision 时间迭代；最后逐元素检查 FP32 输出及两个缓冲的 guard。
`queue_notifier` 覆盖已有 device/memory info、pointer attributes、Notifier
query/wait、跨 Queue 依赖和计时 API，不扩展 API 状态或能力边界。

构建与一键运行（自动启动并回收独立 TCP server）：

```sh
./scripts/build.sh
python3 scripts/verify-workloads.py
```

两个终端模拟本地客户端与远程服务端，推荐先运行 `./scripts/build.sh`。

终端 A（服务端，使用本机 MLU）：

```sh
MLU_REMOTE_BIND=127.0.0.1 MLU_REMOTE_PORT=50261 ./scripts/run-server.sh
```

终端 B（客户端，编译一次后运行三种模式）：

```sh
python3 scripts/verify-workloads.py --external-server --host 127.0.0.1 --port 50261
```

这是同机独立进程、真实 TCP；不代表第二物理主机验收。external-server 模式不
启动、终止或重新配置终端 A 的服务端。Native 对照仍需客户端所在机器有 MLU。

只编译或复用同一批 executable：

```sh
python3 scripts/verify-workloads.py --build-only
python3 scripts/verify-workloads.py --external-server --skip-build --port 50261
# 按需选择一个或多个实验，仍对照三种模式并运行一次 core smoke
python3 scripts/verify-workloads.py --external-server --skip-build --port 50261 \
  --workload naive_gemm --workload heat_diffusion --out /tmp/mlu-workload-demo
```

`--skip-build` 验证源码与 executable 的 SHA256，过期构建会报错。
`row_center` 构建时复用 `generate_bang_metadata.py`，把 CNCC IR 类型描述附入
`.mlu.remote.abi`；Native 和两种 Remote 模式均运行这一份 executable。

脚本覆盖默认和小尺寸，共 22 组：非方形矩阵（17×31）、非方形 GEMM（17×23×19）、
非整齐图像、1×1 threshold、1×7 blur/Sobel、单列中心化，以及热扩散的
偶数/奇数轮次（32/5）和无内部像素边界（1×7，1 步）。输出统一包含
`WORKLOAD/SHAPE/CPU_CHECKSUM/MLU_CHECKSUM/MAX_ABS_ERROR/TOLERANCE/MISMATCH/RESULT`；
图像额外报告 `WIDTH/HEIGHT/BOUNDARY`。`MLU_CHECKSUM` 在三种模式统一命名，模式由
脚本记录，应用不判断环境。未写入输出初始化为 NaN，非有限结果直接失败。

Driver 默认隔离运行隐藏 SDK 路径和 `/dev`，但沿用宿主网络及共享项目文件视图。需要更严格的本地模拟时，使用 `--realistic-isolation`：Driver Server 和 Client 在独立 network namespace 运行，经 veth/TCP 通信；Client 在只包含 executable、shim、系统动态库和显式文件参数的 chroot 中运行。

```sh
# 不依赖 MLU 的拓扑和输入传输自检
bash scripts/test-isolated-topology.sh

# 有 MLU 的主机上运行 Driver workload 的实际隔离路径
python3 scripts/verify-workloads.py --realistic-isolation --workload cndrv_gemm
```

拓扑自检把 Driver image 作为 Client 文件参数 staged 后传到 Server，比较两端 SHA-256；它只验证 namespace、文件视图与 TCP 传输，不模拟 MLU Server 或 runtime protocol。独立证据见 [isolation topology](../evidence/isolation-topology-20261108/README.md)。

`--out` 指定证据目录（默认 `evidence/workloads-20261108-full`），保存各次
stdout/stderr、编译命令、源码/executable SHA256、struct ABI manifest 和
`results.json` 数值汇总。每个程序只构建一次；原有八个程序和 `llm_transformer` 运行
Native、library replacement 和 LD_PRELOAD，两个 Driver 程序另加隔离客户端。全部 11 个程序共覆盖 22 组输入：自动服务端模式运行 68 次 workload；
`--external-server` 跳过两个隔离客户端，共运行 66 次。比较 CPU reference、逐元素误差与
Native/Remote checksum 容限。最后运行一次现有 `native_bang_generic` core smoke。没有触发
fault、quota、restart、DSO stress、parser fuzz 等历史 suite。

单独观察 Remote 客户端（同一个 executable 也可以直接 Native 或 LD_PRELOAD 运行）：

```sh
MLU_REMOTE_HOST=127.0.0.1 MLU_REMOTE_PORT=50261 \
LD_LIBRARY_PATH="$PWD/build/shim:$PWD/build" ./build/workloads/naive_gemm 17 23 19
```

[此前完整运行](../evidence/workloads-20261107-iterative/README.md)：原有八个程序的
19 组输入、57 次 workload 执行全部 PASS；GEMM 和行中心化的最大绝对误差均为
`9.53674316e-7`，热扩散为 `5.96046448e-8`，其他五项为 0。新增三程序的最新结果见
[CNDrv GEMM](../evidence/workloads-20261108-cndrv/results.json)、
[LLM transformer](../evidence/workloads-20261108-llm/results.json) 和
[CNRT/CNDrv interop](../evidence/workloads-20261108-interop/results.json)；
[realistic isolation](../evidence/workloads-20261108-realistic-final/results.json) 另通过了
Driver image 的 netns/veth/chroot 路径。较早的无设备尝试保留在[历史记录](../evidence/workloads-20261108-next/README.md)。
