#!/usr/bin/env python3
"""Callback pairing, replay routing, user ownership and channel cleanup."""
import socket,struct,threading,sys
from mlur_wire import Session,idarg,MAGIC,HOST,PORT

def control(action,g=0,node=0,e=0,token=0,value=0,flags=0,data=b'',capacity=0):
    return struct.pack('!I5QI2Q',action,g,node,e,token,value,flags,len(data),capacity)+data
def call(s,action,**kw):return s.ok(92,control(action,**kw))
def reject(s,op,data):
    status,out=s.rpc(op,data);assert status and not out,(status,out)
def graph(s):return struct.unpack_from('!Q',s.ok(80,struct.pack('!IIQQ',1,0,0,0)))[0]
def entity(s,g):
    st,e,_,_=struct.unpack('!IQQI',s.ok(84,struct.pack('!I4Q',1,0,g,0,0)));assert not st;return e
def recv(sock,n):
    data=b''
    while len(data)<n:
        part=sock.recv(n-len(data));assert part,'callback EOF';data+=part
    return data
def attach(secret):
    sock=socket.create_connection((HOST,PORT),timeout=15)
    sock.sendall(struct.pack('!IHHQII',MAGIC,1,91,1,16,0)+secret)
    h=struct.unpack('!IHHQII',recv(sock,24));body=recv(sock,h[4]);return sock,h[5],body
class Channel:
    def __init__(self,s):
        secret=call(s,1)[:16];self.sock,status,body=attach(secret);assert not status and body==bytes(8)
        duplicate,status,_=attach(secret);assert status;duplicate.close()
        self.events=[];self.errors=[];self.thread=threading.Thread(target=self.pump);self.thread.start()
    def pump(self):
        try:
            seq=1
            while True:
                h=struct.unpack('!IHHQII',recv(self.sock,24));seq+=1
                assert h[:4]==(MAGIC,1,91,seq) and not h[5]
                data=recv(self.sock,h[4]);kind,run,token,n=struct.unpack_from('!4Q',data)
                assert n==0 and len(data)==32
                self.events.append((kind,run,token))
                self.sock.sendall(struct.pack('!IHHQII',MAGIC,1,91,seq,0,0))
                if kind==6:return
        except Exception as error:self.errors.append(str(error))
    def close(self):
        self.thread.join(timeout=15);assert not self.thread.is_alive();assert not self.errors,self.errors;self.sock.close()

def quota():
    s=Session();channel=Channel(s)
    try:
        g=graph(s);allocation=struct.unpack('!Q',s.ok(3,idarg(128)))[0]
        node=struct.unpack_from('!Q',call(s,2,g=g,token=101))[0]
        for kind,token in ((1,0x10000),(2,0x20000)):
            dst,doff,src,soff=(allocation,0,0,token) if kind==1 else (0,token,allocation,0)
            node=struct.unpack_from('!Q',s.ok(86,struct.pack('!I8QIQ',1,g,0,0,dst,doff,src,soff,64,2 if kind==1 else 3,1)+idarg(node)))[0]
        e=entity(s,g);plan=call(s,12,e=e,capacity=72)[32:];assert len(plan)==72
        for cookie in (1,2):
            status,data=s.rpc(92,control(13,e=e,token=cookie,data=plan))
            assert status==0x80000007 and not data,(status,data)
            assert call(s,12,e=e,capacity=72)[32:]==plan
        # Failed pinned staging admission leaves the paired executor usable.
        g2=graph(s);call(s,2,g=g2,token=102);e2=entity(s,g2)
        plan=call(s,12,e=e2,capacity=24)[32:]
        transfer=struct.unpack_from('!Q',call(s,13,e=e2,token=3,data=plan))[0]
        s.ok(24,idarg(transfer));s.ok(28,idarg(transfer));s.ok(18);channel.close()
        assert channel.events==[(1,3,102),(4,3,0),(6,0,0)],channel.events
        print('PASS callback staging quota rollback preserves plan, releases temporary views and permits later callback replay')
    finally:s.s.close()

if '--quota' in sys.argv:
    quota();sys.exit(0)

a,b=Session(),Session();channel=None
try:
    channel=Channel(a);g=graph(a);node=struct.unpack_from('!Q',call(a,2,g=g,token=101))[0]
    e=entity(a,g)
    reject(b,92,control(3,node=node));reject(a,92,control(3,node=g))
    assert struct.unpack_from('!Q',call(a,3,node=node),8)[0]==101
    # Source placeholders must never execute through an earlier replay route.
    response=a.ok(84,struct.pack('!I4Q',3,e,0,0,0));assert struct.unpack_from('!I',response)[0]
    reject(a,89,struct.pack('!I4Q',2,e,0,0,0))
    plan=call(a,12,e=e,capacity=24)[32:];assert plan==struct.pack('!3Q',101,0,3)
    reject(a,92,control(13,e=e,token=1,data=struct.pack('!3Q',102,0,3)))
    transfer=struct.unpack_from('!Q',call(a,13,e=e,token=1,data=plan))[0]
    a.ok(24,idarg(transfer));a.ok(28,idarg(transfer))
    # User objects remain session-typed; final release is a one-shot event.
    u=struct.unpack_from('!Q',call(a,6,token=201,value=1,flags=1))[0]
    reject(b,92,control(8,node=u,value=1));reject(a,92,control(8,node=node,value=1))
    call(a,8,node=u,value=1)
    a.ok(18);channel.close()
    assert channel.events==[(1,1,101),(4,1,0),(5,0,201),(6,0,0)],channel.events
    channel=None
    # Closing the paired channel poisons the primary session, without a wait API.
    secret=call(a,1)[:16];sock,status,_=attach(secret);assert not status;sock.close()
    a.s.settimeout(5);assert a.s.recv(1)==b''
    print('PASS callback owner/type, pairing, replay whitelist, placeholder routing, user release, reset and channel EOF')
finally:
    if channel:channel.sock.close();channel.thread.join(timeout=16)
    a.s.close();b.s.close()
