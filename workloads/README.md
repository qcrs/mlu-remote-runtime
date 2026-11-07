# Application Workloads

普通 CNCC/BANG C 应用：Host 生成确定性输入，CPU reference 逐元素对照，使用
`cnrtMalloc/cnrtMemcpy/kernel<<<>>>/sync`。源码没有模式判断、remote helper 或
preset operator RPC。七个程序优先展示正确性，不提供性能结论。

| 程序 | 默认尺寸 | 计算与覆盖能力 | 绝对误差容限 |
|---|---|---|---:|
| matrix_add | 256×256 FP32 | A+B；显式 Queue | 0 |
| naive_gemm | M=N=K=64 FP32 | M/N/K 三重循环；CPU double reference，非二进制分数输入 | 1e-4 |
| image_threshold | 512×512 灰度 | >127 输出255，否则0；输入含126/127/128 | 0 |
| image_blur | 512×512 灰度 | 3×3 均值；四周边界复制输入 | 1e-4 |
| image_sobel | 512×512 灰度 | 3×3 Sobel，min(abs(Gx)+abs(Gy),255)；四周边界置零 | 0 |
| queue_notifier | 4096 FP32 | async H2D → Notifier → 跨 Queue affine kernel | 1e-6 |
| row_center | 65×127 FP32 | 行均值归约 → `(x-mean)*1.25-0.5`，nested struct/array 按值参数 | 1e-4 |

图像以 float 存储 0..255 灰度，无需图片库。Kernel 使用 `taskId/taskDim` 分配
4 个 Block task。`row_center` 的两个 Kernel 在同一 image 内，使用已有编译器
metadata 机制，不要求修改应用源码；没有 embedded pointer 或 pointer graph。
它还验证默认 Queue、async memset、context sync、pitched D2D、allocation offset，
并读回 padding/首尾 guard 与中间均值，检查越界写入及中间结果。
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
```

`--skip-build` 验证源码与 executable 的 SHA256，过期构建会报错。
`row_center` 构建时复用 `generate_bang_metadata.py`，把 CNCC IR 类型描述附入
`.mlu.remote.abi`；Native 和两种 Remote 模式均运行这一份 executable。

脚本覆盖默认和小尺寸，共 16 组：非方形矩阵（17×31）、非方形 GEMM（17×23×19）、
非整齐图像、1×1 threshold、1×7 blur/Sobel 和单列中心化。输出统一包含
`WORKLOAD/SHAPE/CPU_CHECKSUM/MLU_CHECKSUM/MAX_ABS_ERROR/TOLERANCE/MISMATCH/RESULT`；
图像额外报告 `WIDTH/HEIGHT/BOUNDARY`。`MLU_CHECKSUM` 在三种模式统一命名，模式由
脚本记录，应用不判断环境。未写入输出初始化为 NaN，非有限结果直接失败。

`--out` 指定证据目录（默认 `evidence/workloads-20261107-extended`），保存各次
stdout/stderr、编译命令、源码/executable SHA256、struct ABI manifest 和
`results.json` 数值汇总。每个程序只构建一次，再运行 Native、library replacement
和 LD_PRELOAD；比较 CPU reference、逐元素误差与 Native/Remote checksum 容限。
最后运行一次现有 `native_bang_generic` core smoke。没有触发 fault、quota、restart、
DSO stress、parser fuzz 等历史 suite。

单独观察 Remote 客户端（同一个 executable 也可以直接 Native 或 LD_PRELOAD 运行）：

```sh
MLU_REMOTE_HOST=127.0.0.1 MLU_REMOTE_PORT=50261 \
LD_LIBRARY_PATH="$PWD/build/shim:$PWD/build" ./build/workloads/naive_gemm 17 23 19
```

[本轮实测](../evidence/workloads-20261107-extended/README.md)：48 次 workload 执行
全部 PASS；GEMM 和行中心化的最大绝对误差均为 `9.53674316e-7`，其他五项为 0。
