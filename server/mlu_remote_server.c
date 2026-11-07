#include "mlu_backend.h"
#include "remote_protocol.h"
#include "remote_io.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "signal notification requires lock-free atomic int");
static _Atomic int stop_server;
static int listen_fd = -1;
static uint32_t handshake_timeout_ms, io_timeout_ms;
static int signal_pipe[2] = {-1, -1};
static pthread_mutex_t workers_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t workers_empty = PTHREAD_COND_INITIALIZER;
static unsigned active_workers, cleanup_errors;
struct worker_arg { int fd; int64_t handshake_deadline; struct worker_arg *next; };
static struct worker_arg *workers;
static void request_stop(void);

static uint64_t ntohll_u(uint64_t x) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  return ((uint64_t)ntohl((uint32_t)x) << 32) | ntohl((uint32_t)(x >> 32));
#else
  return x;
#endif
}
static uint64_t htonll_u(uint64_t x) { return ntohll_u(x); }
static uint32_t get32(const unsigned char *p) { uint32_t x; memcpy(&x, p, 4); return ntohl(x); }
static uint64_t get64(const unsigned char *p) { uint64_t x; memcpy(&x, p, 8); return ntohll_u(x); }
static void put32(unsigned char *p, uint32_t x) { x = htonl(x); memcpy(p, &x, 4); }
static void put64(unsigned char *p, uint64_t x) { x = htonll_u(x); memcpy(p, &x, 8); }

static int parse_operator_tensor(const unsigned char **cursor, uint32_t *remaining,
                                mlu_backend_tensor *tensor) {
  if (!cursor || !*cursor || !remaining || !tensor || *remaining < 32) return -1;
  const unsigned char *p = *cursor;
  tensor->allocation_id = get64(p); tensor->offset = get64(p + 8);
  tensor->rank = get32(p + 16); tensor->layout = get32(p + 20);
  tensor->datatype = get32(p + 24);
  if (tensor->rank == 0 || tensor->rank > MLU_REMOTE_MAX_RANK) return -1;
  uint64_t need = 32ull + 16ull * tensor->rank;
  if (need > *remaining) return -1;
  p += 32;
  for (uint32_t i = 0; i < tensor->rank; ++i, p += 8) tensor->shape[i] = get64(p);
  for (uint32_t i = 0; i < tensor->rank; ++i, p += 8) tensor->stride[i] = get64(p);
  *cursor += (size_t)need; *remaining -= (uint32_t)need;
  return 0;
}

static int send_response(int fd, uint16_t opcode, uint64_t request_id, uint32_t status,
                         const void *extra, uint32_t extra_len) {
  const char *name = status ? mlu_backend_error_name((int)status) : "";
  const char *msg = status ? mlu_backend_error_string((int)status) : "";
  uint32_t nl = (uint32_t)strlen(name), ml = (uint32_t)strlen(msg);
  uint32_t len = 8u + nl + ml + extra_len;
  if (len > MLU_REMOTE_MAX_PAYLOAD) return -1;
  unsigned char *payload = (unsigned char *)malloc(len);
  if (!payload) return -1;
  put32(payload, nl); put32(payload + 4, ml);
  memcpy(payload + 8, name, nl); memcpy(payload + 8 + nl, msg, ml);
  if (extra_len) memcpy(payload + 8 + nl + ml, extra, extra_len);
  struct mlu_remote_wire_header h;
  h.magic = htonl(MLU_REMOTE_MAGIC); h.version = htons(MLU_REMOTE_VERSION); h.opcode = htons(opcode);
  h.request_id = htonll_u(request_id); h.payload_len = htonl(len); h.status = htonl(status);
  int64_t deadline = mlu_deadline_after(io_timeout_ms);
  int r = mlu_io_full(fd, &h, sizeof(h), 1, deadline);
  if (r == 1) r = mlu_io_full(fd, payload, len, 1, deadline);
  if (r != 1) fprintf(stderr, "[server] response failed fd=%d opcode=%u request=%llu errno=%d (%s)\n",
                      fd, opcode, (unsigned long long)request_id, errno, strerror(errno));
  free(payload); return r == 1 ? 0 : -1;
}

