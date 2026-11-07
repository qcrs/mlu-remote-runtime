#!/usr/bin/env python3
"""Verify Remote IPC lifetime policy and descriptor ownership with independent peers."""
import argparse,array,hashlib,json,os,pathlib,selectors,socket,subprocess,time
ROOT=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',default='evidence/remaining-ipc-20261107/lifetime');p.add_argument('--port',type=int,default=50342)
a=p.parse_args();out=ROOT/a.out;out.mkdir(parents=True,exist_ok=True)
env=dict(os.environ,MLU_REMOTE_HOST='127.0.0.1',MLU_REMOTE_PORT=str(a.port),MLU_TEST_SHIM='1',LD_LIBRARY_PATH=':'.join(str(ROOT/d) for d in ('build/shim','build/driver-shim','build')));env.pop('LD_PRELOAD',None)
results=[];bins={}
for lib in ('cnrt','cndrv'):
 binary=ROOT/('build/probes/ipc-lifetime-'+lib);cmd=['gcc','-std=c11','-O2','-Wall','-Wextra','-Wno-misleading-indentation','-I/usr/local/neuware/include','tests/native_ipc_lifetime.c','-L/usr/local/neuware/lib64','-Wl,-rpath,/usr/local/neuware/lib64','-l'+lib,'-o',str(binary)]
 if lib=='cnrt':cmd.insert(1,'-DUSE_CNRT')
 subprocess.run(cmd,cwd=ROOT,check=True,capture_output=True);bins[lib]=str(binary)
def start(name,cmd):
 err=(out/(name+'.stderr')).open('w');child=subprocess.Popen(cmd,cwd=ROOT,env=env,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=err);child.raw=b'';child.name=name;err.close();return child
def ready(child,tag=None):
 with selectors.DefaultSelector() as sel:
  sel.register(child.stdout,selectors.EVENT_READ);deadline=time.monotonic()+20
  while time.monotonic()<deadline:
   for line in child.raw.splitlines():
    if line.startswith(b'{'):
     row=json.loads(line)
     if row.get('ready') and (tag is None or row['ready']==tag):return row
   if not sel.select(max(0,deadline-time.monotonic())):break
   data=os.read(child.stdout.fileno(),65536)
   if not data:break
   child.raw+=data
 raise RuntimeError(child.name+' failed readiness: '+child.raw.decode())
def done(child,signal=None):
 tail,_=child.communicate(signal,timeout=20);child.raw+=tail;(out/(child.name+'.jsonl')).write_bytes(child.raw)
 assert child.returncode==0,(child.name,child.returncode,child.raw.decode())
 rows=[json.loads(l) for l in child.raw.splitlines() if l.startswith(b'{')];assert all(r.get('pass',True) for r in rows)
 return sum('check' in r for r in rows)
for lib,host in [('cnrt','device'),('cndrv','device'),('cnrt','host')]:
 for method in ('free','reset','eof'):
  name=lib+'-'+host+'-'+method;children=[]
  try:
   exporter=start(name+'-export',[bins[lib],'export',host,method]);children.append(exporter);token=ready(exporter)['handle'];importers=[]
   for i in range(2):
    importer=start(name+'-import-'+str(i),[bins[lib],'hold',host,token,'reset' if i else 'eof']);children.append(importer);ready(importer);importers.append(importer)
   exporter.stdin.write(b'retire\n');exporter.stdin.flush()
   if method=='free':
    ready(exporter,'freed')
   else:
    done(exporter)
    if method=='eof':time.sleep(.1)
   checks=0
   if method!='reset' or host!='host':
    reopen=start(name+'-reopen',[bins[lib],'reopen',host,token]);children.append(reopen);checks+=done(reopen)
   for importer in importers:checks+=done(importer,b'go\n')
   if method=='free':checks+=done(exporter,b'done\n')
   results.append(dict(name=name,passed=True,checks=checks));print(name,'PASS',flush=True)
  finally:
   for child in children:
    if child.poll() is None:child.kill();child.wait(timeout=5)
# Imported identity may close before a Graph holding its backing is invoked.
for lib in ('cnrt','cndrv'):
 name=lib+'-graph-import-lease';children=[]
 try:
  exporter=start(name+'-export',[bins[lib],'export','device','free']);children.append(exporter);token=ready(exporter)['handle']
  importer=start(name+'-import',[bins[lib],'graph','device',token]);children.append(importer);ready(importer)
  exporter.stdin.write(b'retire\n');exporter.stdin.flush();ready(exporter,'freed')
  checks=done(importer,b'go\n')+done(exporter,b'done\n');results.append(dict(name=name,passed=True,checks=checks));print(name,'PASS',flush=True)
 finally:
  for child in children:
   if child.poll() is None:child.kill();child.wait(timeout=5)
# Final exported FD reference outlives exporter Context and all its generic handles.
receiver,sender=socket.socketpair();receiver.settimeout(20);fds=[]
try:
 with (out/'vmm-retire-export.stderr').open('w') as err:child=subprocess.Popen([str(ROOT/'build/probes/probe-graph-ipc-cndrv'),'vmm-export',str(sender.fileno()),'retire'],cwd=ROOT,env=env,pass_fds=(sender.fileno(),),stdout=subprocess.PIPE,stderr=err)
 child.name='vmm-retire-export';child.raw=b'';sender.close()
 data,anc,flags,_=receiver.recvmsg(128,socket.CMSG_SPACE(4));assert not flags&socket.MSG_CTRUNC
 for level,kind,value in anc:
  if level==socket.SOL_SOCKET and kind==socket.SCM_RIGHTS:
   ints=array.array('i');ints.frombytes(value);fds.extend(ints)
 assert len(fds)==1;duplicate=os.dup(fds[0]);os.close(fds.pop());fds.append(duplicate);done(child)
 with (out/'vmm-retire-import.stderr').open('w') as err:
  peer=subprocess.Popen([str(ROOT/'build/probes/probe-graph-ipc-cndrv'),'vmm-import',str(duplicate),data.decode()],cwd=ROOT,env=env,pass_fds=(duplicate,),stdout=subprocess.PIPE,stderr=err)
 peer.name='vmm-retire-import';peer.raw=b'';done(peer)
 results.append(dict(name='vmm-fd-dup-exporter-retirement',passed=True));print('vmm-fd-dup-exporter-retirement PASS',flush=True)
finally:
 receiver.close();sender.close()
 for fd in fds:os.close(fd)
(out/'results.json').write_text(json.dumps(results,indent=2)+'\n')
files=['scripts/verify-ipc-lifetime.py','tests/native_ipc_lifetime.c','probes/probe_graph_ipc.c','server/ipc_lifetime.inc','server/ipc_control.inc','client/ipc_descriptor.inc','client/host_memory.c','shim/driver_ipc.inc','shim/runtime_ipc.inc']
(out/'provenance.json').write_text(json.dumps({f:hashlib.sha256((ROOT/f).read_bytes()).hexdigest() for f in files},indent=2)+'\n')
print('PASS IPC lifetime:',len(results),'scenarios')
