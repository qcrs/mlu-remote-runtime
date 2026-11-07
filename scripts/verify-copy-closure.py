#!/usr/bin/env python3
"""Same binaries: offline local errors and inferred copies over separate TCP processes."""
import argparse
import json
import os
import pathlib
import socket
import subprocess
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser()
p.add_argument('--out', default='evidence/core-copy-20261107')
p.add_argument('--port', type=int, default=50223)
a = p.parse_args()
out = ROOT / a.out
out.mkdir(parents=True, exist_ok=True)
base = dict(os.environ, LD_LIBRARY_PATH=os.environ.get('NEUWARE_HOME', '/usr/local/neuware') + '/lib64')
base.pop('LD_PRELOAD', None)
base.pop('MLU_TEST_SHIM', None)
remote = dict(base, MLU_REMOTE_HOST='127.0.0.1', MLU_REMOTE_PORT=str(a.port), MLU_TEST_SHIM='1',
              LD_LIBRARY_PATH=':'.join(str(ROOT / d) for d in ('build/shim', 'build/driver-shim', 'build')))
preload = dict(base, MLU_REMOTE_HOST='127.0.0.1', MLU_REMOTE_PORT=str(a.port), MLU_TEST_SHIM='1',
              LD_PRELOAD=':'.join(str(ROOT / d) for d in ('build/shim/libcnrt.so', 'build/driver-shim/libcndrv.so')))
results = []

def run(name, command, env):
    r = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, text=True, timeout=120)
    (out / (name + '.log')).write_text(r.stdout)
    (out / (name + '.stderr')).write_text(r.stderr)
    results.append(dict(name=name, command=command, returncode=r.returncode))
    print(name, r.returncode, flush=True)
    if r.returncode:
        raise RuntimeError(name + ': ' + r.stderr)
    return r.stdout

def compare(name, command, offline=False):
    expected = run(name + '-native', command, base)
    for mode, env in [('remote', remote), ('preload', preload), ('isolated', remote)]:
        env = dict(env, MLU_REMOTE_PORT='1') if offline else dict(env)
        cmd = command
        if mode == 'isolated':
            cmd = ['bash', 'scripts/run-isolated-client.sh', *command]
            env['MLU_REMOTE_SHIM'] = 'both'
        actual = run(name + '-' + mode, cmd, env)
        if (not actual.endswith(expected)) if mode == 'isolated' else actual != expected:
            raise RuntimeError(name + ' ' + mode + ' differs from Native')

server = None
log = None
try:
    compare('errors-offline', [str(ROOT / 'build/native_error_catalog')], offline=True)
    log = (out / 'server.log').open('w')
    server = subprocess.Popen([str(ROOT / 'build/mlu-remote-server')], cwd=ROOT,
                              env=dict(base, MLU_REMOTE_BIND='127.0.0.1', MLU_REMOTE_PORT=str(a.port)),
                              stdout=log, stderr=subprocess.STDOUT)
    for _ in range(100):
        if server.poll() is not None:
            raise RuntimeError('server exited')
        try:
            socket.create_connection(('127.0.0.1', a.port), .1).close()
            break
        except OSError:
            time.sleep(.05)
    else:
        raise RuntimeError('server not listening')
    for family in ('runtime', 'driver'):
        command = [str(ROOT / 'build/native_inferred_copy'), family]
        compare(family + '-copy', command)
        # These exercise the shim's fake VA registry and staging ownership;
        # native freed addresses can alias and must not be probed as Host data.
        run(family + '-ownership-remote', [*command, 'ownership'], remote)
        run(family + '-ownership-isolated', ['bash', 'scripts/run-isolated-client.sh', *command, 'ownership'],
            dict(remote, MLU_REMOTE_SHIM='both'))
finally:
    if server:
        server.terminate()
        server.wait(timeout=15)
    if log:
        log.close()
    (out / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
print('PASS copy/error closure:', len(results), 'runs')
