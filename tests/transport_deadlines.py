#!/usr/bin/env python3
"""Real sockets and the SDK-free client: absolute deadlines and no replay.

No remote driver is needed here. The real MLU server has separate native-backed
handshake/receive/backpressure probes in server_transport.py.
"""
import ctypes as c
import errno
import os
import pathlib
import signal
import socket
import struct
import threading
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
LIB = c.CDLL(str(ROOT / 'build/libremote_cnrt.so'), use_errno=True)
TIMEOUT, PROTOCOL, BAD_LENGTH = 0x80000006, 0x80000001, 0x80000003
BUDGET = 200
os.environ['MLU_REMOTE_RPC_TIMEOUT_MS'] = str(BUDGET)
os.environ['MLU_REMOTE_CONNECT_TIMEOUT_MS'] = str(BUDGET)


class Error(c.Structure):
    _fields_ = [('code', c.c_uint32), ('name', c.c_char * 96), ('message', c.c_char * 256)]


def bind(name, args, result=c.c_int):
    f = getattr(LIB, 'mlu_remote_' + name)
    f.argtypes, f.restype = args, result
    return f


P, U64, U32 = c.c_void_p, c.c_uint64, c.c_uint32
connect = bind('connect', [c.c_char_p, c.c_uint16], P)
disconnect = bind('disconnect', [P], None)
last = bind('last_error', [P], c.POINTER(Error))
connect_error = bind('last_connect_error', [], c.POINTER(Error))
alloc = bind('alloc', [P, U64, c.POINTER(U64)])
count = bind('device_count', [P, c.POINTER(U32)])
h2d = bind('memcpy_h2d', [P, U64, U64, P, U64, U64, c.POINTER(U64)])


def receive(s, n):
    data = bytearray()
    while len(data) < n:
        part = s.recv(n - len(data))
        assert part, 'client EOF before complete request'
        data += part
    return bytes(data)


def request(s):
    header = struct.unpack('!IHHQII', receive(s, 24))
    return header, receive(s, header[4])


def reply(h, extra=b''):
    return struct.pack('!IHHQII', 0x4d4c5552, 1, h[2], h[3], 8 + len(extra), 0) + bytes(8) + extra


def bounded(elapsed):
    assert BUDGET / 1000 * .75 <= elapsed < 1.2, elapsed


def exercise(label, behavior, hello=False, sending=False):
    errors, done = [], threading.Event()
    with socket.socket() as listener:
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
        listener.bind(('127.0.0.1', 0))
        listener.listen(1)
        listener.settimeout(3)

        def peer():
            try:
                with listener.accept()[0] as s:
                    s.settimeout(3)
                    h, _ = request(s)
                    if not hello:
                        s.sendall(reply(h, struct.pack('!IIII', 1, 1, 0xfff, 0x08010001)))
                        if sending:
                            h = struct.unpack('!IHHQII', receive(s, 24))
                        else:
                            h, _ = request(s)
                    behavior(s, h, done)
                    if sending:
                        total = 0
                        while True:
                            part = s.recv(65536)
                            if not part:
                                break
                            total += len(part)
                        assert total < h[4], 'whole payload fit; send backpressure was not exercised'
                    else:
                        try:
                            assert s.recv(1) == b'', 'poisoned client sent a request/replay'
                        except ConnectionResetError:
                            pass
            except BaseException as error:
                errors.append(error)

        worker = threading.Thread(target=peer, daemon=True)
        worker.start()
        started = time.monotonic()
        ctx = connect(b'127.0.0.1', listener.getsockname()[1])
        if hello:
            elapsed = time.monotonic() - started
            assert not ctx
            assert connect_error().contents.code == TIMEOUT
            assert c.get_errno() == errno.ETIMEDOUT
        else:
            assert ctx
            try:
                value = U64(77)
                data = c.create_string_buffer(16 * 1024 * 1024) if sending else None
                started = time.monotonic()
                status = h2d(ctx, 1, 0, data, len(data), 0, c.byref(value)) if sending else alloc(ctx, 32, c.byref(value))
                elapsed = time.monotonic() - started
                assert status & 0xffffffff == TIMEOUT, (label, status)
                assert value.value == 77 and last(ctx).contents.name == b'REMOTE_TIMEOUT'
                sentinel = U32(99)
                assert count(ctx, c.byref(sentinel)) & 0xffffffff == PROTOCOL
                assert sentinel.value == 99 and last(ctx).contents.name == b'REMOTE_CONTEXT_POISONED'
            finally:
                disconnect(ctx)
        done.set()
        worker.join(4)
        assert not worker.is_alive(), label + ': peer did not terminate'
        if errors:
            raise errors[0]
        bounded(elapsed)
        print(f'PASS {label} elapsed={elapsed:.3f}s budget={BUDGET}ms', flush=True)


