#!/usr/bin/env python3
"""Graph argument wire, resource ownership and retired getter identities."""
import pathlib
import struct
import sys
from mlur_wire import Session, idarg, ptr, scalar

ROOT=pathlib.Path(__file__).resolve().parents[1]
IMAGE=(ROOT/'build/graph-kernel-driver/native_graph_kernel-bang-mlisa-cambricon-bang.cnfatbin').read_bytes()
NAME=b'graph_relocated'

def graph(s):
    return struct.unpack('!QI',s.ok(80,struct.pack('!IIQQ',1,0,0,0)))[0]

def load(s):
    return struct.unpack('!QQ',s.ok(26,struct.pack('!II',len(IMAGE),len(NAME))+IMAGE+NAME))

def reject(s,op,body):
    st,data=s.rpc(op,body); assert st and not data,(op,st,data)

def aggregate(a,offsets=(0,8,24,32),dirty=False):
    data=bytearray(56)
    if dirty: data[0]=1
    return struct.pack('!IIQQII',3,56,0,0,len(offsets),0)+data+b''.join(
        struct.pack('!IIQQ',off,0,a if i in (0,2) else 0,4 if i in (0,2) else 0)
        for i,off in enumerate(offsets))

def request(g=0,n=0,e=0,m=0,k=0,action=1,args=(),deps=(),count=None,length=None,capacity=0):
    data=b''.join(idarg(d) for d in deps)+b''.join(args)
    return struct.pack('!I5Q6I3Q',action,g,n,e,m,k,1,1,1,1,0,len(args),
                       len(deps) if count is None else count,len(data) if length is None else length,capacity)+data

def get(s,node,capacity=0):
    data=s.ok(87,request(n=node,action=2,capacity=capacity))
    assert len(data)==1072+capacity
    head=struct.unpack('!QQ6IQ',data[:48])
    return head,data[1072:1072+head[-1]]

def attr(node,action=1,src=0,allocation=0):
    return struct.pack('!IQQIQQQfII',action,node,src,1,allocation,0,0,0.,0,0)

if '--quota' in sys.argv:
    s=Session()
    try:
        g=graph(s);m,k=load(s);allocation=struct.unpack('!Q',s.ok(3,idarg(64)))[0]
        args=(aggregate(allocation),scalar(struct.pack('<i',10)),ptr(allocation,16))
        st,data=s.rpc(87,request(g=g,m=m,k=k,args=args))
        assert st==0x80000007 and not data,(st,data)
        # Rejection leaves a healthy session and an empty graph.
        assert struct.unpack('!Q',s.ok(82,struct.pack('!IIQQQ',1,0,g,0,0))[:8])[0]==0
        print('PASS persistent graph kernel parameters respect staging admission before native node creation')
    finally:s.s.close()
    sys.exit(0)

a,b=Session(),Session()
try:
    ga,gb=graph(a),graph(b); ma,ka=load(a); mb,kb=load(b)
    aa=struct.unpack('!Q',a.ok(3,idarg(64)))[0]; ab=struct.unpack('!Q',b.ok(3,idarg(64)))[0]
    args=(aggregate(aa),scalar(struct.pack('<i',10)),ptr(aa,16))
    valid=request(g=ga,m=ma,k=ka,args=args)
    for body in (b'',valid[:-1],valid+b'\0',request(g=ga,m=ma,k=ka,args=args,count=1),
                 request(g=ga,m=ma,k=ka,args=args,length=2097153),request(g=ga,m=ma,k=ka,args=args,capacity=2097153)):
        reject(a,87,body)
    for kwargs in (dict(g=gb,m=ma,k=ka),dict(g=ga,m=mb,k=ka),dict(g=ga,m=ma,k=kb)):
        reject(a,87,request(args=args,**kwargs))
    for bad in (aggregate(ab),aggregate(aa,dirty=True),aggregate(aa,(0,8,24,24)),aggregate(aa,(0,8,24,52))):
        reject(a,87,request(g=ga,m=ma,k=ka,args=(bad,args[1],args[2])))
    for bad in (ptr(ab),ptr(ga),ptr(aa,65),ptr(0,1),ptr(aa)[:-1]+b'\1'):
        reject(a,87,request(g=ga,m=ma,k=ka,args=(args[0],args[1],bad)))
    node=struct.unpack('!Q',a.ok(87,valid)[:8])[0]
    head,wire=get(a,node,260); assert head[1]==ka and head[-1]==260 and wire==b''.join(args)
    reject(b,87,request(n=node,action=2))
    other=graph(a); empty=struct.unpack('!Q',a.ok(81,struct.pack('!IQQ',1,other,0)))[0]
    reject(a,87,request(g=ga,m=ma,k=ka,args=args,deps=(empty,)))
    reject(a,87,request(n=empty,action=2))
    executable=struct.unpack('!IQQI',a.ok(84,struct.pack('!I4Q',1,0,other,0,0)))[1]
    reject(a,87,request(n=node,e=executable,m=ma,k=ka,action=4,args=args))
    reject(a,88,attr(node,action=2,allocation=ab))
    reject(b,88,attr(node));reject(a,88,attr(node,action=3,src=empty))
    assert a.ok(88,attr(node))==bytes(36)
    a.ok(4,idarg(aa)); a.ok(25,idarg(ma))
    head,wire=get(a,node,260);assert head[1]==ka and wire==b''.join(args)
    reject(a,87,valid)
    a.ok(18);reject(a,87,request(n=node,action=2))
    print('PASS graph kernel exact buffer bounds, nested relocation slots and resource/dependency/attribute ownership')
    print('PASS entity membership, retired module/allocation getter identities and reset stale IDs')
finally:
    a.s.close();b.s.close()
