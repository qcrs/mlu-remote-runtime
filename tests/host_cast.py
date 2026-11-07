#!/usr/bin/env python3
"""Host-only CNRT cast observations; each library runs in its own process."""
import ctypes as C
import json
import struct
import sys

lib = C.CDLL(sys.argv[1])
math = C.CDLL('libm.so.6')
legacy = [('f16',0x12,2),('f32',0x13,4),('f64',0x14,8),('i4',0x20,1),
          ('i8',0x21,1),('i16',0x22,2),('i32',0x23,4),('i64',0x24,8),
          ('auto',0x25,1),('u8',0x31,1),('u16',0x32,2),('u32',0x33,4),
          ('fix8',0x41,1),('quant8',0x51,1),('bool',0x61,1)]
v2 = [('f64',1,8),('f32',2,4),('f16',3,2),('bf16',4,2),('u64',0x11,8),
      ('u32',0x12,4),('u16',0x13,2),('u8',0x14,1),('i64',0x21,8),
      ('i32',0x22,4),('i16',0x23,2),('i8',0x24,1),('bool',0x31,1)]
formats = dict(f64='d',f32='f',f16='e',i8='b',i16='h',i32='i',i64='q',
               u8='B',u16='H',u32='I',u64='Q',bool='?',fix8='b',quant8='b',auto='b',i4='b')
lib.cnrtCreateQuantizedParam.argtypes = [C.POINTER(C.c_void_p),C.c_int,C.c_float,C.c_int]
lib.cnrtCreateQuantizedParamByChannel.argtypes = [C.POINTER(C.c_void_p),C.POINTER(C.c_int),
    C.POINTER(C.c_float),C.POINTER(C.c_float),C.c_int,C.POINTER(C.c_int),C.c_int]
lib.cnrtDestroyQuantizedParam.argtypes = [C.c_void_p]
lib.cnrtCastDataType.argtypes = [C.c_void_p,C.c_int,C.c_void_p,C.c_int,C.c_int,C.c_void_p]
lib.cnrtCastDataType_V2.argtypes = lib.cnrtCastDataType.argtypes + [C.c_int]


def scalar(pos, scale, offset):
    p = C.c_void_p()
    assert lib.cnrtCreateQuantizedParam(C.byref(p),pos,scale,offset) == 0
    return p


def channel():
    p = C.c_void_p()
    poses = (C.c_int*3)(0,1,-1)
    scales = (C.c_float*3)(.5,2,1.5)
    offsets = (C.c_float*3)(0,3,-2)
    dims = (C.c_int*3)(2,3,2)
    assert lib.cnrtCreateQuantizedParamByChannel(C.byref(p),poses,scales,offsets,3,dims,1) == 0
    # The created parameter owns its arrays; callers can reuse the input storage.
    for array in (poses,scales,offsets,dims):
        for i in range(3):
            array[i] = 7
    return p


def encode(kind, width, values):
    if kind == 'bf16':
        return b''.join(struct.pack('<H',struct.unpack('<I',struct.pack('<f',x))[0]>>16) for x in values)
    if kind == 'f16':
        values = [max(-65504,min(x,65504)) for x in values]
    elif kind == 'bool':
        values = [bool(x) for x in values]
    elif kind not in ('f32','f64'):
        # Native legacy i16->half hangs on -32768: avoid that SDK defect.
        low = 0 if kind.startswith('u') else -(1<<(8*width-1))+1
        high = (1<<(8*width-(0 if kind.startswith('u') else 1)))-1
        values = [max(low,min(int(x),high)) for x in values]
    return b''.join(struct.pack('<'+formats[kind],x) for x in values)


records = []


def observe(version, source, dest, values, param, profile, mode, label):
    src, st, ss = source
    dst, dt, ds = dest
    raw = encode(src,ss,values)
    a = C.create_string_buffer(b'\x5a'*16 + raw + b'\x5a'*16, len(raw)+32)
    b = C.create_string_buffer(b'\xa5'*(len(values)*ds+32), len(values)*ds+32)
    f = lib.cnrtCastDataType if version == 1 else lib.cnrtCastDataType_V2
    assert math.fesetround(0) == 0
    status = f(C.byref(a,16),st,C.byref(b,16),dt,len(values),param,*([mode] if version == 2 else []))
    assert a.raw == b'\x5a'*16 + raw + b'\x5a'*16, (label,'source changed')
    assert b.raw[:16] == b'\xa5'*16 and b.raw[-16:] == b'\xa5'*16, (label,src,dst,'guard changed')
    records.append(dict(label=label,v=version,q=profile,s=src,d=dst,m=mode,status=status,
                        hex=b.raw[16:-16].hex()))


params = [None,scalar(1,.75,3),channel()]
try:
    profiles = [
        [-3,-2.5,-1.5,-.5,0,.5,1.5,2.5,10,63,64,100],
        [-65504,-40000,-130,-128.5,-3.003,-1.001,-.1,-0.,1e-9,.1,1.001,3.003,127.5,256,2049,32768,65504,65520]]
    for v,types in ((1,legacy),(2,v2)):
        for q,p in enumerate(params):
            for source in types:
                for dest in types:
                    for mode in (range(6) if v == 2 else [5]):
                        for label,values in enumerate(profiles):
                            observe(v,source,dest,values[:12] if q == 2 else values,p,q,mode,str(label))
    # Values above int32 exercise 64-bit storage and avoid an int32 intermediate.
    for v,types in ((1,legacy),(2,v2)):
        names = {x[0]:x for x in types}
        pairs = [('f32','i64'),('i64','f32'),('i64','f16')]
        if v == 2:
            pairs += [('f32','u64'),('u64','f32'),('i64','bf16'),('u64','f16')]
        for src,dst in pairs:
            for mode in (range(6) if v == 2 else [5]):
                observe(v,names[src],names[dst],[0,2**40+7,2**48,-(2**40+7)],None,0,mode,'wide')
    negative = scalar(-2,.5,0)
    try:
        for v,types in ((1,legacy),(2,v2)):
            names = {x[0]:x for x in types}
            for src,dst in [('f32','i8'),('f32','i16'),('i8','f32'),('i16','f16')]:
                for mode in (range(6) if v == 2 else [5]):
                    observe(v,names[src],names[dst],profiles[0],negative,3,mode,'negative-pos')
    finally:
        assert lib.cnrtDestroyQuantizedParam(negative) == 0
    assert lib.cnrtCastDataType_V2(None,-1,None,-1,0,None,-1) == 0
finally:
    for p in params[1:]:
        assert lib.cnrtDestroyQuantizedParam(p) == 0
    math.fesetround(0)
json.dump(records,sys.stdout,separators=(',',':'))
print()
