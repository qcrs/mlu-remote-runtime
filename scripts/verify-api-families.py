#!/usr/bin/env python3
"""Native/remote family comparisons and compiler ABI checks, over local TCP."""
import argparse, json, os, pathlib, socket, subprocess, time
ROOT=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(); p.add_argument('--out',default='evidence/api-expansion-20261102'); p.add_argument('--port',type=int,default=50203); a=p.parse_args()
out=ROOT/a.out; out.mkdir(parents=True,exist_ok=True)
base=dict(os.environ,LD_LIBRARY_PATH=os.environ.get('NEUWARE_HOME','/usr/local/neuware')+'/lib64')
base.pop('LD_PRELOAD',None); base.pop('MLU_TEST_SHIM',None)
remote=dict(base,MLU_REMOTE_HOST='127.0.0.1',MLU_REMOTE_PORT=str(a.port),MLU_TEST_SHIM='1',
            LD_LIBRARY_PATH=':'.join(str(ROOT/x) for x in ('build/shim','build/driver-shim','build')))
results=[]
def run(name,command,env):
    r=subprocess.run(command,env=env,cwd=ROOT,capture_output=True,text=True,timeout=120)
    (out/(name+'.log')).write_text(r.stdout); (out/(name+'.stderr')).write_text(r.stderr)
    results.append(dict(name=name,command=command,returncode=r.returncode))
    print(name,r.returncode,flush=True)
    if r.returncode: raise RuntimeError(name+': '+r.stderr)
    return r.stdout
log=(out/'server.log').open('w')
s=subprocess.Popen([str(ROOT/'build/mlu-remote-server')],env=dict(base,MLU_REMOTE_BIND='127.0.0.1',MLU_REMOTE_PORT=str(a.port)),stdout=log,stderr=subprocess.STDOUT,cwd=ROOT)
try:
    for _ in range(100):
        if s.poll() is not None: raise RuntimeError('server exited')
        try: socket.create_connection(('127.0.0.1',a.port),.1).close(); break
        except OSError: time.sleep(.05)
    else: raise RuntimeError('server did not listen')
    image=str(ROOT/'build/audit/native_bang_abi-bang-mlisa-cambricon-bang.cnfatbin')
    for family,extra in [('runtime',[]),('driver',['driver',image])]:
        cmd=[str(ROOT/'build/native_api_families'),*extra]
        native=run(family+'-native',cmd,base)
        for mode,env in [('remote',remote),('preload',dict(base,MLU_REMOTE_PORT=str(a.port),MLU_TEST_SHIM='1',LD_PRELOAD=str(ROOT/'build/shim/libcnrt.so')+':'+str(ROOT/'build/driver-shim/libcndrv.so')))]:
            actual=run(family+'-'+mode,cmd,env)
            if native!=actual: raise RuntimeError(f'{family} {mode} observations differ')
        isolated=run(family+'-isolated',['bash','scripts/run-isolated-client.sh',*cmd],dict(remote,MLU_REMOTE_SHIM='both'))
        if not isolated.endswith(native): raise RuntimeError('isolated observations differ')
    cmd=[str(ROOT/'build/native_bang_metadata')]
    native=run('metadata-native',cmd,base)
    actual=run('metadata-remote',cmd,remote); assert actual==native
    actual=run('metadata-preload',cmd,dict(base,MLU_REMOTE_PORT=str(a.port),LD_PRELOAD=str(ROOT/'build/shim/libcnrt.so'))); assert actual==native
    actual=run('metadata-isolated',['bash','scripts/run-isolated-client.sh',*cmd],remote); assert actual.endswith(native)
    run('typed-protocol',['python3','tests/typed_api_protocol.py'],remote)
    run('typed-client-faults',['python3','tests/typed_client_faults.py'],remote)
    run('metadata-validation',['python3','tests/bang_metadata.py'],remote)
finally:
    s.terminate(); s.wait(timeout=15); log.close()
    (out/'results.json').write_text(json.dumps(results,indent=2)+'\n')