static int parse_hello(int fd, const struct mlu_remote_wire_header *h, const unsigned char *p, uint32_t n,
                       mlu_backend_session *s) {
  uint32_t error = 0;
  if (n != 8 && n != 12 && n != 16) error = MLU_REMOTE_BAD_LENGTH;
  else if (get32(p) != MLU_REMOTE_VERSION) error = MLU_REMOTE_BAD_VERSION;
  else if (get32(p + 4) != 0) error = MLU_REMOTE_UNSUPPORTED;
  if (error) {
    send_response(fd, MLU_OP_HELLO, ntohll_u(h->request_id), error, NULL, 0);
    return -1;
  }
  uint32_t device = n >= 12 ? get32(p + 8) : UINT32_MAX;
  uint32_t flags = n == 16 ? get32(p + 12) : CN_CTX_SCHED_SYNC_WAIT;
  int st = device != UINT32_MAX && device > INT_MAX ? CN_ERROR_INVALID_DEVICE :
           flags >= CN_CTX_SCHED_SYNC_NUM ? CN_ERROR_INVALID_VALUE :
           mlu_backend_session_init_flags(s, device == UINT32_MAX ? 0 : (int)device, flags);
  if (st) {
    send_response(fd, MLU_OP_HELLO, ntohll_u(h->request_id), (uint32_t)st, NULL, 0);
    return -1;
  }
  fprintf(stderr, "[server] native context created fd=%d device=%d flags=%u\n", fd, s->device_ordinal, flags);
  int count = 0; st = mlu_backend_device_count(&count);
  unsigned char out[16]; put32(out, MLU_REMOTE_VERSION); put32(out + 4, (uint32_t)count);
  /* Capability bits: device, allocation, copies, queue, notifier, preset
   * module, generic operators, and TransferID completion. */
  put32(out + 8, 0xffu | MLU_CAP_MODULE_IMAGE | MLU_CAP_GENERIC_LAUNCH | MLU_CAP_TRANSFER_RELEASE | MLU_CAP_MODULE_KERNELS | MLU_CAP_DRIVER_CONTEXT | MLU_CAP_TYPED_API);
  put32(out + 12, MLU_REMOTE_KERNEL_ABI);
  int sent = send_response(fd, MLU_OP_HELLO, ntohll_u(h->request_id), (uint32_t)st, out, sizeof(out));
  return st ? -1 : sent;
}

