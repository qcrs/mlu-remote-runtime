#!/usr/bin/env python3
"""Native-backed parent/child module ownership and shared-state probe."""
import pathlib
import struct
from mlur_wire import Session, idarg, ptr, scalar, launch

ROOT = pathlib.Path(__file__).resolve().parents[1]
IMAGE = (ROOT / 'build/module-audit/native_bang_module-bang-mlisa-cambricon-bang.cnfatbin').read_bytes()
WRITE = b'_Z12module_writei'
READ = b'_Z11module_readPii'


def load(s):
    return struct.unpack('!QQ', s.ok(26, struct.pack('!II', len(IMAGE), len(WRITE)) + IMAGE + WRITE))


def lookup(s, mid, name):
    return idarg(mid) + struct.pack('!I', len(name)) + name


def get_reader(s, mid):
    return struct.unpack('!Q', s.ok(29, lookup(s, mid, READ)))[0]


def enqueue(s, mid, kid, q, args):
    tid, = struct.unpack('!Q', s.ok(27, launch(mid, kid, q, args)))
    s.ok(28, idarg(tid))


def integer(n):
    return scalar(struct.pack('<i', n))


def output(s, allocation, q):
    result = s.ok(5, struct.pack('!IQQQQQQI', 2, q, 0, 0, allocation, 0, 16, 0))
    tid, count = struct.unpack_from('!QQ', result)
    assert count == 16 and len(result) == 32
    s.ok(28, idarg(tid))
    return struct.unpack_from('<4i', result, 16)


a, b = Session(), Session()
try:
    q, = struct.unpack('!Q', a.ok(6))
    allocation, = struct.unpack('!Q', a.ok(3, idarg(16)))
    mid, writer = load(a)
    reader = get_reader(a, mid)
    assert writer != reader
    for _ in range(20):
        assert get_reader(a, mid) == reader
        assert struct.unpack('!Q', a.ok(29, lookup(a, mid, WRITE)))[0] == writer
    print('PASS one module has two stable child KernelIDs')

    second, writer2 = load(a)
    reader2 = get_reader(a, second)
    enqueue(a, mid, writer, q, [integer(11)])
    enqueue(a, second, writer2, q, [integer(29)])
    enqueue(a, mid, reader, q, [ptr(allocation), integer(37)])
    a.ok(8, idarg(q))
    assert output(a, allocation, q) == (48, 49, 50, 51)
    enqueue(a, second, reader2, q, [ptr(allocation), integer(37)])
    a.ok(8, idarg(q))
    assert output(a, allocation, q) == (66, 67, 68, 69)
    print('PASS kernels share parent state; separately loaded identical images keep independent state')

    cases = [
        ('cross-session-lookup', b, 29, lookup(b, mid, READ), 100014),
        ('wrong-parent-type', a, 29, lookup(a, allocation, READ), 100014),
        ('cross-parent-launch', a, 27, launch(second, reader, q, [ptr(allocation), integer(0)]), 100014),
        ('missing-symbol', a, 29, lookup(a, mid, b'absent_kernel'), 100301),
        ('empty-symbol', a, 29, lookup(a, mid, b''), 0x80000003),
        ('nul-symbol', a, 29, lookup(a, mid, READ + b'\x00'), 0x80000003),
        ('trailing-lookup', a, 29, lookup(a, mid, READ) + b'x', 0x80000003),
        ('short-lookup', a, 29, idarg(mid), 0x80000003),
    ]
    for label, s, op, payload, expected in cases:
        st, _ = s.rpc(op, payload)
        assert st == expected, (label, st, expected)
        print('PASS', label)

    # Unload with work submitted: its synchronization must preserve output and
    # invalidate every child handle, including when the registry slot is reused.
    enqueue(a, mid, writer, q, [integer(73)])
    enqueue(a, mid, reader, q, [ptr(allocation), integer(37)])
    a.ok(25, idarg(mid))
    assert output(a, allocation, q) == (110, 111, 112, 113)
    assert a.rpc(29, lookup(a, mid, READ))[0] == 100014
    assert a.rpc(27, launch(mid, writer, q, [integer(1)]))[0] == 100014
    assert a.rpc(27, launch(mid, reader, q, [ptr(allocation), integer(0)]))[0] == 100014
    third, writer3 = load(a)
    assert third != mid and writer3 != writer
    assert a.rpc(27, launch(third, reader, q, [ptr(allocation), integer(0)]))[0] == 100014
    print('PASS pending-work unload completes and invalidates all children without ID reuse')

    a.ok(18)
    assert a.rpc(29, lookup(a, second, READ))[0] == 100014
    assert a.rpc(29, lookup(a, third, WRITE))[0] == 100014
    print('PASS reset invalidates all module and child identities')
finally:
    a.s.close()
    b.s.close()
