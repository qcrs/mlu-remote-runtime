"""Shared MLUR v1 TCP helpers for native-backed protocol probes."""
import os, socket, struct
HOST = os.environ.get("MLU_REMOTE_HOST", "127.0.0.1")
PORT = int(os.environ.get("MLU_REMOTE_PORT", "50051"))
MAGIC = 0x4D4C5552

class Session:
    def __init__(self, flags=None):
        self.s = socket.create_connection((HOST, PORT), timeout=15)
        self.seq = 0
        payload=struct.pack('!III',1,0,0xffffffff)
        if flags is not None: payload+=struct.pack('!I',flags)
        status, _ = self.rpc(1,payload)
        assert status == 0
    def recv(self, n):
        data = b''
        while len(data) < n:
            part = self.s.recv(n - len(data))
            assert part, 'unexpected EOF'
            data += part
        return data
    def rpc(self, op, payload=b''):
        self.seq += 1
        self.s.sendall(struct.pack('!IHHQII', MAGIC, 1, op, self.seq, len(payload), 0) + payload)
        magic, version, rop, seq, n, status = struct.unpack('!IHHQII', self.recv(24))
        assert (magic, version, rop, seq) == (MAGIC, 1, op, self.seq)
        data = self.recv(n)
        nl, ml = struct.unpack_from('!II', data)
        assert nl + ml + 8 <= n
        return status, data[8 + nl + ml:]
    def ok(self, op, payload=b''):
        st, data = self.rpc(op, payload); assert st == 0, (op, st)
        return data

def idarg(value): return struct.pack('!Q', value)
def ptr(a, offset=0): return struct.pack('!IIQQQ', 1, 8, a, offset, 0) + bytes(8)
def scalar(v, size=4): return struct.pack('!IIQQQ', 2, size, 0, 0, 0) + v
def launch(m, k, q, arguments, dims=(1, 1, 1), klass=1):
    return struct.pack('!QQQIIIII', m, k, q, *dims, klass, len(arguments)) + b''.join(arguments)

