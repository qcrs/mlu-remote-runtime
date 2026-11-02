#!/usr/bin/env python3
"""Attach compiler-derived parameter types to a CNCC host ELF (no runtime SDK).

Each --pair is DEVICE_LL:EMBEDDED_CNFATBIN from the same compilation. Size and
offset ground truth remains the CNCC device ELF .params.text section. Unsupported
IR types are rejected, never guessed from pointer-shaped values or symbol names.
"""
import argparse, json, pathlib, re, struct, subprocess, tempfile

MAGIC = b'MLUABI01'
MAX_ARGS = 256

def fingerprint(data):
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & ((1 << 64) - 1)
    return value

def signatures(ir):
    result = []
    for name, params in re.findall(r'^define\s+[^\n]*?mlisa_kernel\s+void\s+@([\w.$]+)\(([^\n]*)\)', ir, re.M):
        types = []
        for param in params.split(',') if params.strip() else []:
            param = param.strip()
            if 'byval' in param or 'addrspace' in param:
                raise ValueError(f'{name}: aggregate/address-space parameter requires ABI review')
            token = param.split()[0]
            if re.fullmatch(r'(?:i8|i16|i32|i64|half|float|double)\*',token):
                size, pointer = 8, 1
            elif token in ('i8', 'i16', 'i32', 'i64', 'half', 'float', 'double'):
                size, pointer = {'i8':1,'i16':2,'i32':4,'i64':8,'half':2,'float':4,'double':8}[token], 0
            else:
                raise ValueError(f'{name}: unsupported IR parameter {token}')
            types.append((size, pointer))
        if len(types) > MAX_ARGS or len(name.encode()) >= 256:
            raise ValueError('metadata exceeds protocol limits')
        result.append((name, types))
    if not result:
        raise ValueError('no mlisa_kernel definitions in compiler IR')
    return result

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host', required=True)
    p.add_argument('--pair', action='append', required=True)
    p.add_argument('--manifest', required=True)
    args = p.parse_args()
    records, manifest, seen = [], [], set()
    for pair in args.pair:
        ll, image = map(pathlib.Path, pair.split(':', 1))
        raw = image.read_bytes(); digest = fingerprint(raw)
        for name, types in signatures(ll.read_text()):
            key=(len(raw),digest,name)
            if key in seen: raise ValueError('duplicate image/symbol metadata: '+name)
            seen.add(key)
            record = struct.pack('<QQ256sI', len(raw), digest, name.encode(), len(types))
            record += b''.join(struct.pack('<IB', *t) for t in types)
            record += bytes((MAX_ARGS-len(types))*5)
            records.append(record)
            manifest.append(dict(symbol=name, image_size=len(raw), fingerprint=hex(digest),
                                 parameters=[dict(size=s, kind='pointer' if k else 'scalar') for s,k in types]))
    # A non-allocated ELF section works for executables and DSOs, without
    # exported helper symbols, process environment lookups or vendor libraries.
    blob = MAGIC + struct.pack('<I', len(records)) + b''.join(records)
    with tempfile.TemporaryDirectory() as tmp:
        metadata = pathlib.Path(tmp)/'abi.bin'; metadata.write_bytes(blob)
        subprocess.run(['objcopy','--remove-section','.mlu.remote.abi', args.host],check=True)
        subprocess.run(['objcopy','--add-section',f'.mlu.remote.abi={metadata}', args.host],check=True)
    pathlib.Path(args.manifest).write_text(json.dumps(manifest,indent=2)+'\n')
    print(f'BANG metadata: {len(records)} kernels attached to {args.host}')

if __name__ == '__main__':
    main()
