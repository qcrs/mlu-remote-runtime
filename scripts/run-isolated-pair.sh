#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
PORT=${MLU_REMOTE_PORT:-50081}
SHIM=${MLU_REMOTE_SHIM:-both}
MODULE_DIR=${MLU_REMOTE_MODULE_DIR:-$ROOT/modules}
SERVER_LD_LIBRARY_PATH=${MLU_REMOTE_SERVER_LD_LIBRARY_PATH:-${NEUWARE_HOME:-/usr/local/neuware}/lib64}
SERVER=
CLIENT=
SERVER_ARGS=()
CLIENT_ARGS=()

while (($#)); do
  case $1 in
    --port) PORT=$2; shift 2 ;;
    --shim) SHIM=$2; shift 2 ;;
    --module-dir) MODULE_DIR=$2; shift 2 ;;
    --server)
      SERVER=$2; shift 2
      while (($#)) && [[ $1 != --client ]]; do SERVER_ARGS+=("$1"); shift; done
      ;;
    --client)
      CLIENT=$2; shift 2
      [[ ${1:-} == -- ]] && shift
      CLIENT_ARGS=("$@")
      break
      ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done

[[ -x $SERVER && -x $CLIENT ]] || { echo "server and client executables are required" >&2; exit 2; }
[[ $PORT =~ ^[0-9]+$ ]] && ((PORT > 0 && PORT < 65536)) || { echo "invalid port" >&2; exit 2; }
case $SHIM in
  cnrt) SHIM_DIR="$ROOT/build/shim" ;;
  driver) SHIM_DIR="$ROOT/build/driver-shim" ;;
  both) SHIM_DIR="$ROOT/build/driver-shim:$ROOT/build/shim" ;;
  *) echo "MLU_REMOTE_SHIM must be cnrt, driver or both" >&2; exit 2 ;;
esac
[[ -d $MODULE_DIR ]] || { echo "module directory not found: $MODULE_DIR" >&2; exit 2; }

WORK=$(mktemp -d /tmp/mlu-remote-client.XXXXXX)
ROOTFS=$WORK/rootfs
mkdir -p "$ROOTFS/app/inputs" "$ROOTFS/opt/lib" "$ROOTFS/dev" "$ROOTFS/tmp" "$ROOTFS/lib"
chmod 1777 "$ROOTFS/tmp"
cleanup() { rm -rf "$WORK"; }
trap cleanup EXIT

install -D "$CLIENT" "$ROOTFS/app/client"
copy_client_file() {
  local src=$1 index=$2
  local dest="$ROOTFS/app/inputs/input-$index"
  install -D "$src" "$dest"
  printf '%s' "/app/inputs/input-$index"
}

resolved_ld_path=$(LD_LIBRARY_PATH="$SHIM_DIR:$ROOT/build" ldd "$CLIENT")
while IFS= read -r dependency; do
  [[ -f $dependency ]] || continue
  install -D -m 755 "$dependency" "$ROOTFS/opt/lib/$(basename "$dependency")"
done < <(printf '%s\n' "$resolved_ld_path" | sed -nE 's/.*=> (\/[^ ]+) \(.*/\1/p; s/^[[:space:]]*(\/[^ ]+) \(.*/\1/p')
# The image validator decompresses CNFB payloads through dlopen rather than a
# DT_NEEDED edge.  Stage that system library explicitly so image/module loading
# remains self-contained inside the client chroot.
lz4_library=$(ldconfig -p 2>/dev/null | sed -n 's/^[[:space:]]*liblz4\.so\.1 .*=> \(\/[^ ]*\)$/\1/p' | head -n 1)
if [[ -n $lz4_library && -f $lz4_library ]]; then
  install -D -m 755 "$lz4_library" "$ROOTFS/opt/lib/$(basename "$lz4_library")"
fi
interpreter=$(readelf -l "$CLIENT" | sed -n 's/.*Requesting program interpreter: \(.*\)]/\1/p')
[[ -n $interpreter && -f $interpreter ]] || { echo "client ELF interpreter not found" >&2; exit 2; }
install -D -m 755 "$interpreter" "$ROOTFS$interpreter"
for shim in "$ROOT/build/libremote_cnrt.so" "$ROOT/build/shim/libcnrt.so" "$ROOT/build/driver-shim/libcndrv.so"; do
  [[ -f $shim ]] && install -D -m 755 "$shim" "$ROOTFS/opt/lib/$(basename "$shim")"
done

