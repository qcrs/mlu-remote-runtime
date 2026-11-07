# Application workloads: iterative neighborhood computation

Date: 2026-11-07. Baseline: `6869e58746bc1073d49cc0e85b06ebf4ccee2cc4`.
Environment: AArch64, Neuware 3.5.2, MLU370-S4 / mtp_372, one device.

The requested matrix add, naive GEMM, threshold and blur apps already existed.
This batch adds ordinary BANG C `heat_diffusion`, a repeated five-point Jacobi
computation. It submits all time steps to one queue, swaps two offset device
buffers, synchronizes once at the end and checks both buffers' end guards.
The CPU reference iterates independently in double precision. Fixed boundary
pixels retain their initial values. Even/odd iteration counts catch selecting
the wrong final buffer; the narrow case has no interior pixels.

| Workload | Cases | Native / replacement / preload | Worst absolute error |
|---|---|---|---:|
| matrix_add | 256x256, 17x31 | PASS / PASS / PASS | 0 |
| naive_gemm | 64x64x64, 17x23x19 | PASS / PASS / PASS | 9.53674316e-7 |
| image_threshold | 512x512, 19x13, 1x1 | PASS / PASS / PASS | 0 |
| image_blur | 512x512, 13x9, 1x7 | PASS / PASS / PASS | 0 |
| queue_notifier | 4096-f32-cross-queue | PASS / PASS / PASS | 0 |
| image_sobel | 512x512, 17x11, 1x7 | PASS / PASS / PASS | 0 |
| row_center | 65x127, 3x1 | PASS / PASS / PASS | 9.53674316e-7 |
| heat_diffusion | 193x129x32, 23x17x5, 1x7x1 | PASS / PASS / PASS | 5.96046448e-8 |

All 57 executions passed: eight apps, 19 cases, three modes. All mismatches
and guard mismatches were zero. Heat tolerance is 1e-5; GEMM and row_center
tolerance is 1e-4. Each app compares every element; the runner also compares
the CPU reference and Native/Remote checksums. No Runtime gap was observed.
No Runtime implementation, schema, API status or registration logic changed.
There are no preset operator calls, remote helper calls, mode branches in
application sources or workload-specific Runtime branches.

Two independent PTY sessions simulated the two terminal windows over real TCP:

```sh
# build once
./scripts/build.sh
python3 scripts/verify-workloads.py --build-only --out evidence/workloads-20261107-iterative

# terminal A
MLU_REMOTE_BIND=127.0.0.1 MLU_REMOTE_PORT=50263 ./scripts/run-server.sh

# terminal B; reuses the exact binaries, never controls terminal A's process
python3 scripts/verify-workloads.py --external-server --skip-build \
  --host 127.0.0.1 --port 50263 --out evidence/workloads-20261107-iterative
MLU_REMOTE_PORT=50263 ./scripts/run-isolated-client.sh build/workloads/heat_diffusion 23 17 5
```

This validates independent client/server processes on one physical machine.
The additional [isolated client](isolated-heat.log) hides the SDK and all device
nodes while retaining the host network; it used the same executable. The server
was stopped after validation. No second physical host or performance claim.

Validation:

- L0: [runtime build](build.log), [workload build](build-workloads.log),
  [schema/opcode/codegen](codegen.log), [ledger](ledger.log),
  [export/ABI contracts](contracts.log), Python syntax and git whitespace checks.
- L1: [verification output](verification.log), [structured metrics](results.json),
  per-case stdout/stderr and the isolated client above.
- L2: one existing [generic core smoke](core-smoke.log).
- L3 not triggered: no core Runtime components changed. No transport fault,
  resource quota, restart, DSO stress or parser fuzz suite was rerun.

[Artifact manifest](artifacts.json) records source/executable SHA256 and build
commands, including the existing [row_center aggregate metadata](row_center-abi.json).
`results.json` records the same executable hash in every mode and the Runtime
artifact hashes. [Checks](checks.log) validate the selected build receipt,
guard failure detection, ordinary CNCC imports and unchanged Runtime sources.

Pre-existing uncommitted Symbol API work was preserved in a Git stash before
this workload-only batch and is excluded from the build, evidence and commit.
