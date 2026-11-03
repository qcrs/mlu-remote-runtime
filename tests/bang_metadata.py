#!/usr/bin/env python3
"""Reject stale/incorrect metadata and unsupported IR; no guessed relocation."""
import importlib.util, os, pathlib, struct, subprocess, tempfile
ROOT=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('metadata',ROOT/'scripts/generate_bang_metadata.py')
m=importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
for signature in ['%struct.P* byval(%struct.P) %a','<4 x i32> %v','i128 %n','i32** %p','%struct.P* %p']:
    try: m.signatures('define mlisa_kernel void @sample('+signature+') {')
    except ValueError: pass
    else: raise AssertionError(signature)
print('PASS unknown aggregate/vector/oversize scalar/indirect pointer require ABI review')
prefix='target datalayout = "'+m.DATA_LAYOUT+'"\n'
ir=prefix+'%struct.P = type { i8, [2 x i32], double }\n'
layout={}
assert m.signatures(ir+'define mlisa_kernel void @sample(%struct.P* byval(%struct.P) align 8 %a) {',layout)==[('sample',[(24,2)])]
assert [f['offset'] for f in layout[('sample',0)]['fields']]==[0,4,16]
for fields in ('{ i32, i32* }','{ %struct.P }','opaque','<{ i8, i32 }>'):
    try: m.signatures(prefix+'%struct.P = type '+fields+'\ndefine mlisa_kernel void @sample(%struct.P* byval(%struct.P) align 8 %a) {')
    except ValueError: pass
    else: raise AssertionError(fields)
print('PASS compiler aggregate layout/padding; reject embedded pointers/recursive/opaque/packed types')
with tempfile.TemporaryDirectory() as tmp:
    tmp=pathlib.Path(tmp); blob=tmp/'abi.bin'
    subprocess.run(['objcopy','--dump-section',f'.mlu.remote.abi={blob}',str(ROOT/'build/native_bang_metadata')],check=True)
    original=blob.read_bytes()
    # A host can carry several images with the same exact kernel symbol.
    # An older image record must not hide the matching image/symbol pair.
    data=bytearray(original[12:12+1556]); data[8]^=1
    count=struct.unpack_from('<I',original,8)[0]
    blob.write_bytes(original[:8]+struct.pack('<I',count+1)+data+original[12:])
    exe=tmp/'multiple-images'
    subprocess.run(['objcopy','--update-section',f'.mlu.remote.abi={blob}',str(ROOT/'build/native_bang_metadata'),str(exe)],check=True)
    r=subprocess.run([str(exe)],capture_output=True,text=True,env=os.environ,timeout=20)
    assert r.returncode==0,(r.stdout,r.stderr)
    print('PASS image/symbol association across duplicate symbols')
    for mode in ('stale-image','wrong-size','wrong-kind','truncated'):
        data=bytearray(original)
        if mode=='stale-image': data[20]^=1
        if mode=='wrong-size': struct.pack_into('<I',data,12+276,4)
        if mode=='wrong-kind': data[12+280]=3
        if mode=='truncated': data=data[:-1]
        blob.write_bytes(data); exe=tmp/mode
        subprocess.run(['objcopy','--update-section',f'.mlu.remote.abi={blob}',str(ROOT/'build/native_bang_metadata'),str(exe)],check=True)
        r=subprocess.run([str(exe)],capture_output=True,text=True,env=os.environ,timeout=20)
        assert r.returncode==2,(mode,r.returncode,r.stdout,r.stderr)
        assert 'unsupported kernel ABI: c_types' in r.stderr,(mode,r.stderr)
        print('PASS rejected metadata',mode)
