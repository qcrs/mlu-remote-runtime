# Pointer-free BANG aggregate ABI — 2026-11-03

基线 `cfa3399`；MLU370-S4 / Neuware 3.5.2 / AArch64，单设备。

实现 CNCC IR 中 pointer-free 自然布局 byval struct、nested struct、fixed array 的布局提取与 generic launch。成员 offset/alignment/padding 来自已审查的 compiler datalayout；image 指纹、精确 symbol、设备 ELF 参数数量/大小交叉验证。runtime kind 2 表示已经验证不含 pointer 的 aggregate，按原始字节传递，不按数值猜 pointer；成员布局见 [compiler manifest](compiler-layout-manifest.json)。无需业务源码 remote helper。

Native/replacement/preload/隐藏 SDK 与设备节点的 namespace 均得到 `21 / 11 / -123`，覆盖嵌套 short/int、float 数组、负数及 padding。见 [Native](metadata-native.log)、[Remote](metadata-remote.log)、[preload](metadata-preload.log)、[namespace](metadata-isolated.log)。含 pointer、recursive、opaque、packed 类型仍拒绝；损坏/stale metadata 拒绝见 [validation](metadata-validation.log)。Driver module lookup 的类型范围未扩展。

验证：build 成功；[L0](l0.log) schema/codegen/contracts；[L1/L2](results.json) 15/15，通过 runtime/driver/compiler family 与 replacement/preload/namespace；注册 metadata 核心分类变化触发 [L3](full-suite/results.json)，59/59 通过，包括完整透明执行、资源与故障回归。select-tests.py 已将 bang metadata、runtime_context、backend 改动升级 L3。

API 数量前后不变（IMPLEMENTED / PARTIAL / UNSUPPORTED / NOT_IMPLEMENTED）：CNRT 20/30/3/77；CNDrv 25/48/1/106；合计 45/78/4/183。`cnrtInvokeKernel` 保持 PARTIAL，扩大已验证边界。Typed RPC 40→40，generated public wrappers 46→46，simple object RPC 10→10，Schema 310→310。Module/Kernel 新增一组 ABI 能力，其余 API family 数量增量为 0。见 [批次统计](change-counts.json)、[制品 manifest](artifact-manifest.json)。

HostMalloc/FreeHost、真 Async D2H completion/fetch/writeback 本批未实现；embedded pointer、pointer graph、Host Symbol 和 initialized/constant/external globals 未新增支持，保持现有台账边界。
