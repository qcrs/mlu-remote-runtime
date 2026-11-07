#!/usr/bin/env python3
"""Cross-check schema -> generated protocol -> linked exports -> reviewed ledger.

Evidence is required for an IMPLEMENTED claim. This is a structural guard;
the native/remote suite supplies semantic validation and does not auto-promote.
"""
import csv, json, pathlib, re, subprocess, sys
ROOT=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
from api_codegen import validate
s=json.loads((ROOT/'schema/mlu_api_schema.json').read_text()); validate(s)
def check(condition,message):
    if not condition: raise SystemExit(message)
for script in ('generate_rpc.py','generate_compat.py'):
    subprocess.run([sys.executable,str(ROOT/'scripts'/script),'--check'],check=True)
ledger={r['api']:r for r in csv.DictReader((ROOT/'compat/api-ledger.csv').open())}
check(ledger.keys()=={a['name'] for a in s['apis']},'ledger census differs')
for lib,path in [('CNRT','build/shim/libcnrt.so'),('CNDrv','build/driver-shim/libcndrv.so')]:
    actual={line.split()[-1] for line in subprocess.check_output(['nm','-D','--defined-only',str(ROOT/path)],text=True).splitlines() if line.split()}
    expected={a['name'] for a in s['apis'] if a['library']==lib and a['implementation']!='none'}|set(s['extra_exports'][lib])
    check(actual==expected,f'{lib} export drift: extra={actual-expected}; missing={expected-actual}')
for a in s['apis']:
    check(a['status']==ledger[a['name']]['status'],'ledger status differs: '+a['name'])
    if a['status']=='IMPLEMENTED':
        for key in ('native_test','remote_test'):
            if a[key]=='not_run': raise SystemExit('complete API lacks evidence: '+a['name'])
            for path in a[key].split(';'):
                path=path.strip()
                evidence=ROOT/'evidence'/path if '/' in path else ROOT/'evidence/transparent-20261102'/path
                check(evidence.is_file() and evidence.stat().st_size>0,'missing evidence: '+str(evidence))
    if a['implementation']=='generated':
        check(bool(a.get('wrapper')),'generated API lacks wrapper: '+a['name'])
    if a.get('rpc_operations'):
        check(set(a['rpc_operations'])<=s['operations'].keys(),'unknown API opcode binding')
for rpc in s['typed_rpc']:
    check(bool(rpc['backend_binding']),'missing native binding')
    check(any(rpc['operation'] in a.get('rpc_operations',[]) for a in s['apis']),'unused typed RPC: '+rpc['name'])
    for file,pattern in [('typed_rpc_client.inc','mlu_remote_api_'+rpc['name']),('typed_rpc_server.inc','MLU_OP_'+rpc['operation']),('typed_backend.inc','mlu_backend_api_'+rpc['name'])]:
        check(pattern in (ROOT/'build/generated'/file).read_text(),'missing plumbing: '+pattern)
subprocess.run([sys.executable,str(ROOT/'scripts/verify-closure-audit.py')],check=True)
print(f'PASS API contracts: {len(s["apis"])} declarations; signatures, opcodes, codecs, native bindings, linked exports, ledger and evidence')
