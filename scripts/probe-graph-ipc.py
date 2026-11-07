#!/usr/bin/env python3
"""Record native Graph/IPC contracts; this is not Remote certification."""
import argparse
import array
import hashlib
import json
import os
import pathlib
import selectors
import socket
import subprocess
import time
from datetime import datetime, timezone

ROOT = pathlib.Path(__file__).resolve().parents[1]


def observations(raw):
    return [json.loads(line) for line in raw.splitlines() if line.startswith('{')]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', default='evidence/graph-ipc-contracts-20261107')
    args = parser.parse_args()
    out = (ROOT / args.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    sdk = pathlib.Path(os.environ.get('NEUWARE_HOME', '/usr/local/neuware')).resolve()
    env = dict(os.environ, LD_LIBRARY_PATH=str(sdk / 'lib64'))
    env.pop('LD_PRELOAD', None)
    bins = {}
    results = []
    builds = []
    for library in ('cnrt', 'cndrv'):
        binary = ROOT / 'build/probes' / ('probe-graph-ipc-' + library)
        binary.parent.mkdir(parents=True, exist_ok=True)
        cmd = [os.environ.get('CC', 'gcc'), '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
               '-I' + str(sdk / 'include'), str(ROOT / 'probes/probe_graph_ipc.c'),
               '-L' + str(sdk / 'lib64'), '-Wl,-rpath,' + str(sdk / 'lib64'),
               '-l' + library, '-o', str(binary)]
        if library == 'cnrt':
            cmd.insert(1, '-DUSE_CNRT')
        built = subprocess.run(cmd, capture_output=True, text=True, cwd=ROOT, env=env, timeout=60)
        (out / (library + '-build.log')).write_text(built.stdout + built.stderr)
        built.check_returncode()
        bins[library] = str(binary)
        builds.append(dict(command=cmd, binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest()))

    def save(name, raw, stderr):
        (out / (name + '.jsonl')).write_text(raw)
        (out / (name + '.stderr')).write_text(stderr)
        return observations(raw)

    def record(name, code, rows):
        checks = [x for x in rows if 'check' in x]
        passed = code == 0 and bool(checks) and all(x['pass'] for x in checks)
        results.append(dict(name=name, returncode=code, passed=passed, checks=len(checks)))
        print(name, 'PASS' if passed else 'FAIL', 'checks=' + str(len(checks)), flush=True)

    for library in bins:
        for mode in ('graph', 'capture', 'notifier') + (('ipc-same-process',) if library == 'cndrv' else ()):
            name = library + '-' + mode
            try:
                run = subprocess.run([bins[library], mode], cwd=ROOT, env=env,
                                     capture_output=True, text=True, timeout=30)
                rows = save(name, run.stdout, run.stderr)
                record(name, run.returncode, rows)
            except subprocess.TimeoutExpired as exc:
                raw = (exc.stdout or b'').decode()
                rows = save(name, raw, (exc.stderr or b'').decode())
                record(name, 'timeout', rows)

    for origin, target, kind in [('cnrt', 'cnrt', 'device'), ('cndrv', 'cndrv', 'device'),
                                  ('cnrt', 'cndrv', 'device'), ('cndrv', 'cnrt', 'device'),
                                  ('cnrt', 'cnrt', 'host')]:
        name = 'ipc-' + origin + '-to-' + target + '-' + kind
        # Exporter waits while an entirely separate executable opens/writes/closes.
        with (out / (name + '-exporter.stderr')).open('w') as err:
            exporter = subprocess.Popen([bins[origin], 'ipc-export', kind], cwd=ROOT, env=env,
                                        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=err)
            raw = b''
            ready = None
            try:
                deadline = time.monotonic() + 30
                with selectors.DefaultSelector() as sel:
                    sel.register(exporter.stdout, selectors.EVENT_READ)
                    while time.monotonic() < deadline:
                        if not sel.select(max(0, deadline - time.monotonic())):
                            break
                        data = os.read(exporter.stdout.fileno(), 65536)
                        if not data:
                            break
                        raw += data
                        # Only parse complete lines from the pipe.
                        for row in observations(raw[:raw.rfind(b'\n') + 1].decode()):
                            if row.get('ready'):
                                ready = row
                        if ready:
                            break
                if not ready:
                    raise RuntimeError('exporter did not return a handle: ' + raw.decode())
                importer = subprocess.run([bins[target], 'ipc-import', ready['handle'], kind],
                                          cwd=ROOT, env=env, capture_output=True, timeout=30)
                ir = save(name + '-importer', importer.stdout.decode(), importer.stderr.decode())
                tail, _ = exporter.communicate(b'done\n', timeout=30)
                raw += tail
                er = observations(raw.decode())
                pids = [x['pid'] for x in er + ir if 'pid' in x]
                if len(pids) != 2 or len(set(pids)) != 2:
                    raise RuntimeError('IPC peers did not use independent processes')
                record(name, exporter.returncode or importer.returncode, er + ir)
            except (RuntimeError, subprocess.TimeoutExpired) as exc:
                exporter.kill()
                tail, _ = exporter.communicate(timeout=5)
                raw += tail
                results.append(dict(name=name, passed=False, error=str(exc)))
                print(name, 'FAIL', str(exc), flush=True)
            finally:
                if exporter.poll() is None:
                    exporter.kill()
                    exporter.communicate(timeout=5)
                (out / (name + '-exporter.jsonl')).write_bytes(raw)

    name = 'vmm-cndrv-posix-fd'
    receiver, sender = socket.socketpair()
    receiver.settimeout(30)
    with (out / (name + '-exporter.stderr')).open('w') as err:
        exporter = subprocess.Popen([bins['cndrv'], 'vmm-export', str(sender.fileno())],
                                    cwd=ROOT, env=env, pass_fds=(sender.fileno(),),
                                    stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=err)
        sender.close()
        received = []
        try:
            data, ancillary, flags, _ = receiver.recvmsg(128, socket.CMSG_SPACE(array.array('i').itemsize))
            for level, kind, content in ancillary:
                if level == socket.SOL_SOCKET and kind == socket.SCM_RIGHTS:
                    fds = array.array('i')
                    fds.frombytes(content)
                    received.extend(fds)
            if len(received) != 1 or flags & socket.MSG_CTRUNC:
                raise RuntimeError('exporter did not send exactly one complete POSIX FD')
            fd = received[0]
            importer = subprocess.run([bins['cndrv'], 'vmm-import', str(fd), data.decode()],
                                      cwd=ROOT, env=env, pass_fds=(fd,),
                                      capture_output=True, timeout=30)
            ir = save(name + '-importer', importer.stdout.decode(), importer.stderr.decode())
            raw, _ = exporter.communicate(b'done\n', timeout=30)
            er = observations(raw.decode())
            pids = [x['pid'] for x in er + ir if 'pid' in x]
            if len(pids) != 2 or len(set(pids)) != 2:
                raise RuntimeError('VMM peers did not use independent processes')
            record(name, exporter.returncode or importer.returncode, er + ir)
        except (OSError, RuntimeError, subprocess.TimeoutExpired) as exc:
            exporter.kill()
            raw, _ = exporter.communicate(timeout=5)
            results.append(dict(name=name, passed=False, error=str(exc)))
            print(name, 'FAIL', str(exc), flush=True)
        finally:
            receiver.close()
            for fd in received:
                os.close(fd)
            if exporter.poll() is None:
                exporter.kill()
                raw, _ = exporter.communicate(timeout=5)
            (out / (name + '-exporter.jsonl')).write_bytes(raw)

    (out / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
    provenance = dict(timestamp_utc=datetime.now(timezone.utc).isoformat(), sdk=str(sdk),
                      source_base_commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                      uname=subprocess.check_output(['uname', '-a'], text=True).strip(),
                      source_sha256=hashlib.sha256((ROOT / 'probes/probe_graph_ipc.c').read_bytes()).hexdigest(),
                      runner_sha256=hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest(),
                      headers_sha256={n: hashlib.sha256((sdk / 'include' / n).read_bytes()).hexdigest()
                                      for n in ('cnrt.h', 'cn_api.h')},
                      builds=builds, remote=False, ipc_peers='separate exec processes')
    (out / 'provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
    return 0 if all(x['passed'] for x in results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