def blackhole(s, h, done):
    assert done.wait(2), 'client did not enforce deadline'


def partial_header(s, h, done):
    s.sendall(reply(h, bytes(8))[:7])
    blackhole(s, h, done)


def partial_body(s, h, done):
    s.sendall(reply(h, bytes(8))[:-1])
    blackhole(s, h, done)


def slow_drip(s, h, done):
    for byte in reply(h, bytes(8)):
        if done.wait(.04):
            break
        try:
            s.sendall(bytes([byte]))
        except (BrokenPipeError, ConnectionResetError):
            break


def combined_budget(s, h, done):
    if done.wait(.12):
        return
    s.sendall(reply(h, bytes(8))[:24])
    # A per-phase 200ms timer would renew here; the total budget must not.
    if done.wait(.12):
        return
    s.sendall(reply(h, bytes(8))[24:])


for key in ('MLU_REMOTE_CONNECT_TIMEOUT_MS', 'MLU_REMOTE_RPC_TIMEOUT_MS'):
    for value in ('0', '-1', '+1', ' 10', '', '1x', '3600001', '999999999999999999999'):
        os.environ[key] = value
        assert not connect(b'127.0.0.1', 1)
        assert c.get_errno() == errno.EINVAL
        assert connect_error().contents.code == BAD_LENGTH
    os.environ[key] = str(BUDGET)
print('PASS invalid timeout configuration refused before connecting', flush=True)

for hello in (True, False):
    for name, behavior in [('blackhole', blackhole), ('partial-header', partial_header),
                           ('partial-body', partial_body), ('slow-drip', slow_drip),
                           ('combined-header-body-budget', combined_budget)]:
        exercise(('hello-' if hello else 'rpc-') + name, behavior, hello=hello)
exercise('rpc-send-backpressure', blackhole, sending=True)

# A full local listen backlog provides a real TCP handshake that stays pending,
# without privileged firewall rules, unreliable Internet routes, or mock poll.
with socket.socket() as listener:
    listener.bind(('127.0.0.1', 0))
    listener.listen(0)
    endpoint = listener.getsockname()
    with socket.create_connection(endpoint, timeout=1):
        with socket.socket() as probe:
            probe.settimeout(.05)
            try:
                probe.connect(endpoint)
            except TimeoutError:
                pass
            else:
                raise AssertionError('loopback backlog did not saturate')
        started = time.monotonic()
        assert not connect(b'127.0.0.1', endpoint[1])
        elapsed = time.monotonic() - started
        assert c.get_errno() == errno.ETIMEDOUT
        assert connect_error().contents.name == b'REMOTE_CONNECT_TIMEOUT'
        bounded(elapsed)
        print(f'PASS TCP connect backlog deadline elapsed={elapsed:.3f}s', flush=True)

# Repeated signal interruptions must not renew poll's timeout.
signal.signal(signal.SIGALRM, lambda *_: None)
signal.setitimer(signal.ITIMER_REAL, .01, .01)
try:
    exercise('rpc-blackhole-EINTR', blackhole)
finally:
    signal.setitimer(signal.ITIMER_REAL, 0)
