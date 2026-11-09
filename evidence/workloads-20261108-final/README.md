# M_next workload and isolation verification

This batch ran on the host MLU370-S4 after the earlier restricted execution
environment was shown to lack the device view. The normal workloads use the
same executable for each mode and compare every result against an independent
CPU reference.

| Workload | Native | replacement | preload | realistic netns/veth/chroot | Result |
|---|---:|---:|---:|---:|---|
| `cndrv_gemm` (GEMM + row reduction) | PASS | PASS | PASS | PASS | max error `1.43051147e-6` |
| `llm_transformer` (RMSNorm, QKV, RoPE, KV cache, causal attention, Softmax, SwiGLU) | PASS | PASS | PASS | — | max error `5.96046448e-8` |
| `runtime_driver_interop` (CNRT/CNDrv resources and reset/recreate) | PASS | PASS | PASS | PASS | max error `0` |

The combined final run is [results.json](results.json) (`3 workloads, 11 runs;
core smoke PASS`). Detailed per-workload runs from the same host are also
available:

The existing API-family matrix also passed in the host-accessible environment:
[api-expansion-20261108-host](../api-expansion-20261108-host/results.json).

- [CNDrv GEMM](../workloads-20261108-cndrv/results.json)
- [LLM transformer](../workloads-20261108-llm/results.json)
- [Runtime/Driver interop](../workloads-20261108-interop/results.json)
- [realistic isolation](../workloads-20261108-realistic-final/results.json)

The experiments found and fixed three normal-use defects:

1. `cndrv_gemm` allocated CNDrv Host memory before `cnInit(0)`. CNDrv requires
   initialization first; moving initialization before the pinned allocations
   restored the native path.
2. The LLM CPU reference passed `3 * seq` rows to a helper while its input had
   only `seq` rows. The reference read past its buffer and falsely reported a
   QKV mismatch; an explicit Q/K/V projection reference now matches the kernel
   layout.
3. The isolated client chroot lacked the `liblz4.so.1` library used through
   `dlopen` by CNFB validation, and the isolated Server inherited the Client's
   shim `LD_LIBRARY_PATH`. The harness now stages the dlopen-only system
   dependency and gives the Server its native Neuware library path. Namespace
   holder output is redirected so captured subprocesses cannot remain open on
   orphaned pipe writers.

The isolated workload proves separate network namespaces, veth/TCP transport,
hidden SDK/device/filesystem view, and explicit Driver image transfer. It is a
single-host simulation; it does not claim second-host, multi-device, TLS, or
production deployment validation.
