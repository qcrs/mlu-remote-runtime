# Application Workloads

四个普通 CNCC/BANG C 程序，不调用 remote helper，不使用 preset RPC，也不区分 Native/Remote。使用 malloc Host 数组、CNRT 分配/拷贝、显式 Queue、普通 Kernel launch 与同步。每个程序自行生成确定性输入、计算 CPU reference，并逐元素验证输出；非有限数也判失败。

| 程序 | 尺寸 | 操作 | 绝对误差容限 |
|---|---|---|---|
| matrix_add | 256×256 FP32 | A+B | 0 |
| naive_gemm | M=N=K=64 FP32 | 三重循环矩阵乘 | 1e-4 |
| image_threshold | 512×512 灰度（float 存储） | >127 输出255，否则0 | 0 |
| image_blur | 512×512 灰度（float 存储） | 3×3 均值，四周边界复制输入 | 1e-4 |

Kernel 用 taskId/taskDim 分配工作，4 个 Block task；优先验证正确性，不提供性能结论。GEMM 输入是确定性的二进制可精确表示小数，仍保留 FP32 tolerance。

一键：

```sh
./scripts/build.sh
python3 scripts/verify-workloads.py
```

两个终端模拟本地客户端与远程服务端（同机独立进程、真实 TCP，不代表第二物理主机验收）：

终端 A：

```sh
MLU_REMOTE_BIND=127.0.0.1 MLU_REMOTE_PORT=50221 ./scripts/run-server.sh
```

终端 B：

```sh
python3 scripts/verify-workloads.py --external-server --host 127.0.0.1 --port 50221
```

脚本每个程序只编译一次，Native/replacement/preload 使用同一 executable，结果记录 SHA256。Native 使用 Neuware libcnrt，replacement 设置 Shim library path，preload 设置 LD_PRELOAD。默认自动启动并回收服务端；external-server 模式不管理终端 A 的服务端。支持 --skip-build、--out；输出每种模式日志、stderr、results.json，并运行一次现有 generic core smoke。

单独观察客户端输出：

```sh
MLU_REMOTE_HOST=127.0.0.1 MLU_REMOTE_PORT=50221 \
LD_LIBRARY_PATH="$PWD/build/shim:$PWD/build" ./build/workloads/naive_gemm
```

`MLU_CHECKSUM` 在三种模式统一命名，实际模式由日志文件名区分。程序无环境模式判断。无需图片解码库。
