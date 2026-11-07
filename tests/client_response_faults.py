#!/usr/bin/env python3
"""Fault injection into the real host-only client library, without an SDK.

Malformed success replies must never publish a handle or D2H data. An exchange
failure must close the socket and must not cause a later request or replay.
"""
import ctypes as c
import pathlib
import socket
import struct
import threading

ROOT = pathlib.Path(__file__).resolve().parents[1]
LIB = c.CDLL(str(ROOT / 'build/libremote_cnrt.so'))
U64 = c.c_uint64
U32 = c.c_uint32
PTR = c.c_void_p
MAGIC = 0x4D4C5552
HELLO = struct.pack('!IIII', 1, 1, 0x7fff, 0x08010001)


def bind(name, args, result=c.c_int):
    function = getattr(LIB, 'mlu_remote_' + name)
    function.argtypes, function.restype = args, result
    return function


connect = bind('connect', [c.c_char_p, c.c_uint16], PTR)
disconnect = bind('disconnect', [PTR], None)
count = bind('device_count', [PTR, c.POINTER(U32)])
alloc = bind('alloc', [PTR, U64, c.POINTER(U64)])
queue = bind('queue_create', [PTR, c.POINTER(U64)])
notifier = bind('notifier_create', [PTR, c.POINTER(U64)])
module = bind('module_load_preset', [PTR, c.c_char_p, c.POINTER(U64), c.POINTER(U64)])
image = bind('module_load_image', [PTR, PTR, c.c_size_t, c.c_char_p, c.POINTER(U64), c.POINTER(U64)])
binary = bind('module_load_binary', [PTR, PTR, c.c_size_t, c.POINTER(U64)])
context_sync = bind('context_sync', [PTR])
session_close = bind('session_close', [PTR])
kernel = bind('module_get_kernel', [PTR, U64, c.c_char_p, c.POINTER(U64)])
operator = bind('operator_create', [PTR, U32, c.POINTER(U64), c.POINTER(U64)])
generic = bind('kernel_launch_generic', [PTR, U64, U64, U64, U32, U32, U32, U32, PTR, U32, c.POINTER(U64)])
d2h = bind('memcpy_d2h', [PTR, PTR, U64, U64, U64, U64, c.POINTER(U64)])
h2d = bind('memcpy_h2d', [PTR, U64, U64, PTR, U64, U64, c.POINTER(U64)])
sync = bind('queue_sync', [PTR, U64])


def receive(sock, size):
    data = b''
    while len(data) < size:
        part = sock.recv(size - len(data))
        if not part:
            raise AssertionError('client closed before sending request')
        data += part
    return data


def request(sock):
    header = struct.unpack('!IHHQII', receive(sock, 24))
    assert header[0:2] == (MAGIC, 1)
    return header, receive(sock, header[4])


def reply(header, extra=b'', status=0, prefix=bytes(8)):
    body = prefix + extra
    return struct.pack('!IHHQII', MAGIC, 1, header[2], header[3], len(body), status) + body


def exercise(label, invoke, corrupt, hello_fault=False):
    errors = []
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0))
        listener.listen(1)
        listener.settimeout(3)

        def server():
            try:
                with listener.accept()[0] as sock:
                    sock.settimeout(3)
                    header, _ = request(sock)
                    if not hello_fault:
                        sock.sendall(reply(header, HELLO))
                        header, _ = request(sock)
                    sock.sendall(corrupt(header))
                    sock.shutdown(socket.SHUT_WR)
                    try:
                        assert sock.recv(1) == b'', 'poisoned client sent another request'
                    except ConnectionResetError:
                        pass  # Closing with unread corrupt bytes may send RST.
            except BaseException as error:
                errors.append(error)

        worker = threading.Thread(target=server, daemon=True)
        worker.start()
        ctx = connect(b'127.0.0.1', listener.getsockname()[1])
        if hello_fault:
            assert not ctx, label
        else:
            assert ctx, label
            try:
                assert invoke(ctx) != 0, label + ': malformed success accepted'
                sentinel = U32(0x12345678)
                assert count(ctx, c.byref(sentinel)) != 0, label + ': poisoned session reused'
                assert sentinel.value == 0x12345678
            finally:
                disconnect(ctx)
        worker.join(4)
        assert not worker.is_alive(), label + ': fake server did not terminate'
        if errors:
            raise errors[0]
    print('PASS', label)


