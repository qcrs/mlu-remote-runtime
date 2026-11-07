# BANG C 原生编译与执行链路

范围：实测 CNCC 4.5.1、CNRT 6.5.2、CNDrv 2.5.2、AArch64、mtp_372。依据 `build/audit` 的 -save-temps 产物、SDK 私有声明、readelf/assembly、LZ4 byte comparison 和真机数值结果。

普通 Driver 应用现在也可通过独立 libcndrv.so Shim，直接上传 CNCC image 并调用 module/kernel API；它不经过 CNCC host-stub 注册 hooks。两条原生执行入口汇合到相同服务端 CNDrv module。详见 [Driver 语义与证据](DRIVER_SHIM.md)。

## 编译

```sh
/usr/local/neuware/bin/cncc --bang-mlu-arch=mtp_372 -save-temps \
  /absolute/path/tests/native_bang_abi.mlu -o /absolute/path/build/native_bang_abi
```

CNCC 将 .mlu 拆为 host 与 device 编译：host 产生 aarch64 object 和 launch stub；device 产生 mlisa-cambricon-bang / mtp_372 object；device image 经 CNFB 打包后由 host object 嵌入；最终 executable 默认动态依赖 libcnrt.so（另有 libc）。不是把 CUDA 转成 BANG，也不是远端重新编译源码。

`--bang-fatbin-only` 的 preset 文件以原始 CNFB 开头，server 旧路径通过 cnModuleLoad 文件加载。host 注册使用的 `.cnfatbin` 则是包含 `.cn_fatbin` 的 ELF64 容器。这两种入口不能混为一个解析格式；当前 upload 接受后者。

## 程序装载与注册

`.init_array` 中 __bang_module_ctor 构造注册 wrapper，调用 __bangRegisterFatBinary，随后 __bang_register_globals 调 __bangRegisterFunction。wrapper：

| offset | 内容 |
|---|---|
| 0 | uint32 magic = 0x466243b1 |
| 4 | uint32 version = 1 |
| 8 | host pointer → outer ELF image |
| 16 | reserved pointer |

外层 section `.cn_fatbin` 中包含 CNFB v2 压缩记录。当前测得记录：magic `CNFB 02 80 10 00`；+8 packed size；+20 64-byte padded image span；+24 actual expanded size；+32 compressed byte count；+36 LZ4 bytes。`scripts/inspect_bang.py` 解压并断言逐字节等于 CNCC device .o，结果保存在 image-audit.json。此记录是当前 toolchain 的实证，不是所有 SDK 的公开格式规范。

私有 hooks 的确切签名固定在 `include/bang_registration.h`，来自当前 CNCC bang.h 预处理结果。host function address 只作客户端注册 key，不发送到 server。device symbol 为完整 mangled name；直接替换其业务名字会破坏执行等价。

## 参数 metadata

device ELF 的 `.params.text.<mangled symbol>` 是 u32 数组，前 N 个为 sizes，后 N 个为 offsets。例如：

```text
vector_add(float*, const float*, const float*, int)
sizes   = [8, 8, 8, 4]
offsets = [0, 8, 16, 24]

mixed(float*, const float*, unsigned char, short, int, long long, float, const float*)
sizes   = [8, 8, 1, 2, 4, 8, 4, 8]
offsets = [0, 8,16,18,20,24,32,40]
```

这验证了大小/对齐，但没有指针类别。`generate_bang_metadata.py` 从同次 CNCC 的 device LLVM IR 生成 MLUABI02，写入宿主 executable/DSO 的非加载 `.mlu.remote.abi`；`--attach-images` 同时写入 Driver 加载的 image。记录以 `.cn_fatbin` payload 长度/FNV64 和精确符号关联，包含参数分类及聚合 pointer 字段 offset，并与设备 ELF sizes 交叉核对。该指纹用于关联产物，不提供认证。原业务源码无需修改。

C/enum/template/zero args、primitive pointer 与自然 LP64 byval nested struct/固定数组已通过；聚合中的 pointer 字段转换为 AllocationID+offset，wire 中对应八字节清零。`MLU_CAP_ARGUMENT_RELOCATIONS` 协商 kind 3，24-byte 网络序字段记录携带 offset/reserved/allocation/offset；Server 拒绝越界、未对齐、乱序/重叠、非零指针字节、跨 owner 和非法 NULL，再填入 native 地址。最多 512 个字段。无 metadata 时保留 primitive demangler，v1 metadata reader 保留；损坏/过期 metadata 拒绝。Device pointer graph、packed/opaque/未知地址空间和跨架构 ABI 仍不支持。

