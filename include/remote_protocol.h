#ifndef MLU_REMOTE_PROTOCOL_H
#define MLU_REMOTE_PROTOCOL_H

#include <stdint.h>

#define MLU_REMOTE_MAGIC 0x4d4c5552u /* MLUR */
#define MLU_REMOTE_VERSION 1u
#define MLU_REMOTE_MAX_PAYLOAD (64u * 1024u * 1024u)

enum mlu_remote_opcode {
  MLU_OP_HELLO = 1,
  MLU_OP_DEVICE_QUERY = 2,
  MLU_OP_ALLOC = 3,
  MLU_OP_FREE = 4,
  MLU_OP_MEMCPY = 5,
  MLU_OP_QUEUE_CREATE = 6,
  MLU_OP_QUEUE_DESTROY = 7,
  MLU_OP_QUEUE_SYNC = 8,
  MLU_OP_QUEUE_QUERY = 9,
  MLU_OP_NOTIFIER_CREATE = 10,
  MLU_OP_NOTIFIER_DESTROY = 11,
  MLU_OP_NOTIFIER_RECORD = 12,
  MLU_OP_NOTIFIER_QUERY = 13,
  MLU_OP_NOTIFIER_SYNC = 14,
  MLU_OP_CNNL_OPERATION = 15,
  MLU_OP_MODULE_LOAD_PRESET = 16,
  MLU_OP_KERNEL_LAUNCH = 17,
  MLU_OP_SESSION_RESET = 18,
  MLU_OP_DEVICE_SELECT = 19,
  MLU_OP_OPERATOR_CREATE = 20,
  MLU_OP_OPERATOR_DESTROY = 21,
  MLU_OP_OPERATOR_LAUNCH = 22,
  MLU_OP_TRANSFER_QUERY = 23,
  MLU_OP_TRANSFER_SYNC = 24,
  MLU_OP_MODULE_UNLOAD_PRESET = 25,
  /* Native BANG host stubs use these two operations.  Image bytes are
   * bounded by MLU_REMOTE_MAX_PAYLOAD and are validated by the server before
   * passing them to CNDrv. */
  MLU_OP_MODULE_LOAD_IMAGE = 26,
  MLU_OP_KERNEL_LAUNCH_GENERIC = 27,
  MLU_OP_TRANSFER_RELEASE = 28,
  MLU_OP_MODULE_GET_KERNEL = 29,
  MLU_OP_MODULE_LOAD_BINARY = 30,
  MLU_OP_SESSION_CLOSE = 31,
  MLU_OP_CONTEXT_SYNC = 32,
  /* Driver module memory queries carry the validated CNFB image bytes. */
  MLU_OP_MODULE_QUERY_FATBINARY_MEMORY = 77,
  MLU_OP_CALLBACK_ATTACH = 91,
  MLU_OP_IPC_ATTACH = 97,
#include "generated/typed_opcodes.inc"
};

#define MLU_CAP_GRAPH_CAPTURE 0x10000u
#define MLU_CAP_GRAPH_CALLBACKS 0x20000u
#define MLU_CAP_MODULE_IMAGE 0x100u
#define MLU_CAP_GENERIC_LAUNCH 0x200u
#define MLU_CAP_TRANSFER_RELEASE 0x400u
#define MLU_CAP_MODULE_KERNELS 0x800u
#define MLU_CAP_DRIVER_CONTEXT 0x1000u
#define MLU_CAP_TYPED_API 0x2000u
#define MLU_CAP_DEFERRED_D2H 0x4000u
#define MLU_CAP_ARGUMENT_RELOCATIONS 0x8000u
/* Generic scalar bytes are LP64 little endian. Framing remains network order. */
#define MLU_REMOTE_KERNEL_ABI 0x08010001u

enum mlu_remote_memcpy_direction {
  MLU_MEMCPY_H2D = 1,
  MLU_MEMCPY_D2H = 2,
  MLU_MEMCPY_D2D = 3,
};

enum mlu_remote_protocol_status {
  MLU_REMOTE_PROTOCOL_ERROR = 0x80000001u,
  MLU_REMOTE_BAD_VERSION = 0x80000002u,
  MLU_REMOTE_BAD_LENGTH = 0x80000003u,
  MLU_REMOTE_BAD_OBJECT = 0x80000004u,
  MLU_REMOTE_UNSUPPORTED = 0x80000005u,
  MLU_REMOTE_TIMEOUT = 0x80000006u,
  MLU_REMOTE_RESOURCE_LIMIT = 0x80000007u,
};

/* Stable operator registry IDs.  These IDs describe server-side, validated
 * BANG C presets; they are not native function pointers. */
enum mlu_remote_operator_id {
  MLU_REMOTE_OPERATOR_VECTOR_ADD = 1,
  MLU_REMOTE_OPERATOR_SCALAR_ADD = 2,
  MLU_REMOTE_OPERATOR_RELU = 3,
  MLU_REMOTE_OPERATOR_TENSOR_ADD_STRIDED = 4,
  MLU_REMOTE_OPERATOR_SCALAR_MUL = 5,
  MLU_REMOTE_OPERATOR_TENSOR_MUL_STRIDED = 6,
};

enum mlu_remote_layout {
  MLU_REMOTE_LAYOUT_ANY = 0,
  MLU_REMOTE_LAYOUT_ROW_MAJOR = 1,
};

enum mlu_remote_datatype {
  MLU_REMOTE_DTYPE_F32 = 1,
};

#define MLU_REMOTE_MAX_RANK 8u

/* All integer fields are serialized in network byte order. */
struct mlu_remote_wire_header {
  uint32_t magic;
  uint16_t version;
  uint16_t opcode;
  uint64_t request_id;
  uint32_t payload_len;
  uint32_t status;
};

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(sizeof(struct mlu_remote_wire_header) == 24, "wire header must stay fixed length");
#endif

#endif
