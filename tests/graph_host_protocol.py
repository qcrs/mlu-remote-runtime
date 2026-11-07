#!/usr/bin/env python3
"""Host graph descriptor integrity, stale identities and quota admission."""
import struct
import sys
from mlur_wire import Session, idarg

def reject(s,op,body):
    status,data=s.rpc(op,body)
    assert status and not data,(op,status,data)

def graph(s): return struct.unpack('!QI',s.ok(80,struct.pack('!IIQQ',1,0,0,0)))[0]
def copy(s,g,a,token,kind,node=0,e=0,action=1,deps=(),offset=0):
    dst,doff,src,soff=(a,offset,0,token) if kind==1 else (0,token,a,offset)
    data=s.ok(86,struct.pack('!I8QIQ',action,g,node,e,dst,doff,src,soff,64,2 if kind==1 else 3,len(deps))+b''.join(map(idarg,deps)))
    return struct.unpack_from('!Q',data)[0]
def instantiate(s,g):
    status,e,_,_=struct.unpack('!IQQI',s.ok(84,struct.pack('!I4Q',1,0,g,0,0)))
    assert not status
    return e

def host(e,action=1,data=b'',capacity=0,q=0):
    return struct.pack('!I4Q',action,e,q,len(data),capacity)+data

def plan(s,e):
    _,count,inputs,outputs=struct.unpack('!4Q',s.ok(89,host(e)))
    raw=s.ok(89,host(e,capacity=count*24))
    assert struct.unpack_from('!4Q',raw)[1:]==(count,inputs,outputs)
    return raw[32:]

a,b=Session(),Session()
try:
    g=graph(a);allocation=struct.unpack('!Q',a.ok(3,idarg(128)))[0]
    first=copy(a,g,allocation,0x10000,1);out=copy(a,g,allocation,0x20000,2,deps=(first,));e=instantiate(a,g)
    descriptor=plan(a,e);assert len(descriptor)==48
    payload=descriptor+bytes([37])*64
    if '--quota' in sys.argv:
        reject(a,89,host(e,2,payload))
        assert plan(a,e)==descriptor
        print('PASS Host invocation quota failure preserves descriptor and releases all temporary views')
    else:
        for body in (b'',host(e)[:-1],host(e)+b'x',host(e,3),host(e,1,b'x'),host(e,capacity=98305),host(e,capacity=47),host(e,2,payload[:-1]),host(e,2,payload+b'x'),host(e,2,payload,capacity=1)):
            reject(a,89,body)
        reject(b,89,host(e));reject(a,89,host(allocation));reject(a,89,host(e,q=allocation))
        foreign_q=struct.unpack('!Q',b.ok(6))[0];reject(a,89,host(e,q=foreign_q))
        # Raw entity invoke must never execute server placeholder Host pointers.
        raw=a.ok(84,struct.pack('!I4Q',3,e,0,0,0));assert struct.unpack_from('!I',raw)[0]
        for offset in (0,8,16,24,32,40):
            bad=bytearray(payload);bad[offset+7]^=1;reject(a,89,host(e,2,bad))
        copy(a,0,allocation,0x30000,2,node=out,e=e,action=4)
        reject(a,89,host(e,2,payload));descriptor=plan(a,e)
        reply=a.ok(89,host(e,2,descriptor+bytes([38])*64));transfer=struct.unpack_from('!Q',reply)[0]
        reject(b,23,idarg(transfer))
        a.ok(24,idarg(transfer))
        # API_TRANSFER_FETCH is looked up from the schema to avoid stale opcodes.
        import json,pathlib
        schema=json.loads((pathlib.Path(__file__).resolve().parents[1]/'schema/mlu_api_schema.json').read_text())
        fetch=schema['operations']['API_TRANSFER_FETCH']
        assert a.ok(fetch,struct.pack('!2Q',transfer,64))==bytes([38])*64
        reject(a,fetch,struct.pack('!2Q',transfer,63));a.ok(28,idarg(transfer));reject(a,fetch,struct.pack('!2Q',transfer,64))
        # Entity overrides can remove all Host endpoints from a Host revision.
        for node in (first,out):
            a.ok(86,struct.pack('!I8QIQ',4,0,node,e,allocation,0,allocation,64,64,0,0))
        assert plan(a,e)==b''
        transfer=struct.unpack_from('!Q',a.ok(89,host(e,2)))[0]
        a.ok(24,idarg(transfer));a.ok(28,idarg(transfer))
        # Symmetric update roles cannot justify a Host-affecting setter map.
        original,replacement=graph(a),graph(a)
        nodes=[copy(a,original,allocation,0x40000+i*0x10000,1,offset=i*64) for i in range(2)]
        for i in range(2):copy(a,replacement,allocation,0x60000+i*0x10000,1,offset=i*64)
        symmetric=instantiate(a,original)
        updated=a.ok(84,struct.pack('!I4Q',4,symmetric,replacement,0,0));assert struct.unpack('!IQQI',updated)[::3]==(0,0)
        descriptor=plan(a,symmetric);assert len(descriptor)==48
        status,data=a.rpc(86,struct.pack('!I8QIQ',4,0,nodes[0],symmetric,allocation,0,0,0x80000,64,2,0))
        assert status==0x80000005  # MLU_REMOTE_UNSUPPORTED
        assert not data and plan(a,symmetric)==descriptor
        transfer=struct.unpack_from('!Q',a.ok(89,host(symmetric,2,descriptor+bytes([39])*128)))[0]
        a.ok(24,idarg(transfer));a.ok(28,idarg(transfer))
        a.ok(18);reject(a,89,host(e))
        print('PASS Host descriptor lengths/staleness/ownership, opaque tokens, zero-Host overrides, ambiguous update setter rejection, replay and reset')
finally:
    a.s.close();b.s.close()