def output_call(function, *args):
    def invoke(ctx):
        value = U64(0x12345678)
        status = function(ctx, *args, c.byref(value))
        assert value.value == 0x12345678
        return status
    return invoke


def two_outputs(function, *args):
    def invoke(ctx):
        mid, kid = U64(77), U64(88)
        status = function(ctx, *args, c.byref(mid), c.byref(kid))
        assert (mid.value, kid.value) == (77, 88)
        return status
    return invoke


for label, call in [
    ('allocation', output_call(alloc, 32)),
    ('queue', output_call(queue)),
    ('notifier', output_call(notifier)),
    ('preset-module', two_outputs(module, b'vector_add')),
    ('uploaded-module', two_outputs(image, c.c_char_p(b'elf'), 3, b'kernel')),
    ('binary-module', output_call(binary, c.c_char_p(b'elf'), 3)),
    ('module-kernel', output_call(kernel, 1, b'kernel')),
    ('operator', two_outputs(operator, 1)),
    ('generic-launch', output_call(generic, 1, 2, 3, 1, 1, 1, 1, None, 0)),
    ('h2d', output_call(h2d, 1, 0, c.c_char_p(b'abcd'), 4, 3)),
]:
    exercise(label + '-short-success', call, lambda h: reply(h, b'\0'))
    exercise(label + '-oversized-result', call, lambda h: reply(h, bytes(24)))


def invoke_d2h(ctx):
    buffer = c.create_string_buffer(b'KEEP', 4)
    tid = U64(77)
    status = d2h(ctx, buffer, 1, 0, 4, 3, c.byref(tid))
    assert buffer.raw == b'KEEP' and tid.value == 77
    return status


exercise('d2h-missing-bytes', invoke_d2h, lambda h: reply(h, struct.pack('!QQ', 1, 4) + b'xx'))
exercise('d2h-wrong-count', invoke_d2h, lambda h: reply(h, struct.pack('!QQ', 1, 3) + b'xxxx'))
exercise('sync-unexpected-result', lambda ctx: sync(ctx, 1), lambda h: reply(h, b'x'))
exercise('context-sync-unexpected-result', context_sync, lambda h: reply(h,b'x'))
exercise('session-close-unexpected-result', session_close, lambda h: reply(h,b'x'))
exercise('device-query-invalid-name-length', lambda ctx: count(ctx, c.byref(U32(99))),
         lambda h: reply(h, struct.pack('!IQI', 1, 32, 99)))
exercise('bad-error-prefix', lambda ctx: sync(ctx, 1),
         lambda h: reply(h, prefix=struct.pack('!II', 100, 0)))
exercise('missing-error-prefix', lambda ctx: sync(ctx, 1), lambda h: reply(h, prefix=b'x'))
exercise('wrong-response-sequence', lambda ctx: sync(ctx, 1),
         lambda h: reply((h[0], h[1], h[2], h[3] + 1), b''))
exercise('truncated-payload', lambda ctx: sync(ctx, 1), lambda h: reply(h)[:-1])
exercise('truncated-header', lambda ctx: sync(ctx, 1), lambda h: reply(h)[:10])
exercise('bad-hello-version', None, lambda h: reply(h, struct.pack('!IIII', 99, 1, 0, 0)), True)
exercise('short-hello', None, lambda h: reply(h, bytes(8)), True)

# A legitimate native API failure does not corrupt the protocol connection.
for caps in (0xfff, 0x7ff):
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0))
        listener.listen(1)
        listener.settimeout(3)
        healthy_errors = []

        def healthy_server():
            try:
                with listener.accept()[0] as sock:
                    sock.settimeout(3)
                    h, _ = request(sock)
                    sock.sendall(reply(h, struct.pack("!IIII", 1, 1, caps, 0x08010001)))
                    h, _ = request(sock)
                    assert h[2] == 8, "unsupported capability sent a lookup request"
                    sock.sendall(reply(h, status=1))  # Native NotReady, no poison.
                    h, _ = request(sock)
                    sock.sendall(reply(h, struct.pack('!IQI', 1, 32, 0)))
            except BaseException as error:
                healthy_errors.append(error)

        worker = threading.Thread(target=healthy_server, daemon=True)
        worker.start()
        ctx = connect(b'127.0.0.1', listener.getsockname()[1])
        assert ctx
        try:
            if not (caps & 0x800):
                kid = U64(77)
                assert kernel(ctx, 1, b"kernel", c.byref(kid)) & 0xffffffff == 0x80000005
                assert kid.value == 77
            assert sync(ctx, 1) == 1
            value = U32(0)
            assert count(ctx, c.byref(value)) == 0 and value.value == 1
        finally:
            disconnect(ctx)
        worker.join(4)
        assert not worker.is_alive()
        if healthy_errors:
            raise healthy_errors[0]
    print('PASS native error keeps healthy session usable' if caps & 0x800 else 'PASS old peer capability refuses lookup without sending or splitting module')

