#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
NEUWARE_HOME=${NEUWARE_HOME:-/usr/local/neuware-3.5.2}
CC=${CC:-gcc}
CFLAGS="-std=c11 -D_POSIX_C_SOURCE=200112L -O2 -fPIC -Wall -Wextra -Wno-unused-parameter -Werror=incompatible-pointer-types -I$ROOT/include -I$ROOT/build/generated -I$NEUWARE_HOME/include"
LDFLAGS="-L$NEUWARE_HOME/lib64 -Wl,-rpath,$NEUWARE_HOME/lib64"
mkdir -p "$ROOT/build" "$ROOT/build/generated" "$ROOT/build/probes" "$ROOT/modules"
python3 "$ROOT/scripts/generate_operator_registry.py"

"$ROOT/scripts/build-client.sh"

"$CC" $CFLAGS "$ROOT/server/mlu_remote_server.c" "$ROOT/server/mlu_backend.c" "$ROOT/server/callback_bridge.c" "$ROOT/server/resource_limits.c" "$ROOT/server/object_registry.c" "$ROOT/server/server_session.c" "$ROOT/common/bang_image.c" -o "$ROOT/build/mlu-remote-server" $LDFLAGS -lcndrv -lcnrt -lpthread -ldl
CLIENT_LIBS="-L$ROOT/build -Wl,-rpath,$ROOT/build -lremote_cnrt -lremote_cnnl"
"$CC" $CFLAGS "$ROOT/tests/client_basic.c" -o "$ROOT/build/mlu-remote-client-test" $CLIENT_LIBS $LDFLAGS -lpthread
"$CC" $CFLAGS "$ROOT/tests/client_cnnl.c" -o "$ROOT/build/mlu-remote-cnnl-test" $CLIENT_LIBS $LDFLAGS -lpthread
"$CC" $CFLAGS "$ROOT/tests/client_custom_kernel.c" -o "$ROOT/build/mlu-remote-custom-kernel-test" $CLIENT_LIBS $LDFLAGS -lpthread
"$CC" $CFLAGS "$ROOT/tests/client_operator.c" -o "$ROOT/build/mlu-remote-operator-test" $CLIENT_LIBS $LDFLAGS -lpthread
"$CC" $CFLAGS "$ROOT/tests/client_abrupt.c" -o "$ROOT/build/mlu-remote-abrupt-client" $CLIENT_LIBS $LDFLAGS -lpthread
"$CC" $CFLAGS "$ROOT/tests/protocol_errors.c" -o "$ROOT/build/mlu-remote-protocol-error-test" $LDFLAGS
"$CC" $CFLAGS -I"$ROOT/server" "$ROOT/tests/object_identity.c" "$ROOT/server/object_registry.c" -Wl,--wrap=getrandom -lpthread -o "$ROOT/build/object-identity-test"
"$CC" $CFLAGS -I"$ROOT/server" "$ROOT/tests/backend_resources.c" "$ROOT/server/mlu_backend.c" "$ROOT/server/callback_bridge.c" "$ROOT/server/resource_limits.c" "$ROOT/server/object_registry.c" "$ROOT/common/bang_image.c" -Wl,--wrap=cnMalloc,--wrap=cnModuleLoadFatBinary,--wrap=cnMemcpyHtoDAsync,--wrap=cnQueueSync,--wrap=cnCtxDestroy,--wrap=cnDestroyQueue,--wrap=cnMallocHost,--wrap=cnFreeHost,--wrap=cnMemcpyDtoHAsync,--wrap=cnCreateNotifier,--wrap=cnTaskTopoClone,--wrap=cnTaskTopoInstantiate,--wrap=cnTaskTopoEntityInvoke,--wrap=cnTaskTopoEntityDestroy,--wrap=cnTaskTopoDestroy,--wrap=cnTaskTopoNodeFindInClone,--wrap=cnTaskTopoDestroyNode,--wrap=cnTaskTopoAddMemcpyNode,--wrap=cnTaskTopoMemcpyNodeSetParams,--wrap=cnTaskTopoEntityMemcpyNodeSetParams,--wrap=cnTaskTopoAddKernelNode,--wrap=cnTaskTopoKernelNodeSetParams,--wrap=cnTaskTopoEntityKernelNodeSetParams,--wrap=cnModuleUnload $LDFLAGS -lcndrv -lcnrt -lpthread -ldl -o "$ROOT/build/backend-resources-test"
"$CC" $CFLAGS -shared "$ROOT/tests/fault_cleanup.c" -o "$ROOT/build/libtest-fail-cleanup.so"
"$CC" $CFLAGS "$ROOT/tests/client_isolation.c" -o "$ROOT/build/mlu-remote-session-isolation-test" $CLIENT_LIBS $LDFLAGS -lpthread
"$CC" $CFLAGS "$ROOT/tests/client_device_select.c" -o "$ROOT/build/mlu-remote-device-select-test" $CLIENT_LIBS $LDFLAGS -lpthread
"$CC" $CFLAGS "$ROOT/tests/client_reset.c" -o "$ROOT/build/mlu-remote-session-reset-test" $CLIENT_LIBS $LDFLAGS -lpthread
"$CC" $CFLAGS "$ROOT/probes/probe_device.c" -o "$ROOT/build/probes/probe_device" $LDFLAGS -lcnrt
"$CC" $CFLAGS "$ROOT/probes/probe_memory.c" -o "$ROOT/build/probes/probe_memory" $LDFLAGS -lcnrt
"$CC" $CFLAGS "$ROOT/probes/probe_queue.c" -o "$ROOT/build/probes/probe_queue" $LDFLAGS -lcnrt
"$CC" $CFLAGS "$ROOT/probes/probe_cnnl.c" -o "$ROOT/build/probes/probe_cnnl" -ldl
"$CC" $CFLAGS "$ROOT/probes/probe_kernel.c" -o "$ROOT/build/probes/probe_kernel"
"$CC" $CFLAGS "$ROOT/probes/probe_semantics.c" -o "$ROOT/build/probes/probe_semantics" $LDFLAGS -lcnrt
"$CC" $CFLAGS "$ROOT/probes/probe_driver_semantics.c" -o "$ROOT/build/probes/probe_driver_semantics" $LDFLAGS -lcndrv
"$CC" $CFLAGS "$ROOT/tests/native_driver.c" -o "$ROOT/build/native_driver" $LDFLAGS -lcndrv -lpthread
"$CC" $CFLAGS "$ROOT/tests/native_api_families.c" -o "$ROOT/build/native_api_families" $LDFLAGS -lcnrt -lcndrv
"$CC" $CFLAGS "$ROOT/tests/native_memory_allocations.c" -o "$ROOT/build/native_memory_allocations" $LDFLAGS -lcndrv
"$CC" $CFLAGS "$ROOT/tests/native_cast.c" -o "$ROOT/build/native_cast" $LDFLAGS -lcnrt
"$CC" $CFLAGS -pthread "$ROOT/tests/native_queue_host.c" -o "$ROOT/build/native_queue_host" $LDFLAGS -lcnrt -lcndrv
"$CC" $CFLAGS "$ROOT/tests/native_shared.c" -o "$ROOT/build/native_shared" $LDFLAGS -lcndrv
"$CC" $CFLAGS "$ROOT/tests/native_vmm.c" -o "$ROOT/build/native_vmm" $LDFLAGS -lcndrv
"$CC" $CFLAGS "$ROOT/probes/probe_remaining.c" -o "$ROOT/build/probes/probe_remaining" $LDFLAGS -lcnrt -lcndrv
"$CC" $CFLAGS "$ROOT/tests/native_graph.c" -o "$ROOT/build/native_graph_driver" $LDFLAGS -lcndrv
"$CC" $CFLAGS -DTEST_CNRT "$ROOT/tests/native_graph.c" -o "$ROOT/build/native_graph_runtime" $LDFLAGS -lcnrt
"$CC" $CFLAGS -pthread "$ROOT/tests/native_graph_callbacks.c" -o "$ROOT/build/native_graph_callbacks_driver" $LDFLAGS -lcndrv
"$CC" $CFLAGS -pthread -DTEST_CNRT "$ROOT/tests/native_graph_callbacks.c" -o "$ROOT/build/native_graph_callbacks_runtime" $LDFLAGS -lcnrt
"$CC" $CFLAGS "$ROOT/tests/native_graph_host.c" -o "$ROOT/build/native_graph_host_driver" $LDFLAGS -lcndrv
"$CC" $CFLAGS -DTEST_CNRT "$ROOT/tests/native_graph_host.c" -o "$ROOT/build/native_graph_host_runtime" $LDFLAGS -lcnrt
"$CC" $CFLAGS "$ROOT/tests/native_graph_execution.c" -o "$ROOT/build/native_graph_execution_driver" $LDFLAGS -lcndrv
"$CC" $CFLAGS -DTEST_CNRT "$ROOT/tests/native_graph_execution.c" -o "$ROOT/build/native_graph_execution_runtime" $LDFLAGS -lcnrt
"$CC" $CFLAGS "$ROOT/tests/native_query_boundaries.c" -o "$ROOT/build/native_query_boundaries" $LDFLAGS -lcnrt -lcndrv
"$CC" $CFLAGS "$ROOT/tests/native_error_catalog.c" -o "$ROOT/build/native_error_catalog" $LDFLAGS -lcnrt -lcndrv
"$CC" $CFLAGS "$ROOT/tests/native_inferred_copy.c" -o "$ROOT/build/native_inferred_copy" $LDFLAGS -lcnrt -lcndrv
"$CC" $CFLAGS "$ROOT/tests/native_module_accounting.c" -o "$ROOT/build/native_module_accounting" $LDFLAGS -lcnrt -lcndrv -ldl
"$CC" $CFLAGS "$ROOT/probes/probe_query_semantics.c" -o "$ROOT/build/probes/probe_query_semantics" $LDFLAGS -lcnrt -lcndrv
"$CC" -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -I"$ROOT/include" "$ROOT/common/bang_image.c" "$ROOT/tests/bang_image_bounds.c" -ldl -o "$ROOT/build/bang-image-bounds"

