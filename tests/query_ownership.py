#!/usr/bin/env python3
"""Public wrapper ownership and explicit rejection boundaries."""
import ctypes as c, pathlib
root=pathlib.Path(__file__).resolve().parents[1]
drv=c.CDLL(str(root/'build/driver-shim/libcndrv.so')); rt=c.CDLL(str(root/'build/shim/libcnrt.so'))
def bind(lib,name,args):
    f=getattr(lib,name); f.argtypes=args; f.restype=c.c_int; return f
P=c.c_void_p; U=c.c_uint64
init=bind(drv,'cnInit',[c.c_uint]); create=bind(drv,'cnCtxCreate',[c.POINTER(P),c.c_uint,U]); destroy=bind(drv,'cnCtxDestroy',[P]); current=bind(drv,'cnCtxSetCurrent',[P])
queue=bind(drv,'cnCreateQueue',[c.POINTER(P),c.c_uint]); copy=bind(drv,'cnQueueCopyAttributes',[P,P]); get=bind(drv,'cnQueueGetAttribute',[P,c.c_uint,P]); qctx=bind(drv,'cnQueueGetContext',[P,c.POINTER(P)])
version=bind(drv,'cnCtxGetApiVersion',[P,c.POINTER(c.c_uint)]); malloc=bind(drv,'cnMalloc',[c.POINTER(U),U]); arange=bind(drv,'cnMemGetAddressRange',[c.POINTER(U),c.POINTER(U),U])
assert not init(0); a,b=P(),P(); qa,qb=P(),P(); p=U()
assert not create(c.byref(a),0,0); assert not queue(c.byref(qa),0); assert not malloc(c.byref(p),32)
assert not create(c.byref(b),0,0); assert not queue(c.byref(qb),0)
assert copy(qb,qa)!=0; buf=c.create_string_buffer(128); assert get(None,1,buf)!=0
assert not current(None); owner=P(); assert not qctx(qa,c.byref(owner)) and owner.value==a.value
base,size=U(),U(); assert arange(c.byref(base),c.byref(size),p)==100201
assert not current(b); assert not arange(c.byref(base),c.byref(size),p) and base.value==p.value and size.value==32
assert not destroy(a); owner.value=0x55; assert qctx(qa,c.byref(owner))!=0 and owner.value==0x55
assert arange(c.byref(base),c.byref(size),p)!=0
v=c.c_uint(0x55); assert version(a,c.byref(v))!=0 and v.value==0x55
assert not current(b); assert not destroy(b)
setdev=bind(rt,'cnrtSetDevice',[c.c_int]); getrt=bind(rt,'cnrtQueueGetAttribute',[P,c.c_uint,P]); assert not setdev(0); assert getrt(None,1,buf)!=0
print('PASS foreign current routing, context destruction tombstones, cross-context copy and NULL queue rejection')
