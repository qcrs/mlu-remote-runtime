#!/usr/bin/env python3
"""Capture Native differential contracts and separate-process numerical replay."""
import argparse,hashlib,json,os,pathlib,shutil,socket,subprocess,time
ROOT=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--out',default='evidence/graph-capture-20261107');p.add_argument('--port',type=int,default=50259)
a=p.parse_args();out=ROOT/a.out;out.mkdir(parents=True,exist_ok=True)
base=dict(os.environ,LD_LIBRARY_PATH='/usr/local/neuware/lib64');base.pop('LD_PRELOAD',None);base.pop('MLU_TEST_SHIM',None)
remote=dict(base,MLU_REMOTE_HOST='127.0.0.1',MLU_REMOTE_PORT=str(a.port),MLU_TEST_SHIM='1',LD_LIBRARY_PATH=':'.join(str(ROOT/d) for d in ('build/shim','build/driver-shim','build')))
preload=dict(base,MLU_REMOTE_HOST='127.0.0.1',MLU_REMOTE_PORT=str(a.port),MLU_TEST_SHIM='1',LD_PRELOAD=':'.join(str(ROOT/d) for d in ('build/shim/libcnrt.so','build/driver-shim/libcndrv.so')))
results=[];server=None

def run(name,cmd,env):
    r=subprocess.run(cmd,cwd=ROOT,env=env,capture_output=True,text=True,timeout=120)
    (out/(name+'.log')).write_text(r.stdout);(out/(name+'.stderr')).write_text(r.stderr)
    results.append(dict(name=name,command=cmd,returncode=r.returncode));print(name,r.returncode,flush=True)
    if r.returncode:raise RuntimeError(name+': '+r.stderr)
    return r.stdout

def normalized(raw):
    rows=[];ids={}
    for line in raw.splitlines():
        row=json.loads(line)
        if 'pid' in row or 'sdk_library_version' in row:continue
        if 'id' in row and row['id']!=123:
            key=row['id'];ids.setdefault(key,len(ids)+1);row['id']=ids[key]
        if 'pass' in row:assert row['pass'],row
        rows.append(row)
    return rows

def native_contract(raw,scene,mode):
    rows=normalized(raw);calls={r['call']:r['status'] for r in rows if 'call' in r}
    assert calls['begin']==0
    if scene=='thread':assert calls['end_other_thread']==(100362 if mode==0 else 0)
    elif scene in ('sync','query','wrong_root'):assert calls['end']==100361
    elif scene=='unjoined':assert calls['end']==100364
    elif scene in ('unsafe','unsafe_thread','contexts'):assert calls['end']==(100361 if mode==0 else 0)
    else:
        if 'end' in calls:assert calls['end']==0
    if scene=='merge':assert calls['merge_wait']==100363
    if scene=='isolation':assert calls['captured_wait_external']==100365
    if scene=='wrong_root':assert calls['end_branch']==100366 and calls['enqueue_invalid']==100361
    return rows

def start(logname,quota=None):
    global server
    env=dict(base,MLU_REMOTE_BIND='127.0.0.1',MLU_REMOTE_PORT=str(a.port))
    if quota:env['MLU_REMOTE_SESSION_STAGING_BYTES']=str(quota)
    with (out/logname).open('w') as log:server=subprocess.Popen([str(ROOT/'build/mlu-remote-server')],cwd=ROOT,env=env,stdout=log,stderr=subprocess.STDOUT)
    for _ in range(100):
        if server.poll() is not None:raise RuntimeError('server exited')
        try:
            with socket.create_connection(('127.0.0.1',a.port),.1):return
        except OSError:time.sleep(.05)
    raise RuntimeError('server not listening')

def stop(logname):
    global server
    server.terminate();server.wait(timeout=15);server=None
    text=(out/logname).read_text()
    assert 'CLEANUP_QUARANTINED' not in text
    assert 'RESOURCE_TOTAL device=0 staging=0 image=0' in text
    assert 'DRAIN_COMPLETE workers=0 cleanup_errors=0' in text

