#!/usr/bin/env python3
"""Driver client context failure semantics using a deterministic TCP peer."""
import ctypes as c
import os, pathlib, socket, struct, subprocess, sys, threading
ROOT=pathlib.Path(__file__).resolve().parents[1]
MAGIC=0x4d4c5552
def recv(sock,n):
    data=b''
    while len(data)<n:
        chunk=sock.recv(n-len(data)); assert chunk,'client closed early'; data+=chunk
    return data
def request(sock):
    h=struct.unpack('!IHHQII',recv(sock,24)); assert h[:2]==(MAGIC,1)
    return h,recv(sock,h[4])
def reply(sock,h,body=b'',status=0):
    body=bytes(8)+body
    sock.sendall(struct.pack('!IHHQII',MAGIC,1,h[2],h[3],len(body),status)+body)
def child(port,mode):
    os.environ['MLU_REMOTE_PORT']=port
    os.environ['MLU_REMOTE_HOST']='127.0.0.1'
    lib=c.CDLL(str(ROOT/'build/driver-shim/libcndrv.so'))
    def bind(name,args):
        f=getattr(lib,name); f.argtypes=args; f.restype=c.c_int; return f
    init=bind('cnInit',[c.c_uint]); create=bind('cnCtxCreate',[c.POINTER(c.c_void_p),c.c_uint,c.c_uint64])
    destroy=bind('cnCtxDestroy',[c.c_void_p]); sync=bind('cnCtxSync',[])
    current=bind('cnCtxGetCurrent',[c.POINTER(c.c_void_p)]); setcurrent=bind('cnCtxSetCurrent',[c.c_void_p])
    count=bind('cnDeviceGetCount',[c.POINTER(c.c_int)])
    if mode=='old-capability':
        assert init(0)!=0
        assert current(c.byref(c.c_void_p()))==100002
        return
    assert init(0)==0
    ctx=c.c_void_p(1234)
    if mode=='config':
        for value in ('','garbage','-1','65536','0','1x'):
            os.environ['MLU_REMOTE_PORT']=value
            assert create(c.byref(ctx),0,0)==100006 and ctx.value==1234
        os.environ['MLU_REMOTE_PORT']=port
        assert current(c.byref(ctx))==0 and ctx.value is None
    else:
        assert create(c.byref(ctx),1,0)==0
        old=ctx.value
        if mode=='sync-bad-reply': assert sync()!=0
        assert destroy(ctx)!=0
        assert setcurrent(c.c_void_p(old))==100201
        assert destroy(c.c_void_p(old))==100201
        assert current(c.byref(ctx))==0 and ctx.value is None
    n=c.c_int(-1); assert count(c.byref(n))==0 and n.value==1

if len(sys.argv)>1:
    child(sys.argv[1],sys.argv[2]); sys.exit(0)
for mode in ('config','close-bad-reply','close-EOF','sync-bad-reply','old-capability'):
    errors=[]
    with socket.socket() as listener:
        listener.bind(('127.0.0.1',0)); listener.listen(2); listener.settimeout(8)
        def serve():
            try:
                with listener.accept()[0] as control:
                    control.settimeout(8)
                    h,data=request(control); assert h[2]==1 and data==struct.pack('!IIII',1,0,0,0)
                    reply(control,h,struct.pack('!IIII',1,1,0xfff if mode=='old-capability' else 0x1fff,0x08010001))
                    if mode=='old-capability': assert control.recv(1)==b''; return
                    if mode!='config':
                        with listener.accept()[0] as ctx:
                            ctx.settimeout(8)
                            h,data=request(ctx); assert data==struct.pack('!IIII',1,0,0,1)
                            reply(ctx,h,struct.pack('!IIII',1,1,0x1fff,0x08010001))
                            h,data=request(ctx); assert h[2]==(32 if mode=='sync-bad-reply' else 31) and not data
                            if mode!='close-EOF': reply(ctx,h,b'x')
                            ctx.shutdown(socket.SHUT_WR)
                            assert ctx.recv(1)==b'', 'failed context sent more requests/replayed'
                    h,_=request(control); assert h[2]==2
                    reply(control,h,struct.pack('!IQI',1,0,0))
                    assert control.recv(1)==b''
            except BaseException as e: errors.append(e)
        worker=threading.Thread(target=serve,daemon=True); worker.start()
        env=dict(os.environ,MLU_REMOTE_CONNECT_TIMEOUT_MS='3000',MLU_REMOTE_RPC_TIMEOUT_MS='3000')
        subprocess.run([sys.executable,__file__,str(listener.getsockname()[1]),mode],env=env,check=True,timeout=15)
        worker.join(10); assert not worker.is_alive()
        if errors: raise errors[0]
    print('PASS Driver',mode,': no replay, stale context invalid, control session independent')
