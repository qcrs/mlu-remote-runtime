#!/usr/bin/env python3
"""G1 Native/Remote matrix with independent server and isolated clients."""
import argparse
import hashlib
import json
import os
import pathlib
import socket
import subprocess
import time

ROOT=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser()
p.add_argument('--out',default='evidence/graph-g1-20261107')
p.add_argument('--port',type=int,default=50249)
a=p.parse_args(); out=ROOT/a.out; out.mkdir(parents=True,exist_ok=True)
base=dict(os.environ,LD_LIBRARY_PATH=os.environ.get('NEUWARE_HOME','/usr/local/neuware')+'/lib64')
base.pop('LD_PRELOAD',None); base.pop('MLU_TEST_SHIM',None)
remote=dict(base,MLU_REMOTE_HOST='127.0.0.1',MLU_REMOTE_PORT=str(a.port),MLU_TEST_SHIM='1',
            LD_LIBRARY_PATH=':'.join(str(ROOT/d) for d in ('build/shim','build/driver-shim','build')))
preload=dict(base,MLU_REMOTE_HOST='127.0.0.1',MLU_REMOTE_PORT=str(a.port),MLU_TEST_SHIM='1',
             LD_PRELOAD=':'.join(str(ROOT/d) for d in ('build/shim/libcnrt.so','build/driver-shim/libcndrv.so')))
results=[]
def run(name,cmd,env):
    r=subprocess.run(cmd,cwd=ROOT,env=env,capture_output=True,text=True,timeout=120)
    (out/(name+'.log')).write_text(r.stdout); (out/(name+'.stderr')).write_text(r.stderr)
    results.append(dict(name=name,command=cmd,returncode=r.returncode))
    print(name,r.returncode,flush=True)
    if r.returncode: raise RuntimeError(name+': '+r.stderr)
    return r.stdout

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
            command=[str(ROOT/('build/native_graph_'+family))]
            expected=run(family+'-native',command,base)
            for mode,env in [('remote',remote),('preload',preload),('isolated',dict(remote,MLU_REMOTE_SHIM='both'))]:
                cmd=command if mode!='isolated' else ['bash','scripts/run-isolated-client.sh',*command]
                actual=run(family+'-'+mode,cmd,env)
                if mode=='isolated': actual=actual[actual.index('nodes_truncated='):]
                if actual!=expected: raise RuntimeError(family+' Native/Remote mismatch: '+mode)
            run(family+'-reset',command+['reset'],remote)
            run(family+'-abrupt',command+['abrupt'],remote)
        run('protocol',['python3','tests/graph_protocol.py'],remote)
        # Let workers process EOF, then confirm a new session remains usable.
        time.sleep(.15)
        run('after-cleanup',[str(ROOT/'build/native_graph_driver')],remote)
        server.terminate(); server.wait(timeout=10); server=None
    logtext=(out/'server.log').read_text()
    if 'CLEANUP_QUARANTINED' in logtext or 'status=' not in logtext or 'RESOURCE_TOTAL device=0 staging=0 image=0' not in logtext:
        raise RuntimeError('server did not drain cleanly')
    if logtext.count('GRAPH_CLEANUP')<5: raise RuntimeError('missing reset/EOF graph cleanup evidence')
finally:
    if server: server.terminate(); server.wait(timeout=10)
    (out/'results.json').write_text(json.dumps(results,indent=2)+'\n')
    files=['tests/native_graph.c','tests/graph_protocol.py','scripts/verify-graph.py','scripts/array_codegen.py',
           'scripts/api_codegen.py','scripts/build.sh','server/graph_backend.inc','server/mlu_backend.c',
           'server/mlu_backend.h','server/object_registry.h','include/remote_graph.h','schema/mlu_api_schema.json',
           'include/generated/typed_api.h','include/generated/typed_opcodes.inc',
           'build/mlu-remote-server','build/shim/libcnrt.so','build/driver-shim/libcndrv.so',
           'build/native_graph_runtime','build/native_graph_driver',
           '/usr/local/neuware/include/cnrt.h','/usr/local/neuware/include/cn_api.h']
    provenance={f:hashlib.sha256((ROOT/f).read_bytes()).hexdigest() for f in files}
    (out/'provenance.json').write_text(json.dumps(provenance,indent=2)+'\n')
print('PASS G1:',len(results),'scenarios; native/replacement/preload/isolated, reset/EOF and clean drain')
