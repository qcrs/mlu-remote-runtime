#!/usr/bin/env python3
"""Build ordinary BANG C apps once and verify native/replacement/preload over TCP."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import socket
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build/workloads'
# (arguments, reported shape, element count). Small cases catch fixed-size kernels,
# task tails, non-square indexing, threshold equality and images without interiors.
SPECS = {
    'matrix_add': (0, [((), '256x256', 65536), (('17', '31'), '17x31', 527)]),
    'naive_gemm': (1e-4, [((), '64x64x64', 4096), (('17', '23', '19'), '17x23x19', 391)]),
    'image_threshold': (0, [((), '512x512', 262144), (('19', '13'), '19x13', 247),
                             (('1', '1'), '1x1', 1)]),
    'image_blur': (1e-4, [((), '512x512', 262144), (('13', '9'), '13x9', 117),
                          (('1', '7'), '1x7', 7)]),
    'queue_notifier': (1e-6, [((), '4096-f32-cross-queue', 4096)]),
    'image_sobel': (0, [((), '512x512', 262144), (('17', '11'), '17x11', 187),
                        (('1', '7'), '1x7', 7)]),
    'row_center': (1e-4, [((), '65x127', 8255), (('3', '1'), '3x1', 3)]),
}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source_hashes(name):
    paths = [ROOT / 'workloads' / (name + '.mlu'), ROOT / 'workloads/common.h']
    if name == 'row_center':
        paths.append(ROOT / 'scripts/generate_bang_metadata.py')
    return {str(path.relative_to(ROOT)): digest(path) for path in paths}


def build_workloads(sdk, out):
    BUILD.mkdir(parents=True, exist_ok=True)
    artifacts = {}
    with (out / 'build-workloads.log').open('w') as log:
        for name in SPECS:
            executable = BUILD / name
            command = [str(sdk / 'bin/cncc'), '--bang-mlu-arch=mtp_372']
            cwd = ROOT
            if name == 'row_center':
                cwd = BUILD / 'audit' / name
                cwd.mkdir(parents=True, exist_ok=True)
                command.append('-save-temps')
            command += [str(ROOT / 'workloads' / (name + '.mlu')), '-o', str(executable)]
            commands = [command]
            if name == 'row_center':
                ir = cwd / 'types.ll'
                commands += [
                    [str(sdk / 'bin/cncc'), '--bang-device-only', '--bang-mlu-arch=mtp_372',
                     '-S', '-emit-llvm', str(ROOT / 'workloads/row_center.mlu'), '-o', str(ir)],
                    [sys.executable, str(ROOT / 'scripts/generate_bang_metadata.py'),
                     '--host', str(executable), '--pair',
                     f'{ir}:{cwd}/row_center-bang-mlisa-cambricon-bang.cnfatbin',
                     '--manifest', str(cwd / 'abi.json')],
                ]
            for command in commands:
                log.write('COMMAND ' + repr(command) + '\n'); log.flush()
                subprocess.run(command, cwd=cwd, stdout=log, stderr=subprocess.STDOUT,
                               check=True, timeout=180)
            artifacts[name] = {'sha256': digest(executable), 'sources': source_hashes(name),
                               'build_commands': commands}
            if name == 'row_center':
                metadata = json.loads((cwd / 'abi.json').read_text())
                if len(metadata) != 2 or not all(any(p['kind'] == 'aggregate'
                                                     for p in k['parameters']) for k in metadata):
                    raise RuntimeError('row_center compiler aggregate metadata missing')
                (out / 'row_center-abi.json').write_text(json.dumps(metadata, indent=2) + '\n')
            print('built', name, flush=True)
    (BUILD / 'artifacts.json').write_text(json.dumps(artifacts, indent=2) + '\n')
    return artifacts


def load_artifacts():
    artifacts = json.loads((BUILD / 'artifacts.json').read_text())
    for name in SPECS:
        entry = artifacts[name]
        if entry['sha256'] != digest(BUILD / name) or entry['sources'] != source_hashes(name):
            raise RuntimeError(f'{name}: stale build; rerun without --skip-build')
    return artifacts


def observations(stdout, name, shape, tolerance):
    values = {}
    for line in stdout.splitlines():
        key, sep, value = line.partition('=')
        if sep:
            if key in values:
                raise RuntimeError('duplicate output field: ' + key)
            values[key] = value
    required = {'WORKLOAD', 'SHAPE', 'CPU_CHECKSUM', 'MLU_CHECKSUM',
                'MAX_ABS_ERROR', 'TOLERANCE', 'MISMATCH', 'RESULT'}
    if not required <= values.keys():
        raise RuntimeError('missing workload metrics: ' + repr(required - values.keys()))
    if values['WORKLOAD'] != name or values['SHAPE'] != shape or values['RESULT'] != 'PASS':
        raise RuntimeError('incorrect workload/shape/result: ' + repr(values))
    for key in ('CPU_CHECKSUM', 'MLU_CHECKSUM', 'MAX_ABS_ERROR', 'TOLERANCE'):
        values[key] = float(values[key])
        if not math.isfinite(values[key]):
            raise RuntimeError('nonfinite metric: ' + key)
    values['MISMATCH'] = int(values['MISMATCH'])
    if (values['MISMATCH'] != 0 or not 0 <= values['MAX_ABS_ERROR'] <= tolerance or
            not math.isclose(values['TOLERANCE'], tolerance, rel_tol=1e-6)):
        raise RuntimeError('CPU elementwise comparison failed: ' + repr(values))
    if name == 'row_center':
        values['GUARD_MISMATCH'] = int(values['GUARD_MISMATCH'])
        values['MEAN_MAX_ABS_ERROR'] = float(values['MEAN_MAX_ABS_ERROR'])
        if values['GUARD_MISMATCH'] or not 0 <= values['MEAN_MAX_ABS_ERROR'] <= tolerance:
            raise RuntimeError('row mean/offset/padding comparison failed')
    return values


def run_case(out, results, name, args, shape, count, mode, env, artifact):
    label = name + ('-default' if not args else '-' + 'x'.join(args)) + '-' + mode
    executable = BUILD / name
    before = digest(executable)
    if before != artifact['sha256']:
        raise RuntimeError(name + ': executable changed between modes')
    command = [str(executable), *args]
    entry = {'name': label, 'workload': name, 'mode': mode, 'command': command,
             'sha256': before, 'elements': count,
             'environment': {k: env[k] for k in ('LD_LIBRARY_PATH', 'LD_PRELOAD',
                                                'MLU_REMOTE_HOST', 'MLU_REMOTE_PORT') if k in env}}
    results.append(entry)
    try:
        process = subprocess.run(command, cwd=ROOT, env=env, capture_output=True,
                                 text=True, timeout=180)
        (out / (label + '.log')).write_text(process.stdout)
        (out / (label + '.stderr')).write_text(process.stderr)
        entry['returncode'] = process.returncode
        if process.returncode:
            raise RuntimeError(f'{label}: exit {process.returncode}; see {out}/{label}.stderr')
        entry['metrics'] = observations(process.stdout, name, shape, SPECS[name][0])
        if digest(executable) != before:
            raise RuntimeError(name + ': executable changed during run')
    except Exception as exc:
        entry['error'] = str(exc)
        raise
    print(f'{label}: PASS max_abs_error={entry["metrics"]["MAX_ABS_ERROR"]:.9g}', flush=True)
    return entry['metrics']


def wait_server(host, port, server):
    for _ in range(100):
        if server is not None and server.poll() is not None:
            raise RuntimeError('server exited')
        try:
            with socket.create_connection((host, port), .1):
                return
        except OSError:
            time.sleep(.05)
    raise RuntimeError('server not listening')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', default='evidence/workloads-20261107-extended')
    parser.add_argument('--port', type=int, default=50221)
    parser.add_argument('--host', default='127.0.0.1')
    parser.add_argument('--external-server', action='store_true', help='use server in terminal A')
    parser.add_argument('--skip-build', action='store_true', help='verify a current build receipt')
    parser.add_argument('--build-only', action='store_true', help='compile apps without running them')
    args = parser.parse_args()
    if not args.external_server and args.host != '127.0.0.1':
        parser.error('non-local --host requires --external-server')
    if args.build_only and args.skip_build:
        parser.error('--build-only conflicts with --skip-build')
    out = ROOT / args.out
    out.mkdir(parents=True, exist_ok=True)
    sdk = Path(os.environ.get('NEUWARE_HOME', '/usr/local/neuware'))
    artifacts = load_artifacts() if args.skip_build else build_workloads(sdk, out)
    (out / 'artifacts.json').write_text(json.dumps(artifacts, indent=2) + '\n')
    if args.skip_build:
        (out / 'row_center-abi.json').write_bytes((BUILD / 'audit/row_center/abi.json').read_bytes())
    if args.build_only:
        return
    base = dict(os.environ, LD_LIBRARY_PATH=str(sdk / 'lib64'))
    for key in ('LD_PRELOAD', 'MLU_TEST_SHIM', 'MLU_REMOTE_HOST', 'MLU_REMOTE_PORT'):
        base.pop(key, None)
    remote = dict(base, MLU_REMOTE_HOST=args.host, MLU_REMOTE_PORT=str(args.port),
                  LD_LIBRARY_PATH=f'{ROOT}/build/shim:{ROOT}/build')
    preload = dict(base, MLU_REMOTE_HOST=args.host, MLU_REMOTE_PORT=str(args.port),
                   LD_PRELOAD=str(ROOT / 'build/shim/libcnrt.so'))
    results, smoke = [], []
    baseline = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    runtime_artifacts = {path: digest(ROOT / path) for path in (
        'build/mlu-remote-server', 'build/shim/libcnrt.so', 'build/libremote_cnrt.so')}
    server = log = None
    status = 'FAIL'
    try:
        if not args.external_server:
            log = (out / 'server.log').open('w')
            server = subprocess.Popen([str(ROOT / 'scripts/run-server.sh')], cwd=ROOT,
                                      env=dict(base, MLU_REMOTE_BIND=args.host, MLU_REMOTE_PORT=str(args.port)),
                                      stdout=log, stderr=subprocess.STDOUT)
        wait_server(args.host, args.port, server)
        for name, (tolerance, cases) in SPECS.items():
            for arguments, shape, count in cases:
                native = run_case(out, results, name, arguments, shape, count, 'native', base, artifacts[name])
                for mode, env in [('replacement', remote), ('preload', preload)]:
                    measured = run_case(out, results, name, arguments, shape, count, mode, env, artifacts[name])
                    if measured['CPU_CHECKSUM'] != native['CPU_CHECKSUM']:
                        raise RuntimeError(name + ': CPU reference differs across modes')
                    # Per-element validation is inside every app; do not require
                    # byte-identical FP32 text across native and remote backends.
                    if abs(measured['MLU_CHECKSUM'] - native['MLU_CHECKSUM']) > 2 * count * tolerance:
                        raise RuntimeError(name + ': native/remote checksum drift exceeds tolerance')
        # One existing generic CNCC core smoke; no fault/stress suites.
        command = [str(ROOT / 'build/native_bang_generic')]
        process = subprocess.run(command, cwd=ROOT, env=remote, capture_output=True, text=True, timeout=180)
        (out / 'core-smoke.log').write_text(process.stdout)
        (out / 'core-smoke.stderr').write_text(process.stderr)
        smoke.append({'command': command, 'returncode': process.returncode,
                      'sha256': digest(Path(command[0]))})
        if process.returncode or 'PASS generic_add_one BANG C numerical check' not in process.stdout:
            raise RuntimeError('core smoke failed')
        status = 'PASS'
        print(f'PASS: {len(SPECS)} workloads, {len(results) // 3} cases, '
              f'{len(results)} native/replacement/preload runs; core smoke PASS', flush=True)
    finally:
        if server is not None:
            server.terminate()
            try:
                server.wait(timeout=15)
            except subprocess.TimeoutExpired:
                server.kill(); server.wait(timeout=5)
        if log is not None:
            log.close()
        summary = {'status': status, 'endpoint': f'{args.host}:{args.port}',
                   'external_server': args.external_server, 'baseline_commit': baseline,
                   'runtime_artifacts': runtime_artifacts, 'results': results, 'core_smoke': smoke}
        (out / 'results.json').write_text(json.dumps(summary, indent=2) + '\n')


if __name__ == '__main__':
    main()
