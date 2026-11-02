#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
export MLU_REMOTE_BIND=${MLU_REMOTE_BIND:-127.0.0.1}
export MLU_REMOTE_PORT=${MLU_REMOTE_PORT:-50051}
export MLU_REMOTE_MODULE_DIR=${MLU_REMOTE_MODULE_DIR:-$ROOT/modules}
exec "$ROOT/build/mlu-remote-server"
