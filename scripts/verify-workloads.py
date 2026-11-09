#!/usr/bin/env python3
"""Build ordinary BANG C apps once and verify native/replacement/preload over TCP."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
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
    'heat_diffusion': (1e-5, [((), '193x129x32', 24897),
                              (('23', '17', '5'), '23x17x5', 391),
                              (('1', '7', '1'), '1x7x1', 7)]),
    'cndrv_gemm': (1e-4, [((), '19x17x23', 342)]),
    'llm_transformer': (7e-4, [((), '7x8x12', 56)]),
    'runtime_driver_interop': (1e-4, [((), '4x37', 148)]),
}
DRIVER_WORKLOADS = {'cndrv_gemm', 'runtime_driver_interop'}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def recorded_path(path):
    path = Path(path).resolve()
    try:
        return str(path.relative_to(ROOT))
    except ValueError:
        return str(path)


def source_hashes(name):
    paths = [ROOT / 'workloads' / (name + '.mlu')]
    if name not in ('cndrv_gemm', 'runtime_driver_interop'):
        paths.append(ROOT / 'workloads/common.h')
    if name == 'row_center':
        paths.append(ROOT / 'scripts/generate_bang_metadata.py')
    if name in DRIVER_WORKLOADS:
        paths.append(ROOT / 'scripts/generate_bang_metadata.py')
    return {str(path.relative_to(ROOT)): digest(path) for path in paths}


def build_workloads(sdk, out, names):
    BUILD.mkdir(parents=True, exist_ok=True)
    artifacts = {}
    with (out / 'build-workloads.log').open('w') as log:
        for name in names:
            executable = BUILD / name
            command = [str(sdk / 'bin/cncc'), '--bang-mlu-arch=mtp_372']
            cwd = ROOT
            if name == 'row_center' or name in DRIVER_WORKLOADS:
                cwd = BUILD / 'audit' / name
                cwd.mkdir(parents=True, exist_ok=True)
                if name in DRIVER_WORKLOADS or name == 'row_center':
                    command.append('-save-temps')
            if name in DRIVER_WORKLOADS:
                command.append('-lcndrv')
            if name == 'llm_transformer':
                command.append('-lm')
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
            elif name in DRIVER_WORKLOADS:
                ir = cwd / 'types.ll'
                image = cwd / (name + '-bang-mlisa-cambricon-bang.cnfatbin')
                commands += [
                    [str(sdk / 'bin/cncc'), '--bang-device-only', '--bang-mlu-arch=mtp_372',
                     '-S', '-emit-llvm', str(ROOT / 'workloads' / (name + '.mlu')), '-o', str(ir)],
                    [sys.executable, str(ROOT / 'scripts/generate_bang_metadata.py'),
                     '--host', str(executable), '--pair', f'{ir}:{image}',
                     '--manifest', str(cwd / 'abi.json'), '--attach-images'],
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
            if name in DRIVER_WORKLOADS:
                image = cwd / (name + '-bang-mlisa-cambricon-bang.cnfatbin')
                metadata = json.loads((cwd / 'abi.json').read_text())
                if not image.is_file() or not metadata:
                    raise RuntimeError(name + ': compiler-derived Driver image metadata missing')
                shutil.copy2(image, out / (name + '.cnfatbin'))
                shutil.copy2(cwd / 'abi.json', out / (name + '-abi.json'))
                artifacts[name]['image'] = {
                    'path': str(image.relative_to(ROOT)), 'sha256': digest(image),
                    'evidence_image': recorded_path(out / (name + '.cnfatbin')),
                    'abi_manifest': recorded_path(out / (name + '-abi.json')),
                }
            print('built', name, flush=True)
    receipt = BUILD / 'artifacts.json'
    existing = json.loads(receipt.read_text()) if receipt.exists() else {}
    existing.update(artifacts)
    receipt.write_text(json.dumps(existing, indent=2) + '\n')
    return artifacts


def load_artifacts(names):
    artifacts = json.loads((BUILD / 'artifacts.json').read_text())
    for name in names:
        entry = artifacts[name]
        if entry['sha256'] != digest(BUILD / name) or entry['sources'] != source_hashes(name):
            raise RuntimeError(f'{name}: stale build; rerun without --skip-build')
        if name in DRIVER_WORKLOADS:
            image = ROOT / entry['image']['path']
            if not image.is_file() or entry['image']['sha256'] != digest(image):
                raise RuntimeError(f'{name}: stale CNDrv image; rerun without --skip-build')
    return {name: artifacts[name] for name in names}


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
    if name in ('row_center', 'heat_diffusion'):
        values['GUARD_MISMATCH'] = int(values['GUARD_MISMATCH'])
        if values['GUARD_MISMATCH']:
            raise RuntimeError('device buffer guard changed')
    if name == 'row_center':
        values['MEAN_MAX_ABS_ERROR'] = float(values['MEAN_MAX_ABS_ERROR'])
        if values['GUARD_MISMATCH'] or not 0 <= values['MEAN_MAX_ABS_ERROR'] <= tolerance:
            raise RuntimeError('row mean/offset/padding comparison failed')
    if name == 'llm_transformer':
        stages = ('RMSNORM', 'QKV', 'ROPE', 'ROPE_KEY', 'KV_CACHE_KEY',
                  'KV_CACHE_VALUE', 'ATTENTION_SCORE', 'SOFTMAX', 'ATTENTION',
                  'SWIGLU_PROJECTION', 'SWIGLU_UP', 'SWIGLU')
        for stage in stages:
            key = 'STAGE_' + stage + '_MAX_ABS_ERROR'
            values[key] = float(values[key])
            if not math.isfinite(values[key]) or not 0 <= values[key] <= tolerance:
                raise RuntimeError('LLM intermediate stage comparison failed: ' + key)
    return values


def run_case(out, results, name, args, shape, count, mode, env, artifact):
    label = name + ('-default' if not args else '-' + 'x'.join(args)) + '-' + mode
    executable = BUILD / name
    before = digest(executable)
    if before != artifact['sha256']:
        raise RuntimeError(name + ': executable changed between modes')
    workload_args = list(args)
    if name in DRIVER_WORKLOADS:
        workload_args.append(str(ROOT / artifact['image']['path']))
    command = [str(executable), *workload_args]
    launched_command = command
    if mode == 'isolated':
        launched_command = ['bash', str(ROOT / 'scripts/run-isolated-client.sh'), *command]
    elif mode == 'isolated-netns':
        launched_command = [
            'bash', str(ROOT / 'scripts/run-isolated-pair.sh'),
            '--port', env['MLU_REMOTE_PORT'], '--shim', env.get('MLU_REMOTE_SHIM', 'both'),
            '--server', str(ROOT / 'build/mlu-remote-server'), '--client', str(executable),
            '--', *workload_args,
        ]
    entry = {'name': label, 'workload': name, 'mode': mode, 'command': launched_command,
             'client_command': command,
             'sha256': before, 'elements': count,
             'environment': {k: env[k] for k in ('LD_LIBRARY_PATH', 'LD_PRELOAD',
                                                'MLU_REMOTE_HOST', 'MLU_REMOTE_PORT') if k in env}}
    results.append(entry)
    try:
        process = subprocess.run(launched_command, cwd=ROOT, env=env, capture_output=True,
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
    parser.add_argument('--out', default='evidence/workloads-20261108-full')
    parser.add_argument('--port', type=int, default=50221)
    parser.add_argument('--host', default='127.0.0.1')
    parser.add_argument('--external-server', action='store_true', help='use server in terminal A')
    parser.add_argument('--realistic-isolation', action='store_true',
                        help='run Driver clients in a chroot over sibling network namespaces and veth/TCP')
    parser.add_argument('--skip-build', action='store_true', help='verify a current build receipt')
    parser.add_argument('--build-only', action='store_true', help='compile apps without running them')
    parser.add_argument('--workload', action='append', choices=tuple(SPECS),
                        help='select an app (repeatable); default: all apps')
    args = parser.parse_args()
    if not args.external_server and args.host != '127.0.0.1':
        parser.error('non-local --host requires --external-server')
    if args.build_only and args.skip_build:
        parser.error('--build-only conflicts with --skip-build')
    out = ROOT / args.out
    out.mkdir(parents=True, exist_ok=True)
    sdk = Path(os.environ.get('NEUWARE_HOME', '/usr/local/neuware'))
    names = list(dict.fromkeys(args.workload or SPECS))
    artifacts = load_artifacts(names) if args.skip_build else build_workloads(sdk, out, names)
    (out / 'artifacts.json').write_text(json.dumps(artifacts, indent=2) + '\n')
    if args.skip_build and 'row_center' in names:
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
        'build/mlu-remote-server', 'build/shim/libcnrt.so',
        'build/driver-shim/libcndrv.so', 'build/libremote_cnrt.so')}
    server = log = None
    failure = None
    status = 'FAIL'
    try:
        if not args.external_server:
            log = (out / 'server.log').open('w')
            server = subprocess.Popen([str(ROOT / 'scripts/run-server.sh')], cwd='/tmp',
                                      env=dict(base, MLU_REMOTE_BIND=args.host, MLU_REMOTE_PORT=str(args.port),
                                               MLU_REMOTE_MODULE_DIR=str(ROOT / 'modules')),
                                      stdout=log, stderr=subprocess.STDOUT)
        wait_server(args.host, args.port, server)
        for name in names:
            tolerance, cases = SPECS[name]
            for arguments, shape, count in cases:
                native = run_case(out, results, name, arguments, shape, count, 'native', base, artifacts[name])
                modes = [('replacement', remote), ('preload', preload)]
                if name in DRIVER_WORKLOADS:
                    modes = [
                        ('replacement', dict(remote, MLU_REMOTE_SHIM='both',
                            LD_LIBRARY_PATH=f'{ROOT}/build/driver-shim:{ROOT}/build/shim:{ROOT}/build')),
                        ('preload', dict(preload, MLU_REMOTE_SHIM='both',
                            LD_PRELOAD=f'{ROOT}/build/shim/libcnrt.so:{ROOT}/build/driver-shim/libcndrv.so',
                            LD_LIBRARY_PATH=f'{ROOT}/build/driver-shim:{ROOT}/build/shim:{ROOT}/build')),
                    ]
                for mode, env in modes:
                    measured = run_case(out, results, name, arguments, shape, count, mode, env, artifacts[name])
                    if measured['CPU_CHECKSUM'] != native['CPU_CHECKSUM']:
                        raise RuntimeError(name + ': CPU reference differs across modes')
                    # Per-element validation is inside every app; do not require
                    # byte-identical FP32 text across native and remote backends.
                    if abs(measured['MLU_CHECKSUM'] - native['MLU_CHECKSUM']) > 2 * count * tolerance:
                        raise RuntimeError(name + ': native/remote checksum drift exceeds tolerance')
                if name in DRIVER_WORKLOADS and (not args.external_server or args.realistic_isolation):
                    isolated_mode = 'isolated-netns' if args.realistic_isolation else 'isolated'
                    measured = run_case(out, results, name, arguments, shape, count, isolated_mode,
                                        dict(modes[0][1], MLU_REMOTE_SHIM='both'), artifacts[name])
                    if (measured['CPU_CHECKSUM'] != native['CPU_CHECKSUM'] or
                            abs(measured['MLU_CHECKSUM'] - native['MLU_CHECKSUM']) > 2 * count * tolerance):
                        raise RuntimeError(name + ': isolated client differs from native result')
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
        print(f'PASS: {len(names)} workloads, {len(results)} runs; core smoke PASS', flush=True)
    except Exception as exc:
        failure = str(exc)
        raise
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
                   'realistic_isolation': args.realistic_isolation,
                   'workloads': names, 'failure': failure,
                   'runtime_artifacts': runtime_artifacts, 'results': results, 'core_smoke': smoke}
        (out / 'results.json').write_text(json.dumps(summary, indent=2) + '\n')


if __name__ == '__main__':
    main()
