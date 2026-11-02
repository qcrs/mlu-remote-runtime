#!/usr/bin/env python3
"""Native CNDrv-backed HELLO gating, frame deadlines and client isolation.

Starts only its own server on a loopback ephemeral port. Cleanup log assertions
check code paths, not an independent measurement of native resources freed.
"""
import argparse
import json
import os
import pathlib
import socket
import struct
import subprocess
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser()
p.add_argument('--out', required=True)
args = p.parse_args()
out = pathlib.Path(args.out).resolve()
out.mkdir(parents=True, exist_ok=True)
logpath = out / 'server.log'
results = []
MAGIC, PROTOCOL, BAD_VERSION, BAD_LENGTH, UNSUPPORTED = 0x4d4c5552, 0x80000001, 0x80000002, 0x80000003, 0x80000005
HELLO = struct.pack('!III', 1, 0, 0xffffffff)


def header(op=1, seq=1, n=12, version=1, status=0):
    return struct.pack('!IHHQII', MAGIC, version, op, seq, n, status)


def receive(s, n):
    data = bytearray()
    while len(data) < n:
        part = s.recv(n - len(data))
        assert part, 'incomplete server response'
        data += part
    return bytes(data)


def response(s):
    magic, version, op, seq, n, status = struct.unpack('!IHHQII', receive(s, 24))
    assert (magic, version) == (MAGIC, 1) and 8 <= n <= 67108864
    data = receive(s, n)
    nl, ml = struct.unpack_from('!II', data)
    assert 8 + nl + ml <= n
    return status, data[8 + nl + ml:], op, seq


def closed(s):
    try:
        assert s.recv(1) == b'', 'server did not close failed session'
    except ConnectionResetError:
        pass


def record(name, started=None):
    result = dict(name=name, passed=True)
    if started is not None:
        result['elapsed_seconds'] = round(time.monotonic() - started, 4)
    results.append(result)
    print('PASS', json.dumps(result), flush=True)


with socket.socket() as reserve:
    reserve.bind(('127.0.0.1', 0))
    port = reserve.getsockname()[1]


def connection():
    s = socket.socket()
    s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
    s.settimeout(2)
    s.connect(('127.0.0.1', port))
    return s


class Session:
    def __init__(self, legacy=False):
        self.s, self.seq = connection(), 0
        status, extra = self.rpc(1, HELLO[:8] if legacy else HELLO)
        assert status == 0 and len(extra) == 16

    def rpc(self, op, payload=b''):
        self.seq += 1
        self.s.sendall(header(op, self.seq, len(payload)) + payload)
        status, data, rop, seq = response(self.s)
        assert (rop, seq) == (op, self.seq)
        return status, data

    def ok(self, op, payload=b''):
        status, data = self.rpc(op, payload)
        assert not status, (op, status)
        return data


env = dict(os.environ, MLU_REMOTE_BIND='127.0.0.1', MLU_REMOTE_PORT=str(port),
           MLU_REMOTE_SERVER_HANDSHAKE_TIMEOUT_MS='250', MLU_REMOTE_SERVER_IO_TIMEOUT_MS='350')
