#!/usr/bin/env python3
"""Record compiler-container evidence without copying binaries or SDK headers."""
import ctypes, hashlib, json, pathlib, struct
root=pathlib.Path(__file__).resolve().parents[1]
directory=root/'build/audit'
image=next(directory.glob('*.cnfatbin')).read_bytes()
def sections(data):
    shoff=struct.unpack_from('<Q',data,40)[0]
    entsize,count,strindex=struct.unpack_from('<HHH',data,58)
    table=[struct.unpack_from('<IIQQQQIIQQ',data,shoff+i*entsize) for i in range(count)]
    st=table[strindex]; names=data[st[4]:st[4]+st[5]]
    for s in table:
        yield names[s[0]:].split(b'\0',1)[0].decode(),data[s[4]:s[4]+s[5]]
container=dict(sections(image))['.cn_fatbin']
offset=container.index(b'CNFB\2\x80\x10\0')
packed=struct.unpack_from('<Q',container,offset+8)[0]
padded=struct.unpack_from('<I',container,offset+20)[0]
expanded=struct.unpack_from('<Q',container,offset+24)[0]
compressed=struct.unpack_from('<I',container,offset+32)[0]
out=ctypes.create_string_buffer(expanded)
size=ctypes.CDLL('liblz4.so.1').LZ4_decompress_safe(container[offset+36:offset+36+compressed],out,compressed,expanded)
assert size==expanded
device=next(directory.glob('*mtp_372.o')).read_bytes()
assert device==out.raw, 'decompressed image differs from CNCC device object'
params={name.removeprefix('.params.text.'):list(struct.unpack('<'+'I'*(len(data)//4),data)) for name,data in sections(out.raw) if name.startswith('.params.text.')}
print(json.dumps(dict(outer_size=len(image),outer_sha256=hashlib.sha256(image).hexdigest(),cnfb_offset=offset,
    packed=packed,padded_device_size=padded,expanded_device_size=expanded,compressed_size=compressed,
    device_object_byte_equal=True,device_sha256=hashlib.sha256(device).hexdigest(),params_sizes_then_offsets=params),indent=2))
