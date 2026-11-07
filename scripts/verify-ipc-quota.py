#!/usr/bin/env python3
"""Prove one physical charge across IPC importers and exporter retirement."""
import argparse,json,os,pathlib,selectors,socket,subprocess,time
ROOT=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',default='evidence/remaining-ipc-20261107/quota');p.add_argument('--port',type=int,default=50346)
a=p.parse_args();out=ROOT/a.out;out.mkdir(parents=True,exist_ok=True)
env=dict(os.environ,MLU_REMOTE_HOST='127.0.0.1',MLU_REMOTE_PORT=str(a.port),MLU_TEST_SHIM='1',LD_LIBRARY_PATH=':'.join(str(ROOT/d) for d in ('build/shim','build/driver-shim','build')));env.pop('LD_PRELOAD',None)
results=[];children=[];server=None
def start(name,cmd):
 with (out/(name+'.stderr')).open('w') as err:child=subprocess.Popen(cmd,cwd=ROOT,env=env,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=err)
 child.raw=b'';child.name=name;children.append(child);return child
def ready(child,tag):
 with selectors.DefaultSelector() as sel:
  sel.register(child.stdout,selectors.EVENT_READ);deadline=time.monotonic()+20
  while time.monotonic()<deadline:
   for line in child.raw.splitlines():
    if line.startswith(b'{'):
     row=json.loads(line)
     if row.get('ready')==tag:return row
   if not sel.select(max(0,deadline-time.monotonic())):break
   data=os.read(child.stdout.fileno(),65536)
   if not data:break
   child.raw+=data
 raise RuntimeError(child.name+' readiness failed: '+child.raw.decode())
def done(child,signal):
 tail,_=child.communicate(signal,timeout=20);child.raw+=tail;(out/(child.name+'.jsonl')).write_bytes(child.raw)
 assert child.returncode==0,(child.name,child.returncode,child.raw.decode())
 assert all(json.loads(l).get('pass',True) for l in child.raw.splitlines() if l.startswith(b'{'))
try:
 with (out/'server.log').open('w') as log:server=subprocess.Popen([str(ROOT/'build/mlu-remote-server')],cwd=ROOT,env=dict(env,LD_LIBRARY_PATH='/usr/local/neuware/lib64',MLU_REMOTE_BIND='127.0.0.1',MLU_REMOTE_SESSION_DEVICE_BYTES='4096',MLU_REMOTE_TOTAL_DEVICE_BYTES='4096'),stdout=log,stderr=subprocess.STDOUT)
 for _ in range(100):
  if server.poll() is not None:raise RuntimeError('quota server exited')
  try:
   with socket.create_connection(('127.0.0.1',a.port),.1):break
  except OSError:time.sleep(.05)
 else:raise RuntimeError('quota server not listening')
 for lib in ('cnrt','cndrv'):
  binary=ROOT/('build/probes/ipc-lifetime-'+lib)
  cmd=['gcc','-std=c11','-O2','-Wall','-Wextra','-Wno-misleading-indentation','-I/usr/local/neuware/include','tests/native_ipc_lifetime.c','-L/usr/local/neuware/lib64','-Wl,-rpath,/usr/local/neuware/lib64','-l'+lib,'-o',str(binary)]
  if lib=='cnrt':cmd.insert(1,'-DUSE_CNRT')
  subprocess.run(cmd,cwd=ROOT,check=True,capture_output=True)
  exporter=start(lib+'-export',[str(binary),'export','device','free']);token=ready(exporter,'export')['handle'];peers=[]
  for i in range(2):
   peer=start(lib+'-import-'+str(i),[str(binary),'hold','device',token]);ready(peer,'import-held');peers.append(peer)
  quota=start(lib+'-quota',[str(binary),'quota','device',token]);ready(quota,'import-held')
  exporter.stdin.write(b'retire\n');exporter.stdin.flush();ready(exporter,'freed')
  for peer in peers:done(peer,b'go\n')
  done(quota,b'go\n');done(exporter,b'done\n');results.append(dict(library=lib,passed=True,importers=3,total_device_limit=4096));print(lib,'PASS single-charge and final release',flush=True)
finally:
 for child in children:
  if child.poll() is None:child.kill();child.wait(timeout=5)
 if server:server.terminate();server.wait(timeout=15)
 (out/'results.json').write_text(json.dumps(results,indent=2)+'\n')
log=(out/'server.log').read_text();assert 'RESOURCE_TOTAL device=0 staging=0 image=0' in log and 'DRAIN_COMPLETE workers=0 cleanup_errors=0' in log
assert 'CLEANUP_QUARANTINED' not in log and 'SESSION_CLEANUP_FAILED' not in log
print('PASS IPC quota:',len(results),'scenarios')