static int handle_request(int fd, mlu_backend_session *s, const struct mlu_remote_wire_header *h,
                          const unsigned char *p, uint32_t n) {
  uint16_t op = ntohs(h->opcode); uint64_t req = ntohll_u(h->request_id);
  uint64_t tid, outid; int st = 0;
  unsigned char out[512];
  switch (op) {
#include "simple_rpc_server.inc"
#include "typed_rpc_server.inc"
    case MLU_OP_MODULE_LOAD_BINARY: {
      if (n < 4 || !get32(p) || get32(p) != n-4)
        return send_response(fd,op,req,MLU_REMOTE_BAD_LENGTH,NULL,0);
      uint64_t mid=0;
      st=mlu_backend_module_load_binary(s,p+4,n-4,&mid); put64(out,mid);
      return send_response(fd,op,req,(uint32_t)st,st ? NULL : out,st ? 0 : 8);
    }
    case MLU_OP_MODULE_QUERY_FATBINARY_MEMORY: {
      if (n < 4 || !get32(p) || get32(p) != n - 4)
        return send_response(fd, op, req, MLU_REMOTE_BAD_LENGTH, NULL, 0);
      uint64_t bytes = 0;
      st = mlu_backend_module_query_fatbinary_memory(s, p + 4, get32(p), &bytes);
      put64(out, bytes);
      return send_response(fd, op, req, (uint32_t)st, st ? NULL : out, st ? 0 : 8);
    }
    case MLU_OP_CONTEXT_SYNC:
      if (n) return send_response(fd,op,req,MLU_REMOTE_BAD_LENGTH,NULL,0);
      st=mlu_backend_context_sync(s);
      return send_response(fd,op,req,(uint32_t)st,NULL,0);
    case MLU_OP_SESSION_CLOSE:
      if (n) return send_response(fd,op,req,MLU_REMOTE_BAD_LENGTH,NULL,0);
      st=mlu_backend_session_cleanup(s);
      fprintf(stderr,"[server] SESSION_CLOSE_ACK fd=%d status=%d\n",fd,st);
      return send_response(fd,op,req,(uint32_t)st,NULL,0);
    case MLU_OP_DEVICE_QUERY: {
      if (n != 4) return send_response(fd, op, req, MLU_REMOTE_BAD_LENGTH, NULL, 0);
      uint32_t ordinal = get32(p), count = 0; char name[256] = {0}; uint64_t mem = 0;
      st = mlu_backend_device_count(&((int){0}));
      int icount = 0; st = mlu_backend_device_count(&icount); count = (uint32_t)icount;
      uint32_t nl = 0;
      if (st == 0 && ordinal != 0xffffffffu && ordinal >= count) st = CN_ERROR_INVALID_DEVICE;
      if (st == 0 && ordinal < count) { st = mlu_backend_device_info((int)ordinal, name, sizeof(name), &mem); nl = (uint32_t)strlen(name); }
      put32(out, count); put64(out + 4, mem); put32(out + 12, nl); memcpy(out + 16, name, nl);
      return send_response(fd, op, req, (uint32_t)st, out, 16 + nl);
    }
    case MLU_OP_DEVICE_SELECT:
      if (n != 4) return send_response(fd, op, req, MLU_REMOTE_BAD_LENGTH, NULL, 0);
      st = mlu_backend_session_select_device(s, (int)get32(p));
      return send_response(fd, op, req, (uint32_t)st, NULL, 0);
    case MLU_OP_ALLOC:
      if (n != 8) return send_response(fd, op, req, MLU_REMOTE_BAD_LENGTH, NULL, 0);
      st = mlu_backend_alloc(s, get64(p), &outid); put64(out, outid);
      return send_response(fd, op, req, (uint32_t)st, st ? NULL : out, st ? 0 : 8);
    case MLU_OP_FREE:
      if (n != 8) return send_response(fd, op, req, MLU_REMOTE_BAD_LENGTH, NULL, 0);
      st = mlu_backend_free(s, get64(p)); return send_response(fd, op, req, (uint32_t)st, NULL, 0);
    case MLU_OP_MEMCPY: {
      if (n < 56) return send_response(fd, op, req, MLU_REMOTE_BAD_LENGTH, NULL, 0);
      uint32_t dir = get32(p); uint64_t q = get64(p + 4), dst = get64(p + 12), dstoff = get64(p + 20), src = get64(p + 28), srcoff = get64(p + 36), bytes = get64(p + 44); uint32_t host_len = get32(p + 52);
      /* v1 clients used a 56-byte header.  The async extension is
       * unambiguous when the payload has the extra flag and its host length
       * matches the 60-byte header; retain the old form for a 4-byte H2D
       * payload whose total length is also 60. */
      uint32_t asynchronous = 0, header = 56;
      if (n >= 60 && host_len == n - 60 && get32(p + 56) <= 1) {
        asynchronous = get32(p + 56); header = 60;
      }
      if (bytes > MLU_REMOTE_MAX_PAYLOAD - 16 || host_len != n - header || (dir == MLU_MEMCPY_H2D && host_len != bytes) || (dir != MLU_MEMCPY_H2D && host_len != 0)) return send_response(fd, op, req, MLU_REMOTE_BAD_LENGTH, NULL, 0);
      void *host = NULL; if (dir == MLU_MEMCPY_D2H) host = malloc((size_t)bytes); if (dir == MLU_MEMCPY_D2H && !host) return send_response(fd, op, req, CN_ERROR_SYSTEM_OUT_OF_MEMORY, NULL, 0);
      st = mlu_backend_memcpy_ex(s, (int)dir, dst, dstoff, src, srcoff, dir == MLU_MEMCPY_H2D ? p + header : NULL, host, bytes, q, asynchronous, &tid);
      put64(out, tid); put64(out + 8, (st == 0 && dir == MLU_MEMCPY_D2H) ? bytes : 0);
      unsigned char *response_extra = out;
      if (st == 0 && dir == MLU_MEMCPY_D2H && bytes) {
        response_extra = (unsigned char *)malloc(16 + (size_t)bytes);
        if (!response_extra) { free(host); return send_response(fd, op, req, CN_ERROR_SYSTEM_OUT_OF_MEMORY, NULL, 0); }
        memcpy(response_extra, out, 16); memcpy(response_extra + 16, host, (size_t)bytes);
      }
      int rr = send_response(fd, op, req, (uint32_t)st, response_extra,
                             st == 0 ? (dir == MLU_MEMCPY_D2H ? 16 + (uint32_t)bytes : 8) : 0);
      if (response_extra != out) free(response_extra);
      free(host); return rr;
    }
    case MLU_OP_QUEUE_CREATE:
      if (n) return send_response(fd, op, req, MLU_REMOTE_BAD_LENGTH, NULL, 0);
      st = mlu_backend_queue_create(s, &outid); put64(out, outid); return send_response(fd, op, req, (uint32_t)st, st ? NULL : out, st ? 0 : 8);
    case MLU_OP_NOTIFIER_CREATE:
      if (n) return send_response(fd, op, req, MLU_REMOTE_BAD_LENGTH, NULL, 0);
      st = mlu_backend_notifier_create(s, &outid); put64(out, outid); return send_response(fd, op, req, (uint32_t)st, st ? NULL : out, st ? 0 : 8);
    case MLU_OP_NOTIFIER_RECORD:
      if (n != 16) return send_response(fd, op, req, MLU_REMOTE_BAD_LENGTH, NULL, 0);
      st = mlu_backend_notifier_record(s, get64(p), get64(p + 8)); return send_response(fd, op, req, (uint32_t)st, NULL, 0);
    case MLU_OP_OPERATOR_CREATE: {
      if (n != 4) return send_response(fd, op, req, MLU_REMOTE_BAD_LENGTH, NULL, 0);
      uint64_t mid = 0, kid = 0;
      st = mlu_backend_operator_create(s, get32(p), &mid, &kid);
      put64(out, mid); put64(out + 8, kid);
      return send_response(fd, op, req, (uint32_t)st, st ? NULL : out, st ? 0 : 16);
    }
    case MLU_OP_OPERATOR_DESTROY:
      if (n != 4) return send_response(fd, op, req, MLU_REMOTE_BAD_LENGTH, NULL, 0);
      st = mlu_backend_operator_destroy(s, get32(p));
      return send_response(fd, op, req, (uint32_t)st, NULL, 0);
    case MLU_OP_OPERATOR_LAUNCH: {
      /* Fixed prefix followed by input/output tensor descriptors and scalar
       * bytes. Tensor descriptors never contain native addresses. */
      if (n < 64) return send_response(fd, op, req, MLU_REMOTE_BAD_LENGTH, NULL, 0);
      uint32_t operator_id = get32(p), input_count = get32(p + 24), output_count = get32(p + 28), scalar_bytes = get32(p + 32), flags = get32(p + 36);
      if (flags != 0 || input_count > 16 || output_count > 16 || input_count + output_count > 16 || scalar_bytes > n - 64)
        return send_response(fd, op, req, MLU_REMOTE_BAD_LENGTH, NULL, 0);
      mlu_backend_tensor *inputs = input_count ? calloc(input_count, sizeof(*inputs)) : NULL;
      mlu_backend_tensor *outputs = output_count ? calloc(output_count, sizeof(*outputs)) : NULL;
      if ((input_count && !inputs) || (output_count && !outputs)) { free(inputs); free(outputs); return send_response(fd, op, req, CN_ERROR_SYSTEM_OUT_OF_MEMORY, NULL, 0); }
      const unsigned char *cursor = p + 64; uint32_t remaining = n - 64;
      int parse_error = 0;
      for (uint32_t i = 0; i < input_count && !parse_error; ++i) parse_error = parse_operator_tensor(&cursor, &remaining, &inputs[i]);
      for (uint32_t i = 0; i < output_count && !parse_error; ++i) parse_error = parse_operator_tensor(&cursor, &remaining, &outputs[i]);
      if (parse_error || remaining != scalar_bytes) {
        free(inputs); free(outputs); return send_response(fd, op, req, MLU_REMOTE_BAD_LENGTH, NULL, 0);
      }
      mlu_backend_operator_request request;
      memset(&request, 0, sizeof(request));
      request.operator_id = operator_id; request.kernel_id = get64(p + 8); request.queue_id = get64(p + 16);
      request.input_count = input_count; request.output_count = output_count;
      request.grid_x = get32(p + 40); request.grid_y = get32(p + 44); request.grid_z = get32(p + 48);
      request.block_x = get32(p + 52); request.block_y = get32(p + 56); request.block_z = get32(p + 60);
      request.inputs = inputs; request.outputs = outputs; request.scalars = cursor; request.scalar_bytes = scalar_bytes;
      tid = 0; st = mlu_backend_operator_launch(s, &request, &tid); free(inputs); free(outputs);
      put64(out, tid);
      return send_response(fd, op, req, (uint32_t)st, st ? NULL : out, st ? 0 : 8);
    }
    case MLU_OP_MODULE_LOAD_PRESET: {
      if (n < 4 || get32(p) != n - 4 || get32(p) > 128) return send_response(fd, op, req, MLU_REMOTE_BAD_LENGTH, NULL, 0);
      char name[129]; uint32_t nl = get32(p); memcpy(name, p + 4, nl); name[nl] = 0; uint64_t mid = 0, kid = 0;
      st = mlu_backend_module_load(s, name, &mid, &kid); put64(out, mid); put64(out + 8, kid); return send_response(fd, op, req, (uint32_t)st, st ? NULL : out, st ? 0 : 16);
    }
    case MLU_OP_MODULE_LOAD_IMAGE: {
      if (n < 8) return send_response(fd, op, req, MLU_REMOTE_BAD_LENGTH, NULL, 0);
      uint32_t image_len = get32(p), name_len = get32(p + 4);
      if (!image_len || !name_len || name_len > 1024 || image_len > MLU_REMOTE_MAX_PAYLOAD ||
          (uint64_t)image_len + name_len + 8u != n) return send_response(fd, op, req, MLU_REMOTE_BAD_LENGTH, NULL, 0);
      if (memchr(p + 8 + image_len, 0, name_len)) return send_response(fd, op, req, MLU_REMOTE_BAD_LENGTH, NULL, 0);
      char *name = calloc(1, (size_t)name_len + 1); if (!name) return send_response(fd, op, req, CN_ERROR_SYSTEM_OUT_OF_MEMORY, NULL, 0);
      memcpy(name, p + 8 + image_len, name_len);
      uint64_t mid = 0, kid = 0;
      st = mlu_backend_module_load_image(s, p + 8, image_len, name, &mid, &kid); free(name);
      put64(out, mid); put64(out + 8, kid);
      return send_response(fd, op, req, (uint32_t)st, st ? NULL : out, st ? 0 : 16);
    }
    case MLU_OP_MODULE_GET_KERNEL: {
      if (n < 12) return send_response(fd, op, req, MLU_REMOTE_BAD_LENGTH, NULL, 0);
      uint32_t length = get32(p + 8);
      if (!length || length >= 256 || length != n - 12 || memchr(p + 12, 0, length))
        return send_response(fd, op, req, MLU_REMOTE_BAD_LENGTH, NULL, 0);
      char name[256]; memcpy(name, p + 12, length); name[length] = 0;
      uint64_t kid = 0;
      st = mlu_backend_module_get_kernel(s, get64(p), name, &kid);
      put64(out, kid);
      return send_response(fd, op, req, (uint32_t)st, st ? NULL : out, st ? 0 : 8);
    }
    case MLU_OP_KERNEL_LAUNCH_GENERIC: {
      if (n < 44) return send_response(fd, op, req, MLU_REMOTE_BAD_LENGTH, NULL, 0);
      uint64_t mid = get64(p), kid = get64(p + 8), qid = get64(p + 16);
      uint32_t dx = get32(p + 24), dy = get32(p + 28), dz = get32(p + 32), klass = get32(p + 36), count = get32(p + 40);
      if (count > 256) return send_response(fd, op, req, MLU_REMOTE_BAD_LENGTH, NULL, 0);
      mlu_backend_kernel_arg *args = count ? calloc(count, sizeof(*args)) : NULL;
      if (count && !args) return send_response(fd, op, req, CN_ERROR_SYSTEM_OUT_OF_MEMORY, NULL, 0);
      const unsigned char *cursor = p + 44; uint32_t remaining = n - 44; int parse_error = 0;
      for (uint32_t i = 0; i < count; ++i) {
        if (remaining < 32) { parse_error = 1; break; }
        args[i].kind = get32(cursor); args[i].size = get32(cursor + 4);
        args[i].allocation_id = get64(cursor + 8); args[i].offset = get64(cursor + 16);
        if (get64(cursor + 24) != 0 || (args[i].kind == 2 && (args[i].allocation_id || args[i].offset))) { parse_error = 1; break; }
        cursor += 32; remaining -= 32;
        if (!args[i].size || args[i].size > 4096 || args[i].size > remaining ||
            (args[i].kind != 1 && args[i].kind != 2) || (args[i].kind == 1 && args[i].size != 8)) { parse_error = 1; break; }
        args[i].scalar = args[i].kind == 2 ? cursor : NULL;
        cursor += args[i].size; remaining -= args[i].size;
      }
      if (parse_error || remaining) { free(args); return send_response(fd, op, req, MLU_REMOTE_BAD_LENGTH, NULL, 0); }
      tid = 0; st = mlu_backend_kernel_launch_generic(s, mid, kid, qid, dx, dy, dz, klass, args, count, &tid); free(args);
      put64(out, tid); return send_response(fd, op, req, (uint32_t)st, st ? NULL : out, st ? 0 : 8);
    }
    case MLU_OP_KERNEL_LAUNCH:
      if (n != 56) return send_response(fd, op, req, MLU_REMOTE_BAD_LENGTH, NULL, 0);
      st = mlu_backend_kernel_launch_vector_add(s, get64(p), get64(p + 8), get64(p + 16), get64(p + 24), get64(p + 32), get32(p + 40), get32(p + 44)); return send_response(fd, op, req, (uint32_t)st, NULL, 0);
    case MLU_OP_CNNL_OPERATION:
      return send_response(fd, op, req, MLU_REMOTE_UNSUPPORTED, NULL, 0);
    case MLU_OP_SESSION_RESET:
      if (n != 0) return send_response(fd, op, req, MLU_REMOTE_BAD_LENGTH, NULL, 0);
      st = mlu_backend_session_reset(s);
      return send_response(fd, op, req, (uint32_t)st, NULL, 0);
    default: return send_response(fd, op, req, MLU_REMOTE_PROTOCOL_ERROR, NULL, 0);
  }
}