# Generated size_from response buffers validate exact length before publishing.
class FetchRequest(c.Structure):
    _fields_ = [('transfer', U64), ('bytes', U64)]
class FetchResponse(c.Structure):
    _fields_ = [('unused', c.c_ubyte), ('data', PTR)]
fetch = bind('api_transfer_fetch', [PTR, c.POINTER(FetchRequest), c.POINTER(FetchResponse)])
def invoke_fetch(ctx):
    data = c.create_string_buffer(b'KEEP', 4)
    response = FetchResponse(0, c.cast(data, PTR))
    st = fetch(ctx, c.byref(FetchRequest(1, 4)), c.byref(response))
    assert data.raw == b'KEEP'
    return st
exercise('generated-buffer-short', invoke_fetch, lambda h: reply(h, b'xx'))
exercise('generated-buffer-extra', invoke_fetch, lambda h: reply(h, b'xxxxx'))

# Deterministic completion race: a later same-queue result must wait for an
# earlier NotReady result, while another queue/destination can publish now.
async_d2h = bind('memcpy_d2h_async', [PTR, PTR, U64, U64, U64, U64, c.POINTER(U64)])
release_transfer = bind('transfer_release', [PTR, U64])
errors = []
with socket.socket() as listener:
    listener.bind(('127.0.0.1', 0)); listener.listen(1); listener.settimeout(3)
    def ordering_server():
        try:
            with listener.accept()[0] as sock:
                sock.settimeout(3)
                h, _ = request(sock); sock.sendall(reply(h, HELLO))
                for tid in (1, 2, 3):
                    h, data = request(sock); assert h[2] == 78
                    sock.sendall(reply(h, struct.pack('!Q', tid)))
                def respond(op, tid=None, data=b'', status=0):
                    h, body = request(sock); assert h[2] == op, (h[2], op)
                    if tid is not None: assert struct.unpack_from('!Q', body)[0] == tid
                    sock.sendall(reply(h, data, status))
                respond(8)
                respond(23, 1, status=1)
                # No query/fetch for transfer 2 until transfer 1 can publish.
                respond(23, 3); respond(79, 3, b'THRD'); respond(28, 3)
                respond(8)
                for tid, data in ((1, b'AAAA'), (2, b'BBBB')):
                    respond(23, tid); respond(79, tid, data); respond(28, tid)
                respond(78, data=struct.pack('!Q',4))
                respond(5, data=struct.pack('!QQ',5,4)+b'LAST')
                respond(23,4); respond(79,4,b'OLD!'); respond(28,4)
                assert sock.recv(1) == b''
        except BaseException as error: errors.append(error)
    worker = threading.Thread(target=ordering_server, daemon=True); worker.start()
    ctx = connect(b'127.0.0.1', listener.getsockname()[1]); assert ctx
    common = c.create_string_buffer(b'KEEP', 4); independent = c.create_string_buffer(b'KEEP', 4)
    try:
        for dst, q in ((common, 3), (common, 3), (independent, 4)):
            tid = U64(); assert not async_d2h(ctx, dst, 1, 0, 4, q, c.byref(tid))
            assert not release_transfer(ctx, tid.value)
        assert not sync(ctx, 99)
        assert common.raw == b'KEEP' and independent.raw == b'THRD'
        assert not sync(ctx, 3); assert common.raw == b'BBBB'
        tid = U64(); assert not async_d2h(ctx, common, 1, 0, 4, 3, c.byref(tid))
        assert not release_transfer(ctx, tid.value)
        assert not d2h(ctx, common, 1, 0, 4, 3, c.byref(tid))
        assert common.raw == b'LAST' and tid.value == 5
    finally: disconnect(ctx)
    worker.join(4); assert not worker.is_alive()
    if errors: raise errors[0]
print('PASS deferred D2H preserves queue write order, independent progress and newer synchronous copy output')
