#!/usr/bin/env python3
"""Build once, compare identical native/replacement/preload executables over TCP."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import socket
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
NAMES = ('matrix_add', 'naive_gemm', 'image_threshold', 'image_blur')
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--out', default='evidence/workloads-20261103')
p.add_argument('--port', type=int, default=50221)
p.add_argument('--host', default='127.0.0.1')
p.add_argument('--external-server', action='store_true', help='use server started in a separate terminal')
p.add_argument('--skip-build', action='store_true')
a = p.parse_args()
out = ROOT / a.out
out.mkdir(parents=True, exist_ok=True)
build = ROOT / 'build/workloads'
build.mkdir(parents=True, exist_ok=True)
sdk = Path(os.environ.get('NEUWARE_HOME', '/usr/local/neuware'))
base = dict(os.environ, LD_LIBRARY_PATH=str(sdk / 'lib64'))
for key in ('LD_PRELOAD', 'MLU_TEST_SHIM', 'MLU_REMOTE_HOST', 'MLU_REMOTE_PORT'):
    base.pop(key, None)
remote = dict(base, MLU_REMOTE_HOST=a.host, MLU_REMOTE_PORT=str(a.port),
              LD_LIBRARY_PATH=f'{ROOT}/build/shim:{ROOT}/build')
preload = dict(base, MLU_REMOTE_HOST=a.host, MLU_REMOTE_PORT=str(a.port),
               LD_PRELOAD=str(ROOT / 'build/shim/libcnrt.so'))
results = []
server = None
log = None

def run(name, executable, env):
    r = subprocess.run([str(executable)], cwd=ROOT, env=env, capture_output=True, text=True, timeout=180)
    (out / (name + '.log')).write_text(r.stdout)
    (out / (name + '.stderr')).write_text(r.stderr)
    results.append(dict(name=name, returncode=r.returncode, sha256=hashlib.sha256(executable.read_bytes()).hexdigest()))
    print(name, r.returncode, flush=True)
    if r.returncode or ('RESULT=PASS' not in r.stdout and 'PASS' not in r.stdout):
        raise RuntimeError(name + ': ' + r.stderr + r.stdout)
    return r.stdout

try:
    if not a.skip_build:
        for name in NAMES:
            subprocess.run([str(sdk / 'bin/cncc'), '--bang-mlu-arch=mtp_372',
                            str(ROOT / 'workloads' / (name + '.mlu')), '-o', str(build / name)], check=True)
    if not a.external_server:
        if a.host != '127.0.0.1':
            p.error('non-local --host requires --external-server')
        log = (out / 'server.log').open('w')
        server = subprocess.Popen([str(ROOT / 'build/mlu-remote-server')], cwd=ROOT,
                                  env=dict(base, MLU_REMOTE_BIND=a.host, MLU_REMOTE_PORT=str(a.port)),
                                  stdout=log, stderr=subprocess.STDOUT)
    for _ in range(100):
        if server is not None and server.poll() is not None:
            raise RuntimeError('server exited')
        try:
            with socket.create_connection((a.host, a.port), .1):
                break
        except OSError:
            time.sleep(.05)
    else:
        raise RuntimeError('server not listening')
    for name in NAMES:
        executable = build / name
        expected = run(name + '-native', executable, base)
        for mode, env in [('replacement', remote), ('preload', preload)]:
            if run(name + '-' + mode, executable, env) != expected:
                raise RuntimeError(name + ': native/remote observations differ')
    # One existing generic CNCC core smoke; no fault/stress suites.
    run('core-smoke', ROOT / 'build/native_bang_generic', remote)
finally:
    if server is not None:
        server.terminate()
        server.wait(timeout=15)
    if log is not None:
        log.close()
    (out / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
