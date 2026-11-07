#!/usr/bin/env python3
"""Run native and TCP remote processes and retain reproducible evidence."""
import argparse,os,pathlib,subprocess,socket,time,json,hashlib
root=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(); p.add_argument('--out',default='evidence/transparent-20261102'); p.add_argument('--host'); p.add_argument('--port',type=int,default=50192); args=p.parse_args()
out=root/args.out; out.mkdir(parents=True,exist_ok=True)
base=os.environ.copy(); base.pop('LD_PRELOAD',None); base.pop('MLU_TEST_SHIM',None)
sdk=base.get('NEUWARE_HOME','/usr/local/neuware'); base['LD_LIBRARY_PATH']=sdk+'/lib64'
remote=base.copy(); remote.update(MLU_REMOTE_HOST=args.host or '127.0.0.1',MLU_REMOTE_PORT=str(args.port),LD_LIBRARY_PATH=str(root/'build/shim')+':'+str(root/'build'),MLU_TEST_SHIM='1')
results=[]
def run(name,cmd,env=base,expected=0):
    with (out/(name+'.log')).open('w') as log:
        log.write('COMMAND '+repr(cmd)+'\n')
        log.write('ENV '+repr({k:v for k,v in env.items() if k.startswith('MLU_') or k in ('LD_LIBRARY_PATH','LD_PRELOAD')})+'\n'); log.flush()
        r=subprocess.run(cmd,cwd=root,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=120)
    results.append(dict(name=name,returncode=r.returncode,expected=expected))
    print(name,r.returncode,flush=True)
    if r.returncode!=expected: raise RuntimeError(f'{name} failed: {out/name}.log')
