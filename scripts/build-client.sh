#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CC=${CC:-gcc}
mkdir -p "$ROOT/build/generated" "$ROOT/build/shim"
python3 "$ROOT/scripts/generate_rpc.py"
FLAGS=(-std=c11 -D_POSIX_C_SOURCE=200112L -O2 -fPIC -Wall -Wextra -Wno-unused-parameter -Wno-misleading-indentation -Werror=incompatible-pointer-types -I"$ROOT/include" -I"$ROOT/build/generated")
OBJECTS=()
for SOURCE in runtime_context remote_cnrt transfer host_memory remote_operator remote_cnnl; do
  "$CC" "${FLAGS[@]}" -c "$ROOT/client/$SOURCE.c" -o "$ROOT/build/$SOURCE.o"
  OBJECTS+=("$ROOT/build/$SOURCE.o")
done
ar rcs "$ROOT/build/libmlu_remote_client.a" "${OBJECTS[@]}"
"$CC" -shared "${OBJECTS[@]}" -Wl,-z,defs -o "$ROOT/build/libremote_cnrt.so" -lpthread
"$CC" -shared "$ROOT/build/remote_cnnl.o" -L"$ROOT/build" -lremote_cnrt '-Wl,-rpath,$ORIGIN' -o "$ROOT/build/libremote_cnnl.so"
if [[ ${CLIENT_CORE_ONLY:-0} != 1 ]]; then
  # SDK headers are needed on the builder, no vendor runtime on the client.
  NEUWARE_HOME=${NEUWARE_HOME:-/usr/local/neuware}
  # CNRT host casts retain native per-thread rounding-environment effects.
  "$CC" "${FLAGS[@]}" -frounding-math -I"$NEUWARE_HOME/include" -c "$ROOT/common/host_cast.c" -o "$ROOT/build/host_cast.o"
  "$CC" "${FLAGS[@]}" -I"$NEUWARE_HOME/include" -shared "$ROOT/shim/cnrt_shim.c" "$ROOT/build/host_cast.o" "$ROOT/common/bang_image.c" "$ROOT/common/bang_types.c" "$ROOT/common/bang_metadata.c" "$ROOT/common/bang_args.c" \
    -L"$ROOT/build" -lremote_cnrt '-Wl,-rpath,$ORIGIN/..' -Wl,-soname,libcnrt.so -Wl,-z,defs \
    -Wl,--version-script,"$ROOT/build/generated/cnrt_exports.map" -o "$ROOT/build/shim/libcnrt.so" -lpthread -ldl -lm
  mkdir -p "$ROOT/build/driver-shim"
  "$CC" "${FLAGS[@]}" -I"$NEUWARE_HOME/include" -shared "$ROOT/shim/cndrv_shim.c" "$ROOT/common/bang_image.c" "$ROOT/common/bang_types.c" "$ROOT/common/bang_metadata.c" "$ROOT/common/bang_args.c" \
    -L"$ROOT/build" -lremote_cnrt '-Wl,-rpath,$ORIGIN/..' -Wl,-soname,libcndrv.so -Wl,-z,defs \
    -Wl,--version-script,"$ROOT/build/generated/cndrv_exports.map" -o "$ROOT/build/driver-shim/libcndrv.so" -lpthread -ldl
  ln -sfn shim/libcnrt.so "$ROOT/build/libmlu_remote_cnrt_shim.so"
fi
