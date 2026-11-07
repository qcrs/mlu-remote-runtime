#!/usr/bin/env python3
"""Discover Native cross-Context pointer access; never certifies Remote IPC."""
import argparse
import hashlib
import json
import os
import pathlib
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', default='evidence/ipc-context-20261107')
    args = parser.parse_args()
    out = (ROOT / args.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    sdk = pathlib.Path(os.environ.get('NEUWARE_HOME', '/usr/local/neuware'))
    env = dict(os.environ, LD_LIBRARY_PATH=str(sdk / 'lib64'))
    env.pop('LD_PRELOAD', None)
    build = ROOT / 'build/probes'
    build.mkdir(parents=True, exist_ok=True)
    binary, image = build / 'probe-ipc-context', build / 'ipc-context.fatbin'
    commands = [
        [str(sdk / 'bin/cncc'), '--bang-fatbin-only', '--bang-mlu-arch=mtp_372',
         str(ROOT / 'probes/probe_ipc_context_kernel.mlu'), '-o', str(image)],
        [os.environ.get('CC', 'gcc'), '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
         '-I' + str(sdk / 'include'), str(ROOT / 'probes/probe_ipc_context.c'),
         '-L' + str(sdk / 'lib64'), '-Wl,-rpath,' + str(sdk / 'lib64'),
         '-lcndrv', '-pthread', '-o', str(binary)]
    ]
    with (out / 'build.log').open('w') as log:
        for cmd in commands:
            log.write('$ ' + ' '.join(cmd) + '\n')
            log.flush()
            subprocess.run(cmd, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT,
                           timeout=60, check=True)
    results = []
    for mode in ('same-kernel', 'read', 'write', 'copy', 'memset', 'kernel', 'thread-kernel'):
        cmd = [str(binary), mode, str(image)]
        run = subprocess.run(cmd, cwd=ROOT, env=env, capture_output=True, text=True, timeout=30)
        (out / (mode + '.jsonl')).write_text(run.stdout)
        (out / (mode + '.stderr')).write_text(run.stderr)
        rows = [json.loads(line) for line in run.stdout.splitlines() if line.startswith('{')]
        checks = [row for row in rows if 'check' in row]
        supported = next((row['access_supported'] for row in rows if 'access_supported' in row), None)
        complete = run.returncode == 0 and any(row.get('complete') for row in rows) and all(row['pass'] for row in checks)
        results.append(dict(mode=mode, command=cmd, returncode=run.returncode,
                            complete=complete, access_supported=supported, checks=len(checks)))
        print(mode, 'complete=' + str(complete), 'access_supported=' + str(supported), flush=True)
    (out / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
    files = [ROOT / p for p in ('probes/probe_ipc_context.c', 'probes/probe_ipc_context_kernel.mlu',
                               'scripts/probe-ipc-context.py')]
    files += [binary, image, sdk / 'include/cn_api.h', sdk / 'lib64/libcndrv.so']
    provenance = dict(source_base_commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                      remote=False, build_commands=commands,
                      sha256={str(p.relative_to(ROOT)) if p.is_relative_to(ROOT) else str(p):
                              hashlib.sha256(p.read_bytes()).hexdigest() for p in files})
    (out / 'provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
    return 0 if all(row['complete'] for row in results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
