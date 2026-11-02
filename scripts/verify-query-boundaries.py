#!/usr/bin/env python3
"""Native/remote, preload and namespace comparisons for the query package."""
import argparse, json, os, pathlib, socket, subprocess, time
ROOT=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(); p.add_argument('--out',default='evidence/query-boundaries-20261102'); p.add_argument('--port',type=int,default=50213); a=p.parse_args()
out=ROOT/a.out; out.mkdir(parents=True,exist_ok=True)
base=dict(os.environ,LD_LIBRARY_PATH=os.environ.get('NEUWARE_HOME','/usr/local/neuware')+'/lib64')
base.pop('LD_PRELOAD',None); base.pop('MLU_TEST_SHIM',None)
remote=dict(base,MLU_REMOTE_HOST='127.0.0.1',MLU_REMOTE_PORT=str(a.port),MLU_TEST_SHIM='1',
            LD_LIBRARY_PATH=':'.join(str(ROOT/x) for x in ('build/shim','build/driver-shim','build')))
results=[]
def run(name,cmd,env):
    r=subprocess.run(cmd,env=env,cwd=ROOT,capture_output=True,text=True,timeout=120)
    (out/(name+'.log')).write_text(r.stdout); (out/(name+'.stderr')).write_text(r.stderr)
    results.append(dict(name=name,command=cmd,returncode=r.returncode)); print(name,r.returncode,flush=True)
    if r.returncode: raise RuntimeError(name+': '+r.stderr)
    return r.stdout
log=(out/'server.log').open('w')
server=subprocess.Popen([str(ROOT/'build/mlu-remote-server')],env=dict(base,MLU_REMOTE_BIND='127.0.0.1',MLU_REMOTE_PORT=str(a.port)),stdout=log,stderr=subprocess.STDOUT,cwd=ROOT)
try:
    for _ in range(100):
        if server.poll() is not None: raise RuntimeError('server exited')
        try: socket.create_connection(('127.0.0.1',a.port),.1).close(); break
        except OSError: time.sleep(.05)
    else: raise RuntimeError('server not listening')
    for family,args in [('runtime',[]),('driver',['driver'])]:
        cmd=[str(ROOT/'build/native_query_boundaries'),*args]; native=run(family+'-native',cmd,base)
        for mode,env in [('remote',remote),('preload',dict(base,MLU_REMOTE_PORT=str(a.port),LD_PRELOAD=str(ROOT/'build/shim/libcnrt.so')+':'+str(ROOT/'build/driver-shim/libcndrv.so')))]:
            actual=run(family+'-'+mode,cmd,env)
            if actual!=native: raise RuntimeError(f'{family} {mode} observations differ')
        actual=run(family+'-isolated',['bash','scripts/run-isolated-client.sh',*cmd],dict(remote,MLU_REMOTE_SHIM='both'))
        if not actual.endswith(native): raise RuntimeError('isolated observations differ')
    for name in ['query_protocol','query_ownership','typed_client_faults']:
        run(name,['python3','tests/'+name+'.py'],remote)
finally:
    server.terminate(); server.wait(timeout=15); log.close()
    (out/'results.json').write_text(json.dumps(results,indent=2)+'\n')
