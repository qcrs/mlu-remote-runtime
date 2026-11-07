#!/usr/bin/env python3
"""Raw capture ownership, dispatch isolation, quota rollback and reset/EOF."""
import struct
import sys
from mlur_wire import Session,idarg
CAPTURE=90

def request(action,q=0,thread=1,mode=2,op=0,data=b'',capacity=0):
    return struct.pack('!IQQIIQQ',action,q,thread,mode,op,len(data),capacity)+data

def cap(s,action,q=0,**kw):return s.ok(CAPTURE,request(action,q,**kw))
def info(s,q):return struct.unpack_from('!IIQQQ',cap(s,3,q))
def rejected(s,op,body,status=None):
    got,data=s.rpc(op,body);assert got and not data,(op,got,data)
    if status is not None:assert got==status,(op,got,status)

a,b=Session(),Session()
try:
    q=struct.unpack('!Q',a.ok(6))[0];allocation=struct.unpack('!Q',a.ok(3,idarg(64)))[0]
    if '--begin-quota' in sys.argv:
        rejected(a,CAPTURE,request(1,q),0x80000007);assert info(a,q)[1]==0
        a.ok(18);print('PASS capture Begin quota rollback and reset')
    elif '--quota' in sys.argv:
        cap(a,1,q);n=struct.unpack('!Q',a.ok(10))[0]
        rejected(a,CAPTURE,request(5,q,op=12,data=struct.pack('!QQ',n,q)),0x80000007)
        current=info(a,q);assert current[1]==1 and current[4]==0
        cap(a,2,q);a.ok(18);print('PASS capture notifier metadata admission preserves active capture and releases charges')
    else:
        cap(a,1,q);state=info(a,q);g=state[3];assert state[1]==1 and g and state[4]==0
        rejected(b,CAPTURE,request(3,q));rejected(a,CAPTURE,request(3,allocation))
        # Capturing GraphID is borrowed until End, and cannot be destroyed.
        rejected(a,80,struct.pack('!IIQQ',2,0,g,0))
        rejected(a,CAPTURE,request(4,q,mode=1,data=idarg(allocation)))
        assert info(a,q)==state
        # Raw ordinary RPCs never execute on a captured queue.
        payload=struct.pack('!QQQIIQI',allocation,0,64,37,1,q,1)
        import json,pathlib
        operations=json.loads((pathlib.Path(__file__).resolve().parents[1]/'schema/mlu_api_schema.json').read_text())['operations']
        rejected(a,operations['API_MEMSET'],payload,100360)
        assert info(a,q)[1]==2;rejected(a,CAPTURE,request(2,q),100361);assert info(a,q)[1]==0
        for op,body in [(5,struct.pack('!I6QII',1,q,allocation,0,0,0,64,64,1)+bytes(64)),
                        (operations['API_D2H_SUBMIT'],struct.pack('!4Q',allocation,0,64,q))]:
            cap(a,1,q);rejected(a,op,body,100360);rejected(a,CAPTURE,request(2,q),100361)
        # Logical thread ownership is explicit, independent of server worker TLS.
        cap(a,1,q,mode=0,thread=7);rejected(a,CAPTURE,request(2,q,thread=8),100362)
        assert info(a,q)[1]==0;rejected(a,CAPTURE,request(2,q,thread=7),100015)
        # Session isolation: another client's allocation does not invalidate it.
        cap(a,1,q,mode=0);b.ok(3,idarg(64));assert info(a,q)[1]==1
        rejected(a,3,idarg(64),100360);assert info(a,q)[1]==2
        a.ok(18);rejected(a,CAPTURE,request(3,q))
        q=struct.unpack('!Q',a.ok(6))[0];cap(a,1,q);a.s.close()
        print('PASS raw capture owner/type, borrowed graph, dispatch guard, logical threads, client isolation, reset and active EOF')
finally:
    a.s.close();b.s.close()
