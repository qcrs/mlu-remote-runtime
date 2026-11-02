# API support matrix

Ground truth captured on 2026-10-31: MLU370-S4 (`mtp_372.41`), driver
5.10.35, Neuware 3.5.2, CNRT 6.5.2, CNDrv 2.5.2, one visible device.

| Capability | Status | Evidence / boundary |
|---|---|---|
| Fixed 24-byte TCP header, network byte order, request sequence | IMPLEMENTED | Independent client/server framing and request matching |
| HELLO version and capability negotiation | IMPLEMENTED | Version 1 and capability bits returned by server |
| Device count/name/total memory | IMPLEMENTED | CNDrv query and client-basic output |
| Device selection | IMPLEMENTED | Session context can switch before session resources are created; invalid ordinals are rejected |
| Initial device selection and session reset | IMPLEMENTED | HELLO requested device and `SESSION_RESET` context recreation |
| Allocation/free and checked offsets | IMPLEMENTED | Native `cnMalloc`/`cnFree`, per-session AllocationID registry |
| H2D/D2H/D2D and TransferID | IMPLEMENTED | Queue enqueue, host staging ownership, D2H completion before response |
| Queue create/destroy/query/sync | IMPLEMENTED | Native CNDrv queue APIs |
| Notifier create/record/query/sync | IMPLEMENTED | Native CNDrv notifier APIs |
| Transfer query/sync | IMPLEMENTED | Each queued transfer has an internal CNDrv notifier; query/sync waits on that completion point |
| Vendor error code/name/string | IMPLEMENTED | Common response prefix and client last-error |
| EOF resource cleanup | IMPLEMENTED | Abrupt client plus `SESSION_CLEANUP_EMPTY` server records |
| Preset module load/unload | IMPLEMENTED | Six validated `.fatbin` modules; no upload endpoint |
| `vector_add` operator | IMPLEMENTED | F32 rank-1, two inputs and one output |
| `scalar_add` operator | IMPLEMENTED | F32 rank-1, one input/output, one IEEE-754 F32 scalar |
| `relu` operator | IMPLEMENTED | F32 rank-1, one input/output |
| `tensor_add_strided` operator | IMPLEMENTED | F32 rank-2, per-tensor shape and row/column stride |
| `scalar_mul` operator | IMPLEMENTED | F32 rank-1, one input/output, one IEEE-754 F32 scalar |
| `tensor_mul_strided` operator | IMPLEMENTED | F32 rank-2, per-tensor shape and row/column stride |
| Operator registry and generic launch | IMPLEMENTED | OperatorID validates descriptor, module, kernel and parameter layout |
| Generated operator registry and preset ABI manifest | IMPLEMENTED | Build derives C registry/manifest from JSON schema; server validates before module load |
| Generic object registry | IMPLEMENTED | Session-owned type/generation table backs allocation, queue, notifier, module, kernel, and transfer IDs |
| Queue operation state/dependency metadata | IMPLEMENTED | Transfer records carry sequence, same-queue predecessor, and submitted/complete state |
| Arbitrary module upload | BACKEND_UNSUPPORTED | First stage accepts only server-side `modules` presets |
| Multi-device deployment | PARTIAL | Selection/rebind is implemented, but this host exposes one MLU device |
| CNNL operation | GROUND_TRUTH_BLOCKED | This Neuware installation has no `cnnl.h` or `libcnnl.so` |

The operator implementation uses BANG C and CNDrv directly; it does not
emulate a CNNL or CUDA API.