static void *session_worker(void *arg) {
  struct worker_arg *accepted = arg;
  int fd = accepted->fd, ready = 0;
  /* The bounded worker owns its heap session until native cleanup completes. */
  mlu_backend_session *session = calloc(1, sizeof(*session));
  if (!session) goto finished;
  fprintf(stderr, "[server] session start fd=%d awaiting HELLO\n", fd);
  uint64_t last_request = 0;
  while (!stop_server) {
    int64_t deadline = ready ? mlu_deadline_after(io_timeout_ms) : accepted->handshake_deadline;
    struct mlu_remote_wire_header h; int rr = mlu_io_full(fd, &h, sizeof(h), 0, deadline);
    if (rr <= 0) {
      if (rr < 0) fprintf(stderr, "[server] receive failed fd=%d phase=header ready=%d errno=%d (%s)\n", fd, ready, errno, strerror(errno));
      break;
    }
    uint32_t magic = ntohl(h.magic), version = ntohs(h.version), n = ntohl(h.payload_len);
    if (magic != MLU_REMOTE_MAGIC || version != MLU_REMOTE_VERSION || n > MLU_REMOTE_MAX_PAYLOAD || ntohl(h.status) != 0) { send_response(fd, ntohs(h.opcode), ntohll_u(h.request_id), magic != MLU_REMOTE_MAGIC ? MLU_REMOTE_PROTOCOL_ERROR : (version != MLU_REMOTE_VERSION ? MLU_REMOTE_BAD_VERSION : MLU_REMOTE_BAD_LENGTH), NULL, 0); break; }
    uint64_t request_id = ntohll_u(h.request_id);
    if (!request_id || request_id <= last_request) { send_response(fd, ntohs(h.opcode), request_id, MLU_REMOTE_PROTOCOL_ERROR, NULL, 0); break; }
    last_request = request_id;
    uint16_t op = ntohs(h.opcode);
    if ((!ready && op != MLU_OP_HELLO) || (ready && op == MLU_OP_HELLO)) {
      send_response(fd, op, request_id, MLU_REMOTE_PROTOCOL_ERROR, NULL, 0); break;
    }
    if (!ready && n != 8 && n != 12 && n != 16) {
      send_response(fd, op, request_id, MLU_REMOTE_BAD_LENGTH, NULL, 0); break;
    }
    unsigned char *p = n ? (unsigned char *)malloc(n) : NULL; if (n && !p) break;
    rr = n ? mlu_io_full(fd, p, n, 0, deadline) : 1;
    if (rr <= 0) {
      if (rr < 0) fprintf(stderr, "[server] receive failed fd=%d phase=payload ready=%d errno=%d (%s)\n", fd, ready, errno, strerror(errno));
      free(p); break;
    }
    if (stop_server) { free(p); break; }
    int handled = ready ? handle_request(fd, session, &h, p, n) : parse_hello(fd, &h, p, n, session);
    free(p);
    if (handled < 0 || session->cleanup_failed || !session->context) break;
    ready = 1;
  }
  if (session->context || session->cleanup_failed) {
    fprintf(stderr, "[server] session fd=%d disconnected; cleaning native resources\n", fd);
    int st = session->cleanup_failed ? session->cleanup_failed : mlu_backend_session_cleanup(session);
    fprintf(stderr, "[server] %s fd=%d status=%d\n", st ? "SESSION_CLEANUP_FAILED" : "SESSION_CLEANUP_EMPTY", fd, st);
    if (st) {
      pthread_mutex_lock(&workers_lock); ++cleanup_errors; pthread_mutex_unlock(&workers_lock);
      /* An unconfirmed context cannot be allowed to leak outside admission
       * accounting through repeated new sessions. Contain it by stopping. */
      if (session->context) request_stop();
    }
  } else fprintf(stderr, "[server] SESSION_NO_NATIVE_CONTEXT fd=%d\n", fd);
  free(session);
finished:
  /* Remove and close under the same lock used by shutdown: never shutdown a
   * descriptor that another worker has already closed and the OS reused. */
  pthread_mutex_lock(&workers_lock);
  struct worker_arg **link = &workers;
  while (*link != accepted) link = &(*link)->next;
  *link = accepted->next;
  close(fd); free(accepted); --active_workers;
  pthread_cond_broadcast(&workers_empty);
  pthread_mutex_unlock(&workers_lock);
  return NULL;
}

