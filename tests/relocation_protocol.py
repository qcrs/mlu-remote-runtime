#!/usr/bin/env python3
"""Reject malformed and foreign pointer relocations before native launch."""
import pathlib
import struct
from mlur_wire import Session, idarg, launch

ROOT = pathlib.Path(__file__).resolve().parents[1]
a, b = Session(), Session()
try:
    image = (ROOT / 'build/relocation-audit/native_bang_relocations-bang-mlisa-cambricon-bang.cnfatbin').read_bytes()
    m, = struct.unpack('!Q', a.ok(30, struct.pack('!I', len(image)) + image))
    name = b'relocated'
    k, = struct.unpack('!Q', a.ok(29, struct.pack('!QI', m, len(name)) + name))
    allocation, = struct.unpack('!Q', a.ok(3, idarg(64)))
    alien, = struct.unpack('!Q', b.ok(3, idarg(64)))
    fields = [(8, 0, allocation, 4), (16, 0, 0, 0), (32, 0, allocation, 12),
              (40, 0, allocation, 8), (56, 0, allocation, 4)]

    def argument(entries=fields, raw=bytes(72), kind=3):
        return struct.pack('!IIQQQ', kind, 72, 0, 0, len(entries)) + raw + b''.join(
            struct.pack('!IIQQ', *entry) for entry in entries)

    def modified(index, value):
        entries = list(fields); entries[index] = value; return entries

    cases = [
        ('foreign-allocation', argument(modified(0, (8, 0, alien, 0))), 100006),
        ('allocation-range', argument(modified(0, (8, 0, allocation, 65))), 100006),
        ('null-offset', argument(modified(1, (16, 0, 0, 1))), 100006),
        ('field-range', argument(modified(4, (72, 0, allocation, 0))), 100006),
        ('unaligned-field', argument(modified(0, (9, 0, allocation, 0))), 100006),
        ('overlapping-field', argument(modified(1, fields[0])), 100006),
        ('unsorted-fields', argument([fields[1], fields[0], *fields[2:]]), 100006),
        ('reserved-field', argument(modified(0, (8, 1, allocation, 0))), 100006),
        ('pointer-bytes', argument(raw=bytes(8) + b'\x01' + bytes(63)), 100006),
        ('no-relocations', argument([]), 0x80000003),
        ('scalar-relocations', argument(kind=2), 0x80000003),
        ('too-many-relocations', argument(fields * 103), 0x80000003),
        ('truncated', argument()[:-1], 0x80000003),
    ]
    for label, arg, expected in cases:
        status, _ = a.rpc(27, launch(m, k, 0, [arg]))
        assert status == expected, (label, status, expected)
        print('PASS relocation', label)
    tid, = struct.unpack('!Q', a.ok(27, launch(m, k, 0, [argument()])))
    a.ok(8, idarg(0)); a.ok(28, idarg(tid))
    a.ok(25, idarg(m))
    status, _ = a.rpc(27, launch(m, k, 0, [argument()]))
    assert status == 100014
    print('PASS valid relocation after rejection and stale module')
finally:
    a.s.close(); b.s.close()
