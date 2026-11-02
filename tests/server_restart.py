#!/usr/bin/env python3
"""Stop/kill/restart only a test-owned native server; old sessions never replay.

Opaque IDs must not repeat the deterministic first-session seed after restart.
This is not a test of forced cancellation of a running device kernel.
"""
import argparse
import ctypes as c
import json
import os
import pathlib
import signal
import socket
import subprocess
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser()
p.add_argument('--out', required=True)
args = p.parse_args()
out = pathlib.Path(args.out).resolve()
out.mkdir(parents=True, exist_ok=True)
os.environ['MLU_REMOTE_RPC_TIMEOUT_MS'] = '250'
LIB = c.CDLL(str(ROOT / 'build/libremote_cnrt.so'))
P, U64, U32 = c.c_void_p, c.c_uint64, c.c_uint32


def bind(name, types, result=c.c_int):
    f = getattr(LIB, 'mlu_remote_' + name)
    f.argtypes, f.restype = types, result
    return f


connect = bind('connect', [c.c_char_p, c.c_uint16], P)
disconnect = bind('disconnect', [P], None)
alloc = bind('alloc', [P, U64, c.POINTER(U64)])
free = bind('free', [P, U64])
count = bind('device_count', [P, c.POINTER(U32)])
with socket.socket() as reserve:
    reserve.bind(('127.0.0.1', 0))
    port = reserve.getsockname()[1]
server, log = None, None
contexts, results = [], []


def start(number):
    global server, log
    path = out / f'server-{number}.log'
    log = path.open('w')
    env = dict(os.environ, MLU_REMOTE_BIND='127.0.0.1', MLU_REMOTE_PORT=str(port))
    server = subprocess.Popen([str(ROOT / 'build/mlu-remote-server')], cwd=ROOT, env=env,
                              stdout=log, stderr=subprocess.STDOUT)
    for _ in range(100):
        assert server.poll() is None, 'test server startup failed'
        if 'listening' in path.read_text():
            return
        time.sleep(.02)
    raise AssertionError('test server startup timeout')


def new_context():
    ctx = connect(b'127.0.0.1', port)
    assert ctx, 'native server connect failed'
    contexts.append(ctx)
    return ctx


def poison(ctx):
    value = U32(77)
    assert count(ctx, c.byref(value)) & 0xffffffff == 0x80000001
    assert value.value == 77


def record(name, **values):
    results.append(dict(name=name, passed=True, **values))
    print('PASS', json.dumps(results[-1]), flush=True)


try:
    start(1)
    old = new_context()
    old_id = U64()
    assert alloc(old, 64, c.byref(old_id)) == 0
    server.send_signal(signal.SIGSTOP)
    value = U32(77)
    started = time.monotonic()
    status = count(old, c.byref(value)) & 0xffffffff
    elapsed = time.monotonic() - started
    assert status == 0x80000006 and value.value == 77 and .18 <= elapsed < 1.2
    poison(old)
    server.send_signal(signal.SIGCONT)
    survivor = new_context()
    assert count(survivor, c.byref(value)) == 0 and value.value > 0
    record('stopped-server-deadline-no-replay-new-session-after-resume', elapsed_seconds=round(elapsed, 4))

    server.kill()
    server.wait(timeout=5)
    log.close()
    value.value = 77
    started = time.monotonic()
    assert count(survivor, c.byref(value)) != 0 and value.value == 77
    assert time.monotonic() - started < 1.2
    poison(survivor)
    record('killed-server-incomplete-exchange-poisons-context')

    start(2)
    poison(old)
    poison(survivor)
    assert 'session start' not in (out / 'server-2.log').read_text(), 'old client reconnected/replayed'
    fresh = new_context()
    new_id = U64()
    assert alloc(fresh, 64, c.byref(new_id)) == 0
    assert old_id.value != new_id.value, f'opaque IDs alias across restart: {old_id.value} == {new_id.value}'
    assert free(fresh, old_id) != 0, 'old ID released new session allocation'
    assert free(fresh, new_id) == 0
    record('restart-old-session-and-old-ID-rejected', old_id=old_id.value, new_id=new_id.value)
finally:
    for ctx in contexts:
        disconnect(ctx)
    if server and server.poll() is None:
        server.send_signal(signal.SIGCONT)
        time.sleep(.05)
        server.terminate()
        server.wait(timeout=5)
    if log:
        log.close()
    (out / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
