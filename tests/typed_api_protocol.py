#!/usr/bin/env python3
"""Schema-generated wire-size plan + real resource/range rejection checks."""
import json, pathlib, struct
from mlur_wire import Session, idarg
ROOT=pathlib.Path(__file__).resolve().parents[1]
plan=json.loads((ROOT/'build/generated/typed_test_plan.json').read_text())
schema=json.loads((ROOT/'schema/mlu_api_schema.json').read_text())
ops={r['name']:r['opcode'] for r in schema['typed_rpc']}
a,b=Session(),Session()
try:
    for r in plan:
        sizes=[r['request_bytes']+1]
        if r['request_bytes']: sizes.append(r['request_bytes']-1)
        for size in sizes:
            status,body=a.rpc(r['opcode'],bytes(size))
            assert status==0x80000003 and not body,(r,status)
        print('PASS generated malformed frame',r['name'])
    alloc,=struct.unpack('!Q',a.ok(3,idarg(128)))
    other,=struct.unpack('!Q',b.ok(3,idarg(128)))
    # memset fields: allocation,offset,count,value,width,queue,async
    def fill(id=alloc,off=0,count=128,q=0,width=1):
        return struct.pack('!QQQIIQI',id,off,count,0x7b,width,q,0)
    for label,payload in [('cross-session',fill(id=other)),('overflow',fill(count=(1<<64)-1,width=4)),('offset',fill(off=127,count=2)),('alignment',fill(off=1,count=1,width=4)),('width',fill(width=3)),('wrong-queue',fill(q=alloc))]:
        status,body=a.rpc(ops['memset'],payload); assert status and not body,(label,status)
        print('PASS rejected',label,status)
    a.ok(ops['memset'],fill())
    # 2D/3D invalid spans are rejected before native submission.
    status,_=a.rpc(ops['copy2d'],struct.pack('!8Q',alloc,0,16,alloc,0,16,8,(1<<64)-1)); assert status
    for label,depth,dx in [('depth-overflow',(1<<64)-1,0),('row-span',1,15)]:
        payload=struct.pack('!17Q',alloc,0,16,4,dx,0,0,alloc,0,16,4,0,0,0,8,2,depth)
        status,body=a.rpc(ops['copy3d'],payload)
        assert status and not body,(label,status)
        print('PASS rejected copy3d',label,status)
    a.ok(4,idarg(alloc)); status,_=a.rpc(ops['memset'],fill()); assert status
    print('PASS valid fill after rejection and stale allocation')
finally:
    a.s.close(); b.s.close()
