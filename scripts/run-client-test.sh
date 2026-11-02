#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
export MLU_REMOTE_HOST=${MLU_REMOTE_HOST:-127.0.0.1}
export MLU_REMOTE_PORT=${MLU_REMOTE_PORT:-50051}
exec "$ROOT/build/mlu-remote-client-test"
