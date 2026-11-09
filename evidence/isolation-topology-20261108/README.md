# Local Isolation Topology Self-Check

The opt-in paired harness creates separate server and client network namespaces,
connects them with a veth pair (`198.18.0.1/30` and `198.18.0.2/30`), and runs the
client in a staged chroot. The client view contains its executable, selected
shim libraries, required system libraries, explicit file arguments, an empty
`/dev`, and a private `/tmp`; it does not contain the Neuware SDK or repository
tree.

The MLU-independent fixture transferred the generated Driver image from the
client-side staged path over TCP and matched the received bytes:

```text
VETH_TCP_PASS bytes=4672 sha256=cee1190b905bae3d42e8947188729c9c1762ed275a834428e62ce2b514cd7e17
```

Reproduce with:

```sh
bash scripts/test-isolated-topology.sh
```

This validates local namespace setup, the client filesystem boundary, and raw
TCP payload transfer. It does not start the MLU server, exercise the runtime
protocol, or validate a second physical host. The real isolated workload mode
is available as `python3 scripts/verify-workloads.py --realistic-isolation` on
a host with a usable MLU device.
