# 资源配额与服务端关闭

范围：2026-11-02，AArch64 / Neuware 3.5.2 / MLU370-S4。保留原有 TCP、对象所有权和 native backend；新增配额不会把原生资源错误改成成功。实现位于 `server/resource_limits.c`、`server/mlu_backend.c` 和 `server/mlu_remote_server.c`。

## 配置与计量口径

启动时读取配置，只接受正十进制整数；空值、零、负数、尾随空白、溢出和超范围均在监听前失败。所有 byte 配额的范围为 1..UINT64_MAX，可大于物理设备容量；实际分配仍须通过 native CNDrv。MiB = 1024² bytes。

| 环境变量 | 默认值 | 计量 |
|---|---:|---|
| MLU_REMOTE_MAX_CONNECTIONS | 32，最大 1024 | 已接受且尚未完成清理的 worker，包括未发送 HELLO、partial frame、发送阻塞及 native cleanup 中的连接 |
| MLU_REMOTE_SESSION_DEVICE_BYTES | 1 GiB | 单 session 的成功设备分配请求字节及在途预留 |
| MLU_REMOTE_TOTAL_DEVICE_BYTES | 8 GiB | 全进程上述设备分配预留与存活字节 |
| MLU_REMOTE_SESSION_STAGING_BYTES | 64 MiB | 单 session 持有的异步 H2D host snapshot |
| MLU_REMOTE_TOTAL_STAGING_BYTES | 256 MiB | 全进程异步 H2D host snapshot |
| MLU_REMOTE_SESSION_IMAGE_BYTES | 64 MiB | 单 session 保留的上传 image 原始字节及解析/加载时预留 |
| MLU_REMOTE_TOTAL_IMAGE_BYTES | 256 MiB | 全进程上述上传 image 字节 |
| MLU_REMOTE_SESSION_MAX_TRANSFERS | 1024，最大 1024 | 尚未回收的 TransferID；包括已完成但未 release，以及 release 后 native completion 尚未到达的项 |

会话预留和全进程预留在同一 mutex 内检查、增加；使用减法检查余量，拒绝 UINT64_MAX 等超额请求时不会整数回绕。不同 worker 不能同时抢占同一份剩余额度。单 worker 串行处理本 session；global lock 不跨 native 调用持有。

连接满额时立即关闭新 socket，已有连接可继续使用。此时没有可用 session，不发送虚构 HELLO 成功。其他配额拒绝返回 `MLU_REMOTE_RESOURCE_LIMIT=0x80000007`、错误名 `REMOTE_RESOURCE_LIMIT`；CNRT Shim 映射为 `cnrtErrorNoMem`。该错误保留连接，不会 poison。错误表示本次设备分配、image load、copy 或 launch 尚未提交；调用者可在释放资源后主动重试。

Transfer 容量在 copy/launch 的 native 提交前检查。不能先改变内存或 kernel 状态，再把可预知的额度不足当作失败返回。创建 native notifier 等后续原生失败仍可能发生在提交之后；这条旧语义没有变成事务式回滚，不能任意自动重放。

## 释放与失败路径

设备分配 native 失败会退回预留；成功 free 退回对应字节。异步 H2D 只在确认 queue completion 后释放 snapshot 和额度；拒绝、host malloc 或 native enqueue 失败退回本次预留。不同 queue 的 snapshot 不互相释放。

image 在解析前预留；坏 image、分配失败、native load 失败退回预留。成功 unload 释放 image。失败回滚中的 native unload 如果也失败，保留镜像及额度，交由 session cleanup/context destruction 处理。预置 module 不保留上传 image，因此不计入上传 image 字节。

当前 D2H 是回复携带主机数据的同步模型：显式 queue 先 sync，成功后再执行同步 `cnMemcpyDtoH`。这样 sync 失败时不会留下向 dispatcher 即将释放的回复缓冲区写入的异步任务。仍未实现 pinned host buffer、异步主机可见性和延后写回。

EOF/reset/关闭时逐项记录 native cleanup 错误，包括操作名、对象 ID 和原生状态；不再无条件打印清理成功。queue sync 失败时，H2D snapshot 保持存活，直到成功销毁 native context。若 context 销毁也失败，保留尚未确认可释放的 host 内存和额度，报告 `CLEANUP_QUARANTINED`，停止接受新连接并 drain 其他 worker，最终非零退出。不能通过重复断开连接无限逃逸配额。进程退出是这种异常的隔离边界；没有实现 native context 修复。

## SIGTERM / SIGINT

信号处理只设置 lock-free stop flag，并向非阻塞 self-pipe 写通知。主线程拥有 listener 的唯一 close；避免从信号处理器和主线程重复关闭已被系统复用的 fd。

主线程停止 accept 后，在 worker registry 锁内对现有 socket 执行 `shutdown(SHUT_RDWR)`，唤醒 header/body 接收、空闲和回复发送等待。socket 的关闭和 registry 移除使用同一锁，避免 shutdown 已被复用的 socket。已开始的 native 工作按已有 queue 同步和 context 清理路径完成。主线程等所有 worker 清理结束，再报告 `DRAIN_COMPLETE` 和 `RESOURCE_TOTAL`，正常退出返回 0；观测到 native cleanup 错误返回 1。

这没有 native 执行截止时间。卡住的 CNDrv、长 kernel 或不可返回的 cleanup 仍可使 drain 等待；没有通过 pthread cancel、强制释放在途 host buffer 或设备 reset 冒充取消。SIGKILL 的验证仅覆盖客户端连接失效，不能算 graceful drain。

## 真机证据与限制

见 [本轮证据](../evidence/resources-20261102/README.md)。测试包括原生三线程 CNDrv 对照、真实 TCP 会话/进程字节配额、并发抢占、跨 session 对象拒绝、transfer 拒绝前数值不变、坏 image/native 失败回滚、pending H2D 跨 queue 的额度回收、EOF/reset、连接准入、partial frame/回复 backpressure、有限 kernel 在 queue 明确 NOT_READY 时 SIGTERM，以及 native cleanup 失败注入。

独立 `cnmon` 实测：原生三线程分配 40+40 MiB、free/再分配 20 MiB 的序列，初始物理使用 0 MiB，80 MiB 分配后为 80 MiB，全部 free 后仍为 112 MiB，reset 后 32 MiB，worker 退出后为 0。远程同序列复现相同值。数字属于该批设备环境，不能外推为所有 SDK 的固定缓存大小或缓存实现；证据仅证明逻辑 free 不等于物理驻留立即归零。首次测试错误地要求 free/reset 后物理为零，失败记录保留。

设备配额计请求字节，**不是 native 物理内存硬隔离**。Driver rounding/cache、context/queue/module 的内部设备内存、其他进程、host frame/response 缓冲、parser 解压临时内存和线程栈不在 byte counters 中；它们分别受协议长度、parser 上限、对象表及连接上限约束。只测量了所列序列的物理内存回收，没有逐类原生对象计数器或长期泄漏证明。

配额按 session 和当前 server 进程生效。没有认证身份、租户保留份额、跨进程/多设备分池、公平调度或单一客户端多连接约束；总额可被先来的合法 session 占满。每类 1024 个对象的既有硬上限保留，transfer 全局数量还受连接数×session 上限约束。因此 M4.2/M4.3 的上述基础已实现并验证，完整生产资源隔离与有界 native 取消仍是部分完成。