with logpath.open('w') as log:
    server = subprocess.Popen([str(ROOT / 'build/mlu-remote-server')], cwd=ROOT, env=env,
                              stdout=log, stderr=subprocess.STDOUT)
    try:
        for _ in range(100):
            assert server.poll() is None, 'server failed to start'
            if 'listening' in logpath.read_text():
                break
            time.sleep(.02)
        else:
            raise AssertionError('server startup timeout')

        invalid = [
            ('first-ALLOC-before-HELLO', header(op=3, n=8), PROTOCOL),
            ('bad-HELLO-length', header(n=4), BAD_LENGTH),
            ('bad-frame-version', header(version=2), BAD_VERSION),
            ('nonzero-request-status', header(status=1), BAD_LENGTH),
            ('zero-sequence', header(seq=0), PROTOCOL),
            ('bad-HELLO-version', header() + struct.pack('!III', 99, 0, 0xffffffff), BAD_VERSION),
            ('unsupported-HELLO-flags', header() + struct.pack('!III', 1, 1, 0xffffffff), UNSUPPORTED),
            ('invalid-device-HELLO', header() + struct.pack('!III', 1, 0, 0xfffffffe), 100005),
        ]
        for name, packet, expected in invalid:
            with connection() as s:
                s.sendall(packet)
                status, _, _, _ = response(s)
                assert status == expected, (name, status, expected)
                closed(s)
            record(name)

        for name, first in [('handshake-blackhole', b''), ('handshake-partial-header', header()[:7]),
                            ('handshake-partial-body', header() + HELLO[:5])]:
            with connection() as s:
                started = time.monotonic()
                if first:
                    s.sendall(first)
                closed(s)
                elapsed = time.monotonic() - started
                assert .18 <= elapsed < 1.2, elapsed
            record(name, started)

        with connection() as s:
            started = time.monotonic()
            # Header completes at 160ms, body is still incomplete at 250ms.
            s.sendall(header()[:12])
            time.sleep(.16)
            s.sendall(header()[12:] + HELLO[:4])
            closed(s)
            elapsed = time.monotonic() - started
            assert .18 <= elapsed < .39, elapsed
        record('handshake-shares-header-body-budget', started)
        assert 'native context created' not in logpath.read_text(), 'invalid first frames created native context'
        record('no-native-context-before-valid-HELLO')

        legacy = Session(legacy=True)
        legacy.ok(2, struct.pack('!I', 0xffffffff))
        legacy.s.close()
        record('legacy-8-byte-HELLO-accepted')

        owner = Session()
        old = owner.ok(3, struct.pack('!Q', 64))
        status, _ = owner.rpc(1, HELLO)
        assert status == PROTOCOL
        closed(owner.s)
        owner.s.close()
        fresh = Session()
        assert fresh.rpc(4, old)[0] != 0
        fresh.ok(4, fresh.ok(3, struct.pack('!Q', 64)))
        fresh.s.close()
        record('duplicate-HELLO-closes-session-old-ID-rejected')

        for drip in (False, True):
            idle = Session()
            started = time.monotonic()
            if drip:
                idle.s.sendall(header(op=3, seq=2, n=8)[:12])
                time.sleep(.20)
                idle.s.sendall(header(op=3, seq=2, n=8)[12:] + bytes(4))
            closed(idle.s)
            elapsed = time.monotonic() - started
            assert .27 <= elapsed < .52, elapsed
            idle.s.close()
            record('ready-combined-header-body-budget' if drip else 'ready-idle-deadline', started)

        healthy = Session()
        for name in ('ready-partial-header', 'ready-partial-body'):
            bad = Session()
            bad.ok(3, struct.pack('!Q', 64))
            # ALLOC consumed sequence 2; the incomplete frame must use 3.
            packet = (header(op=3, seq=3, n=8)[:6] if 'header' in name
                      else header(op=3, seq=3, n=8) + bytes(2))
            started = time.monotonic()
            bad.s.sendall(packet)
            # Keep another native-backed session active through the failure.
            for _ in range(4):
                healthy.ok(4, healthy.ok(3, struct.pack('!Q', 64)))
                time.sleep(.10)
            closed(bad.s)
            bad.s.close()
            record(name + '-other-client-continues', started)

        slow = Session()
        allocation = slow.ok(3, struct.pack('!Q', 8 * 1024 * 1024))
        slow.seq += 1
        payload = struct.pack('!IQQQQQQI', 2, 0, 0, 0, struct.unpack('!Q', allocation)[0], 0, 8 * 1024 * 1024, 0)
        slow.s.sendall(header(5, slow.seq, len(payload)) + payload)
        started = time.monotonic()
        # Do not read a large D2H response until the server write deadline fires.
        for _ in range(7):
            healthy.ok(2, struct.pack('!I', 0xffffffff))
            time.sleep(.10)
        received = 0
        while True:
            try:
                part = slow.s.recv(65536)
            except ConnectionResetError:
                break
            if not part:
                break
            received += len(part)
        assert 0 < received < 8 * 1024 * 1024, received
        slow.s.close()
        healthy.s.close()
        text = logpath.read_text()
        assert 'response failed' in text and 'Connection timed out' in text
        assert 'phase=payload ready=1 errno=110' in text
        record('server-send-backpressure-other-client-continues', started)
        # Wait for these workers' cleanup paths before stopping our server.
        for _ in range(100):
            text = logpath.read_text()
            if text.count('SESSION_CLEANUP_EMPTY') >= 9:
                break
            time.sleep(.02)
        assert text.count('SESSION_CLEANUP_EMPTY') >= 9
        record('native-cleanup-paths-returned-not-resource-zero-measurement')
    finally:
        server.terminate()
        server.wait(timeout=5)
        (out / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
