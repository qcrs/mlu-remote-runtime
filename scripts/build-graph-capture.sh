#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SDK=${NEUWARE_HOME:-/usr/local/neuware}
CAPTURE_CNCC=${CNCC:-$SDK/bin/cncc}
for VARIANT in runtime driver; do
  FLAGS=()
  LIBRARY=cndrv
  if [ "$VARIANT" = runtime ]; then FLAGS=(-DTEST_CNRT); LIBRARY=cnrt; fi
  mkdir -p "$ROOT/build/graph-capture-$VARIANT" "$ROOT/build/probes"
  (cd "$ROOT/build/graph-capture-$VARIANT" && "$CAPTURE_CNCC" --bang-mlu-arch=mtp_372 -save-temps "${FLAGS[@]}" \
    "$ROOT/tests/native_graph_capture.mlu" -lcndrv -o "$ROOT/build/native_graph_capture_$VARIANT")
  "$CAPTURE_CNCC" --bang-device-only --bang-mlu-arch=mtp_372 -S -emit-llvm \
    "$ROOT/tests/native_graph_capture.mlu" -o "$ROOT/build/graph-capture-$VARIANT/types.ll"
  python3 "$ROOT/scripts/generate_bang_metadata.py" --host "$ROOT/build/native_graph_capture_$VARIANT" \
    --pair "$ROOT/build/graph-capture-$VARIANT/types.ll:$ROOT/build/graph-capture-$VARIANT/native_graph_capture-bang-mlisa-cambricon-bang.cnfatbin" \
    --manifest "$ROOT/build/graph-capture-$VARIANT/manifest.json" --attach-images
  FLAGS=()
  if [ "$VARIANT" = runtime ]; then FLAGS=(-DUSE_CNRT); fi
  gcc -std=c11 -O2 -Wall -Wextra -Wno-unused-function "${FLAGS[@]}" -I"$SDK/include" \
    "$ROOT/probes/probe_graph_capture.c" -L"$SDK/lib64" -Wl,-rpath,"$SDK/lib64" \
    -l"$LIBRARY" -lpthread -o "$ROOT/build/probes/probe-capture-$VARIANT"
done
