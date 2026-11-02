#!/usr/bin/env python3
"""Native-backed quotas, rejection before submission and graceful socket drain.

Own servers only. cnmon physical memory measurements are independent of the
runtime counters; they do not measure every native object class or host RSS.
"""
import argparse
import ctypes
from concurrent.futures import ThreadPoolExecutor
from contextlib import contextmanager
import json
import os
from pathlib import Path
import re
import queue as queues
import signal
import socket
import struct
import subprocess
import time
import threading
import mlur_wire as wire
from mlur_wire import Session, idarg, launch, ptr, scalar

ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser()
p.add_argument('--out', required=True)
out = Path(p.parse_args().out).resolve()
out.mkdir(parents=True, exist_ok=True)
results = []
LIMIT = 0x80000007
MIB = 1024 * 1024
IMAGE = (ROOT / 'build/module-audit/native_bang_module-bang-mlisa-cambricon-bang.cnfatbin').read_bytes()
WRITE, READ = b'_Z12module_writei', b'_Z11module_readPii'
settings = dict(MLU_REMOTE_SESSION_DEVICE_BYTES=str(64 * MIB),
                MLU_REMOTE_TOTAL_DEVICE_BYTES=str(96 * MIB),
                MLU_REMOTE_SESSION_STAGING_BYTES='16', MLU_REMOTE_TOTAL_STAGING_BYTES='24',
                MLU_REMOTE_SESSION_IMAGE_BYTES=str(2 * len(IMAGE)),
                MLU_REMOTE_TOTAL_IMAGE_BYTES=str(3 * len(IMAGE)),
                MLU_REMOTE_SESSION_MAX_TRANSFERS='4', MLU_REMOTE_MAX_CONNECTIONS='8')


def record(name, **facts):
    results.append(dict(name=name, passed=True, **facts))
    print('PASS', json.dumps(results[-1]), flush=True)


def wait_for(predicate, timeout=5):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        value = predicate()
        if value:
            return value
        time.sleep(.02)
    raise AssertionError('condition did not become true')


@contextmanager
def server(name, **overrides):
    with socket.socket() as reserved:
        reserved.bind(('127.0.0.1', 0))
        wire.PORT = reserved.getsockname()[1]
    path = out / (name + '.log')
    env = dict(os.environ, **settings)
    env.update(overrides, MLU_REMOTE_PORT=str(wire.PORT), MLU_REMOTE_BIND='127.0.0.1')
    with path.open('w') as log:
        proc = subprocess.Popen([str(ROOT / 'build/mlu-remote-server')], cwd=ROOT,
                                env=env, stdout=log, stderr=subprocess.STDOUT)
        try:
            wait_for(lambda: 'listening' in path.read_text() or proc.poll() is not None)
            assert proc.poll() is None, path.read_text()
            yield proc, path
        finally:
            if proc.poll() is None:
                proc.terminate()
            try:
                proc.wait(timeout=15)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
                raise AssertionError('native drain did not return in test budget')
            text = path.read_text()
            assert proc.returncode == 0, text
            assert 'DRAIN_COMPLETE workers=0 cleanup_errors=0' in text, text
            assert 'RESOURCE_TOTAL device=0 staging=0 image=0' in text, text


def allocate(s, n): return struct.unpack('!Q', s.ok(3, idarg(n)))[0]
def queue(s): return struct.unpack('!Q', s.ok(6))[0]
def load_payload(image=IMAGE, symbol=WRITE):
    return struct.pack('!II', len(image), len(symbol)) + image + symbol
def load(s): return struct.unpack('!QQ', s.ok(26, load_payload()))
def integer(n): return scalar(struct.pack('<i', n))
def copy_payload(a, data, q=0):
    return struct.pack('!IQQQQQQI', 1, q, a, 0, 0, 0, len(data), len(data)) + data
def release(s, tid): s.ok(28, idarg(tid))
def readback(s, a, n):
    result = s.ok(5, struct.pack('!IQQQQQQI', 2, 0, 0, 0, a, 0, n, 0))
    tid, count = struct.unpack_from('!QQ', result)
    assert count == n
    release(s, tid)
    return result[16:]


