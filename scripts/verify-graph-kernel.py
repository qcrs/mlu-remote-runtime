#!/usr/bin/env python3
"""Native and separate-process Graph kernel matrix, including driverless clients."""
import argparse
import hashlib
import json
import os
import pathlib
import shutil
import socket
import subprocess
import time

ROOT=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--out',default='evidence/graph-g2-kernel-20261107');p.add_argument('--port',type=int,default=50254)
a=p.parse_args();out=ROOT/a.out;out.mkdir(parents=True,exist_ok=True)
base=dict(os.environ,LD_LIBRARY_PATH='/usr/local/neuware/lib64')
base.pop('LD_PRELOAD',None);base.pop('MLU_TEST_SHIM',None)
remote=dict(base,MLU_REMOTE_HOST='127.0.0.1',MLU_REMOTE_PORT=str(a.port),MLU_TEST_SHIM='1',
    LD_LIBRARY_PATH=':'.join(str(ROOT/d) for d in ('build/shim','build/driver-shim','build')))
preload=dict(base,MLU_REMOTE_HOST='127.0.0.1',MLU_REMOTE_PORT=str(a.port),MLU_TEST_SHIM='1',
    LD_PRELOAD=':'.join(str(ROOT/d) for d in ('build/shim/libcnrt.so','build/driver-shim/libcndrv.so')))
image=str(ROOT/'build/graph-kernel-driver/native_graph_kernel-bang-mlisa-cambricon-bang.cnfatbin')
results=[]
def command(family,mode='normal'):
    return [str(ROOT/('build/native_graph_kernel_'+family)),mode]+([image] if family=='driver' else [])
def run(name,cmd,env):
    r=subprocess.run(cmd,cwd=ROOT,env=env,capture_output=True,text=True,timeout=120)
    (out/(name+'.log')).write_text(r.stdout);(out/(name+'.stderr')).write_text(r.stderr)
    results.append(dict(name=name,command=cmd,returncode=r.returncode));print(name,r.returncode,flush=True)
    if r.returncode: raise RuntimeError(name+': '+r.stderr)
    return '\n'.join(s for s in r.stdout.splitlines() if s.startswith(('PASS ','attribute_status=')))

server=None
try:
    with (out/'server.log').open('w') as log:
        server=subprocess.Popen([str(ROOT/'build/mlu-remote-server')],cwd=ROOT,
            env=dict(base,MLU_REMOTE_BIND='127.0.0.1',MLU_REMOTE_PORT=str(a.port)),stdout=log,stderr=subprocess.STDOUT)
        for _ in range(100):
            if server.poll() is not None: raise RuntimeError('server exited')
            try:
                with socket.create_connection(('127.0.0.1',a.port),.1): break
            except OSError: time.sleep(.05)
        else: raise RuntimeError('server not listening')
        for family in ('runtime','driver'):
            cmd=command(family);expected=run(family+'-native',cmd,base)
            for mode,env in [('remote',remote),('preload',preload),('isolated',dict(remote,MLU_REMOTE_SHIM='both'))]:
                actual=run(family+'-'+mode,cmd if mode!='isolated' else ['bash','scripts/run-isolated-client.sh',*cmd],env)
                if actual!=expected: raise RuntimeError(family+' mismatch: '+mode)
            for mode in ('lease','reset','abrupt'):run(family+'-'+mode,command(family,mode),remote)
        run('protocol',['python3','tests/graph_kernel_protocol.py'],remote)
        run('after-cleanup',command('driver'),remote)
        server.terminate();server.wait(timeout=15);server=None
    logtext=(out/'server.log').read_text()
    if ('CLEANUP_QUARANTINED' in logtext or 'RESOURCE_TOTAL device=0 staging=0 image=0' not in logtext or
        'DRAIN_COMPLETE workers=0 cleanup_errors=0' not in logtext or logtext.count('GRAPH_CLEANUP')<4):
        raise RuntimeError('server did not drain graphs and all resource charges cleanly')
    run('backend-resource-rollback',[str(ROOT/'build/backend-resources-test'),str(ROOT/'build/module-audit/native_bang_module-bang-mlisa-cambricon-bang.cnfatbin')],base)
    with (out/'quota-server.log').open('w') as log:
        server=subprocess.Popen([str(ROOT/'build/mlu-remote-server')],cwd=ROOT,
            env=dict(base,MLU_REMOTE_BIND='127.0.0.1',MLU_REMOTE_PORT=str(a.port),MLU_REMOTE_SESSION_STAGING_BYTES='1024'),stdout=log,stderr=subprocess.STDOUT)
        for _ in range(100):
            if server.poll() is not None:raise RuntimeError('quota server exited')
            try:
                with socket.create_connection(('127.0.0.1',a.port),.1):break
            except OSError:time.sleep(.05)
        else:raise RuntimeError('quota server not listening')
        run('quota',['python3','tests/graph_kernel_protocol.py','--quota'],remote)
        server.terminate();server.wait(timeout=15);server=None
    quota_log=(out/'quota-server.log').read_text()
    if 'RESOURCE_TOTAL device=0 staging=0 image=0' not in quota_log or 'DRAIN_COMPLETE workers=0 cleanup_errors=0' not in quota_log:
        raise RuntimeError('quota rejection leaked resource charges')
finally:
    if server:server.terminate();server.wait(timeout=15)
    (out/'results.json').write_text(json.dumps(results,indent=2)+'\n')
    files=['tests/native_graph_kernel.mlu','tests/graph_kernel_protocol.py','tests/backend_resources.c','scripts/verify-graph-kernel.py',
        'scripts/build.sh','scripts/api_codegen.py','scripts/buffer_codegen.py','server/graph_backend.inc',
        'server/graph_host_backend.inc','server/graph_host_views.inc','client/graph_host_client.inc','client/runtime_context.c','client/host_memory.c',
        'server/mlu_backend.c','server/mlu_backend.h',
        'include/graph_kernel_wire.h','shim/graph_kernel.inc','shim/cnrt_shim.c','shim/cndrv_shim.c','schema/mlu_api_schema.json',
        'include/generated/typed_api.h','include/generated/typed_opcodes.inc','build/mlu-remote-server','build/shim/libcnrt.so',
        'build/driver-shim/libcndrv.so','build/native_graph_kernel_runtime','build/native_graph_kernel_driver','build/backend-resources-test',
        image,'/usr/local/neuware/include/cnrt.h','/usr/local/neuware/include/cn_api.h']
    (out/'provenance.json').write_text(json.dumps({f:hashlib.sha256((ROOT/f).read_bytes()).hexdigest() for f in files},indent=2)+'\n')
    for family in ('runtime','driver'):shutil.copyfile(ROOT/('build/graph-kernel-'+family+'/manifest.json'),out/(family+'-metadata.json'))
print('PASS graph kernel:',len(results),'scenarios; Native/replacement/preload/isolated and resource drain')
