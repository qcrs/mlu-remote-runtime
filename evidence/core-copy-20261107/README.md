# Error catalog 与统一拷贝收口（2026-11-07）

目标：MLU370-S4，Neuware 3.5.2 / CNRT 6.5.2 / CNDrv 2.5.2，AArch64→AArch64，单设备。

```sh
./scripts/build.sh
python3 scripts/capture-error-catalog.py --check
python3 scripts/verify-copy-closure.py --out evidence/core-copy-20261107 --port 50223
python3 scripts/verify-transparent.py --out evidence/core-copy-20261107/full --port 50224
python3 scripts/verify-api-families.py --out evidence/core-copy-20261107/family --port 50225
python3 scripts/verify-query-boundaries.py --out evidence/core-copy-20261107/query --port 50226
python3 scripts/verify-api-contracts.py
```

16/16 新批次运行通过；完整核心回归 59/59、family 15/15、query 11/11 通过。Server 与 Client 是独立 TCP 进程；namespace 保留网络、隐藏 SDK 和 `/dev`。测试输入来自 SDK 枚举，预期文本来自独立 Native 运行，不从生成表复制预期值。

- error：41 个 CNRT 与 127 个 CNDrv 枚举声明的并集共 139 个不同数值；另有八个未知输入，覆盖 signed-negative、199999 分界和 INT_MAX。四入口在 init 前运行，Shim 指向远端 port 1，replacement/preload/isolated 与 Native 文本、返回码和稳定生命周期一致。CNRT name/description 与 CNDrv 文本的标点/引号保持原样。Native Driver 空输出会崩溃，Shim 明确返回 INVALID_VALUE。
- copy：两库同步 H2H/H2D/D2H/D2D、非零 offsets，CNRT NoDirection async H2D/D2D/D2H 完成后数据，CNDrv unified async H2D/D2D、显式/NULL Queue 均与 Native 一致。
- owned VA：Shim 独立检查 tombstone、guard、超范围、Driver foreign Context；异步 H2D 返回后改写 Host 源仍保持 server staging snapshot。CNDrv async H2H/D2H 明确拒绝；CNRT async D2H 仍阻塞，未将它提升为完整。

本批四个 error API 与 cnrtMemcpy 升级 IMPLEMENTED；126 完整、14 部分、7 阻塞、2 范围外、4 拒绝、157 未实现。Schema/ledger/summary/family plan 与 closure resolution 同步；45 个公共 RPC wrappers、4 个生成的本地 API，旧 error RPC 保留 wire 兼容。

仅清除 SDK/cnmon 日志的行末空白；测试返回码、数值和内容保留。
