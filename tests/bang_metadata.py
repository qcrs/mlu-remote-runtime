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
pointer_layout = {}
assert m.signatures(prefix+'%struct.P = type { i32, [2 x i32*] }\ndefine mlisa_kernel void @sample(%struct.P* byval(%struct.P) align 8 %a) {', pointer_layout) == [('sample', [(24, 2)])]
assert m.pointer_offsets(pointer_layout[('sample', 0)]) == [8, 16]
for fields in ('{ %struct.P }','opaque','<{ i8, i32 }>'):
    try: m.signatures(prefix+'%struct.P = type '+fields+'\ndefine mlisa_kernel void @sample(%struct.P* byval(%struct.P) align 8 %a) {')
    except ValueError: pass
    else: raise AssertionError(fields)
print('PASS compiler aggregate layout/padding and pointer offsets; reject recursive/opaque/packed types')
with tempfile.TemporaryDirectory() as tmp:
    tmp=pathlib.Path(tmp); blob=tmp/'abi.bin'
    subprocess.run(['objcopy','--dump-section',f'.mlu.remote.abi={blob}',str(ROOT/'build/native_bang_metadata')],check=True)
    original=blob.read_bytes()
    # Preserve the v1 reader for binaries produced before field relocations.
    image=(ROOT/'build/metadata-audit/native_bang_metadata-bang-mlisa-cambricon-bang.cnfatbin').read_bytes()
    legacy=[]; cursor=12
    for _ in range(struct.unpack_from('<I',original,8)[0]):
        length=struct.unpack_from('<I',original,cursor)[0]; cursor+=4
        record=original[cursor:cursor+length]; cursor+=length
        argc,fields=struct.unpack_from('<II',record,272); assert not fields
        legacy.append(struct.pack('<QQ',len(image),m.fingerprint(image))+record[16:276]+
                      record[280:280+argc*5]+bytes((m.MAX_ARGS-argc)*5))
    blob.write_bytes(b'MLUABI01'+struct.pack('<I',len(legacy))+b''.join(legacy))
    exe=tmp/'legacy-v1'
    subprocess.run(['objcopy','--update-section',f'.mlu.remote.abi={blob}',str(ROOT/'build/native_bang_metadata'),str(exe)],check=True)
    r=subprocess.run([str(exe)],capture_output=True,text=True,env=os.environ,timeout=20)
    assert r.returncode==0,(r.stdout,r.stderr)
    print('PASS legacy v1 compiler metadata numerical launch')
    # A host can carry several images with the same exact kernel symbol.
    # An older image record must not hide the matching image/symbol pair.
    assert original[:8] == b'MLUABI02'
    length=struct.unpack_from('<I',original,12)[0]
    data=bytearray(original[12:16+length]); data[12]^=1
    count=struct.unpack_from('<I',original,8)[0]
    blob.write_bytes(original[:8]+struct.pack('<I',count+1)+data+original[12:])
    exe=tmp/'multiple-images'
    subprocess.run(['objcopy','--update-section',f'.mlu.remote.abi={blob}',str(ROOT/'build/native_bang_metadata'),str(exe)],check=True)
    r=subprocess.run([str(exe)],capture_output=True,text=True,env=os.environ,timeout=20)
    assert r.returncode==0,(r.stdout,r.stderr)
    print('PASS image/symbol association across duplicate symbols')
    for mode in ('stale-image','wrong-size','wrong-kind','truncated'):
        data=bytearray(original)
        if mode=='stale-image': data[24]^=1
        if mode=='wrong-size': struct.pack_into('<I',data,16+280,4)
        if mode=='wrong-kind': data[16+284]=3
        if mode=='truncated': data=data[:-1]
        blob.write_bytes(data); exe=tmp/mode
        subprocess.run(['objcopy','--update-section',f'.mlu.remote.abi={blob}',str(ROOT/'build/native_bang_metadata'),str(exe)],check=True)
        r=subprocess.run([str(exe)],capture_output=True,text=True,env=os.environ,timeout=20)
        assert r.returncode==2,(mode,r.returncode,r.stdout,r.stderr)
        assert 'unsupported kernel ABI: c_types' in r.stderr,(mode,r.stderr)
        print('PASS rejected metadata',mode)
    source=ROOT/'build/native_bang_relocations'
    device=ROOT/'build/relocation-audit/native_bang_relocations-bang-mlisa-cambricon-bang.cnfatbin'
    subprocess.run(['objcopy','--dump-section',f'.mlu.remote.abi={blob}',str(source)],check=True)
    original=blob.read_bytes(); cursor=12
    while cursor<len(original):
        length=struct.unpack_from('<I',original,cursor)[0]; record=cursor+4
        if original[record+16:record+272].split(b'\0',1)[0]==b'relocated': break
        cursor=record+length
    else: raise AssertionError('relocated metadata missing')
    argc,fields=struct.unpack_from('<II',original,record+272)
    assert argc==1 and fields==5
    field=record+280+argc*5
    for mode in ('field-argument','field-range','field-alignment','field-overlap','field-kind','field-count'):
        data=bytearray(original)
        if mode=='field-argument': struct.pack_into('<I',data,field,argc)
        if mode=='field-range': struct.pack_into('<I',data,field+4,72)
        if mode=='field-alignment': struct.pack_into('<I',data,field+4,9)
        if mode=='field-overlap': struct.pack_into('<I',data,field+12,8)
        if mode=='field-kind': data[record+284]=0
        if mode=='field-count': struct.pack_into('<I',data,record+276,513)
        blob.write_bytes(data)
        exe=tmp/mode; bad_image=tmp/(mode+'.cnfatbin')
        subprocess.run(['objcopy','--update-section',f'.mlu.remote.abi={blob}',str(source),str(exe)],check=True)
        subprocess.run(['objcopy','--update-section',f'.mlu.remote.abi={blob}',str(device),str(bad_image)],check=True)
        r=subprocess.run([str(exe),'runtime'],capture_output=True,text=True,env=os.environ,timeout=20)
        assert r.returncode==2 and 'unsupported kernel ABI: relocated' in r.stderr,(mode,r.stdout,r.stderr)
        r=subprocess.run([str(source),'driver',str(bad_image)],capture_output=True,text=True,env=os.environ,timeout=20)
        assert r.returncode==2 and 'cnModuleGetKernel(module, "relocated", &kernel)' in r.stderr,(mode,r.stdout,r.stderr)
        print('PASS rejected host/Driver metadata',mode)
