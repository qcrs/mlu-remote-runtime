#!/usr/bin/env python3
"""G4 Native differential, paired client callbacks and lifecycle evidence."""
import argparse,hashlib,json,os,pathlib,socket,subprocess,time
ROOT=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--out',default='evidence/graph-callbacks-20261107');p.add_argument('--port',type=int,default=50267)
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
    return '\n'.join(s for s in r.stdout.splitlines() if s.startswith(('PASS ','dot ')))
try:
    with (out/'server.log').open('w') as log:
        server=subprocess.Popen([str(ROOT/'build/mlu-remote-server')],cwd=ROOT,env=dict(base,MLU_REMOTE_BIND='127.0.0.1',MLU_REMOTE_PORT=str(a.port)),stdout=log,stderr=subprocess.STDOUT)
    for _ in range(100):
        if server.poll() is not None:raise RuntimeError('server exited')
        try:
            with socket.create_connection(('127.0.0.1',a.port),.1):break
        except OSError:time.sleep(.05)
    else:raise RuntimeError('server not listening')
    for family in ('runtime','driver'):
        cmd=[str(ROOT/('build/native_graph_callbacks_'+family)),'all',str(out/(family+'.dot'))]
        expected=run(family+'-native',cmd,base)
        for name,env in [('remote',remote),('preload',preload),('isolated',dict(remote,MLU_REMOTE_SHIM='both'))]:
            command=cmd if name!='isolated' else ['bash','scripts/run-isolated-client.sh',*cmd]
            actual=run(family+'-'+name,command,env);assert actual==expected,(family,name)
            if name=='isolated':assert 'VERIFIED vendor SDK path and /dev hidden' in (out/(family+'-isolated.log')).read_text()
        for mode in ('reentry','reset','abrupt','lease'):
            run(family+'-'+mode,[cmd[0],mode],remote)
    run('protocol',['python3','tests/graph_callbacks_protocol.py'],remote)
    run('submit-disconnect',['python3','tests/graph_callbacks_disconnect.py'],remote)
    # A fresh process also confirms admission after EOF cleanup.
    run('after-cleanup',[str(ROOT/'build/native_graph_callbacks_driver'),'all',str(out/'after.dot')],remote)
    server.terminate();server.wait(timeout=15);server=None
    log=(out/'server.log').read_text()
    assert 'CLEANUP_QUARANTINED' not in log and 'SESSION_CLEANUP_FAILED' not in log
    assert 'RESOURCE_TOTAL device=0 staging=0 image=0' in log and 'DRAIN_COMPLETE workers=0 cleanup_errors=0' in log
    with (out/'quota-server.log').open('w') as log:
        server=subprocess.Popen([str(ROOT/'build/mlu-remote-server')],cwd=ROOT,env=dict(base,MLU_REMOTE_BIND='127.0.0.1',MLU_REMOTE_PORT=str(a.port),MLU_REMOTE_SESSION_STAGING_BYTES='192'),stdout=log,stderr=subprocess.STDOUT)
    for _ in range(100):
        if server.poll() is not None:raise RuntimeError('quota server exited')
        try:
            with socket.create_connection(('127.0.0.1',a.port),.1):break
        except OSError:time.sleep(.05)
    else:raise RuntimeError('quota server not listening')
    run('quota',['python3','tests/graph_callbacks_protocol.py','--quota'],remote)
    server.terminate();server.wait(timeout=15);server=None
    log=(out/'quota-server.log').read_text()
    assert 'CLEANUP_QUARANTINED' not in log and 'SESSION_CLEANUP_FAILED' not in log
    assert 'RESOURCE_TOTAL device=0 staging=0 image=0' in log and 'DRAIN_COMPLETE workers=0 cleanup_errors=0' in log
finally:
    if server:server.terminate();server.wait(timeout=15)
    (out/'results.json').write_text(json.dumps(results,indent=2)+'\n')
    files=['tests/native_graph_callbacks.c','tests/graph_callbacks_protocol.py','tests/graph_callbacks_disconnect.py','scripts/verify-graph-callbacks.py','scripts/build.sh','client/graph_callback_client.inc','client/graph_callback_invoke.inc','client/graph_host_client.inc','client/runtime_context.c','server/callback_bridge.c','server/callback_bridge.h','server/graph_callback_backend.inc','server/graph_control_backend.inc','server/graph_backend.inc','server/graph_host_backend.inc','server/graph_host_views.inc','server/mlu_backend.c','server/mlu_backend.h','server/mlu_remote_server.c','server/object_registry.h','shim/cnrt_shim.c','shim/cndrv_shim.c','schema/mlu_api_schema.json','include/remote_callback.h','include/remote_cnrt.h','include/remote_protocol.h','include/generated/typed_api.h','include/generated/typed_opcodes.inc','build/mlu-remote-server','build/libremote_cnrt.so','build/shim/libcnrt.so','build/driver-shim/libcndrv.so','build/native_graph_callbacks_runtime','build/native_graph_callbacks_driver','/usr/local/neuware/include/cnrt.h','/usr/local/neuware/include/cn_api.h']
    (out/'provenance.json').write_text(json.dumps({f:hashlib.sha256((ROOT/f).read_bytes()).hexdigest() for f in files},indent=2)+'\n')
print('PASS graph callbacks:',len(results),'process scenarios')
