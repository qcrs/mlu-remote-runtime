# MLU Remote Runtime architecture

The runtime has two independent Linux processes. The client stores only
opaque object IDs and sends structured requests over a TCP stream. Each
accepted socket owns one `ServerSession` containing one CNDrv context and
separate allocation, queue, notifier, module/kernel, staging, and transfer
registries. Native `CNaddr`, `CNqueue`, `CNnotifier`, `CNmodule`, and `CNkernel`
values never cross the wire.

```text
CPU application -> libremote_cnrt.so / libremote_cnnl.so
                -> fixed 24-byte TCP frame
                -> mlu-remote-server -> CNDrv 2.5.2 / CNRT 6.5.2 -> MLU370-S4
```

The frame contains magic, version, opcode, request sequence, payload length,
and status in network byte order. A response always starts with the encoded
error name and message, followed by an operation result. The server rejects
bad magic/version, oversized or truncated payloads, and malformed object
descriptors before calling CNDrv.

The operator request uses one generic layout. Its fixed prefix carries
OperatorID, KernelID, QueueID, input/output counts, scalar byte count, and
grid/block dimensions. Each tensor descriptor carries AllocationID, byte
offset, rank, layout, datatype, shape, and stride. The trailing scalar region
contains serialized values (the scalar_add F32 value is an IEEE-754 32-bit
value in network byte order). No request contains a function pointer or
native device address.

The server-side registry maps:

```text
OperatorID -> preset module file -> verified kernel symbol -> parameter layout
```

The current entries are `vector_add`, `scalar_add`, `relu`,
`tensor_add_strided`, `scalar_mul`, and `tensor_mul_strided`.
`OPERATOR_CREATE` loads a preset once per session and
returns the session ModuleID/KernelID. `OPERATOR_LAUNCH` validates the
descriptor, resolves AllocationIDs, checks the required allocation span, builds
the CNDrv parameter array, and calls `cnInvokeKernel` on the selected queue.
The server records an internal CNDrv notifier after each queued operation and
returns its TransferID. Transfer query/sync uses that notifier, so completion
is tied to the operation's queue position rather than a whole-queue query.
Destroying an operator or module synchronizes pending queue work before
unloading the module; a stale KernelID or ModuleID then returns
`CN_ERROR_INVALID_HANDLE`.

The registry table and preset manifests are generated from the JSON schema at
build time. A manifest is a small trusted ABI boundary containing the module
name, expected kernel symbol, architecture, datatype, rank, tensor counts,
scalar size, and ABI version. The server validates it before loading the
fatbin, so a renamed or architecture-mismatched preset is rejected before
operator launch.

Memory operations follow CNDrv queue semantics. H2D data is copied into
session-owned staging memory before `cnMemcpyHtoDAsync`; staging is released
only after its queue completes. D2H enqueues the copy and synchronizes the
queue before response bytes are sent. D2D remains entirely device-side. Free
and queue destruction synchronize pending work first.

On EOF the session worker synchronizes queues, releases staging, unloads
modules/kernels, destroys notifiers and queues, frees allocations, invalidates
TransferIDs, and destroys the context. Every registry entry records its owner
token and slot generation. Opaque IDs include a session generation and are
monotonic within that session, so an old ID cannot alias a new socket's slot.

CNNL is retained only as an explicit unsupported client seam. The installed
Neuware image has no CNNL development or runtime artifact, so the server
returns `MLU_REMOTE_UNSUPPORTED` rather than fabricating a CNNL result.

HELLO creates a device-0 context for compatibility with the existing client.
`DEVICE_SELECT` can rebind that session to another valid ordinal while the
session has no live objects; after resources exist, switching is rejected so
allocation, queue, module, and notifier ownership remains unambiguous.
`SESSION_RESET` provides the explicit lifecycle boundary: it synchronizes and
destroys all native resources, recreates the context on the current device,
and changes the owner generation so every previous object ID is stale.
