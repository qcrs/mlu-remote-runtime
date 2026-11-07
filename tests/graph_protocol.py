#!/usr/bin/env python3
"""Necessary Graph identity, parent, generation, and array transport checks."""
import struct
from mlur_wire import Session, idarg

ACTION, EDIT, QUERY = 80, 81, 82

def action(s, op, graph=0, node=0):
    return struct.unpack('!QI', s.ok(ACTION, struct.pack('!IIQQ', op, 0, graph, node)))[0]

def edit(s, graph, ids=(), op=1):
    return struct.unpack('!Q',s.ok(EDIT,struct.pack('!IQQ',op,graph,len(ids))+b''.join(idarg(i) for i in ids)))[0]

def query(s, graph, capacity=0, kind=1, has_array=0):
    return s.ok(QUERY,struct.pack('!IIQQQ',kind,has_array,graph,capacity,capacity*(2 if kind==2 else 1)))

def rejected(s, op, body):
    status, extra=s.rpc(op,body)
    assert status and not extra, (op,status,len(extra))

a,b=Session(),Session()
try:
    ga,gb=action(a,1),action(b,1)
    na,nb=edit(a,ga),edit(b,gb)
    assert ga!=gb and na!=nb and ga!=na
    for graph,node in [(gb,0),(ga,nb),(na,0)]:
        rejected(a,ACTION,struct.pack('!IIQQ',4 if node else 2,0,graph,node))
    rejected(a,EDIT,struct.pack('!IQQQ',1,ga,1,nb))
    other=action(a,1); node=edit(a,other)
    rejected(a,EDIT,struct.pack('!IQQQQ',2,ga,2,na,node))
    for body in [b'',struct.pack('!IQQ',1,ga,1),struct.pack('!IQQ',1,ga,65537),struct.pack('!IQQQ',1,ga,0,na)]:
        rejected(a,EDIT,body)
    rejected(a,QUERY,struct.pack('!IIQQQ',1,1,ga,65537,65537))
    rejected(a,QUERY,struct.pack('!IIQQQ',2,1,ga,2,2))
    count,*ids=struct.unpack('!4Q',query(a,ga,3,has_array=1))
    assert (count,ids)==(1,[na,0,0])
    assert query(a,ga,0,has_array=1)==idarg(0)
    clone=action(a,3,ga); cloned=action(a,4,clone,na)
    assert cloned not in (na,nb)
    action(a,2,ga)
    rejected(a,ACTION,struct.pack('!IIQQ',5,0,0,na))
    assert struct.unpack('!2Q',query(a,clone,1,has_array=1))==(1,cloned)
    a.ok(18)
    rejected(a,QUERY,struct.pack('!IIQQQ',1,0,clone,0,0))
    new=action(a,1); assert new not in (ga,gb,clone,other)
    action(a,2,new)
    b.ok(31); a.ok(31)
    print('PASS Graph ownership, clone identities, stale/reset IDs and bounded arrays')
finally:
    a.s.close(); b.s.close()
