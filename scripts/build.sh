#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
NEUWARE_HOME=${NEUWARE_HOME:-/usr/local/neuware-3.5.2}
CC=${CC:-gcc}
CFLAGS="-std=c11 -D_POSIX_C_SOURCE=200112L -O2 -fPIC -Wall -Wextra -Wno-unused-parameter -I$ROOT/include -I$ROOT/build/generated -I$NEUWARE_HOME/include"
LDFLAGS="-L$NEUWARE_HOME/lib64 -Wl,-rpath,$NEUWARE_HOME/lib64"
mkdir -p "$ROOT/build" "$ROOT/build/generated" "$ROOT/build/probes" "$ROOT/modules"
python3 "$ROOT/scripts/generate_operator_registry.py"

"$CC" $CFLAGS -c "$ROOT/client/runtime_context.c" -o "$ROOT/build/runtime_context.o"
"$CC" $CFLAGS -c "$ROOT/client/remote_cnrt.c" -o "$ROOT/build/remote_cnrt.o"
"$CC" $CFLAGS -c "$ROOT/client/transfer.c" -o "$ROOT/build/transfer.o"
"$CC" $CFLAGS -c "$ROOT/client/remote_operator.c" -o "$ROOT/build/remote_operator.o"
"$CC" $CFLAGS -c "$ROOT/client/remote_cnnl.c" -o "$ROOT/build/remote_cnnl.o"
ar rcs "$ROOT/build/libmlu_remote_client.a" "$ROOT/build/runtime_context.o" "$ROOT/build/remote_cnrt.o" "$ROOT/build/transfer.o" "$ROOT/build/remote_operator.o" "$ROOT/build/remote_cnnl.o"
"$CC" -shared "$ROOT/build/runtime_context.o" "$ROOT/build/remote_cnrt.o" "$ROOT/build/transfer.o" "$ROOT/build/remote_operator.o" -o "$ROOT/build/libremote_cnrt.so" -lpthread
"$CC" -shared "$ROOT/build/remote_cnnl.o" -L"$ROOT/build" -lremote_cnrt -Wl,-rpath,"$ROOT/build" -o "$ROOT/build/libremote_cnnl.so"

"$CC" $CFLAGS "$ROOT/server/mlu_remote_server.c" "$ROOT/server/mlu_backend.c" "$ROOT/server/object_registry.c" "$ROOT/server/server_session.c" -o "$ROOT/build/mlu-remote-server" $LDFLAGS -lcndrv -lpthread
CLIENT_LIBS="-L$ROOT/build -Wl,-rpath,$ROOT/build -lremote_cnrt -lremote_cnnl"
"$CC" $CFLAGS "$ROOT/tests/client_basic.c" -o "$ROOT/build/mlu-remote-client-test" $CLIENT_LIBS $LDFLAGS -lpthread
"$CC" $CFLAGS "$ROOT/tests/client_cnnl.c" -o "$ROOT/build/mlu-remote-cnnl-test" $CLIENT_LIBS $LDFLAGS -lpthread
"$CC" $CFLAGS "$ROOT/tests/client_custom_kernel.c" -o "$ROOT/build/mlu-remote-custom-kernel-test" $CLIENT_LIBS $LDFLAGS -lpthread
"$CC" $CFLAGS "$ROOT/tests/client_operator.c" -o "$ROOT/build/mlu-remote-operator-test" $CLIENT_LIBS $LDFLAGS -lpthread
"$CC" $CFLAGS "$ROOT/tests/client_abrupt.c" -o "$ROOT/build/mlu-remote-abrupt-client" $CLIENT_LIBS $LDFLAGS -lpthread
"$CC" $CFLAGS "$ROOT/tests/protocol_errors.c" -o "$ROOT/build/mlu-remote-protocol-error-test" $LDFLAGS
"$CC" $CFLAGS "$ROOT/tests/client_isolation.c" -o "$ROOT/build/mlu-remote-session-isolation-test" $CLIENT_LIBS $LDFLAGS -lpthread
"$CC" $CFLAGS "$ROOT/tests/client_device_select.c" -o "$ROOT/build/mlu-remote-device-select-test" $CLIENT_LIBS $LDFLAGS -lpthread
"$CC" $CFLAGS "$ROOT/tests/client_reset.c" -o "$ROOT/build/mlu-remote-session-reset-test" $CLIENT_LIBS $LDFLAGS -lpthread
"$CC" $CFLAGS "$ROOT/probes/probe_device.c" -o "$ROOT/build/probes/probe_device" $LDFLAGS -lcnrt
"$CC" $CFLAGS "$ROOT/probes/probe_memory.c" -o "$ROOT/build/probes/probe_memory" $LDFLAGS -lcnrt
"$CC" $CFLAGS "$ROOT/probes/probe_queue.c" -o "$ROOT/build/probes/probe_queue" $LDFLAGS -lcnrt
"$CC" $CFLAGS "$ROOT/probes/probe_cnnl.c" -o "$ROOT/build/probes/probe_cnnl" -ldl
"$CC" $CFLAGS "$ROOT/probes/probe_kernel.c" -o "$ROOT/build/probes/probe_kernel"

if command -v cncc >/dev/null 2>&1 || [ -x "$NEUWARE_HOME/bin/cncc" ] || [ -x /usr/local/neuware/bin/cncc ]; then
  CNCC=$(command -v cncc || true); [ -n "$CNCC" ] || CNCC=/usr/local/neuware/bin/cncc
  # MLU370-S4 is mtp_372 in this Neuware release. Each registry entry has an
  # independently loadable preset module so stale or unsupported symbols are
  # isolated to that operator.
  for OP in vector_add scalar_add relu tensor_add_strided scalar_mul tensor_mul_strided; do
    "$CNCC" --bang-fatbin-only --bang-mlu-arch=mtp_372 "$ROOT/kernels/$OP/$OP.mlu" -o "$ROOT/modules/$OP.fatbin" || echo "WARN: $OP.fatbin was not produced"
  done
else
  echo "WARN: cncc not found; preset kernel remains PARTIAL"
fi
echo "built in $ROOT/build"