CNDrv 的 cnInvokeKernel 接受 void** 的逐参数地址；server 持有对齐的各参数值，因此无需把 offsets 当成连续 packed host buffer 再传给 Driver。

## Launch

```text
业务 <<<cnrtDim3_t, cnrtFunctionType_t, cnrtQueue_t>>>
  → __bangPushCallConfiguration(dim by value, class, queue)
  → CNCC host stub，构建每个参数值的地址数组
  → __bangPopCallConfiguration(&dim,&class,&queue)
  → cnrtInvokeKernel(host_function,dim,class,args,reserved,queue)
  → 当前 native CNRT 内部桥接（未逐条反汇编其实现）
  → module lookup / Native CNDrv cnInvokeKernel → hardware
```

透明路径替换最后的 CNRT 边界：lazy 上传原始 image → cnModuleLoadFatBinary → cnModuleGetKernel → 检查/重定位 allocation pointer → cnInvokeKernel → notifier / TransferID。server 的实际调用已经数值验证；不把 vendor CNRT 内部所有调用顺序当作已观测事实。

cnrtDim3_t 是 BANG task grid，xyz={2,2,2} 的 taskDim=8 已由 kernel 写回验证。KernelClass 使用 CNDrv 原生枚举，不引入 CUDA block/thread/shared-memory 语义。Block 已实测；Union 系列只做白名单转发，尚需对应真机能力族验收。

## 卸载

CNCC __bang_module_dtor 调 __bangUnregisterFatBinary。当前 Shim 每个 FatBinary registration 持有一个远程 ModuleID；后续 Kernel 通过 opcode 29 在同一 ModuleID 下查询，保留各自 KernelID。注销时同步 session 队列并卸载该 module，全部子 KernelID 失效，再释放 image、kernel/name metadata；仅保留小型 registration 身份 tombstone，避免旧 handle 被新 DSO 复用。session reset 清理远端对象但保留有效 host 注册，下次首个 launch 重新上传，其余符号从同一个 module 重新查询。

`tests/native_bang_module.mlu` 的两个 Kernel 通过 `__mlu_device__ int module_state[4]` 共享状态。RegisterVar 参数 ext=0、size=16、constant=0、global=0；device ELF 的 OBJECT size=64，位于零文件长度的 `.data.gdram_uninit.module_state`，host size 与 device symbol size 不相等。续批 `native_module_globals.mlu` 验证普通初始化与 const 对象：注册 flags 同样全零，`.data.gdram_init.<symbol>` 文件长度等于按 64-byte 对齐后的 OBJECT size，st_value=0。Shim 验证这两类 section 后发布 host registration size；原生 module 持有存储与初始化。Host symbol address/copy、external globals 不包含在此支持中。

`cnrtDeviceQueryKernelMemoryUsage` 使用客户端存活 registration 求和，缓存 Server 原生 `cnModuleQueryFatBinaryMemoryUsage` estimate。Native CNRT 在注册时已计量且 reset 保留，独立 CNDrv load/launch 的 CNRT 计数始终为零；直接读取 Server CNRT 计数会漏计。因此最终 unregister 才扣除对应 image；reset 只使 ModuleID 失效，下一次 launch 重新加载。证据见 [module 批次](../evidence/module-accounting-20261107/README.md)。

共享状态与 reset/reload 用例在 native/replacement/preload 和隐藏 SDK/设备的客户端通过；另有 20 次 DSO 重载、保留一个 loader 引用、pending-work unload 对照，以及 parent/child 所有权、重复 lookup、stale IDs 的 TCP 负例。见 [共享模块证据](../evidence/shared-module-20261102/README.md)。并发 dlclose 压力、跨 DSO image 引用和 multi-arch fatbin 仍为 M3 待办。

## 动态链接边界

当前同一 executable 三种运行方式通过：原生 vendor libcnrt、LD_LIBRARY_PATH 同 SONAME 替换、LD_PRELOAD 同 SONAME 预加载。dependencies.log 记录 DT_NEEDED/RPATH/ldd；client.strace 与 driver-access.json 核对没有访问 libcndrv/libcnbin 或 cambricon 节点。namespace 测试把 SDK 与整个 /dev 隐藏后仍通过。

这些证据证明当前动态链接形式的源码和 binary 兼容，以及客户端运行无原生 MLU Driver 依赖；不证明跨物理主机或静态链接可拦截。静态 CNRT 已内联/绑定的函数不能由 LD_PRELOAD 自动替换；需重新配置动态链接或研究受控重链接，不应要求业务源码改写。