try:
    start('server.log')
    scenes=[('deps',2),('thread',0),('thread',1),('thread',2),('sync',2),('query',2),('contextsync',0),('contextsync',1),('contextsync',2),('fork',2),('unjoined',2),('wrong_root',2),('merge',2),('unsafe',0),('unsafe',1),('unsafe',2),('unsafe_thread',0),('isolation',2)]
    for family in ('runtime','driver'):
        for scene,mode in scenes+([('contexts',0),('contexts',2)] if family=='driver' else []):
            cmd=[str(ROOT/('build/probes/probe-capture-'+family)),scene,str(mode)];prefix=f'{family}-{scene}-{mode}'
            expected=native_contract(run(prefix+'-native',cmd,base),scene,mode)
            actual=normalized(run(prefix+'-remote',cmd,remote));assert expected==actual,(prefix,expected,actual)
        cmd=[str(ROOT/('build/native_graph_capture_'+family)),'normal']+([str(ROOT/'build/graph-capture-driver/native_graph_capture-bang-mlisa-cambricon-bang.cnfatbin')] if family=='driver' else [])
        expected=run(family+'-numeric-native',cmd,base)
        for name,env in [('remote',remote),('preload',preload),('isolated',dict(remote,MLU_REMOTE_SHIM='both'))]:
            actual=run(family+'-numeric-'+name,cmd if name!='isolated' else ['bash','scripts/run-isolated-client.sh',*cmd],env)
            if name=='isolated':
                assert 'VERIFIED vendor SDK path and /dev hidden' in actual
                actual=''.join(line+'\n' for line in actual.splitlines() if line.startswith('PASS '))
            assert actual==expected,(family,name)
        for mode in ('reset','abrupt'):run(family+'-'+mode,[cmd[0],mode,*cmd[2:]],remote)
        for scene,mode in [('thread',0),('fork',2),('unsafe_thread',0)]:
            cmd=[str(ROOT/('build/probes/probe-capture-'+family)),scene,str(mode)]
            expected=normalized((out/f'{family}-{scene}-{mode}-native.log').read_text())
            assert normalized(run(f'{family}-{scene}-{mode}-preload',cmd,preload))==expected
    run('protocol',['python3','tests/graph_capture_protocol.py'],remote);stop('server.log')
    for quota,flag in [(1024,'--begin-quota'),(40000,'--quota')]:
        log=f'quota-{quota}-server.log';start(log,quota);run(f'quota-{quota}',['python3','tests/graph_capture_protocol.py',flag],remote);stop(log)
finally:
    if server:server.terminate();server.wait(timeout=15)
    (out/'results.json').write_text(json.dumps(results,indent=2)+'\n')
    files=['tests/native_graph_capture.mlu','probes/probe_graph_capture.c','tests/graph_capture_protocol.py','scripts/verify-graph-capture.py','scripts/build-graph-capture.sh','scripts/build.sh','scripts/api_codegen.py','client/graph_capture_client.inc','client/runtime_context.c','server/graph_capture_backend.inc','server/graph_backend.inc','server/mlu_backend.c','server/mlu_backend.h','server/mlu_remote_server.c','shim/graph_kernel.inc','shim/cnrt_shim.c','shim/cndrv_shim.c','schema/mlu_api_schema.json','include/remote_cnrt.h','include/remote_protocol.h','include/generated/typed_api.h','include/generated/typed_opcodes.inc','build/mlu-remote-server','build/libremote_cnrt.so','build/shim/libcnrt.so','build/driver-shim/libcndrv.so','/usr/local/neuware/include/cn_api.h','/usr/local/neuware/include/cnrt.h']
    for family in ('runtime','driver'):
        files.extend([f'build/probes/probe-capture-{family}',f'build/native_graph_capture_{family}',f'build/graph-capture-{family}/native_graph_capture-bang-mlisa-cambricon-bang.cnfatbin'])
        shutil.copyfile(ROOT/f'build/graph-capture-{family}/manifest.json',out/(family+'-metadata.json'))
    (out/'provenance.json').write_text(json.dumps({f:hashlib.sha256((ROOT/f).read_bytes()).hexdigest() for f in files},indent=2)+'\n')
print('PASS graph capture:',len(results),'process scenarios')
