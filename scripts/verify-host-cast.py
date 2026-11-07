#!/usr/bin/env python3
"""Compare host casts offline, in separate Native/replacement/preload processes."""
import argparse
import json
import os
import pathlib
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser()
p.add_argument('--out', default='evidence/host-cast-20261107')
a = p.parse_args()
out = ROOT / a.out
out.mkdir(parents=True,exist_ok=True)
base = dict(os.environ,LD_LIBRARY_PATH=os.environ.get('NEUWARE_HOME','/usr/local/neuware')+'/lib64',
            MLU_REMOTE_HOST='127.0.0.1',MLU_REMOTE_PORT='1',MLU_REMOTE_CONNECT_TIMEOUT_MS='100')
base.pop('LD_PRELOAD',None)
remote = dict(base,LD_LIBRARY_PATH=':'.join(str(ROOT/d) for d in ('build/shim','build/driver-shim','build')))
results = []
expected = None
for name,env,library in [
    ('native',base,'libcnrt.so'),('remote',remote,'libcnrt.so'),
    ('preload',dict(base,LD_PRELOAD=str(ROOT/'build/shim/libcnrt.so')),'libcnrt.so'),
    ('isolated',remote,'libcnrt.so')]:
    cmd = ['/usr/bin/python3','tests/host_cast.py',library]
    if name == 'isolated':
        cmd = ['bash','scripts/run-isolated-client.sh',*cmd]
    r = subprocess.run(cmd,cwd=ROOT,env=env,capture_output=True,text=True,timeout=120)
    (out/(name+'.stderr')).write_text(r.stderr)
    raw = r.stdout
    if name == 'isolated':
        prefix,raw = raw.split('[{',1)
        (out/'isolation.log').write_text(prefix)
        raw = '[{'+raw
    (out/(name+'.json')).write_text(raw)
    if r.returncode:
        raise RuntimeError(name+': '+r.stderr)
    actual = json.loads(raw)
    if expected is None:
        expected = actual
    else:
        diffs = [(x,y) for x,y in zip(expected,actual) if x != y]
        if len(expected) != len(actual) or diffs:
            raise RuntimeError(name+' differs: '+str(diffs[:8]))
    results.append(dict(name=name,returncode=r.returncode,cases=len(actual),guards=True,
                        endpoint='127.0.0.1:1 (unreachable)',matches_native=True))
    print(name,'PASS',len(actual),'byte-exact cases, source/destination guards',flush=True)
(out/'results.json').write_text(json.dumps(results,indent=2)+'\n')
