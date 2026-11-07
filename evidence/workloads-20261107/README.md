# Application workload regression (2026-11-07)

`verify-workloads.py` compiled each workload once with CNCC and ran the same
executable in Native, replacement, and `LD_PRELOAD` modes. The automatic mode
started a local TCP server; the separate-window check used:

```sh
# terminal A
MLU_REMOTE_BIND=127.0.0.1 MLU_REMOTE_PORT=50252 ./scripts/run-server.sh

# terminal B
python3 scripts/verify-workloads.py --external-server --skip-build \
  --host 127.0.0.1 --port 50252 \
  --out evidence/workloads-20261107/external-client
```

All 15 workload executions and the existing generic core smoke returned zero.
The four application workloads cover matrix add, naive GEMM, threshold, and a
3×3 blur; `queue_notifier` additionally exercises `cnrtMemGetInfo`, owned
device pointer attributes, asynchronous H2D, notifier query/wait, a
cross-queue dependency, and elapsed-time reporting.

| Workload | Shape | Native / replacement / preload | Max absolute error |
|---|---|---|---:|
| matrix_add | 256×256 | PASS / PASS / PASS | 0 |
| naive_gemm | 64×64×64 | PASS / PASS / PASS | 0 |
| image_threshold | 512×512 | PASS / PASS / PASS | 0 |
| image_blur | 512×512 | PASS / PASS / PASS | 0 |
| queue_notifier | 4096 FP32 | PASS / PASS / PASS | 0 |

The outputs in the sibling `*.log` files include CPU and MLU checksums,
mismatch counts, and the SHA256 records in `results.json` prove that the same
CNCC executable was used for all three modes. No runtime source was changed by
this workload batch.
