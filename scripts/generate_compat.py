#!/usr/bin/env python3
"""Inventory preprocessed installed SDK declarations; preserve reviewed policy.

No API gets an implemented status merely because a symbol exists in the shim.
"""
import argparse, collections, csv, hashlib, io, json, os, pathlib, re, subprocess
ROOT = pathlib.Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser()
p.add_argument('--sdk', default=os.environ.get('NEUWARE_HOME','/usr/local/neuware'))
p.add_argument('--check', action='store_true')
args = p.parse_args()
sdk = pathlib.Path(args.sdk)
policy = json.loads((ROOT/'compat/annotations.json').read_text())
exports = {family:{line.split()[-1] for line in subprocess.check_output(
    ['nm','-D','--defined-only',str(ROOT/path)],text=True).splitlines() if line.split()}
    for family,path in [('CNRT','build/shim/libcnrt.so'),('CNDrv','build/driver-shim/libcndrv.so')]}
backend = (ROOT/'server/mlu_backend.c').read_text()
rows, hashes = [], {}
for family, header, prefix, return_type in [('CNRT','cnrt.h','cnrt','cnrtRet_t'),('CNDrv','cn_api.h','cn','CNresult')]:
    hashes[header] = hashlib.sha256((sdk/'include'/header).read_bytes()).hexdigest()
    source = subprocess.check_output([os.environ.get('CC','gcc'),'-E','-P','-x','c','-I'+str(sdk/'include'),'-include',header,'/dev/null'],text=True)
    pattern = rf'\b({return_type}|const\s+char\s*\*)\s*({prefix}[A-Z]\w*)\s*\(([^;{{}}]*)\)\s*;'
    apis = {name:' '.join((ret+' '+name+'('+params+');').split()) for ret,name,params in re.findall(pattern,source)}
    seen = set(re.findall(rf'\b({prefix}[A-Z]\w*)\s*\(',source))
    if seen != apis.keys(): raise SystemExit('unparsed declarations: '+repr(seen-apis.keys()))
    for name, declaration in sorted(apis.items()):
        note = policy.get(name,{})
        state = note.get('status','NOT_IMPLEMENTED')
        exported = name in exports[family]
        if state in ('IMPLEMENTED','PARTIAL_IMPLEMENTED','UNSUPPORTED') and not exported:
            raise SystemExit('reviewed API has no shim export: '+name)
        if exported and not note: raise SystemExit('unreviewed exported API: '+name)
        rows.append(dict(family=family,api=name,status=state,client_export=str(exported).lower(),
            backend_used=str(family=='CNDrv' and bool(re.search(r'\b'+name+r'\s*\(',backend))).lower(),
            native_test=note.get('native_test','not_run'),remote_test=note.get('remote_test','not_run'),
            boundary=note.get('boundary','No client export; loader may reject this API'),
            next_step=note.get('next_step','M3: typed driver shim and native/remote family probe' if family=='CNDrv' else 'M3: semantic review before schema/API expansion'),
            declaration=declaration))
buf = io.StringIO(); writer = csv.DictWriter(buf,fieldnames=list(rows[0]),lineterminator='\n'); writer.writeheader(); writer.writerows(rows)
summary = dict(headers_sha256=hashes,scope='installed preprocessed public headers; deprecated transitive declarations included; private BANG hooks counted separately',
    counts={f:dict(collections.Counter(r['status'] for r in rows if r['family']==f)) for f in ('CNRT','CNDrv')},
    totals={f:sum(r['family']==f for r in rows) for f in ('CNRT','CNDrv')},
    private_bang_hooks=sorted(x for x in exports['CNRT'] if x.startswith('__bang')),
    legacy_exports_outside_headers=sorted(x for x in exports['CNRT'] if x.startswith('cnrt') and x not in {r['api'] for r in rows}),
    generated_object_rpc_count=len(json.loads((ROOT/'schema/mlu_api_schema.json').read_text())['simple_rpc']))
for relative, text in [('compat/api-ledger.csv',buf.getvalue()),('compat/summary.json',json.dumps(summary,indent=2,ensure_ascii=False)+'\n')]:
    target=ROOT/relative
    if args.check:
        if not target.exists() or target.read_text()!=text: raise SystemExit('out of date: '+relative)
    else: target.write_text(text)
print(json.dumps(summary['counts'],ensure_ascii=False))
