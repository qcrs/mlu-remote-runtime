#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SDK_ROOT=$(readlink -f "${NEUWARE_HOME:-/usr/local/neuware}")
CLIENT_PROGRAM=$(realpath "${1:-$ROOT/build/native_bang_abi}")
if [[ $# -gt 0 ]]; then shift; fi
test -d "$SDK_ROOT"
test -x "$CLIENT_PROGRAM"
case ${MLU_REMOTE_SHIM:-cnrt} in
  cnrt) SHIM_DIR="$ROOT/build/shim" ;;
  driver) SHIM_DIR="$ROOT/build/driver-shim" ;;
  *) echo "MLU_REMOTE_SHIM must be cnrt or driver" >&2; exit 2 ;;
esac
export LD_LIBRARY_PATH="$SHIM_DIR:$ROOT/build"
export MLU_REMOTE_HOST=${MLU_REMOTE_HOST:-127.0.0.1}
export MLU_REMOTE_PORT=${MLU_REMOTE_PORT:-50051}
export MLU_TEST_SHIM=1
# A private user/mount namespace hides vendor userspace and every device node.
# Keep the network namespace so the independent host server remains reachable.
exec unshare -Ur -m --propagation private bash -eu -c '
  sdk_root=$1; client_program=$2; shift 2
  echo "ISOLATION user/mount namespace; host network retained"
  for kind in user mnt net; do printf "%s " "$kind"; readlink "/proc/self/ns/$kind"; done
  mount -t tmpfs tmpfs "$sdk_root"
  mount -t tmpfs tmpfs /dev
  test ! -e "$sdk_root/lib64/libcnrt.so"
  test ! -e "$sdk_root/lib64/libcndrv.so"
  test ! -e "$sdk_root/bin/cncc"
  test ! -e /dev/cambricon_dev0
  echo "VERIFIED vendor SDK path and /dev hidden"
  printf "ENDPOINT %s:%s\n" "$MLU_REMOTE_HOST" "$MLU_REMOTE_PORT"
  printf "CLIENT %s\n" "$client_program"
  exec "$client_program" "$@"
' bash "$SDK_ROOT" "$CLIENT_PROGRAM" "$@"
