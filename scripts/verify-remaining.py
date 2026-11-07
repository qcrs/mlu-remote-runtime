#!/usr/bin/env python3
"""Native/Remote closure for shared Context, VMM, Symbols and ordinary queue Host/atomics."""
import argparse,hashlib,json,os,pathlib,selectors,socket,subprocess,time
ROOT=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',default='evidence/remaining-closure-20261107');p.add_argument('--port',type=int,default=50224)
a=p.parse_args();out=ROOT/a.out;out.mkdir(parents=True,exist_ok=True)
base=dict(os.environ,LD_LIBRARY_PATH='/usr/local/neuware/lib64');base.pop('LD_PRELOAD',None);base.pop('MLU_TEST_SHIM',None)
remote=dict(base,MLU_REMOTE_HOST='127.0.0.1',MLU_REMOTE_PORT=str(a.port),MLU_TEST_SHIM='1',LD_LIBRARY_PATH=':'.join(str(ROOT/d) for d in ('build/shim','build/driver-shim','build')))
preload=dict(base,MLU_REMOTE_HOST='127.0.0.1',MLU_REMOTE_PORT=str(a.port),MLU_TEST_SHIM='1',LD_PRELOAD=':'.join(str(ROOT/d) for d in ('build/shim/libcnrt.so','build/driver-shim/libcndrv.so')))
results=[];server=None
cases=[('shared',['native_shared','shared']),('shared-life',['native_shared','shared-life']),('vmm',['native_vmm']),('queue-driver',['native_queue_host']),('queue-runtime',['native_queue_host','runtime']),('symbol-runtime',['native_symbols']),('symbol-driver',['native_symbols',str(ROOT/'build/symbol-audit/native_symbols-bang-mlisa-cambricon-bang.cnfatbin')])]
for family in ('driver','runtime'):cases.append(('graph-host-'+family,['native_graph_host_'+family]))
def run(name,cmd,env):
 r=subprocess.run(cmd,cwd=ROOT,env=env,capture_output=True,text=True,timeout=90)
 (out/(name+'.log')).write_text(r.stdout);(out/(name+'.stderr')).write_text(r.stderr)
 results.append(dict(name=name,command=cmd,returncode=r.returncode));print(name,r.returncode,flush=True)
 if r.returncode:raise RuntimeError(name+': '+r.stdout+r.stderr)
 return '\n'.join(line for line in r.stdout.splitlines() if not line.startswith(('ISOLATION ','user ','mnt ','net ','VERIFIED ','ENDPOINT ','CLIENT ')))
try:
 with (out/'server.log').open('w') as log:server=subprocess.Popen([str(ROOT/'build/mlu-remote-server')],cwd=ROOT,env=dict(base,MLU_REMOTE_BIND='127.0.0.1',MLU_REMOTE_PORT=str(a.port)),stdout=log,stderr=subprocess.STDOUT)
 for _ in range(100):
  if server.poll() is not None:raise RuntimeError('server exited')
  try:
   with socket.create_connection(('127.0.0.1',a.port),.1):break
  except OSError:time.sleep(.05)
 else:raise RuntimeError('server not listening')
 for name,args in cases:
  cmd=[str(ROOT/'build'/args[0]),*args[1:]];expected=run(name+'-native',cmd,base)
  for mode,env in [('remote',remote),('preload',preload),('isolated',dict(remote,MLU_REMOTE_SHIM='both'))]:
   actual=run(name+'-'+mode,cmd if mode!='isolated' else ['bash','scripts/run-isolated-client.sh',*cmd],env)
   assert actual==expected,(name,mode,expected,actual)
 for mode in ('abrupt','blocked-sync'):
  cmd=[str(ROOT/'build/native_queue_host'),mode]
  with (out/('atomic-'+mode+'.stderr')).open('w') as err:
   child=subprocess.Popen(cmd,cwd=ROOT,env=remote,stdout=subprocess.PIPE,stderr=err)
   raw=b''
   try:
    with selectors.DefaultSelector() as sel:
     sel.register(child.stdout,selectors.EVENT_READ);deadline=time.monotonic()+20
     while b'READY ' not in raw:
      if not sel.select(max(0,deadline-time.monotonic())):raise RuntimeError('compare setup timeout')
      data=os.read(child.stdout.fileno(),65536)
      if not data:raise RuntimeError('compare setup failed')
      raw+=data
    if mode=='blocked-sync':time.sleep(.1);child.kill()
    tail,_=child.communicate(timeout=10);raw+=tail
    assert child.returncode==(0 if mode=='abrupt' else -9)
    (out/('atomic-'+mode+'.log')).write_bytes(raw);results.append(dict(name='atomic-'+mode,returncode=child.returncode,passed=True));print('atomic-'+mode,'PASS',flush=True)
   finally:
    if child.poll() is None:child.kill();child.wait(timeout=5)
 run('after-eof',[str(ROOT/'build/native_queue_host')],remote)
 server.terminate();server.wait(timeout=15);server=None
 log=(out/'server.log').read_text();assert log.count('ATOMIC_EOF_CANCEL')>=2
 assert 'CLEANUP_QUARANTINED' not in log and 'SESSION_CLEANUP_FAILED' not in log
 assert 'RESOURCE_TOTAL device=0 staging=0 image=0' in log and 'DRAIN_COMPLETE workers=0 cleanup_errors=0' in log
finally:
 if server:server.terminate();server.wait(timeout=15)
 (out/'results.json').write_text(json.dumps(results,indent=2)+'\n')
 files=['scripts/verify-remaining.py','tests/native_shared.c','tests/native_vmm.c','tests/native_symbols.mlu','tests/native_queue_host.c','tests/native_graph_host.c','server/mlu_backend.c','server/queue_host_backend.inc','server/vmm_backend.inc','server/symbol_backend.inc','shim/shared_context.inc','shim/driver_atomics.inc','shim/driver_vmm.inc','shim/driver_symbols.inc','shim/runtime_symbols.inc','build/mlu-remote-server','build/libremote_cnrt.so','build/shim/libcnrt.so','build/driver-shim/libcndrv.so']
 (out/'provenance.json').write_text(json.dumps({f:hashlib.sha256((ROOT/f).read_bytes()).hexdigest() for f in files},indent=2)+'\n')
print('PASS remaining closure:',len(results),'process scenarios')