def slow_module(s):
    image = (ROOT / 'build/drain-audit/native_bang_drain-bang-mlisa-cambricon-bang.cnfatbin').read_bytes()
    return struct.unpack('!QQ', s.ok(26, load_payload(image, b'_Z10drain_workPji')))


def slow_work(s, mid, kid, q, dst, iterations=2000000):
    tid, = struct.unpack('!Q', s.ok(27, launch(mid, kid, q, [ptr(dst), integer(iterations)])))
    status, _ = s.rpc(9, idarg(q))
    assert status == 1, ('expected native CN_ERROR_NOT_READY', status)
    return tid


def memory(label):
    text = subprocess.check_output(['cnmon', 'info', '-c', '0', '-m'], text=True)
    (out / (label + '-cnmon.log')).write_text(text)
    match = re.search(r'Physical Memory Usage\s*:\s*\n\s*Total\s*:\s*\d+ MiB\s*\n\s*Used\s*:\s*(\d+) MiB', text)
    assert match, text
    return int(match[1])


try:
    # Establish the same allocation/free/destruction behavior through CNDrv
    # directly, independently of RPC and the backend's resource accounting.
    driver = ctypes.CDLL(str(Path(os.environ.get('NEUWARE_HOME', '/usr/local/neuware')) / 'lib64/libcndrv.so'))
    vp, u64 = ctypes.c_void_p, ctypes.c_uint64
    driver.cnCtxCreate.argtypes = [ctypes.POINTER(vp), ctypes.c_uint, ctypes.c_int]
    driver.cnCtxSetCurrent.argtypes = [vp]
    driver.cnCtxDestroy.argtypes = [vp]
    driver.cnMalloc.argtypes = [ctypes.POINTER(u64), u64]
    driver.cnFree.argtypes = [u64]
    assert driver.cnInit(0) == 0
    native_before = memory('native-before')
    def native_worker(commands, replies):
        context, allocation = vp(), u64()
        try:
            assert driver.cnCtxCreate(ctypes.byref(context), 1, 0) == 0
            assert driver.cnCtxSetCurrent(context) == 0
            replies.put(None)
            while True:
                command, n = commands.get()
                if command == 'alloc':
                    assert driver.cnMalloc(ctypes.byref(allocation), n * MIB) == 0
                elif command == 'free':
                    assert driver.cnFree(allocation) == 0
                elif command == 'reset':
                    assert driver.cnCtxDestroy(context) == 0
                    assert driver.cnCtxCreate(ctypes.byref(context), 1, 0) == 0
                    assert driver.cnCtxSetCurrent(context) == 0
                elif command == 'stop':
                    assert driver.cnCtxDestroy(context) == 0
                    replies.put(None)
                    return
                replies.put(None)
        except Exception as exc:
            replies.put(exc)
    workers = []
    for _ in range(3):
        commands, replies = queues.Queue(), queues.Queue()
        thread = threading.Thread(target=native_worker, args=(commands, replies), daemon=True)
        thread.start(); assert replies.get(timeout=5) is None
        workers.append((thread, commands, replies))
    def native(i, command, n=0):
        _, commands, replies = workers[i]
        commands.put((command, n)); result = replies.get(timeout=5)
        if result: raise result
    native(0, 'alloc', 40); native(1, 'alloc', 40)
    native_during = memory('native-during')
    native(0, 'free'); native(2, 'alloc', 20)
    native(1, 'free'); native(2, 'free')
    native_free = memory('native-free')
    for i in range(3): native(i, 'reset')
    native_reset = memory('native-reset')
    for i in range(3): native(i, 'stop')
    for thread, _, _ in workers: thread.join(timeout=5)
    native_destroy = memory('native-destroy')
    assert native_during >= native_before + 72
    assert native_destroy <= native_before + 4
    record('native-CNDrv-memory-free-context-destroy-reference', before=native_before,
           allocated=native_during, after_free=native_free, after_reset=native_reset,
           after_worker_exit=native_destroy)

    for i, (name, value) in enumerate([
        ('MLU_REMOTE_SESSION_DEVICE_BYTES', '0'), ('MLU_REMOTE_TOTAL_DEVICE_BYTES', '-1'),
        ('MLU_REMOTE_SESSION_IMAGE_BYTES', '18446744073709551616'),
        ('MLU_REMOTE_TOTAL_STAGING_BYTES', '1 '), ('MLU_REMOTE_MAX_CONNECTIONS', '1025'),
        ('MLU_REMOTE_SESSION_MAX_TRANSFERS', '1025'),
    ]):
        env = dict(os.environ, **settings)
        env[name] = value
        r = subprocess.run([str(ROOT / 'build/mlu-remote-server')], cwd=ROOT, env=env,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=3, text=True)
        (out / f'invalid-{i}.log').write_text(r.stdout)
        assert r.returncode == 1 and f'invalid {name}' in r.stdout
    record('invalid-resource-settings-fail-before-listen', cases=6)

    with server('quotas') as (proc, path):
        a, b, c = Session(), Session(), Session()
        baseline = memory('before')
        aa = allocate(a, 40 * MIB)
        assert a.rpc(3, idarg(32 * MIB))[0] == LIMIT
        bb = allocate(b, 40 * MIB)
        during = memory('during')
        assert during >= baseline + 72, (baseline, during)
        assert c.rpc(3, idarg(20 * MIB))[0] == LIMIT
        assert a.rpc(3, idarg(2**64 - 1))[0] == LIMIT
        assert b.rpc(4, idarg(aa))[0] == 100014
        a.ok(4, idarg(aa))
        cc = allocate(c, 20 * MIB)
        record('device-session-and-total-limits-other-session-continues', native_mib_before=baseline, native_mib_during=during)
        b.ok(4, idarg(bb)); c.ok(4, idarg(cc))
        after = memory('after-free')
        # Observe free/reset separately; native worker lifetime can retain
        # physical memory. Assert reclamation after workers leave below.
        a.ok(18); b.ok(18); c.ok(18)
        after_reset = memory('after-reset')
        record('native-memory-free-versus-context-reset', after_free=after, after_reset=after_reset)

        # Four retained completions fill the quota even if native work finished.
        q = queue(a)
        mid, writer = load(a)
        tids = [struct.unpack('!Q', a.ok(27, launch(mid, writer, q, [integer(seed)])))[0]
                for seed in (11, 12, 13, 14)]
        assert a.rpc(27, launch(mid, writer, q, [integer(99)]))[0] == LIMIT
        a.ok(8, idarg(q))
        for tid in tids:
            release(a, tid)
        reader, = struct.unpack('!Q', a.ok(29, idarg(mid) + struct.pack('!I', len(READ)) + READ))
        dst = allocate(a, 32)
        tid, = struct.unpack('!Q', a.ok(27, launch(mid, reader, q, [ptr(dst), integer(0)])))
        a.ok(8, idarg(q)); release(a, tid)
        assert struct.unpack('<4i', readback(a, dst, 16)) == (14, 15, 16, 17)
        record('transfer-kernel-quota-rejects-before-native-state-change')

        tids = [struct.unpack('!Q', a.ok(5, copy_payload(dst, struct.pack('<i', n))))[0]
                for n in range(4)]
        assert a.rpc(5, copy_payload(dst, struct.pack('<i', 99)))[0] == LIMIT
        for tid in tids:
            release(a, tid)
        assert struct.unpack('<i', readback(a, dst, 4))[0] == 3
        record('transfer-copy-quota-rejects-before-memory-change-and-recovers')

        assert a.rpc(5, copy_payload(dst, bytes(17), q))[0] == LIMIT
        tid, = struct.unpack('!Q', a.ok(5, copy_payload(dst, b'1234567890abcdef', q)))
        a.ok(8, idarg(q)); release(a, tid)
        assert readback(a, dst, 16) == b'1234567890abcdef'
        record('staging-byte-quota-recovers-after-queue-completion')

        # A malformed image must return its reservation, repeatedly.
        for _ in range(12):
            assert a.rpc(26, load_payload(b'bad-image'))[0] == 0x80000005
        mid2, _ = load(a)
        assert a.rpc(26, load_payload())[0] == LIMIT
        bmid, _ = load(b)
        assert c.rpc(26, load_payload())[0] == LIMIT
        a.ok(25, idarg(mid2))
        cmid, _ = load(c)
        record('image-session-total-quotas-malformed-rollback-and-unload-recovery')
        a.ok(18)  # reset releases device, staging, image and retained transfers
        mid3, _ = load(a)
        assert mid3 != mid
        a.ok(18); b.ok(18); c.ok(18)
        record('session-reset-returns-all-quotas-with-stale-identity-rejection')
        assert a.rpc(25, idarg(mid))[0] == 100014

        allocate(a, 60 * MIB)
        a.s.close()
        def recovered():
            status, data = b.rpc(3, idarg(60 * MIB))
            if status == LIMIT:
                return False
            assert status == 0
            b.ok(4, data)
            return True
        wait_for(recovered)
        record('abrupt-EOF-releases-global-quota-for-another-session')
        b.s.close(); c.s.close()
        wait_for(lambda: path.read_text().count('SESSION_CLEANUP_EMPTY') >= 3)
    after_exit = memory('after-quota-server-exit')
    assert after_exit <= baseline + 4, (baseline, after_exit)
    record('native-physical-memory-reclaimed-after-server-workers-exit', after=after_exit)

    with server('concurrent-reservations') as (proc, path):
        sessions = [Session() for _ in range(4)]
        barrier = threading.Barrier(5)
        release_allocations = threading.Event()
        statuses = []
        def compete(s):
            status, data = s.rpc(3, idarg(40 * MIB))
            statuses.append(status)
            barrier.wait(timeout=5)
            assert release_allocations.wait(timeout=5)
            if not status: s.ok(4, data)
        with ThreadPoolExecutor(max_workers=4) as pool:
            futures = [pool.submit(compete, s) for s in sessions]
            try:
                barrier.wait(timeout=5)
                assert sorted(statuses) == [0, 0, LIMIT, LIMIT], statuses
            finally:
                release_allocations.set()
            for future in futures: future.result(timeout=5)
        for s in sessions:
            s.ok(4, idarg(allocate(s, 40 * MIB))); s.s.close()
        record('concurrent-global-reservations-never-overcommit-and-recover', statuses=statuses)

    with server('staging-total', MLU_REMOTE_SESSION_IMAGE_BYTES=str(16 * MIB),
                MLU_REMOTE_TOTAL_IMAGE_BYTES=str(32 * MIB)) as (proc, path):
        a, b = Session(), Session()
        qa, qb, aa, bb = queue(a), queue(b), allocate(a, 32), allocate(b, 32)
        ma, ka = slow_module(a); mb, kb = slow_module(b)
        ta = slow_work(a, ma, ka, qa, aa)
        ca, = struct.unpack('!Q', a.ok(5, copy_payload(aa, b'ABCDEFGHIJKLMNOP', qa)))
        tb = slow_work(b, mb, kb, qb, bb)
        assert b.rpc(5, copy_payload(bb, b'abcdefghijklmnop', qb))[0] == LIMIT
        # The first queue still owns staging; a failed second reservation must
        # neither release it nor consume quota. Sync makes a later try valid.
        a.ok(8, idarg(qa)); release(a, ta); release(a, ca)
        cb, = struct.unpack('!Q', b.ok(5, copy_payload(bb, b'abcdefghijklmnop', qb)))
        b.ok(8, idarg(qb)); release(b, tb); release(b, cb)
        assert readback(a, aa, 16) == b'ABCDEFGHIJKLMNOP'
        assert readback(b, bb, 16) == b'abcdefghijklmnop'
        a.s.close(); b.s.close()
        record('global-staging-limit-retains-pending-buffer-and-recovers')

    before_drain = memory('before-pending-drain')
    with server('pending-drain', MLU_REMOTE_SESSION_IMAGE_BYTES=str(16 * MIB)) as (proc, path):
        a = Session()
        dst, q = allocate(a, 32), queue(a)
        mid, kid = slow_module(a)
        slow_work(a, mid, kid, q, dst, iterations=1000000)
        a.ok(5, copy_payload(dst, b'0123456789ABCDEF', q))
        assert a.rpc(9, idarg(q))[0] == 1
        started = time.monotonic()
        proc.send_signal(signal.SIGTERM)
        proc.wait(timeout=10)
        elapsed = time.monotonic() - started
        assert proc.returncode == 0
        a.s.close()
        record('SIGTERM-drains-confirmed-native-pending-kernel-and-H2D', seconds=round(elapsed, 4),
               queue_status_before_signal=1)
    after_drain = memory('after-pending-drain')
    assert after_drain <= before_drain + 4, (before_drain, after_drain)
    record('pending-drain-native-physical-memory-reclaimed', before=before_drain, after=after_drain)

    with server('admission', MLU_REMOTE_MAX_CONNECTIONS='3') as (proc, path):
        a, b = Session(), Session()
        pending = socket.create_connection((wire.HOST, wire.PORT), timeout=2)
        wait_for(lambda: path.read_text().count('session start') == 3)
        extra = socket.create_connection((wire.HOST, wire.PORT), timeout=2)
        assert extra.recv(1) == b''
        extra.close()
        a.ok(4, idarg(allocate(a, 16)))
        pending.close()
        wait_for(lambda: 'SESSION_NO_NATIVE_CONTEXT' in path.read_text())
        c = Session()
        c.ok(4, idarg(allocate(c, 16)))
        record('connection-admission-includes-pre-HELLO-and-recovers')
        # Leave one partial frame, one idle native session, and one blocked
        # D2H response; shutdown must wake each without waiting for I/O timers.
        c.s.sendall(struct.pack('!IHHQII', wire.MAGIC, 1, 3, c.seq + 1, 8, 0)[:7])
        allocation = allocate(b, 8 * MIB)
        b.s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
        b.seq += 1
        payload = struct.pack('!IQQQQQQI', 2, 0, 0, 0, allocation, 0, 8 * MIB, 0)
        b.s.sendall(struct.pack('!IHHQII', wire.MAGIC, 1, 5, b.seq, len(payload), 0) + payload)
        time.sleep(.10)
        started = time.monotonic()
        proc.send_signal(signal.SIGTERM)
        proc.wait(timeout=5)
        elapsed = time.monotonic() - started
        assert elapsed < 3 and proc.returncode == 0
        for s in (a, b, c): s.s.close()
        record('SIGTERM-drains-idle-partial-frame-backpressure-workers', seconds=round(elapsed, 4))

    # Repeated startup/EOF/shutdown catches descriptor reuse and lost wakeups.
    for i in range(6):
        with server(f'drain-{i}') as (proc, path):
            a = Session()
            a.ok(4, idarg(allocate(a, 64)))
            a.s.close()
            raw = socket.create_connection((wire.HOST, wire.PORT), timeout=2)
            proc.send_signal(signal.SIGINT)
            proc.wait(timeout=5)
            raw.close()
    record('repeated-SIGINT-drain-and-descriptor-reuse', cycles=6)

    with socket.socket() as reserved:
        reserved.bind(('127.0.0.1', 0)); wire.PORT = reserved.getsockname()[1]
    path = out / 'failed-context-cleanup.log'
    env = dict(os.environ, **settings)
    env.update(MLU_REMOTE_PORT=str(wire.PORT), MLU_REMOTE_BIND='127.0.0.1',
               LD_PRELOAD=str(ROOT / 'build/libtest-fail-cleanup.so'))
    with path.open('w') as log:
        proc = subprocess.Popen([str(ROOT / 'build/mlu-remote-server')], cwd=ROOT,
                                env=env, stdout=log, stderr=subprocess.STDOUT)
        try:
            wait_for(lambda: 'listening' in path.read_text() or proc.poll() is not None)
            assert proc.poll() is None
            a = Session()
            allocate(a, 64); queue(a)
            a.s.close()
            # A native context that could not be destroyed must close admission
            # and return a failing exit status, without another signal from us.
            proc.wait(timeout=5)
            text = path.read_text()
            assert proc.returncode == 1, text
            assert 'CLEANUP_ERROR op=cnCtxDestroy' in text and 'CLEANUP_QUARANTINED' in text
            assert 'SESSION_CLEANUP_FAILED' in text
            assert 'DRAIN_COMPLETE workers=0 cleanup_errors=1' in text
            record('injected-context-cleanup-failure-stops-admission-and-exits-nonzero')
        finally:
            if proc.poll() is None: proc.kill(); proc.wait(timeout=5)
finally:
    (out / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
