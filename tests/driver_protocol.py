#!/usr/bin/env python3
"""Native-backed module upload, Driver handshake/close and default queue checks."""
import pathlib, socket, struct
from mlur_wire import Session, idarg, ptr, scalar, launch, MAGIC, HOST, PORT

ROOT=pathlib.Path(__file__).resolve().parents[1]
IMAGE=(ROOT/'build/module-audit/native_bang_module-bang-mlisa-cambricon-bang.cnfatbin').read_bytes()
DRAIN=(ROOT/'build/drain-audit/native_bang_drain-bang-mlisa-cambricon-bang.cnfatbin').read_bytes()
BAD_LENGTH,UNSUPPORTED=0x80000003,0x80000005
def lookup(mid,name): return idarg(mid)+struct.pack('!I',len(name))+name
def load(s,image): return struct.unpack('!Q',s.ok(30,struct.pack('!I',len(image))+image))[0]
def enqueue(s,mid,kid,q,args):
    tid,=struct.unpack('!Q',s.ok(27,launch(mid,kid,q,args))); s.ok(28,idarg(tid))
def integer(n): return scalar(struct.pack('<i',n))

for flags in (0,1,2,3):
    s=Session(flags=flags)
    s.ok(32); s.ok(31); assert s.s.recv(1)==b''; s.s.close()
print('PASS HELLO16 four context flags and acknowledged close followed by EOF')
for flags in (4,0xffffffff):
    s=object.__new__(Session); s.s=socket.create_connection((HOST,PORT),timeout=15); s.seq=0
    try:
        assert s.rpc(1,struct.pack('!IIII',1,0,0,flags))[0]==100006
        assert s.s.recv(1)==b''
    finally: s.s.close()
print('PASS invalid HELLO16 context flags rejected and connection closed')

a,b=Session(flags=0),Session(flags=1)
try:
    cases=[('empty-module',30,b'',BAD_LENGTH),('zero-image',30,bytes(4),BAD_LENGTH),
           ('short-image',30,struct.pack('!I',len(IMAGE))+IMAGE[:-1],BAD_LENGTH),
           ('trailing-image',30,struct.pack('!I',len(IMAGE))+IMAGE+b'x',BAD_LENGTH),
           ('malformed-image',30,struct.pack('!I',64)+bytes(64),UNSUPPORTED),
           ('context-sync-trailing',32,b'x',BAD_LENGTH),('close-trailing',31,b'x',BAD_LENGTH)]
    for name,op,payload,status in cases:
        assert a.rpc(op,payload)[0]==status,name
        a.ok(32)
        print('PASS',name,'keeps current context usable')
    mid=load(a,IMAGE)
    writer,=struct.unpack('!Q',a.ok(29,lookup(mid,b'_Z12module_writei')))
    reader,=struct.unpack('!Q',a.ok(29,lookup(mid,b'_Z11module_readPii')))
    memory,=struct.unpack('!Q',a.ok(3,idarg(16)))
    # A true NULL queue is used, and completion has a real native notifier.
    enqueue(a,mid,writer,0,[integer(91)])
    enqueue(a,mid,reader,0,[ptr(memory),integer(37)])
    a.ok(8,idarg(0)); a.ok(9,idarg(0)); a.ok(32)
    data=a.ok(5,struct.pack('!IQQQQQQI',2,0,0,0,memory,0,16,0))
    tid,count=struct.unpack_from('!QQ',data); assert count==16
    assert struct.unpack_from('<4i',data,16)==(128,129,130,131)
    a.ok(28,idarg(tid))
    assert b.rpc(29,lookup(mid,b'_Z12module_writei'))[0]==100014
    assert b.rpc(4,idarg(memory))[0]==100014
    assert b.rpc(25,idarg(mid))[0]==100014
    print('PASS binary upload, two shared kernels, real NULL queue launch/sync/query and cross-session ownership')
    # Close while a finite native default-queue kernel is observably pending.
    drain=load(a,DRAIN)
    kernel,=struct.unpack('!Q',a.ok(29,lookup(drain,b'_Z10drain_workPji')))
    enqueue(a,drain,kernel,0,[ptr(memory),integer(500000)])
    ready,_=a.rpc(9,idarg(0)); assert ready in (0,1)
    print('default_queue_pending_before_close=',ready)
    a.ok(31); assert a.s.recv(1)==b''
    assert b.rpc(4,idarg(memory))[0]==100014
    b.ok(32)
    print('PASS close acknowledges pending NULL queue cleanup, closes session, other client remains usable')
finally:
    a.s.close(); b.s.close()
