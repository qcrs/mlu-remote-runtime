#!/usr/bin/env python3
"""Every generated RPC rejects malformed success before publishing outputs."""
import ctypes as c, json, os, pathlib, socket, struct, subprocess, sys, threading
ROOT=pathlib.Path(__file__).resolve().parents[1]
lib=c.CDLL(str(ROOT/'build/libremote_cnrt.so'))
lib.mlu_remote_connect.argtypes=[c.c_char_p,c.c_uint16]; lib.mlu_remote_connect.restype=c.c_void_p
lib.mlu_remote_disconnect.argtypes=[c.c_void_p]
def recv(s,n):
    data=b''
    while len(data)<n:
        part=s.recv(n-len(data)); assert part; data+=part
    return data
def request(s):
    h=struct.unpack('!IHHQII',recv(s,24)); recv(s,h[4]); return h
def reply(s,h,data):
    data=bytes(8)+data
    s.sendall(struct.pack('!IHHQII',h[0],1,h[2],h[3],len(data),0)+data)
if len(sys.argv)>1 and sys.argv[1]=='--public-string':
    driver=sys.argv[2]=='driver'
    shim=c.CDLL(str(ROOT/('build/driver-shim/libcndrv.so' if driver else 'build/shim/libcnrt.so')))
    if driver:
        shim.cnInit.argtypes=[c.c_uint]; assert not shim.cnInit(0)
    fn=getattr(shim,'cnDeviceGetPCIBusId' if driver else 'cnrtDeviceGetPCIBusId')
    fn.argtypes=[c.c_void_p,c.c_int,c.c_uint64 if driver else c.c_int]
    out=c.create_string_buffer(b'\xa5'*80)
    assert fn(out,13,0)!=0
    assert out.raw==b'\xa5'*80+b'\0','reply exceeded caller capacity'
    sys.exit(0)
plan=json.loads((ROOT/'build/generated/typed_test_plan.json').read_text())
schema=json.loads((ROOT/'schema/mlu_api_schema.json').read_text())
string_ops={r['name'] for r in schema['typed_rpc'] if any(f['type']=='cstring' for f in r['response'])}
cases=[(r,False) for r in plan]+[(r,True) for r in plan if r['name'] in string_ops]
for r,bad_string in cases:
    errors=[]
    with socket.socket() as listener:
        listener.bind(('127.0.0.1',0)); listener.listen(1); listener.settimeout(3)
        def server():
            try:
                with listener.accept()[0] as s:
                    s.settimeout(3); h=request(s); reply(s,h,struct.pack('!IIII',1,1,0x3fff,0x08010001))
                    h=request(s); assert h[2]==r['opcode']
                    reply(s,h,b'x'*r['response_bytes'] if bad_string else bytes(r['response_bytes']-1 if r['response_bytes'] else 1))
                    assert not s.recv(1),'malformed connection reused'
            except BaseException as e: errors.append(e)
        thread=threading.Thread(target=server); thread.start()
        ctx=lib.mlu_remote_connect(b'127.0.0.1',listener.getsockname()[1]); assert ctx
        try:
            fn=getattr(lib,'mlu_remote_api_'+r['name']); fn.argtypes=[c.c_void_p,c.c_void_p,c.c_void_p]
            req=c.create_string_buffer(16384); out=c.create_string_buffer(b'\xa5'*16384)
            assert fn(ctx,req,out)!=0
            assert out.raw==b'\xa5'*16384+b'\0'
            assert fn(ctx,req,out)!=0
        finally: lib.mlu_remote_disconnect(ctx)
        thread.join(4); assert not thread.is_alive()
        if errors: raise errors[0]
    print('PASS generated malformed success',r['name'],'unterminated string' if bad_string else 'length')

# A terminated wire string can still exceed the public caller's smaller buffer.
for family,rpc in [('driver','driver_pci_id'),('runtime','runtime_pci_id')]:
    opcode=next(r['opcode'] for r in plan if r['name']==rpc); errors=[]
    with socket.socket() as listener:
        listener.bind(('127.0.0.1',0)); listener.listen(1); listener.settimeout(3)
        def server():
            try:
                with listener.accept()[0] as s:
                    s.settimeout(3); h=request(s); reply(s,h,struct.pack('!IIII',1,1,0x3fff,0x08010001))
                    h=request(s); assert h[2]==opcode
                    reply(s,h,b'x'*63+b'\0')
                    assert not s.recv(1)
            except BaseException as e: errors.append(e)
        thread=threading.Thread(target=server); thread.start()
        child=subprocess.run([sys.executable,__file__,'--public-string',family],
            env=dict(os.environ,MLU_REMOTE_HOST='127.0.0.1',MLU_REMOTE_PORT=str(listener.getsockname()[1])),
            capture_output=True,text=True,timeout=10)
        thread.join(4); assert not thread.is_alive()
        if errors: raise errors[0]
        assert child.returncode==0,child.stderr
    print('PASS public PCI output capacity',family)
