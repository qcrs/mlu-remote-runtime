# Application workload expansion — 2026-11-07

Baseline: `8211f1712cc9c35c88c0680cdf6e114cd0122ca9`. Device: MLU370-S4, AArch64, Neuware 3.5.2,
CNCC 4.5.1. Runtime, public API surface, schema and compatibility statuses were
unchanged. The earlier, unfinished Symbol API work was preserved separately in
Git stash before building this baseline; it was not used for these tests.

Added `image_sobel` and `row_center`; extended the existing matrix add, GEMM,
threshold and blur applications with runtime dimensions and boundary cases.
The existing `queue_notifier` remains in the application suite. All are ordinary
BANG C apps with CNRT calls and CPU references, with no remote helpers, preset
operator calls, runtime branches by kernel name, or native/remote source modes.

| Workload | Tested shapes | Native / replacement / preload | Worst max absolute error |
|---|---|---|---:|
| matrix_add | 256x256, 17x31 | PASS / PASS / PASS | 0 |
| naive_gemm | 64x64x64, 17x23x19 | PASS / PASS / PASS | 9.53674316e-07 |
| image_threshold | 512x512, 19x13, 1x1 | PASS / PASS / PASS | 0 |
| image_blur | 512x512, 13x9, 1x7 | PASS / PASS / PASS | 0 |
| queue_notifier | 4096-f32-cross-queue | PASS / PASS / PASS | 0 |
| image_sobel | 512x512, 17x11, 1x7 | PASS / PASS / PASS | 0 |
| row_center | 65x127, 3x1 | PASS / PASS / PASS | 9.53674316e-07 |

All 48 executions passed (7 workloads, 16 cases, 3 modes). Every app checks each
output element, finite values and its own tolerance. Matrix add, threshold,
blur, Sobel and queue pipeline had zero error. GEMM uses non-binary fractions
and a CPU double accumulation reference; GEMM/row-center tolerance is 1e-4.
The row mean intermediate error was at most 5.21540642e-8; padding and leading/
trailing guard mismatches were zero. A 1x7 blur copies the input boundary; a
1x7 Sobel writes zeros because no interior exists. Threshold input contains
126/127/128 to distinguish strict greater-than from greater-or-equal.

`row_center` runs a row reduction then affine centering with two kernels in one
module, on the default queue. It exercises an allocation offset, pitched D2D,
async memset, context sync and queue query. Its pointer-free `CenterParams`
contains a nested `Shape` and a float array; the existing compiler IR tool
attaches both 20-byte aggregate descriptions to the same executable before all
three execution modes. See [compiler ABI manifest](row_center-abi.json).
No Host Symbol, pointer graph or true async D2H capability is claimed.

Two independent terminal processes simulated client/server over TCP:

```sh
# terminal A — server; stdout/stderr captured in server.log
MLU_REMOTE_BIND=127.0.0.1 MLU_REMOTE_PORT=50261 ./scripts/run-server.sh

# terminal B — build once and compare all modes
python3 scripts/verify-workloads.py --external-server --host 127.0.0.1 --port 50261
# Reuse verified binaries after extending runner provenance (no recompilation)
python3 scripts/verify-workloads.py --external-server --skip-build --port 50261
```

This is local TCP validation, not a second physical host. The client runner did
not manage the separately launched server. Both new apps additionally passed in
the existing SDK-/device-hidden namespace client: [row center](isolated-row-center.log),
[Sobel](isolated-sobel.log). Both used the exact binaries from the three-mode suite.

Validation performed:

- L0: [runtime build](build.log), [workload build](build-workloads.log),
  [schema/opcode/codegen](codegen.log), [ledger consistency](ledger.log),
  [export/ABI/contracts](contracts.log), Python syntax and `git diff --check`.
- Runner/static checks: [static-checks.log](static-checks.log) records rejection
  of malformed/nonfinite/incorrect metrics and stale binary receipts, checks
  ordinary executable symbols and confirms no Runtime/schema/API source changes.
- L1: [verification output](verification.log) and [structured results](results.json)
  record every CPU/MLU checksum, shape, mode, return code and error. Per-case
  `*.log` files contain raw observations; `*.stderr` files were empty.
- L2: existing [generic core smoke](core-smoke.log) passed. It was repeated once
  when runner provenance was added; the final result is retained here.
- L3 was not triggered: no RuntimeContext, session, framing, ownership, fake VA,
  compiler registration, module lifetime or host-completion code was changed.
  No transport fault, quota, restart, DSO stress or parser fuzz suite was rerun.

[Artifacts](artifacts.json) record sources, build commands and final executable
SHA256. `results.json` checks and records the identical SHA256 in every mode,
and includes the baseline commit plus loaded Runtime artifact hashes. The
runner compares CPU references and bounded checksums, while each app performs
the meaningful per-element numerical check. No new Runtime gap was observed;
no Runtime implementation fix or API expansion was needed.
