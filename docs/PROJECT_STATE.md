# Project state

Updated 2026-10-31 after independent TCP client/server operator verification.

## Ground truth

- Product: MLU370-S4 (`cnmon` reports one visible device), architecture
  `mtp_372.41`.
- Driver: 5.10.35; firmware v1.1.6.
- Neuware: `/usr/local/neuware-3.5.2`.
- CNRT: 6.5.2. CNDrv: 2.5.2.
- CNCC: `/usr/local/neuware/bin/cncc`.
- CNNL: no `cnnl.h` and no `libcnnl.so`; CNNL remains
  `GROUND_TRUTH_BLOCKED`.

## Build and module artifacts

```text
./scripts/build.sh
```

The build uses:

```text
cncc --bang-fatbin-only --bang-mlu-arch=mtp_372 kernels/<operator>/<operator>.mlu -o modules/<operator>.fatbin
```

The server registry loads only `vector_add.fatbin`, `scalar_add.fatbin`,
`relu.fatbin`, `tensor_add_strided.fatbin`, `scalar_mul.fatbin`, and
`tensor_mul_strided.fatbin` from `MLU_REMOTE_MODULE_DIR` (default `modules`).
The binaries are built for `mtp_372`; another MLU architecture requires
rebuilding the presets.
The build also creates `build/generated/operator_registry.h` and sidecar
manifests. A manifest mismatch is rejected before `cnModuleLoad` (verified by
running the server with `MLU_REMOTE_MLU_ARCH=bad`).

## Verified localhost results

Server command:

```text
MLU_REMOTE_BIND=127.0.0.1 MLU_REMOTE_PORT=50072 \
MLU_REMOTE_MODULE_DIR=/home/lvtong/mlu-remote-runtime/modules \
./build/mlu-remote-server
```

Client outputs from separate processes on the same TCP listener:

```text
HELLO device_count=1 device0=MLU370-S4[mtp_372.41] memory=25769803776
PASS error code=100014 name=CN_ERROR_INVALID_HANDLE
PASS memory H2D/D2D/D2H queue=4294968297 notifier=4294968298 transfer=4294968303
CNNL elementwise_add_f32 status=-2147483643 (expected GROUND_TRUTH_BLOCKED when CNNL is absent)
PASS operator vector_add module=8589935599 kernel=8589935600 transfer=8589935601
PASS operator parameter and registry errors
PASS operator scalar_add transfer=8589935605
PASS operator scalar_mul transfer=8589935609
PASS operator relu transfer=8589935614
PASS operator tensor_add_strided rank=2 stride=6 transfer=8589935623
PASS operator tensor_mul_strided rank=2 stride=6 transfer=8589935627
PASS operator two_queue_ordering queue1=8589935593 queue2=8589935629
PASS preset BANG vector_add module=12884902889 kernel=12884902890
PASS device_selection available=1 invalid_status=100005 resource_switch_status=0
PASS session_reset old_queue=21474837481 old_allocation=21474837482 stale_queue=100014 stale_allocation=100014
MLU_REMOTE_DEVICE=1 -> connect failed (one visible device)
PASS protocol oversized_payload and truncated_payload rejected
PASS session isolation queue_a=30064772073 queue_b=34359739369 cross_status=100014
CNNL_STATUS=-2147483643
ABRUPT_LIVE queue=51539608553 allocation=51539608554
```

Server records one cleanup for every socket, including the abrupt client:

```text
[server] session fd=10 disconnected; cleaning native resources
[server] SESSION_CLEANUP_EMPTY fd=10
```

The full independent commands are listed in `docs/DEVELOPMENT.md`.

## Status

- `IMPLEMENTED`: TCP framing and request sequence, HELLO capabilities,
  session-scoped object IDs, allocation/free, checked offset copies, queue and
  notifier lifecycle, internal TransferID completion notifiers, preset module
  lifecycle, generic operator RPC, six validated BANG C operators, device
  selection before resource creation, session reset, error responses, and EOF
  cleanup, generated registry/manifest validation, and operation sequence
  metadata.
- `PARTIAL`: this host exposes one device, so multi-device placement is not
  ground-truth tested; arbitrary module upload is intentionally absent.
- `BACKEND_UNSUPPORTED`: arbitrary module upload is outside the first-stage
  trusted preset boundary.
- `GROUND_TRUTH_BLOCKED`: CNNL execution because the installed Neuware image
  has no CNNL development or runtime library.

## Known limits and next extension

The current registry supports F32 only, ranks one and two, and the six presets
above. A future extension can add more datatypes and explicit completion
policies while retaining the TransferID/notifier wire boundary. Multi-device
stress coverage is the next validation step when another device is visible.
