# Context flags、默认 Queue 与内存边界续批

本批在本机 MLU370-S4 / Neuware 3.5.2 上完成 native、remote、LD_PRELOAD 和独立 namespace 对照。

`results.json` 中的 runtime/Driver 四种模式和协议负例全部返回 0。新增断言覆盖：

- `cnrtGetDeviceFlag` / `cnrtSetDeviceFlag` 的有效、无效和 active-process 行为；
- CNRT `NULL` Queue 的异步 H2D、查询、同步和数据可见性；
- CNDrv `NULL` Queue 的 H2D、D2D 与通用 `cnMemcpyAsync`；
- owned allocation 的零长度、one-past 零长度、越界非零长度和 copy offset；
- 既有 Context、PCI、属性、Queue union 和协议 ownership 检查。

CNRT flags 在该硬件上接受合法 setter，但 native 查询保持有效的 SPIN 状态；Schema 因多设备/shared-context TLS 未验收而保持 `PARTIAL_IMPLEMENTED`。HostMalloc/FreeHost、异步 D2H writeback、复杂 Symbol/aggregate ABI 没有因本批查询成功而升级。
