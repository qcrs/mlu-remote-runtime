#!/usr/bin/env python3
"""Drop a callback submit response and require a clean client error, not a crash."""
import os,pathlib,select,socket,struct,subprocess,threading

ROOT=pathlib.Path(__file__).resolve().parents[1]
upstream=(os.environ.get('MLU_REMOTE_HOST','127.0.0.1'),int(os.environ['MLU_REMOTE_PORT']))
stop=threading.Event();dropped=[];errors=[];workers=[]
listener=socket.socket();listener.bind(('127.0.0.1',0));listener.listen();listener.settimeout(.1)

def relay(client):
    server=None
    try:
        server=socket.create_connection(upstream,timeout=5)
        client.settimeout(5);server.settimeout(5);pending=b''
        while not stop.is_set():
            ready,_,_=select.select([client,server],[],[],.1)
            for source in ready:
                data=source.recv(65536)
                if not data:return
                if source is server:client.sendall(data);continue
                pending+=data
                while len(pending)>=24:
                    _,_,opcode,_,n,_=struct.unpack_from('!IHHQII',pending)
                    if len(pending)<24+n:break
                    frame,pending=pending[:24+n],pending[24+n:]
                    if opcode==92 and n>=4 and struct.unpack_from('!I',frame,24)[0]==13:
                        dropped.append(True);return
                    server.sendall(frame)
    except (OSError,ValueError) as error:
        if not stop.is_set() and not dropped:errors.append(str(error))
    finally:
        client.close()
        if server:server.close()

def accept():
    while not stop.is_set():
        try:client,_=listener.accept()
        except socket.timeout:continue
        except OSError:return
        worker=threading.Thread(target=relay,args=(client,));workers.append(worker);worker.start()

thread=threading.Thread(target=accept);thread.start()
try:
    env=dict(os.environ,MLU_REMOTE_HOST='127.0.0.1',MLU_REMOTE_PORT=str(listener.getsockname()[1]))
    result=subprocess.run([str(ROOT/'build/native_graph_callbacks_driver'),'repeat'],env=env,cwd=ROOT,capture_output=True,text=True,timeout=20)
    assert dropped and not errors,(dropped,errors)
    assert result.returncode==1,(result.returncode,result.stdout,result.stderr)
    assert 'EntityInvoke' in result.stderr,result.stderr
    print('PASS callback submit EOF returns an API error and releases client run leases without a signal')
finally:
    stop.set();listener.close();thread.join(timeout=6)
    for worker in workers:worker.join(timeout=6)
