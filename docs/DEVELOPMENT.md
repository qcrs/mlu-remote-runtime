# Development and verification

Build on the installed Neuware image:

```sh
cd /home/lvtong/mlu-remote-runtime
./scripts/build.sh
```

The script first runs `scripts/generate_operator_registry.py`, which generates
the server operator table and one manifest per preset from
`schema/mlu_api_schema.json`. It then builds `libremote_cnrt.so`,
`libremote_cnnl.so`, the server, all independent clients, the protocol error
probe, native probes, and six BANG C fatbins using
`--bang-mlu-arch=mtp_372`.

Run the required two-process check:

```sh
# terminal A
MLU_REMOTE_BIND=127.0.0.1 MLU_REMOTE_PORT=50051 \
./build/mlu-remote-server

# terminal B
MLU_REMOTE_HOST=127.0.0.1 MLU_REMOTE_PORT=50051 \
./build/mlu-remote-client-test
```

Run the operator and lifecycle checks against the same listener:

```sh
MLU_REMOTE_HOST=127.0.0.1 MLU_REMOTE_PORT=50051 ./build/mlu-remote-operator-test
MLU_REMOTE_HOST=127.0.0.1 MLU_REMOTE_PORT=50051 ./build/mlu-remote-custom-kernel-test
MLU_REMOTE_HOST=127.0.0.1 MLU_REMOTE_PORT=50051 ./build/mlu-remote-protocol-error-test
MLU_REMOTE_HOST=127.0.0.1 MLU_REMOTE_PORT=50051 ./build/mlu-remote-session-isolation-test
MLU_REMOTE_HOST=127.0.0.1 MLU_REMOTE_PORT=50051 ./build/mlu-remote-device-select-test
MLU_REMOTE_HOST=127.0.0.1 MLU_REMOTE_PORT=50051 ./build/mlu-remote-session-reset-test
MLU_REMOTE_HOST=127.0.0.1 MLU_REMOTE_PORT=50051 ./scripts/run-abrupt-cleanup-test.sh
MLU_REMOTE_HOST=127.0.0.1 MLU_REMOTE_PORT=50051 ./build/mlu-remote-cnnl-test
```

`mlu-remote-operator-test` performs H2D, generic Operator RPC, TransferID
query/sync, D2H, and numerical checks for all six presets. The parameter
layouts are:

| Operator | Inputs/outputs | Shape and stride | Scalars |
|---|---|---|---|
| `vector_add` | 2 / 1 | F32 rank-1, contiguous stride 1 | none |
| `scalar_add` | 1 / 1 | F32 rank-1, contiguous stride 1 | one network-order F32 |
| `scalar_mul` | 1 / 1 | F32 rank-1, contiguous stride 1 | one network-order F32 |
| `relu` | 1 / 1 | F32 rank-1, contiguous stride 1 | none |
| `tensor_add_strided` | 2 / 1 | F32 rank-2, independent row/column strides | none |
| `tensor_mul_strided` | 2 / 1 | F32 rank-2, independent row/column strides | none |

The custom-kernel test retains the compatibility path for loading the
`vector_add` preset by name. The protocol error test sends an oversized frame
and a truncated payload. The device selection test exercises available
ordinals and rejects an out-of-range ordinal. The abrupt test exits with live
IDs; the server must log `cleaning native resources` and
`SESSION_CLEANUP_EMPTY`.

Object IDs include a session generation and the registry stores owner and slot
generation metadata. The isolation test opens two sockets and confirms a queue
ID from the first socket is rejected by the second.

The server must run from the project root or be given an absolute
`MLU_REMOTE_MODULE_DIR`; `scripts/run-server.sh` sets that directory to the
project `modules` directory automatically.

To select a device at connection time, set `MLU_REMOTE_DEVICE` or call
`mlu_remote_connect_device`. `SESSION_RESET` synchronizes and destroys the
current session resources, recreates the context on the same device, and makes
all previous IDs invalid.

Preset loading requires a generated `.manifest` beside the `.fatbin`. The
server checks module name, kernel symbol, `mtp_372` architecture, datatype,
rank, input/output counts, scalar size, and `mlu_remote_operator_v1` ABI before
calling `cnModuleLoad`. Set `MLU_REMOTE_MLU_ARCH` on the server to enforce a
different target architecture.
