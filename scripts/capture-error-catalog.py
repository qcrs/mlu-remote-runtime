#!/usr/bin/env python3
"""Explicit SDK-builder step: capture native error strings for reviewed codegen.

Normal client builds consume the checked-in JSON and need neither SDK libraries
nor a device. Re-capture only when intentionally changing the target SDK.
"""
import argparse
import ctypes
import hashlib
import json
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser()
p.add_argument('--sdk', type=pathlib.Path, default=pathlib.Path('/usr/local/neuware'))
p.add_argument('--check', action='store_true')
args = p.parse_args()

def declarations(header, typename):
    text = re.sub(r'/\*.*?\*/', '', header.read_text(), flags=re.S)
    body = re.search(r'typedef enum\s*\{([^}]+)\}\s*' + typename + r'\s*;', text, re.S)[1]
    value = -1
    rows = []
    for item in body.split(','):
        name, sep, expression = item.strip().partition('=')
        if not name:
            continue
        # Current SDK enums use integer constants, never eval vendor text.
        value = int(expression, 0) if sep else value + 1
        rows.append({'name': name.strip(), 'value': value})
    return rows

headers = {'CNRT': ('cnrt.h', 'cnrtRet_t'), 'CNDrv': ('cn_api.h', 'CNresult')}
catalog = {'scope': 'Neuware 3.5.2; CNRT 6.5.2; CNDrv 2.5.2; AArch64',
           'headers_sha256': {}, 'declarations': {}}
for library, (filename, typename) in headers.items():
    path = args.sdk / 'include' / filename
    catalog['headers_sha256'][filename] = hashlib.sha256(path.read_bytes()).hexdigest()
    catalog['declarations'][library] = declarations(path, typename)
rt = ctypes.CDLL(str(args.sdk / 'lib64/libcnrt.so'))
drv = ctypes.CDLL(str(args.sdk / 'lib64/libcndrv.so'))
for name in ('cnrtGetErrorName', 'cnrtGetErrorStr'):
    fn = getattr(rt, name)
    fn.argtypes = [ctypes.c_int]
    fn.restype = ctypes.c_char_p
for name in ('cnGetErrorName', 'cnGetErrorString'):
    fn = getattr(drv, name)
    fn.argtypes = [ctypes.c_int, ctypes.POINTER(ctypes.c_char_p)]
    fn.restype = ctypes.c_int

def observe(code):
    row = {'value': code}
    for key, fn in [('runtime_name', rt.cnrtGetErrorName), ('runtime_description', rt.cnrtGetErrorStr)]:
        row[key] = fn(code).decode()
    for key, fn in [('driver_name', drv.cnGetErrorName), ('driver_description', drv.cnGetErrorString)]:
        output = ctypes.c_char_p()
        assert fn(code, ctypes.byref(output)) == 0 and output.value is not None
        row[key] = output.value.decode()
    return row

values = sorted({r['value'] for rows in catalog['declarations'].values() for r in rows})
catalog['entries'] = [observe(code) for code in values]
# Native CNRT's signed comparison forwards unlisted codes <= 199999 to
# CNDrv; larger codes use CNRT's own fallback. Reviewed against the installed
# library's lookup routine and the boundary samples below.
catalog['runtime_driver_fallback_max'] = 199999
unknown = [observe(code) for code in (-2147483648, -1, 123456, 199999, 200001, 999990, 999993, 0x7fffffff)]
catalog['fallback'] = {k: v for k, v in unknown[0].items() if k != 'value'}
catalog['runtime_fallback'] = {k: v for k, v in unknown[-1].items() if k.startswith('runtime_')}
catalog['unknown_samples'] = unknown
for row in unknown:
    expected = dict(catalog['fallback'])
    if row['value'] > catalog['runtime_driver_fallback_max']:
        expected.update(catalog['runtime_fallback'])
    assert {k: v for k, v in row.items() if k != 'value'} == expected
content = json.dumps(catalog, ensure_ascii=False, indent=2) + '\n'
path = ROOT / 'schema/error_catalog.json'
if args.check:
    assert path.read_text() == content, 'native error catalogue drift'
else:
    path.write_text(content)
print('PASS native error catalogue:', len(values), 'distinct codes;',
      {k: len(v) for k, v in catalog['declarations'].items()})
