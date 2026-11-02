# 共享 Module 与本机无 NPU 客户端验证

2026-11-02，AArch64 / MLU370-S4 / Neuware 3.5.2。此批次在既有透明链路基础上，落实一个 FatBinary registration→一个远程 ModuleID→多个 KernelID；保留 TCP、Session、对象表、内存/Queue/Notifier 和六 preset。

## 实际结果

[38 项 suite/results.json](suite/results.json) 全部返回期望值。运行命令：

```sh
./scripts/build.sh
python3 scripts/generate_compat.py
python3 scripts/verify-transparent.py --out evidence/shared-module-20261102/suite --port 52291
```

Runner 启动独立 loopback server，结束后只停止自己创建的进程。[build.log](build.log) 保留完整构建输出；其中既有 warning 未作为零 warning 宣称。原始单一 native 探针的首次编译/运行另保留在 [native-build.log](native-build.log)、[native.log](native.log)，最终带 reset/DSO 的程序结果以 suite 为准。

| 验证项 | 原生证据 | 远程证据 |
|---|---|---|
| 两个 kernel 共享 `__mlu_device__ int[4]`；reset 后原注册重载 | [native](suite/native_bang_module-native.log) | [replacement](suite/native_bang_module-replacement.log)、[preload](suite/native_bang_module-preload.log)、[隔离客户端](suite/isolated-shared-module.log) |
| DSO 20 次重载；一个 loader 引用仍存活；提交后 dlclose；中途 reset | [native](suite/native_bang_module_dso-native.log) | [replacement](suite/native_bang_module_dso-replacement.log)、[preload](suite/native_bang_module_dso-preload.log)、[隔离客户端](suite/isolated-module-dso.log) |
| 稳定 child IDs、独立加载相同 image 保持状态隔离、错误 parent/跨 session、unload/reset 级联失效 | native-backed TCP 后端 | [shared-module-protocol.log](suite/shared-module-protocol.log) |
| 新 opcode 回复长度/输出 sentinel/坏连接拒绝；旧 peer 无 capability 时不发送 lookup | host-only 故障注入 | [首次 30 项](suite/client-response-faults.log)、[扩展 31 项含旧 peer](capability-and-response-faults.log) |
| 真实 image、截断/section/NOBITS、变量 OBJECT metadata、三组各 2000 次变异 | ASan/UBSan host parser | [parser-bounds.log](suite/parser-bounds.log) |
| 原有六 preset、ABI/1200 launch/copy、对象隔离及异常清理回归 | 原生对照和旧测试保留 | [完整结果](suite/results.json) |

[native-metadata.txt](native-metadata.txt) 包含 device ELF symbol/section、RegisterVar 的 host 汇编传参和 DSO 动态依赖。host size=16，但 device OBJECT size=64，section 是零文件长度 `.data.gdram_uninit.module_state`。当前按这一已测格式校验；不把 host size 直接当作 device 大小。

[provenance.json](provenance.json) 保存未提交源码快照、SDK headers、编译产物、原始日志的 SHA-256。旧证据批次仍保留，不能用旧 batch 的 hashes 代表当前实现。

## 本机双窗口模拟结论

隐藏当前 SDK 与整个 `/dev` 的客户端用同一 CNCC executable/DSO 通过 TCP 访问外部 server。[隔离共享状态日志](suite/isolated-shared-module.log) 和 [隔离 DSO 日志](suite/isolated-module-dso.log) 记录不同 user/mount namespace、同一 host network namespace、SDK/device 隐藏检查和数值 PASS。客户端运行不需要 vendor runtime/Driver；不是仅新开一个普通终端。

双终端命令见 [演示说明](../../docs/DRIVERLESS_LOCAL_DEMO.md)。第二物理主机、物理网络故障和异构 CPU 仍未验收。

## 语义边界

- `MODULE_GET_KERNEL=29` 及 capability `0x800` 为增量；opcode 26/27/28 和六 preset 保留。旧 peer 无能力时明确拒绝第二个 kernel lookup，不复制 module 伪装共享状态。
- 已测普通未初始化设备变量在两个 Kernel 内部共享。Host symbol address/copy API 尚无客户端导出；initialized/constant/external globals 不接受本轮注册路径。
- 20 次顺序 DSO reload 与 pending-work unload 通过；并发 dlclose、跨 DSO image 引用、恶意 stale function pointer 不在该结果范围。注销释放 kernel/name/image metadata，保留小型 registration 身份 tombstone 至进程退出。
- CNRT/CNDrv 公共 API 数量没有增加，`cnrtInvokeKernel` 仍为部分实现。M3.1 仍部分完成，不能据此宣布 M3/M4 或最终跨主机目标完成。
- CNNL 项验证明确拒绝；`SESSION_CLEANUP_EMPTY` 仍只是代码路径标记，未独立测量所有 native 资源归零。
