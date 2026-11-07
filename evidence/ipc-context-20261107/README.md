# IPC broker 的 Native Context 可访问性

2026-11-07，AArch64 / MLU370-S4 / Neuware 3.5.2 / CNDrv 2.5.2。此批仅为 Native 架构验证，不运行 Remote Server 或 Shim，不提升任何 API 状态。

此前独立 exec 的 native memory IPC 已成功，但同进程跨 Context 的 `cnIpcOpenMemHandle` 返回 100102。本批验证另一条路径：owner Context 分配真实设备内存，另一 Context 直接使用该进程中的原始地址；owner 及 allocation 全程存活。每个模式为独立进程，pthread 模式还在另一线程新建 importer Context。

| 模式 | 观察 | 检查数 |
|---|---|---:|
| same-kernel | 同 Context 数值正基线 | 34 |
| read | importer `cnMemcpyDtoH` 读到 owner 的 17 | 33 |
| write | importer `cnMemcpyHtoD` 写 23，owner 读到 23 | 17 |
| copy | importer `cnMemcpyDtoD` 将 owner 数据复制到自己的 allocation | 33 |
| memset | importer `cnMemsetD32` 写 29，owner 读到 29 | 17 |
| kernel | importer module/queue 的 kernel 混用 owner 指针与本地 input/output，得到 117+i | 33 |
| thread-kernel | 另一线程/Context 完成同样的混合 kernel | 35 |
| 合计 | 7/7 完成，全部 access_supported=true | 202 |

所有 native 调用返回 0，检查全部通过；importer 释放自己的 module/queue/allocations 并销毁 Context 后，owner allocation 内容仍正确，最终 owner free/destroy 成功。`results.json` 区分探针完成与 API 可访问性，拒绝观察不能当成支持；实际数值与每次调用见各模式 `.jsonl`，本批七项均支持。

这为单设备、单 Server 进程的 device backing broker 提供执行依据：导入会话可以引用同一 backing 地址并使用自己的 Context/queue/module，无须再次调用同进程 native IPC open。Remote 必须通过 broker token、ImportedAllocationID 和 owner/type/range 校验控制访问，原始 native 地址只留在 Server。

此推论限于实测的显式 copy、D32 memset 与有限 kernel；未证明所有 Native API 都允许跨 Context 地址。尚未验证 owner Context 提前销毁、跨设备访问、跨 Context Queue/Notifier handle 使用或不同 SDK。实现仍需 backing/Context refs、source revoke、operation leases、计量一次与失败回滚。owner EOF 后必须保留 backing 的原 Context，不能先调用普通 session cleanup 销毁它。Host CPU 共享及 VMM OS descriptor transport 没有因此解决。

复现：

```sh
python3 scripts/probe-ipc-context.py --out evidence/ipc-context-recheck
```

runner 使用 vendor cncc 编译 mtp_372 fatbin，gcc 直接链接 vendor CNDrv，显式清除 LD_PRELOAD 并选择 vendor LD_LIBRARY_PATH。`provenance.json` 保存 build commands、source base commit、探针/runner/binary/fatbin/SDK header/lib 哈希。运行结果不代表 Remote IPC 已完成；I1/I2 继续为 NOT_IMPLEMENTED。
