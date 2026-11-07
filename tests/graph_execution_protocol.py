#!/usr/bin/env python3
"""Graph execution wire bounds and cross-session resource identities."""
import struct
from mlur_wire import Session, idarg

def graph(s):
    return struct.unpack('!QI',s.ok(80,struct.pack('!IIQQ',1,0,0,0)))[0]

def fill(g,a,offset=0,count=0):
    return struct.pack('!I6Q2I3Q',1,g,0,0,a,offset,0,17,1,64,1,count)

def entity(s,action,e=0,g=0,q=0,capacity=0):
    data=s.ok(84,struct.pack('!I4Q',action,e,g,q,capacity))
    assert len(data)==24+capacity
    return struct.unpack('!IQQI',data[:24])

def reject(s,op,body):
    status,data=s.rpc(op,body)
    assert status and not data,(op,status,data)

a,b=Session(),Session()
try:
    ga,gb=graph(a),graph(b)
    alloc=struct.unpack('!Q',a.ok(3,idarg(64)))[0]
    reject(b,83,fill(gb,alloc))
    reject(a,83,fill(ga,alloc,offset=1))
    for body in (b'',fill(ga,alloc,count=1),fill(ga,alloc,count=4097),fill(ga,alloc)+idarg(0)):
        reject(a,83,body)
    a.ok(83,fill(ga,alloc))
    st,ex,_,_=entity(a,1,g=ga); assert not st and ex
    assert entity(b,3,e=ex)[0]
    bad_queue=struct.unpack('!Q',b.ok(6))[0]
    assert entity(a,3,e=ex,q=bad_queue)[0]
    reject(a,84,struct.pack('!I4Q',1,0,ga,0,65537))
    for body in (b'',struct.pack('!I5Q',1,ga,0,gb,0,0),struct.pack('!I5Q',1,ga,0,ga,0,4097)):
        reject(a,85,body)
    child=struct.unpack('!Q',a.ok(85,struct.pack('!I5Q',1,ga,0,graph(a),0,0)))[0]
    embedded=struct.unpack('!Q',a.ok(85,struct.pack('!I5Q',2,0,child,0,0,0)))[0]
    reject(a,80,struct.pack('!IIQQ',2,0,embedded,0))
    a.ok(18)
    assert entity(a,3,e=ex)[0]
    reject(a,85,struct.pack('!I5Q',2,0,child,0,0,0))
    print('PASS typed graph payload bounds, entity/allocation/queue ownership, borrowed child ownership and reset retirement')
finally:
    a.s.close(); b.s.close()
