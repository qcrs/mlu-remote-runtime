#!/usr/bin/env python3
"""Compare CNRT registered-image accounting with independent CNDrv loading."""
import argparse
import json
import os
import pathlib
import socket
import subprocess
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser()
p.add_argument('--out', default='evidence/module-accounting-20261107')
p.add_argument('--port', type=int, default=50229)
a = p.parse_args()
out = ROOT / a.out
out.mkdir(parents=True, exist_ok=True)
base = dict(os.environ, LD_LIBRARY_PATH=os.environ.get('NEUWARE_HOME', '/usr/local/neuware') + '/lib64')
base.pop('LD_PRELOAD', None)
base.pop('MLU_TEST_SHIM', None)
remote = dict(base, MLU_REMOTE_HOST='127.0.0.1', MLU_REMOTE_PORT=str(a.port),
              LD_LIBRARY_PATH=':'.join(str(ROOT / d) for d in ('build/shim', 'build/driver-shim', 'build')))
preload = dict(base, MLU_REMOTE_HOST='127.0.0.1', MLU_REMOTE_PORT=str(a.port),
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


server = None
log = None
try:
    log = (out / 'server.log').open('w')
    server = subprocess.Popen([str(ROOT / 'build/mlu-remote-server')], cwd=ROOT,
                              env=dict(base, MLU_REMOTE_BIND='127.0.0.1', MLU_REMOTE_PORT=str(a.port)),
                              stdout=log, stderr=subprocess.STDOUT)
    for _ in range(100):
        if server.poll() is not None:
            raise RuntimeError('server exited')
        try:
            with socket.create_connection(('127.0.0.1', a.port), .1):
                break
        except OSError:
            time.sleep(.05)
    else:
        raise RuntimeError('server not listening')
    images = [str(ROOT / f'build/globals-{i}/native_module_globals-bang-mlisa-cambricon-bang.cnfatbin') for i in (1, 2)]
    libraries = [str(ROOT / f'build/libnative_module_globals_{i}.so') for i in (1, 2)]
    for family in ('runtime', 'driver'):
        command = [str(ROOT / 'build/native_module_accounting'), family,
                   images[0], libraries[0], images[1], libraries[1]]
        expected = run(family + '-native', command, base)
        for mode, env in [('remote', remote), ('preload', preload), ('isolated', remote)]:
            cmd = command
            if mode == 'isolated':
                cmd = ['bash', 'scripts/run-isolated-client.sh', *command]
                env = dict(env, MLU_REMOTE_SHIM='both')
            actual = run(family + '-' + mode, cmd, env)
            if not (actual.endswith(expected) if mode == 'isolated' else actual == expected):
                raise RuntimeError(f'{family} {mode} differs from Native')
    image = str(ROOT / 'build/relocation-audit/native_bang_relocations-bang-mlisa-cambricon-bang.cnfatbin')
    for family in ('runtime', 'driver'):
        command = [str(ROOT / 'build/native_bang_relocations'), family] + ([image] if family == 'driver' else [])
        expected = run('relocation-' + family + '-native', command, base)
        for mode, env in [('remote', remote), ('preload', preload), ('isolated', remote)]:
            env = dict(env, MLU_TEST_SHIM='1')
            cmd = command
            if mode == 'isolated':
                cmd = ['bash', 'scripts/run-isolated-client.sh', *command]
                env['MLU_REMOTE_SHIM'] = 'both'
            actual = run('relocation-' + family + '-' + mode, cmd, env)
            # Additional stale/foreign handle checks apply only to fake VAs.
            if expected not in actual:
                raise RuntimeError(f'relocation {family} {mode} differs from Native')
    run('relocation-protocol', ['python3', 'tests/relocation_protocol.py'], remote)
    run('metadata-validation', ['python3', 'tests/bang_metadata.py'], remote)
    run('parser-bounds', [str(ROOT / 'build/bang-image-bounds'),
        str(ROOT / 'build/audit/native_bang_abi-bang-mlisa-cambricon-bang.cnfatbin'),
        '_Z10vector_addPfPKfS1_i',
        str(ROOT / 'build/module-audit/native_bang_module-bang-mlisa-cambricon-bang.cnfatbin'),
        str(ROOT / 'build/module-audit/native_bang_module-bang-mlisa-cambricon-bang-mtp_372.o'),
        images[0]], base)
finally:
    if server:
        server.terminate()
        server.wait(timeout=15)
    if log:
        log.close()
    (out / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
server_text = (out / 'server.log').read_text()
if server.returncode or 'RESOURCE_TOTAL device=0 staging=0 image=0' not in server_text or 'CLEANUP_ERROR' in server_text:
    raise RuntimeError('Server resource drain failed')
print('PASS module accounting/globals/argument relocations:', len(results), 'runs')
