#!/usr/bin/env python3
"""Bounds, ownership and no native pointer leakage for query/union codecs."""
import json, pathlib, struct
from mlur_wire import Session, idarg
root=pathlib.Path(__file__).resolve().parents[1]
schema=json.loads((root/'schema/mlu_api_schema.json').read_text()); ops={r['name']:r['opcode'] for r in schema['typed_rpc']}
a,b=Session(),Session()
try:
    alloc,=struct.unpack('!Q',a.ok(3,idarg(123))); other,=struct.unpack('!Q',b.ok(3,idarg(123)))
    q,=struct.unpack('!Q',a.ok(6)); foreign,=struct.unpack('!Q',b.ok(6))
    def bad(name,p):
        st,body=a.rpc(ops[name],p); assert st and not body,(name,st); print('PASS rejected',name,st)
    for name in ['driver_pci_device','runtime_pci_device']: bad(name,b'x'*64)
    for name in ['address_range','pointer_attributes']:
        for id,off in [(other,0),(alloc,123),(alloc,(1<<64)-1)]: bad(name,struct.pack('!QQ',id,off))
    for attr in [2,6,17]:
        value,width=struct.unpack('!QI',a.ok(ops['memory_attribute'],struct.pack('!QQI',alloc,7,attr)))
        assert width==8 and value=={2:7,6:1,17:0}[attr],(attr,value,width)
    for id in [other,q]: bad('memory_attribute',struct.pack('!QQI',id,0,1))
    bad('queue_context',idarg(foreign)); bad('queue_attribute_get',struct.pack('!QI',foreign,1))
    bad('queue_attribute_get',struct.pack('!QI',0,1)); bad('queue_attribute_get',struct.pack('!QI',q,999))
    bad('queue_attribute_copy',struct.pack('!QQ',q,foreign))
    def policy(id=alloc,off=0,size=0): return struct.pack('!QIQQQfII',q,1,id,off,size,.5,2,0)
    for args in [(other,0,0),(alloc,123,0),(alloc,122,2),(0,1,0)]: bad('queue_attribute_set',policy(*args))
    a.ok(ops['queue_attribute_set'],policy(alloc,16,0))
    aid,off,size,ratio,hit,miss=struct.unpack('!QQQfII',a.ok(ops['queue_attribute_get'],struct.pack('!QI',q,1)))
    assert (aid,off,size,ratio,hit,miss)==(alloc,16,0,.5,2,0)
    a.ok(4,idarg(alloc)); bad('queue_attribute_get',struct.pack('!QI',q,1)); bad('address_range',struct.pack('!QQ',alloc,0))
    a.ok(7,idarg(q)); bad('queue_context',idarg(q))
    print('PASS query reverse relocation, stale resources, cross sessions and union bounds')
finally: a.s.close(); b.s.close()
