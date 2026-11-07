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
schema = json.loads((ROOT/'schema/mlu_api_schema.json').read_text())
schema_apis = {a['name']:a for a in schema['apis']}
sdk = pathlib.Path(args.sdk)
exports = {family:{line.split()[-1] for line in subprocess.check_output(
    ['nm','-D','--defined-only',str(ROOT/path)],text=True).splitlines() if line.split()}
    for family,path in [('CNRT','build/shim/libcnrt.so'),('CNDrv','build/driver-shim/libcndrv.so')]}
backend = (ROOT/'server/mlu_backend.c').read_text()
# Native semantic adapters live beside the backend, rather than in generated
# fixed-field bindings. Count actual calls there too, without conflating exports.
backend += '\n'+'\n'.join(path.read_text() for path in sorted((ROOT/'server').glob('*.inc')))
rows, hashes = [], {}
for family, header, prefix, return_type in [('CNRT','cnrt.h','cnrt','cnrtRet_t'),('CNDrv','cn_api.h','cn','CNresult')]:
    hashes[header] = hashlib.sha256((sdk/'include'/header).read_bytes()).hexdigest()
    source = subprocess.check_output([os.environ.get('CC','gcc'),'-E','-P','-x','c','-I'+str(sdk/'include'),'-include',header,'/dev/null'],text=True)
    enums = set(re.findall(r'typedef\s+enum\b[^{}]*\{[^{}]*\}\s*(\w+)\s*;', source))
    pattern = rf'\b({return_type}|const\s+char\s*\*)\s*({prefix}[A-Z]\w*)\s*\(([^;{{}}]*)\)\s*;'
    apis = {name:' '.join((ret+' '+name+'('+params+');').split()) for ret,name,params in re.findall(pattern,source)}
    seen = set(re.findall(rf'\b({prefix}[A-Z]\w*)\s*\(',source))
    if seen != apis.keys(): raise SystemExit('unparsed declarations: '+repr(seen-apis.keys()))
    for name, declaration in sorted(apis.items()):
        if name not in schema_apis: raise SystemExit('declaration absent from schema: '+name)
        note = schema_apis[name]
        if re.sub(r'\s+','',declaration) != re.sub(r'\s+','',note['signature']):
            raise SystemExit('SDK/schema signature drift: '+name)
        if note['library']!=family: raise SystemExit('schema library drift: '+name)
        for param in note['parameters']:
            c_type=param['c_type']
            if '*' not in c_type and (c_type in enums or c_type.startswith('enum ')) and param['kind']!='enum':
                raise SystemExit('SDK/schema enum kind drift: '+name+'.'+param['name'])
        state = schema_apis.get(name,{}).get('status', note.get('status','NOT_IMPLEMENTED'))
        exported = name in exports[family]
        if state in ('IMPLEMENTED','PARTIAL_IMPLEMENTED','UNSUPPORTED') and not exported:
            raise SystemExit('reviewed API has no shim export: '+name)
        if exported and note.get('implementation','none') == 'none': raise SystemExit('unreviewed exported API: '+name)
        rows.append(dict(family=family,api=name,feature_family=note['family'],priority=note['priority'],status=state,client_export=str(exported).lower(),
            backend_used=str(bool(re.search(r'\b'+name+r'\s*\(',backend+'\n'+(ROOT/'build/generated/typed_backend.inc').read_text()))).lower(),
            native_test=note.get('native_test','not_run'),remote_test=note.get('remote_test','not_run'),
            boundary=note.get('boundary','No client export; loader may reject this API'),
            blocker=note.get('blocker',''),
            gap=note.get('closure',{}).get('gap',''),
            fix=note.get('closure',{}).get('fix',''),
            acceptance=note.get('closure',{}).get('acceptance',''),
            next_step=schema_apis.get(name,{}).get('next_step',note.get('next_step','M3: typed driver shim and native/remote family probe' if family=='CNDrv' else 'M3: semantic review before schema/API expansion')),
            declaration=declaration))
if {r['api'] for r in rows} != schema_apis.keys(): raise SystemExit('schema contains non-public declarations')
buf = io.StringIO(); writer = csv.DictWriter(buf,fieldnames=list(rows[0]),lineterminator='\n'); writer.writeheader(); writer.writerows(rows)
summary = dict(headers_sha256=hashes,scope='MLU370-S4, single device, Neuware 3.5.2, CNRT 6.5.2, CNDrv 2.5.2, AArch64 -> AArch64; installed public headers including deprecated declarations; private BANG hooks separate',
    counts={f:dict(collections.Counter(r['status'] for r in rows if r['family']==f)) for f in ('CNRT','CNDrv')},
    totals={f:sum(r['family']==f for r in rows) for f in ('CNRT','CNDrv')},
    private_bang_hooks=sorted(x for x in exports['CNRT'] if x.startswith('__bang')),
    legacy_exports_outside_headers=sorted(x for x in exports['CNRT'] if x.startswith('cnrt') and x not in {r['api'] for r in rows}),
    generated_object_rpc_count=len(schema['simple_rpc']),generated_typed_rpc_count=len(schema['typed_rpc']),generated_public_wrapper_count=sum(bool(a.get('wrapper')) for a in schema['apis']),generated_local_api_count=sum(a['implementation']=='generated_local' for a in schema['apis']))