if command -v cncc >/dev/null 2>&1 || [ -x "$NEUWARE_HOME/bin/cncc" ] || [ -x /usr/local/neuware/bin/cncc ]; then
  CNCC=$(command -v cncc || true); [ -n "$CNCC" ] || CNCC=/usr/local/neuware/bin/cncc
  # MLU370-S4 is mtp_372 in this Neuware release. Each registry entry has an
  # independently loadable preset module so stale or unsupported symbols are
  # isolated to that operator.
  for OP in vector_add scalar_add relu tensor_add_strided scalar_mul tensor_mul_strided; do
    "$CNCC" --bang-fatbin-only --bang-mlu-arch=mtp_372 "$ROOT/kernels/$OP/$OP.mlu" -o "$ROOT/modules/$OP.fatbin"
  done
  mkdir -p "$ROOT/build/symbol-audit"
  (cd "$ROOT/build/symbol-audit" && "$CNCC" --bang-mlu-arch=mtp_372 -save-temps "$ROOT/tests/native_symbols.mlu" -lcndrv -o "$ROOT/build/native_symbols")
  for TEST in native_bang_remote native_bang_generic; do
    "$CNCC" --bang-mlu-arch=mtp_372 "$ROOT/tests/$TEST.mlu" -o "$ROOT/build/$TEST"
  done
  "$CNCC" --bang-mlu-arch=mtp_372 "$ROOT/tests/native_queue_retirement.mlu" -lcndrv -o "$ROOT/build/native_queue_retirement"
  "$CNCC" --bang-mlu-arch=mtp_372 "$ROOT/tests/native_host_async.mlu" -lcndrv -o "$ROOT/build/native_host_async"
  # Keep the actual embedded ELF container/host stub for ABI and parser probes.
  mkdir -p "$ROOT/build/audit"
  (cd "$ROOT/build/audit" && "$CNCC" --bang-mlu-arch=mtp_372 -save-temps "$ROOT/tests/native_bang_abi.mlu" -o "$ROOT/build/native_bang_abi")
  mkdir -p "$ROOT/build/module-audit"
  (cd "$ROOT/build/module-audit" && "$CNCC" --bang-mlu-arch=mtp_372 -save-temps "$ROOT/tests/native_bang_module.mlu" -o "$ROOT/build/native_bang_module")
  mkdir -p "$ROOT/build/drain-audit"
  (cd "$ROOT/build/drain-audit" && "$CNCC" --bang-mlu-arch=mtp_372 -save-temps "$ROOT/tests/native_bang_drain.mlu" -o "$ROOT/build/native_bang_drain")
  "$CNCC" --bang-mlu-arch=mtp_372 -shared -fPIC -DMLU_MODULE_LIBRARY "$ROOT/tests/native_bang_module.mlu" -o "$ROOT/build/libnative_bang_module.so"
  "$CC" $CFLAGS "$ROOT/tests/native_bang_module_dso.c" -o "$ROOT/build/native_bang_module_dso" $LDFLAGS -lcnrt -ldl
  mkdir -p "$ROOT/build/metadata-audit"
  (cd "$ROOT/build/metadata-audit" && "$CNCC" --bang-mlu-arch=mtp_372 -save-temps "$ROOT/tests/native_bang_metadata.mlu" -o "$ROOT/build/native_bang_metadata")
  "$CNCC" --bang-device-only --bang-mlu-arch=mtp_372 -S -emit-llvm "$ROOT/tests/native_bang_metadata.mlu" -o "$ROOT/build/metadata-audit/types.ll"
  python3 "$ROOT/scripts/generate_bang_metadata.py" --host "$ROOT/build/native_bang_metadata" \
    --pair "$ROOT/build/metadata-audit/types.ll:$ROOT/build/metadata-audit/native_bang_metadata-bang-mlisa-cambricon-bang.cnfatbin" \
    --manifest "$ROOT/build/metadata-audit/manifest.json"

  for VARIANT in 1 2; do
    mkdir -p "$ROOT/build/globals-$VARIANT"
    (cd "$ROOT/build/globals-$VARIANT" && "$CNCC" --bang-mlu-arch=mtp_372 -save-temps -shared -fPIC \
      -DMODULE_VARIANT="$VARIANT" "$ROOT/tests/native_module_globals.mlu" -o "$ROOT/build/libnative_module_globals_$VARIANT.so")
  done
  mkdir -p "$ROOT/build/relocation-audit"
  (cd "$ROOT/build/relocation-audit" && "$CNCC" --bang-mlu-arch=mtp_372 -save-temps \
    "$ROOT/tests/native_bang_relocations.mlu" -lcndrv -o "$ROOT/build/native_bang_relocations")
  "$CNCC" --bang-device-only --bang-mlu-arch=mtp_372 -S -emit-llvm \
    "$ROOT/tests/native_bang_relocations.mlu" -o "$ROOT/build/relocation-audit/types.ll"
  python3 "$ROOT/scripts/generate_bang_metadata.py" --host "$ROOT/build/native_bang_relocations" \
    --pair "$ROOT/build/relocation-audit/types.ll:$ROOT/build/relocation-audit/native_bang_relocations-bang-mlisa-cambricon-bang.cnfatbin" \
    --manifest "$ROOT/build/relocation-audit/manifest.json" --attach-images


  for GRAPH_VARIANT in runtime driver; do
    GRAPH_FLAGS=()
    if [ "$GRAPH_VARIANT" = runtime ]; then GRAPH_FLAGS=(-DTEST_CNRT); fi
    mkdir -p "$ROOT/build/graph-kernel-$GRAPH_VARIANT"
    (cd "$ROOT/build/graph-kernel-$GRAPH_VARIANT" && "$CNCC" --bang-mlu-arch=mtp_372 -save-temps "${GRAPH_FLAGS[@]}" \
      "$ROOT/tests/native_graph_kernel.mlu" -lcndrv -o "$ROOT/build/native_graph_kernel_$GRAPH_VARIANT")
    "$CNCC" --bang-device-only --bang-mlu-arch=mtp_372 -S -emit-llvm \
      "$ROOT/tests/native_graph_kernel.mlu" -o "$ROOT/build/graph-kernel-$GRAPH_VARIANT/types.ll"
    python3 "$ROOT/scripts/generate_bang_metadata.py" --host "$ROOT/build/native_graph_kernel_$GRAPH_VARIANT" \
      --pair "$ROOT/build/graph-kernel-$GRAPH_VARIANT/types.ll:$ROOT/build/graph-kernel-$GRAPH_VARIANT/native_graph_kernel-bang-mlisa-cambricon-bang.cnfatbin" \
      --manifest "$ROOT/build/graph-kernel-$GRAPH_VARIANT/manifest.json" --attach-images
  done

  bash "$ROOT/scripts/build-graph-capture.sh"

else
  echo "WARN: cncc not found; preset kernel remains PARTIAL"
fi
echo "built in $ROOT/build"
