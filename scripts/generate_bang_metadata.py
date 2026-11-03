#!/usr/bin/env python3
"""Attach compiler-derived parameter types to a CNCC host ELF (no runtime SDK).

Each --pair is DEVICE_LL:EMBEDDED_CNFATBIN from the same compilation. Size and
offset ground truth remains the CNCC device ELF .params.text section. Unsupported
IR types are rejected, never guessed from pointer-shaped values or symbol names.
"""
import argparse, json, pathlib, re, struct, subprocess, tempfile

MAGIC = b'MLUABI01'
MAX_ARGS = 256

SCALARS = {'i8':1, 'i16':2, 'i32':4, 'i64':8, 'half':2, 'float':4, 'double':8}
DATA_LAYOUT = 'e-p:64:64:64-i64:64-v16:16-v32:32-n16:32:64'

def split_types(text):
    """Split LLVM lists without splitting nested arrays, structs or attributes."""
    parts, start, depth = [], 0, 0
    for i, char in enumerate(text):
        if char in '([{<': depth += 1
        elif char in ')]}>': depth -= 1
        elif char == ',' and depth == 0:
            parts.append(text[start:i].strip()); start = i+1
    if text[start:].strip(): parts.append(text[start:].strip())
    return parts

def aggregate_layout(token, definitions, active=()):
    """Measured CNCC LP64 layout subset; pointers and unsupported types fail closed."""
    token = token.strip()
    if token in SCALARS:
        size = SCALARS[token]
        return size, size, {'type': token, 'size': size, 'alignment': size}
    if token.startswith('%'):
        if token in active or token not in definitions:
            raise ValueError('unknown/recursive aggregate: '+token)
        return aggregate_layout(definitions[token], definitions, active+(token,))
    array = re.fullmatch(r'\[(\d+) x (.+)\]', token)
    if array:
        count = int(array[1]); size, align, element = aggregate_layout(array[2], definitions, active)
        total = count*size
        if not count or total > 4096: raise ValueError('aggregate array exceeds ABI limit')
        return total, align, dict(type='array', count=count, size=total, alignment=align, element=element)
    if token.startswith('{') and token.endswith('}'):
        cursor, alignment, fields = 0, 1, []
        for field in split_types(token[1:-1]):
            size, align, desc = aggregate_layout(field, definitions, active)
            cursor = (cursor+align-1)//align*align
            fields.append(dict(offset=cursor, layout=desc)); cursor += size
            alignment = max(alignment, align)
        total = (cursor+alignment-1)//alignment*alignment
        if not total or total > 4096: raise ValueError('aggregate exceeds ABI limit')
        return total, alignment, dict(type='struct', size=total, alignment=alignment, fields=fields)
    raise ValueError('unsupported aggregate member: '+token)

def fingerprint(data):
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & ((1 << 64) - 1)
    return value

def signatures(ir, layouts=None):
    result = []
    definitions = dict(re.findall(r'^(%[\w.$]+) = type (.+)$', ir, re.M))
    for name, params in re.findall(r'^define\s+[^\n]*?mlisa_kernel\s+void\s+@([\w.$]+)\(([^\n]*)\)', ir, re.M):
        types = []
        for param in split_types(params):
            param = param.strip()
            if 'addrspace' in param:
                raise ValueError(f'{name}: address-space parameter requires ABI review')
            byval = re.search(r'byval\((%[\w.$]+)\)\s+align\s+(\d+)', param)
            if 'byval' in param:
                if not byval or f'target datalayout = "{DATA_LAYOUT}"' not in ir:
                    raise ValueError(f'{name}: unknown byval ABI/data layout')
                size, alignment, desc = aggregate_layout(byval[1], definitions)
                if int(byval[2]) != alignment:
                    raise ValueError(f'{name}: byval alignment mismatch')
                types.append((size, 2))
                if layouts is not None: layouts[(name, len(types)-1)] = desc
                continue
            token = param.split()[0]
            if re.fullmatch(r'(?:i8|i16|i32|i64|half|float|double)\*',token):
                size, pointer = 8, 1
            elif token in SCALARS:
                size, pointer = SCALARS[token], 0
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
        layouts = {}
        for name, types in signatures(ll.read_text(), layouts):
            key=(len(raw),digest,name)
            if key in seen: raise ValueError('duplicate image/symbol metadata: '+name)
            seen.add(key)
            record = struct.pack('<QQ256sI', len(raw), digest, name.encode(), len(types))
            record += b''.join(struct.pack('<IB', *t) for t in types)
            record += bytes((MAX_ARGS-len(types))*5)
            records.append(record)
            manifest.append(dict(symbol=name, image_size=len(raw), fingerprint=hex(digest),
                                 parameters=[dict(size=s, kind=('scalar','pointer','aggregate')[k],
                                                  **({'layout': layouts[(name,i)]} if k==2 else {}))
                                             for i,(s,k) in enumerate(types)]))
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