MAPPED_CLIENT_ARGS=()
input_index=0
for value in "${CLIENT_ARGS[@]}"; do
  if [[ $value == /* && -f $value ]]; then
    MAPPED_CLIENT_ARGS+=("$(copy_client_file "$value" "$input_index")")
    ((input_index += 1))
  else
    MAPPED_CLIENT_ARGS+=("$value")
  fi
done

echo "CLIENT_ROOTFS staged at $ROOTFS (SDK, repository, /dev and host /tmp are absent)"
unshare -Urnm --propagation private --fork bash -euo pipefail -c '
  rootfs=$1; port=$2; server=$3; module_dir=$4; shim=$5; server_ld_path=$6; server_log=$7; shift 7
  printf "PAIR_ARGS rootfs=%s port=%s server=%s shim=%s\n" "$rootfs" "$port" "$server" "$shim"
  server_args=()
  while (($#)) && [[ $1 != --client ]]; do server_args+=("$1"); shift; done
  [[ ${1:-} == --client ]] || { echo "missing client marker" >&2; exit 2; }
  shift
  client=$1; shift
  client_args=("$@")
  server_ip=198.18.0.1
  client_ip=198.18.0.2
  base_ns=$(readlink /proc/self/ns/net)
  # Namespace holders must not inherit the harness stdout/stderr pipes.  If a
  # caller times out or kills this shell before its EXIT trap runs, inherited
  # pipe writers would otherwise keep subprocess capture open indefinitely.
  unshare -n sleep 300 >/dev/null 2>&1 & server_ns_pid=$!
  unshare -n sleep 300 >/dev/null 2>&1 & client_ns_pid=$!
  server_pid=
  cleanup() {
    local status=$?
    trap - EXIT
    [[ -z ${server_pid:-} ]] || kill "$server_pid" 2>/dev/null || true
    kill "$server_ns_pid" "$client_ns_pid" 2>/dev/null || true
    exit "$status"
  }
  trap cleanup EXIT
  for attempt in $(seq 1 100); do
    server_ns=$(readlink "/proc/$server_ns_pid/ns/net" 2>/dev/null || true)
    client_ns=$(readlink "/proc/$client_ns_pid/ns/net" 2>/dev/null || true)
    [[ -n $server_ns && -n $client_ns && $server_ns != "$base_ns" && $client_ns != "$base_ns" && $server_ns != "$client_ns" ]] && break
    sleep .02
  done
  [[ -n ${server_ns:-} && -n ${client_ns:-} && $server_ns != "$client_ns" ]] || { echo "network namespaces did not become ready" >&2; exit 1; }
  ip link add mlu-srv type veth peer name mlu-cli
  ip link set mlu-srv netns "$server_ns_pid"
  ip link set mlu-cli netns "$client_ns_pid"
  nsenter -t "$server_ns_pid" -n -- ip link set lo up
  nsenter -t "$server_ns_pid" -n -- ip addr add "$server_ip/30" dev mlu-srv
  nsenter -t "$server_ns_pid" -n -- ip link set mlu-srv up
  nsenter -t "$client_ns_pid" -n -- ip link set lo up
  nsenter -t "$client_ns_pid" -n -- ip addr add "$client_ip/30" dev mlu-cli
  nsenter -t "$client_ns_pid" -n -- ip link set mlu-cli up
  echo "ISOLATION server_netns=$server_ns client_netns=$client_ns veth=$server_ip/30<->$client_ip/30"

  nsenter -t "$server_ns_pid" -n -- env LD_LIBRARY_PATH="$server_ld_path" \
    MLU_REMOTE_BIND="$server_ip" MLU_REMOTE_PORT="$port" \
    MLU_REMOTE_MODULE_DIR="$module_dir" "$server" "${server_args[@]}" >"$server_log" 2>&1 &
  server_pid=$!
  server_ready=0
  for attempt in $(seq 1 200); do
    if ! kill -0 "$server_pid" 2>/dev/null; then
      cat "$server_log" >&2
      exit 1
    fi
    if nsenter -t "$client_ns_pid" -n -- timeout 1 bash -c "</dev/tcp/$server_ip/$port" 2>/dev/null; then
      server_ready=1
      break
    fi
    sleep .05
  done
  if ((server_ready == 0)); then cat "$server_log" >&2; echo "server did not accept TCP connections" >&2; exit 1; fi
  kill -0 "$server_pid" 2>/dev/null || { cat "$server_log" >&2; exit 1; }
  echo "VERIFIED client chroot has no vendor SDK, device nodes, repository tree, or shared host /tmp"
  printf "CLIENT_EXEC chroot=%s executable=/app/client arguments=%s\n" "$rootfs" "${#client_args[@]}"
  nsenter -t "$client_ns_pid" -n -- timeout "${MLU_REMOTE_ISOLATED_TIMEOUT:-180}" env \
    LD_LIBRARY_PATH=/opt/lib MLU_REMOTE_HOST="$server_ip" MLU_REMOTE_PORT="$port" \
    MLU_REMOTE_SHIM="$shim" MLU_TEST_SHIM=1 chroot "$rootfs" /app/client "${client_args[@]}"
' bash "$ROOTFS" "$PORT" "$SERVER" "$MODULE_DIR" "$SHIM" "$SERVER_LD_LIBRARY_PATH" "$WORK/server.log" \
  "${SERVER_ARGS[@]}" --client "$ROOTFS/app/client" "${MAPPED_CLIENT_ARGS[@]}"