server=None
try:
    run('environment',['bash','-c','uname -a; cnmon; /usr/local/neuware/bin/cncc --version; sha256sum /usr/local/neuware/include/cnrt.h /usr/local/neuware/include/cn_api.h'])
    if not args.host:
        log=(out/'server.log').open('w')
        server=subprocess.Popen([str(root/'build/mlu-remote-server')],cwd=root,env=dict(base,MLU_REMOTE_BIND='127.0.0.1',MLU_REMOTE_PORT=str(args.port)),stdout=log,stderr=subprocess.STDOUT)
        for _ in range(100):
            if server.poll() is not None: raise RuntimeError('server exited')
            try:
                with socket.create_connection(('127.0.0.1',args.port),timeout=.1): break
            except OSError: time.sleep(.05)
        else: raise RuntimeError('server did not listen')
    for name in ['native_bang_remote','native_bang_generic','native_bang_abi','native_bang_module','native_bang_module_dso','native_bang_drain']:
        run(name+'-native',[str(root/'build'/name)])
        run(name+'-replacement',[str(root/'build'/name)],remote)
        run(name+'-preload',[str(root/'build'/name)],dict(base,MLU_REMOTE_HOST=remote['MLU_REMOTE_HOST'],MLU_REMOTE_PORT=str(args.port),LD_PRELOAD=str(root/'build/shim/libcnrt.so'),MLU_TEST_SHIM='1'))
    driver=dict(remote,LD_LIBRARY_PATH=str(root/'build/driver-shim')+':'+str(root/'build'))
    images=[str(root/'build'/directory/(name+'-bang-mlisa-cambricon-bang.cnfatbin')) for directory,name in
            [('module-audit','native_bang_module'),('audit','native_bang_abi'),('drain-audit','native_bang_drain')]]
    retirement=dict(remote,LD_LIBRARY_PATH=':'.join(str(root/d) for d in ('build/shim','build/driver-shim','build')))
    for family in ('runtime','driver'):
        command=[str(root/'build/native_queue_retirement'),family]+([images[2]] if family=='driver' else [])
        run(family+'-retirement-native',command)
        run(family+'-retirement-replacement',command,retirement)
        run(family+'-retirement-preload',command,dict(base,MLU_REMOTE_HOST=remote['MLU_REMOTE_HOST'],MLU_REMOTE_PORT=str(args.port),
            LD_PRELOAD=':'.join(str(root/d) for d in ('build/shim/libcnrt.so','build/driver-shim/libcndrv.so')),MLU_TEST_SHIM='1'))
        if not args.host:
            run(family+'-retirement-isolated',['bash','scripts/run-isolated-client.sh',*command],dict(retirement,MLU_REMOTE_SHIM='both'))
    driver_command=[str(root/'build/native_driver'),*images]
    run('driver-native',driver_command)
    run('driver-replacement',driver_command,driver)
    run('driver-preload',driver_command,dict(base,MLU_REMOTE_HOST=remote['MLU_REMOTE_HOST'],MLU_REMOTE_PORT=str(args.port),LD_PRELOAD=str(root/'build/driver-shim/libcndrv.so'),MLU_TEST_SHIM='1'))
    probe=[str(root/'build/probes/probe_driver_semantics'),images[0]]
    run('driver-semantics-native',probe)
    run('driver-semantics-remote',probe,driver)
    # The log headers contain different environment dictionaries; compare all
    # native semantic observations, including error codes and output values.
    native=(out/'driver-semantics-native.log').read_text().splitlines()[2:]
    measured=(out/'driver-semantics-remote.log').read_text().splitlines()[2:]
    if native!=measured: raise RuntimeError('Driver native/remote semantic observations differ')
    run('driver-protocol',['python3','tests/driver_protocol.py'],driver)
    run('api-semantics-native',[str(root/'build/probes/probe_semantics')])
    run('api-semantics-remote',[str(root/'build/probes/probe_semantics')],remote)
    for name in ['client-test','operator-test','custom-kernel-test','protocol-error-test','session-isolation-test','device-select-test','session-reset-test','cnnl-test']:
        run(name,[str(root/('build/mlu-remote-'+name))],remote)
    run('generic-protocol',['python3','tests/generic_protocol.py'],remote)
    run('shared-module-protocol',['python3','tests/shared_module_protocol.py'],remote)
    if not args.host:
        run('isolated-client',['bash','scripts/run-isolated-client.sh'],remote)
        run('isolated-shared-module',['bash','scripts/run-isolated-client.sh','build/native_bang_module'],remote)
        run('isolated-module-dso',['bash','scripts/run-isolated-client.sh','build/native_bang_module_dso'],remote)
        run('isolated-driver',['bash','scripts/run-isolated-client.sh',*driver_command],dict(driver,MLU_REMOTE_SHIM='driver'))
    run('abrupt',['bash','scripts/run-abrupt-cleanup-test.sh'],remote)
    run('dependencies',['bash','-c','readelf -d build/native_bang_abi; readelf -d build/shim/libcnrt.so; readelf -d build/libremote_cnrt.so; LD_LIBRARY_PATH=build/shim:build ldd build/native_bang_abi'])
    run('no-driver-access',['strace','-f','-e','trace=openat,ioctl,connect','-o',str(out/'client.strace'),str(root/'build/native_bang_abi')],remote)
    trace=(out/'client.strace').read_text()
    forbidden=[line for line in trace.splitlines() if any(s in line for s in ('libcndrv','libcnrt.so.6','libcnbin','/dev/cambricon'))]
    (out/'driver-access.json').write_text(json.dumps({'forbidden_access':forbidden,'scope':'client process with replacement CNRT on MLU host; not a separate driverless machine'},indent=2)+'\n')
    if forbidden: raise RuntimeError('native dependency leaked')
    run('driver-dependencies',['bash','-c','readelf -d build/native_driver; readelf -d build/driver-shim/libcndrv.so; LD_LIBRARY_PATH=build/driver-shim:build ldd build/native_driver'])
    run('driver-no-device-access',['strace','-f','-e','trace=openat,ioctl,connect','-o',str(out/'driver.strace'),*driver_command],driver)
    trace=(out/'driver.strace').read_text()
    sdk_searches=[line for line in trace.splitlines() if '/usr/local/neuware' in line]
    forbidden=[line for line in trace.splitlines() if '/dev/cambricon' in line or 'libcnbin' in line or
               ('/usr/local/neuware' in line and ' = -1 ' not in line)]
    (out/'driver-access-driver.json').write_text(json.dumps({'forbidden_access':forbidden,'failed_sdk_path_searches':[line for line in sdk_searches if ' = -1 ' in line],'scope':'replacement Driver library; no successful vendor SDK opens or device access; original executable RUNPATH may probe missing system libraries in SDK directory; isolated-driver separately hides SDK and devices'},indent=2)+'\n')
    if forbidden: raise RuntimeError('native Driver dependency leaked')
    run('generated',['python3','scripts/generate_rpc.py','--check'])
    run('compatibility-ledger',['python3','scripts/generate_compat.py','--check'])
    run('api-contracts',['python3','scripts/verify-api-contracts.py'])
    run('client-response-faults',['python3','tests/client_response_faults.py'])
    run('driver-client-faults',['python3','tests/driver_client_faults.py'])
    run('client-transport-deadlines',['python3','tests/transport_deadlines.py'])
    run('object-identity',[str(root/'build/object-identity-test')])
    if not args.host:
        run('server-transport-deadlines',['python3','tests/server_transport.py','--out',str(out/'server-transport')])
        run('server-restart',['python3','tests/server_restart.py','--out',str(out/'server-restart')])
        run('server-resources',['python3','tests/server_resources.py','--out',str(out/'server-resources')])
        resource_image=str(root/'build/module-audit/native_bang_module-bang-mlisa-cambricon-bang.cnfatbin')
        run('backend-resource-rollback',[str(root/'build/backend-resources-test'),resource_image])
        run('backend-resource-quarantine',[str(root/'build/backend-resources-test'),resource_image,'quarantine'])
    run('parser-bounds',[str(root/'build/bang-image-bounds'),str(root/'build/audit/native_bang_abi-bang-mlisa-cambricon-bang.cnfatbin'),'_Z10vector_addPfPKfS1_i',str(root/'build/module-audit/native_bang_module-bang-mlisa-cambricon-bang.cnfatbin'),str(root/'build/module-audit/native_bang_module-bang-mlisa-cambricon-bang-mtp_372.o')])
finally:
    if server:
        time.sleep(.1); server.terminate(); server.wait(timeout=10); log.close()
    (out/'results.json').write_text(json.dumps(results,indent=2)+'\n')
