#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SOURCE=$(realpath "${1:-$ROOT/evidence/workloads-20261108-next/cndrv_gemm.cnfatbin}")
[[ -f $SOURCE ]] || { echo "test input not found: $SOURCE" >&2; exit 2; }
WORK=$(mktemp -d /tmp/mlu-isolation-test.XXXXXX)
trap 'rm -rf "$WORK"' EXIT
FIXTURE=$WORK/fixture
PORT=$((40000 + $$ % 20000))
gcc -std=c11 -O2 -Wall -Wextra -Werror "$ROOT/tests/isolation_tcp_fixture.c" -o "$FIXTURE"
MLU_REMOTE_ISOLATED_TIMEOUT=15 bash "$ROOT/scripts/run-isolated-pair.sh" --port "$PORT" \
  --server "$FIXTURE" listen "$PORT" "$WORK/received-image" \
  --client "$FIXTURE" -- send 198.18.0.1 "$PORT" "$SOURCE"
cmp "$SOURCE" "$WORK/received-image"
source_hash=$(sha256sum "$SOURCE" | cut -d ' ' -f 1)
received_hash=$(sha256sum "$WORK/received-image" | cut -d ' ' -f 1)
[[ $source_hash == "$received_hash" ]]
printf 'VETH_TCP_PASS bytes=%s sha256=%s\n' "$(wc -c < "$SOURCE" | tr -d ' ')" "$source_hash"
