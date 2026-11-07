# Host cast（CNRT 6.5.2 / AArch64）

`common/host_cast.c` 为 legacy/V2 cast 及 quantized parameter 提供共享本地实现；不初始化设备、不建立 TCP 连接，也不加载 vendor 库。支持范围由当前安装 SDK 的独立进程差分决定，不能从 enum 声明推导全部类型均可互转。此处记录 SDK 行为，包括与通常数值转换不同的结果。

## 类型组合

下面 `f16/f32/f64/bf16` 表示 half/float/double/bfloat，`iN/uN` 表示 N-bit signed/unsigned。相同类型也必须在支持表内；其他组合返回 `cnrtErrorArgsInvalid`（100006）。

| 来源 | Legacy plain 目标 | Legacy scalar quant 目标 |
|---|---|---|
| f16 | f32, i8, i16, i32, u8 | i8, i16 |
| f32 | f16, f32, i8, i16, i32, i64, u8, bool | f16, f32, i8, i16, i32, bool |
| i8 / i16 | f16, f32 | f16, f32 |
| i32 | f16, f32 | f32 |
| i64 / u8 | f16, f32 | — |
| bool | f32 | — |

| 来源 | V2 plain 目标 | V2 scalar quant 目标 |
|---|---|---|
| f64 | f16, bf16 | bf16（成功但不写目标） |
| f32 | f32, f16, bf16, 全部 i8–i64/u8–u64, bool | f32, f16, i32, i16, i8, bool |
| f16 | f32, u32, u16, u8, i64, i32, i16, i8 | i16, i8 |
| bf16 | f32, u64, u32, u16, i64, i32 | — |
| u64 / u32 / u16 | f32, f16, bf16 | — |
| u8 / i64 | f32, f16, bf16 | f32 |
| i32 | f32, f16, bf16 | — |
| i16 | f32, f16, bf16 | f32, f16 |
| i8 | f32, f16, bf16 | f32, f16, bf16（bf16 成功但不写目标） |
| bool | f32 | f32 |

Legacy plain/scalar 分别 24/13 对，V2 为 53/17 对。两版本按通道量化仅支持 `f32→i8/i16`。INT4、auto、fix8、quant8 以及 legacy double 等已在完整 enum 组合中验证拒绝；当前 SDK 也拒绝头文件所列 V2 `half→bfloat/u64`，这不是 Remote 缺项。

## 参数与数值

- 参数创建复制输入；by-channel 保存完整 shape、channel 维和 inner stride，索引为 `(i / inner) % channels`。当前入口要求 count 等于 shape 的元素总数；不认证部分 tensor 或重叠转换。调用者提供有效 Host 源/目标缓冲区和存活的参数对象。
- 量化 factor 为 `scale / 2^pos`，支持负 pos。float/half 到 i8/i16 的原生 offset 使用整数值的 float 位解释；实现保留 fused multiply/add、double 除法再回到 float 的顺序。负 offset 可生成 NaN，整数结果按原生路径处理。
- Legacy i8/i16 反量化同样按位解释 offset；V2 使用 offset 的正常数值。例如 pos=1、scale=.75、offset=3 时 V2 i8 `[-2,0,2]→[-1.3333334,4,9.333333]`。
- Scalar `f32→i32` 忽略量化并截断原值。Legacy quantized `f32→f16` 输出量化后 rounded int16 的位模式；V2 才输出 half，并忽略 offset。f32 identity 直接复制；表中的两个 V2 dispatch 保留原生成功但不写目标行为。
- V2 integer `rn` 是 `2*round(x/2)`，例如 -3→-4、1→2、63→64；rz/rd/ru/ro/rm 分别为截断、向下、向上、离零、half-away。不能将该 SDK 的 rn 描述为标准 nearest-even。
- V2 integer rz 设置并保留当前线程的 FE_TOWARDZERO，后续量化元素会受影响；共享模块单独用 `-frounding-math` 编译。测试每次调用前恢复默认环境，仍检查一次调用内的全部元素。
- V2 half 的 rn/rm 使用 ties-even，整数到 half/bfloat 忽略传入 mode。BF16 directed modes 按截断后的高 16 bits 加减：rd 总是减一，ru 总是加一，ro 正加负减；rz 截断。
- Half 到窄整数保留 SDK 的先 int32 再窄化行为；half→u8 负值 clamp0、正溢出 wrap，half→u16 仅 rn 负值 wrap。其余整数路径按目标宽度饱和，legacy f32→i64 的范围特例为 ±2³²。
- Legacy half 转换把非零 underflow 和 -0 映射到带符号最小 subnormal，预先 overflow/Inf/NaN clamp 到 ±65504；正常 ties away，65520 的 rounding carry 可生成 Inf。Plain i16→half 截断。

## 验证边界

[批次证据](../evidence/host-cast-20261107/README.md) 包含 7,507 个逐字节观察，覆盖全 enum 对、六种 V2 mode、两组有限数值、scalar/by-channel、owned-array mutation、负 pos、64-bit 大数及源/目标 guards。Native 与三种客户端模式各在独立进程运行；客户端远端不可达，隔离模式隐藏 SDK 和设备节点。UBSan/float-cast-overflow 检查同样通过。

本机 Native legacy `i16→half` 输入 -32768 会挂起（单元素独立进程 timeout 已复现）；Remote 不复制挂起缺陷，矩阵 signed i16 最小样本改用 -32767。此项以及非有限浮点 payload、double rounding 的全部临界点、所有浮点环境、任意内存 overlap、其他 SDK/CPU 不在本次数值认证中。实现表和状态按当前明确边界收口，不代表通用 IEEE 转换库。
