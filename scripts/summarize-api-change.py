#!/usr/bin/env python3
"""Record a reproducible API delta against an explicit committed schema."""
import argparse, collections, hashlib, json, pathlib, subprocess
ROOT=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(); p.add_argument('--baseline',required=True); p.add_argument('--out',required=True); args=p.parse_args()
baseline=subprocess.check_output(['git','rev-parse',args.baseline],cwd=ROOT,text=True).strip()
old=json.loads(subprocess.check_output(['git','show',baseline+':schema/mlu_api_schema.json'],cwd=ROOT,text=True))
new=json.loads((ROOT/'schema/mlu_api_schema.json').read_text()); before={a['name']:a for a in old['apis']}
assert before.keys()=={a['name'] for a in new['apis']},'census changed; audit denominator before reporting a delta'
added=[]; changed=[]
for a in new['apis']:
    b=before[a['name']]
    record={k:a[k] for k in ('name','library','family','status','implementation')}
    if b['implementation']=='none' and a['implementation']!='none': added.append(record)
    elif a['status']!=b['status']: changed.append(dict(record,previous_status=b['status']))
def counts(s): return dict(collections.Counter(a['status'] for a in s['apis']))
report=dict(baseline_commit=baseline,census=len(new['apis']),new_apis=added,new_status_counts=dict(collections.Counter(a['status'] for a in added)),
            existing_status_changes=changed,before=counts(old),after=counts(new),
            generated_before=dict(typed_rpc=len(old['typed_rpc']),public_wrappers=sum(bool(a.get('wrapper')) for a in old['apis'])),
            generated_after=dict(typed_rpc=len(new['typed_rpc']),public_wrappers=sum(bool(a.get('wrapper')) for a in new['apis'])))
out=ROOT/args.out; out.mkdir(parents=True,exist_ok=True)
(out/'change-counts.json').write_text(json.dumps(report,indent=2)+'\n')
code=['typed_rpc_client.inc','typed_rpc_server.inc','typed_backend.inc','cnrt_api.inc','cndrv_api.inc','typed_success.inc','typed_backend_decls.inc']
paths=['build/mlu-remote-server','build/native_query_boundaries','build/probes/probe_query_semantics',
       'build/shim/libcnrt.so','build/driver-shim/libcndrv.so','build/libremote_cnrt.so',
       'schema/mlu_api_schema.json','include/generated/typed_api.h','include/generated/typed_opcodes.inc',
       'server/api_queries.inc','shim/driver_queries.inc']+['build/generated/'+f for f in code]
files=[]
for name in paths:
    data=(ROOT/name).read_bytes(); item=dict(path=name,bytes=len(data),sha256=hashlib.sha256(data).hexdigest())
    if name.startswith('build/generated/'): item['lines']=len(data.splitlines())
    files.append(item)
(out/'artifact-manifest.json').write_text(json.dumps(dict(generated_c_lines=sum(f.get('lines',0) for f in files),files=files),indent=2)+'\n')
print(json.dumps(dict(new_apis=len(added),new_status_counts=report['new_status_counts'],existing_status_changes=len(changed),totals=report['after']),ensure_ascii=False))
