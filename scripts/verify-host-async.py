#!/usr/bin/env python3
"""Same CNCC binary: Host memory and deferred D2H across independent TCP processes."""
import argparse
import json
import os
import pathlib
import socket
import subprocess
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser()
p.add_argument('--out', default='evidence/host-async-20261107')
p.add_argument('--port', type=int, default=50228)
a = p.parse_args()
out = ROOT / a.out
out.mkdir(parents=True, exist_ok=True)
base = dict(os.environ, LD_LIBRARY_PATH=os.environ.get('NEUWARE_HOME', '/usr/local/neuware') + '/lib64')
base.pop('LD_PRELOAD', None)
base.pop('MLU_TEST_SHIM', None)
base.pop('MLU_QUEUE_OBSERVE_NATIVE', None)
base.pop('MLU_HOST_OBSERVE_NATIVE', None)
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
        env = dict(env)
        if offline:
            env['MLU_REMOTE_PORT'] = '1'
        cmd = command
        if mode == 'isolated':
            cmd = ['bash', 'scripts/run-isolated-client.sh', *command]
            env['MLU_REMOTE_SHIM'] = 'both'
        actual = run(name + '-' + mode, cmd, env)
        matches = actual.endswith(expected) if mode == 'isolated' else actual == expected
        if not matches:
            raise RuntimeError(name + ' ' + mode + ' differs from Native')

server = None
log = None
try:
    compare('host-offline', [str(ROOT / 'build/native_host_async'), 'offline'], offline=True)
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
    image = str(ROOT / 'build/drain-audit/native_bang_drain-bang-mlisa-cambricon-bang.cnfatbin')
    for family in ('runtime', 'driver'):
        command = [str(ROOT / 'build/native_host_async'), family]
        if family == 'driver':
            command.append(image)
        compare(family + '-host-async', command)
    run('d2h-protocol', ['python3', 'tests/host_async_protocol.py'], remote)
    image = str(ROOT / 'build/module-audit/native_bang_module-bang-mlisa-cambricon-bang.cnfatbin')
    run('backend-host-rollback', [str(ROOT / 'build/backend-resources-test'), image], base)
    run('backend-host-quarantine', [str(ROOT / 'build/backend-resources-test'), image, 'quarantine'], base)

finally:
    if server:
        server.terminate()
        server.wait(timeout=15)
    if log:
        log.close()
    (out / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
server_text = (out / 'server.log').read_text()
if server.returncode or 'RESOURCE_TOTAL device=0 staging=0 image=0' not in server_text or 'CLEANUP_ERROR' in server_text:
    raise RuntimeError('Server did not drain all native/host resource charges cleanly')
print('PASS Host memory/deferred D2H:', len(results), 'runs')
