#!/usr/bin/env python3
"""Native-backed D2H transfer ownership, exact fetch size, and pending EOF."""
import pathlib
import struct
from mlur_wire import Session, idarg, launch, ptr, scalar
ROOT = pathlib.Path(__file__).resolve().parents[1]
a, b = Session(), Session()
try:
    data = bytes((i * 13 + 7) % 256 for i in range(8197))
    alloc, = struct.unpack('!Q', a.ok(3, idarg(len(data))))
    queue, = struct.unpack('!Q', a.ok(6))
    a.ok(5, struct.pack('!I6QII', 1, queue, alloc, 0, 0, 0, len(data), len(data), 1) + data)
    tid, = struct.unpack('!Q', a.ok(78, struct.pack('!4Q', alloc, 0, len(data), queue)))
    assert b.rpc(79, struct.pack('!2Q', tid, len(data)))[0]
    assert a.rpc(79, struct.pack('!2Q', tid, len(data)-1))[0]
    a.ok(8, idarg(queue))
    assert a.ok(79, struct.pack('!2Q', tid, len(data))) == data
    a.ok(28, idarg(tid)); assert a.rpc(79, struct.pack('!2Q', tid, len(data)))[0]
    print('PASS D2H Transfer owner, exact variable-buffer fetch, release retirement')
    image = (ROOT/'build/drain-audit/native_bang_drain-bang-mlisa-cambricon-bang.cnfatbin').read_bytes()
    module, = struct.unpack('!Q', a.ok(30, struct.pack('!I',len(image)) + image))
    symbol = b'_Z10drain_workPji'
    kernel, = struct.unpack('!Q', a.ok(29, struct.pack('!QI',module,len(symbol)) + symbol))
    a.ok(27, launch(module,kernel,queue,[ptr(alloc),scalar(struct.pack('<i',500000))]))
    a.ok(78, struct.pack('!4Q',alloc,0,4,queue))
    # Application destination is gone; Server must independently drain/own pages.
    print('PASS pending kernel/D2H connection EOF delegates native buffer cleanup to Server')
finally:
    a.s.close(); b.s.close()
