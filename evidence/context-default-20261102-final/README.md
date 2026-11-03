# Context flags / default Queue / boundary full regression

`verify-transparent.py --out evidence/context-default-20261102-final` 完成了构建后的完整透明回归。native、replacement、preload、Driver shim、协议故障、资源配额、生成契约、Parser bounds 和隔离客户端均通过；`server-resources` 复跑也通过并保存在 `server-resources-retry`。

本批新增的 wire async flag 保持了 56-byte v1 memcpy 请求兼容性；旧的 4-byte H2D payload 不会被误判成 async header。QueueID 0 映射 native NULL Queue，默认队列 staging 只在默认队列完成后释放。