static void request_stop(void) {
  int saved = errno; stop_server = 1;
  unsigned char byte = 1;
  if (signal_pipe[1] >= 0) { ssize_t ignored = write(signal_pipe[1], &byte, 1); (void)ignored; }
  errno = saved;
}
static void on_signal(int sig) { (void)sig; request_stop(); }

int main(void) {
  if (mlu_timeout_env("MLU_REMOTE_SERVER_HANDSHAKE_TIMEOUT_MS", 5000, &handshake_timeout_ms) ||
      mlu_timeout_env("MLU_REMOTE_SERVER_IO_TIMEOUT_MS", 120000, &io_timeout_ms)) {
    fprintf(stderr, "server timeouts must be integers from 1 to 3600000 milliseconds\n"); return 1;
  }
  const char *bind_addr = getenv("MLU_REMOTE_BIND"); if (!bind_addr) bind_addr = "127.0.0.1";
  int port = getenv("MLU_REMOTE_PORT") ? atoi(getenv("MLU_REMOTE_PORT")) : 50051;
  int st = mlu_backend_global_init(); if (st) { fprintf(stderr, "backend init failed: %s (%d)\n", mlu_backend_error_string(st), st); return 1; }
  int count = 0; mlu_backend_device_count(&count); char name[128] = {0}; uint64_t mem = 0; if (count) mlu_backend_device_info(0, name, sizeof(name), &mem);
  fprintf(stderr, "[server] MLU_REMOTE_BIND=%s PORT=%d device_count=%d device0=%s memory=%llu\n", bind_addr, port, count, name, (unsigned long long)mem);
  listen_fd = socket(AF_INET, SOCK_STREAM, 0); if (listen_fd < 0) { perror("socket"); return 1; }
  int one = 1; setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  struct sockaddr_in addr; memset(&addr, 0, sizeof(addr)); addr.sin_family = AF_INET; addr.sin_port = htons((uint16_t)port); if (inet_pton(AF_INET, bind_addr, &addr.sin_addr) != 1) { fprintf(stderr, "invalid bind address\n"); return 1; }
  if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) || listen(listen_fd, 16)) { perror("bind/listen"); return 1; }
  if (pipe(signal_pipe)) { perror("signal pipe"); close(listen_fd); return 1; }
  if (fcntl(signal_pipe[0], F_SETFL, O_NONBLOCK) || fcntl(signal_pipe[1], F_SETFL, O_NONBLOCK) ||
      fcntl(listen_fd, F_SETFL, O_NONBLOCK)) { perror("nonblocking descriptors"); return 1; }
  struct sigaction action; memset(&action, 0, sizeof(action)); action.sa_handler = on_signal;
  sigemptyset(&action.sa_mask);
  sigaction(SIGINT, &action, NULL); sigaction(SIGTERM, &action, NULL);
  fprintf(stderr, "[server] listening handshake_timeout_ms=%u io_timeout_ms=%u max_connections=%u\n",
          handshake_timeout_ms, io_timeout_ms, mlu_resource_max_connections());
  int failed = 0;
  while (!stop_server) {
    struct pollfd waiting[] = {{.fd = listen_fd, .events = POLLIN}, {.fd = signal_pipe[0], .events = POLLIN}};
    int polled = poll(waiting, 2, -1);
    if (polled < 0) { if (errno == EINTR) continue; perror("poll accept"); failed = 1; break; }
    if (stop_server) break;
    if (!(waiting[0].revents & POLLIN)) continue;
    int fd = accept(listen_fd, NULL, NULL);
    if (fd < 0) { if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue; perror("accept"); failed = 1; break; }
    int no_delay = 1; setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &no_delay, sizeof(no_delay));
    struct worker_arg *arg = malloc(sizeof(*arg)); if (!arg) { close(fd); continue; }
    pthread_mutex_lock(&workers_lock);
    if (active_workers >= mlu_resource_max_connections()) {
      fprintf(stderr, "[server] CONNECTION_LIMIT active=%u\n", active_workers);
      pthread_mutex_unlock(&workers_lock); close(fd); free(arg); continue;
    }
    arg->fd = fd; arg->handshake_deadline = mlu_deadline_after(handshake_timeout_ms);
    arg->next = workers; workers = arg; ++active_workers;
    pthread_attr_t attr; pthread_attr_init(&attr); pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_t t; int created = pthread_create(&t, &attr, session_worker, arg);
    pthread_attr_destroy(&attr);
    if (created) { workers = arg->next; --active_workers; close(fd); free(arg); }
    pthread_mutex_unlock(&workers_lock);
  }
  stop_server = 1;
  close(listen_fd); listen_fd = -1;
  pthread_mutex_lock(&workers_lock);
  fprintf(stderr, "[server] DRAIN_BEGIN workers=%u\n", active_workers);
  for (struct worker_arg *w = workers; w; w = w->next) shutdown(w->fd, SHUT_RDWR);
  while (active_workers) pthread_cond_wait(&workers_empty, &workers_lock);
  fprintf(stderr, "[server] DRAIN_COMPLETE workers=0 cleanup_errors=%u\n", cleanup_errors);
  if (cleanup_errors) failed = 1;
  pthread_mutex_unlock(&workers_lock);
  mlu_resource_report();
  /* Keep the self-pipe open until exit so a late signal cannot write a reused fd. */
  fprintf(stderr, "[server] stopped\n"); return failed;
}
