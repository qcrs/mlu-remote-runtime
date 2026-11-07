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

def copy(g=0,node=0,entity=0,dst=0,doff=0,src=0,soff=0,width=32,direction=0,action=1,ids=(),count=None):
    return struct.pack('!I8QIQ',action,g,node,entity,dst,doff,src,soff,width,direction,
                       len(ids) if count is None else count)+b''.join(idarg(i) for i in ids)

def copy_get(s,node):
    data=s.ok(86,copy(node=node,action=2))
    assert len(data)==52
    return struct.unpack('!6QI',data)

def empty(s,g):
    return struct.unpack('!Q',s.ok(81,struct.pack('!IQQ',1,g,0)))[0]

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
    # Both endpoints, dependencies, node kinds and executable identities must
    # belong to this session; nonzero offsets are checked independently.
    src=struct.unpack('!Q',a.ok(3,idarg(64)))[0]
    foreign=struct.unpack('!Q',b.ok(3,idarg(64)))[0]
    local_other=graph(a); other_node=empty(a,local_other); foreign_node=empty(b,gb)
    valid=copy(g=ga,dst=alloc,src=src,doff=16,soff=8)
    for body in (b'',valid[:-1],valid+idarg(0),
                 copy(g=ga,dst=alloc,src=src,count=1),
                 copy(g=ga,dst=alloc,src=src,count=4097)):
        reject(a,86,body)
    for field,value in (('dst',foreign),('src',foreign),('dst',ga),('src',ga),
                        ('dst',0),('src',0),('doff',33),('soff',33),
                        ('doff',2**64-1),('soff',2**64-1),('width',0),('width',1048577)):
        kwargs=dict(g=ga,dst=alloc,src=src); kwargs[field]=value
        reject(a,86,copy(**kwargs))
    reject(b,86,valid)
    reject(a,86,copy(g=gb,dst=alloc,src=src))
    for dependency in (foreign_node,other_node,ga):
        reject(a,86,copy(g=ga,dst=alloc,src=src,ids=(dependency,)))
    node=struct.unpack('!6QI',a.ok(86,valid))[0]
    assert copy_get(a,node)[1:]==(alloc,16,src,8,32,0)
    same=struct.unpack('!6QI',a.ok(86,copy(g=ga,dst=alloc,doff=32,src=alloc,ids=(node,))))[0]
    assert copy_get(a,same)[1:]==(alloc,32,alloc,0,32,0)
    for action in (2,3,4):
        reject(a,86,copy(node=other_node,entity=ex,dst=alloc,src=src,action=action))
        reject(a,86,copy(node=foreign_node,entity=ex,dst=alloc,src=src,action=action))
        reject(a,86,copy(node=node,entity=ex,dst=alloc,src=src,action=action,ids=(node,)))
    # The original entity predates the copy node and cannot acquire it through
    # its setter. A new snapshot can, while a foreign entity cannot.
    reject(a,86,copy(node=node,entity=ex,dst=alloc,src=src,action=4))
    _,copy_entity,_,_=entity(a,1,g=ga)
    _,foreign_entity,_,_=entity(b,1,g=gb)
    reject(a,86,copy(node=node,entity=foreign_entity,dst=alloc,src=src,action=4))
    a.ok(86,copy(node=node,entity=copy_entity,dst=alloc,src=src,action=4))
    # Native rejects direction=1 on this SDK; a failed setter leaves getters
    # unchanged and must not replace either backing lease.
    reject(a,86,copy(node=node,dst=alloc,src=src,action=3,direction=1))
    assert copy_get(a,node)[1:]==(alloc,16,src,8,32,0)
    a.ok(4,idarg(src))
    assert copy_get(a,node)[1:]==(alloc,16,src,8,32,0)
    for action in (1,3,4):
        reject(a,86,copy(g=ga,node=node,entity=copy_entity,dst=alloc,src=src,action=action))
        reject(a,86,copy(g=ga,node=node,entity=copy_entity,dst=src,src=alloc,action=action))
    a.ok(4,idarg(alloc))
    assert copy_get(a,node)[1:]==(alloc,16,src,8,32,0)
    a.ok(18)
    assert entity(a,3,e=ex)[0]
    reject(a,85,struct.pack('!I5Q',2,0,child,0,0,0))
    reject(a,86,copy(node=node,action=2))
    fresh=graph(a)
    reject(a,86,copy(g=fresh,dst=alloc,src=src))
    print('PASS typed graph payload bounds, entity/allocation/queue ownership, borrowed child ownership and reset retirement')
    print('PASS memcpy exact wire lengths, both endpoint ranges/ownership, dependency parents, node/entity types, stale IDs and tombstone getters')
finally:
    a.s.close(); b.s.close()
