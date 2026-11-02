#!/usr/bin/env python3
"""Adversarial generic-module/launch checks through real TCP and CNDrv."""
import os, pathlib, socket, struct
ROOT = pathlib.Path(__file__).resolve().parents[1]
HOST = os.environ.get('MLU_REMOTE_HOST', '127.0.0.1')
PORT = int(os.environ.get('MLU_REMOTE_PORT', '50051'))
MAGIC = 0x4D4C5552

from mlur_wire import Session, idarg, ptr, scalar, launch

a, b = Session(), Session()
try:
    image = (ROOT / 'build/audit/native_bang_abi-bang-mlisa-cambricon-bang.cnfatbin').read_bytes()
    name = b'_Z10vector_addPfPKfS1_i'
    payload = struct.pack('!II', len(image), len(name)) + image + name
    m, k = struct.unpack('!QQ', a.ok(26, payload))
    q, = struct.unpack('!Q', a.ok(6))
    alloc, = struct.unpack('!Q', a.ok(3, idarg(32)))
    other, = struct.unpack('!Q', b.ok(3, idarg(32)))
    args = [ptr(alloc), ptr(alloc), ptr(alloc), scalar(struct.pack('<i', 1))]
    cases = [
        ('arg-count', a, 27, launch(m, k, q, args[:3]), 100006),
        ('arg-size', a, 27, launch(m, k, q, args[:3]+[scalar(bytes(8), 8)]), 100006),
        ('cross-module', b, 27, launch(m, k, q, args), 100014),
        ('wrong-kernel', a, 27, launch(m, alloc, q, args), 100014),
        ('cross-allocation', a, 27, launch(m, k, q, [ptr(other)]+args[1:]), 100006),
        ('bad-offset', a, 27, launch(m, k, q, [ptr(alloc, 33)]+args[1:]), 100006),
        ('zero-dim', a, 27, launch(m, k, q, args, (0, 1, 1)), 100006),
        ('bad-class', a, 27, launch(m, k, q, args, klass=999), 100006),
        ('trailing-bytes', a, 27, launch(m, k, q, args)+b'x', 0x80000003),
        ('bad-image', a, 26, struct.pack('!II', 64, len(name))+bytes(64)+name, 0x80000005),
    ]
    for label, session, op, data, expected in cases:
        st, _ = session.rpc(op, data)
        assert st == expected, (label, st, expected)
        print('PASS', label, st)
    # A rejected launch leaves the live module usable.
    tid, = struct.unpack('!Q', a.ok(27, launch(m, k, q, args)))
    a.ok(8, idarg(q)); a.ok(28, idarg(tid))
    a.ok(25, idarg(m))
    st, _ = a.rpc(27, launch(m, k, q, args)); assert st == 100014
    print('PASS valid launch after rejection and stale kernel after unload')
finally:
    a.s.close(); b.s.close()