annotations={a['name']:{k:a[k] for k in ('status','boundary','native_test','remote_test','next_step','blocker')} | ({'closure':a['closure']} if a.get('closure') else {}) for a in schema['apis'] if a['implementation']!='none' or a['status'] in ('BLOCKED','OUT_OF_SCOPE')}
families=[]
for family in sorted({a['family'] for a in schema['apis']}):
    apis=[a for a in schema['apis'] if a['family']==family]
    families.append(dict(family=family,priority=min(a['priority'] for a in apis),counts=dict(collections.Counter(a['status'] for a in apis)),
        remaining=[dict(api=a['name'],status=a['status'],priority=a['priority'],depends_on=a['depends_on'],blocker=a['blocker'],next_step=a['next_step'],closure=a.get('closure',{}),
                        **({'contract_stage':a['contract_stage']} if a.get('contract_stage') else {})) for a in apis if a['status']!='IMPLEMENTED']))
    stages=[stage for stage in schema.get('graph_ipc_plan',{}).get('stages',[]) if stage['family']==family]
    if stages: families[-1]['implementation_stages']=stages
plan=['# API Census / 功能族计划','','由 `scripts/generate_compat.py` 从当前 SDK 声明及 Schema 生成。完整、部分、阻塞、范围外、拒绝、未实现是不同状态；剩余项的逐 API 依赖、阻塞和下一步见 `family-plan.json`。',
      '','| 功能族 | 公共声明 | 完整 | 部分 | 阻塞 | 范围外 | 拒绝 | 未实现 | 优先级 |','|---|---:|---:|---:|---:|---:|---:|---:|---:|']
for f in families:
    c=f['counts']; plan.append('| '+f['family']+' | '+str(sum(c.values()))+' | '+' | '.join(str(c.get(k,0)) for k in ('IMPLEMENTED','PARTIAL_IMPLEMENTED','BLOCKED','OUT_OF_SCOPE','UNSUPPORTED','NOT_IMPLEMENTED'))+' | P'+str(f['priority'])+' |')
plan+=['','P0：设备/context/版本基础查询；P1：owned memory、stride copy、queue/notifier；P2：Host completion 与 kernel/module ABI；P3：Host 数值工具；P4：多设备 Peer；P5：Graph/IPC。具体 API 可在同一功能族中按依赖再排序。','']
if schema.get('graph_ipc_plan'):
    plan+=['## Graph / IPC 分批实现契约','','原生契约已验证，Remote 仍待实现；共享状态机、SDK 特例和验收见 [设计](../docs/GRAPH_IPC.md) 与 [证据](../evidence/'+schema['graph_ipc_plan']['evidence']+')。native_test 中的 Notifier 证据证明创建前提缺失，并非成功导出/导入。',
           '','| 批次 | 能力 | API 数 | 依赖 |','|---|---|---:|---|']
    for stage in schema['graph_ipc_plan']['stages']:
        count=sum(a.get('contract_stage')==stage['id'] for a in schema['apis'])
        plan.append('| '+stage['id']+' | '+stage['title']+' | '+str(count)+' | '+(', '.join(stage['depends_on']) or '无')+' |')
    plan+=['','每批具体缺口及验收见 `family-plan.json` 的 `implementation_stages`；该计划不新增 wire opcode、客户端导出或 IMPLEMENTED 声明。','']
for relative, text in [('compat/api-ledger.csv',buf.getvalue()),('compat/summary.json',json.dumps(summary,indent=2,ensure_ascii=False)+'\n'),
                       ('compat/annotations.json',json.dumps(annotations,indent=2,ensure_ascii=False)+'\n'),('compat/family-plan.json',json.dumps(families,indent=2,ensure_ascii=False)+'\n'),('compat/family-plan.md','\n'.join(plan))]:
    target=ROOT/relative
    if args.check:
        if not target.exists() or target.read_text()!=text: raise SystemExit('out of date: '+relative)
    else: target.write_text(text)
print(json.dumps(summary['counts'],ensure_ascii=False))
