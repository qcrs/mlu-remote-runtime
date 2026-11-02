#include "mlu_backend.h"
#include "remote_protocol.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static volatile sig_atomic_t stop_server;
static int listen_fd = -1;

static uint64_t ntohll_u(uint64_t x) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  return ((uint64_t)ntohl((uint32_t)x) << 32) | ntohl((uint32_t)(x >> 32));
#else
  return x;
#endif
}
static uint64_t htonll_u(uint64_t x) { return ntohll_u(x); }
static int read_full(int fd, void *buf, size_t n) {
  unsigned char *p = (unsigned char *)buf;
  while (n) { ssize_t r = recv(fd, p, n, 0); if (r == 0) return 0; if (r < 0) { if (errno == EINTR) continue; return -1; } p += r; n -= (size_t)r; }
  return 1;
}
static int write_full(int fd, const void *buf, size_t n) {
  const unsigned char *p = (const unsigned char *)buf;
  while (n) { ssize_t r = send(fd, p, n, MSG_NOSIGNAL); if (r < 0) { if (errno == EINTR) continue; return -1; } p += r; n -= (size_t)r; }
  return 0;
}
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
  int r = write_full(fd, &h, sizeof(h)); if (!r) r = write_full(fd, payload, len);
  free(payload); return r;
}

static int parse_hello(int fd, const struct mlu_remote_wire_header *h, const unsigned char *p, uint32_t n,
                       mlu_backend_session *s) {
  if (n != 8 && n != 12) return send_response(fd, MLU_OP_HELLO, ntohll_u(h->request_id), MLU_REMOTE_BAD_LENGTH, NULL, 0);
  uint32_t version = get32(p);
  if (version != MLU_REMOTE_VERSION)
    return send_response(fd, MLU_OP_HELLO, ntohll_u(h->request_id), MLU_REMOTE_BAD_VERSION, NULL, 0);
  if (n == 12 && get32(p + 8) != UINT32_MAX) {
    int select_status = mlu_backend_session_select_device(s, (int)get32(p + 8));
    if (select_status != 0)
      return send_response(fd, MLU_OP_HELLO, ntohll_u(h->request_id), (uint32_t)select_status, NULL, 0);
  }
  int count = 0; int st = mlu_backend_device_count(&count);
  unsigned char out[16]; put32(out, MLU_REMOTE_VERSION); put32(out + 4, (uint32_t)count);
  /* Capability bits: device, allocation, copies, queue, notifier, preset
   * module, generic operators, and TransferID completion. */
  put32(out + 8, 0xffu); put32(out + 12, 0u);
  return send_response(fd, MLU_OP_HELLO, ntohll_u(h->request_id), (uint32_t)st, out, sizeof(out));
}

static int handle_request(int fd, mlu_backend_session *s, const struct mlu_remote_wire_header *h,
                          const unsigned char *p, uint32_t n) {
  uint16_t op = ntohs(h->opcode); uint64_t req = ntohll_u(h->request_id);
  uint64_t tid, outid; int st = 0;
  unsigned char out[512];
  switch (op) {
    case MLU_OP_HELLO: return parse_hello(fd, h, p, n, s);
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
      if (bytes > MLU_REMOTE_MAX_PAYLOAD - 16 || host_len != n - 56 || (dir == MLU_MEMCPY_H2D && host_len != bytes) || (dir != MLU_MEMCPY_H2D && host_len != 0)) return send_response(fd, op, req, MLU_REMOTE_BAD_LENGTH, NULL, 0);
      void *host = NULL; if (dir == MLU_MEMCPY_D2H) host = malloc((size_t)bytes); if (dir == MLU_MEMCPY_D2H && !host) return send_response(fd, op, req, CN_ERROR_SYSTEM_OUT_OF_MEMORY, NULL, 0);
      st = mlu_backend_memcpy(s, (int)dir, dst, dstoff, src, srcoff, dir == MLU_MEMCPY_H2D ? p + 56 : NULL, host, bytes, q, &tid);
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
    case MLU_OP_QUEUE_DESTROY: case MLU_OP_QUEUE_SYNC: case MLU_OP_QUEUE_QUERY:
      if (n != 8) return send_response(fd, op, req, MLU_REMOTE_BAD_LENGTH, NULL, 0);
      if (op == MLU_OP_QUEUE_DESTROY) st = mlu_backend_queue_destroy(s, get64(p)); else if (op == MLU_OP_QUEUE_SYNC) st = mlu_backend_queue_sync(s, get64(p)); else st = mlu_backend_queue_query(s, get64(p));
      return send_response(fd, op, req, (uint32_t)st, NULL, 0);
    case MLU_OP_NOTIFIER_CREATE:
      if (n) return send_response(fd, op, req, MLU_REMOTE_BAD_LENGTH, NULL, 0);
      st = mlu_backend_notifier_create(s, &outid); put64(out, outid); return send_response(fd, op, req, (uint32_t)st, st ? NULL : out, st ? 0 : 8);
    case MLU_OP_NOTIFIER_DESTROY: case MLU_OP_NOTIFIER_QUERY: case MLU_OP_NOTIFIER_SYNC:
      if (n != 8) return send_response(fd, op, req, MLU_REMOTE_BAD_LENGTH, NULL, 0);
      if (op == MLU_OP_NOTIFIER_DESTROY) st = mlu_backend_notifier_destroy(s, get64(p)); else if (op == MLU_OP_NOTIFIER_QUERY) st = mlu_backend_notifier_query(s, get64(p)); else st = mlu_backend_notifier_sync(s, get64(p));
      return send_response(fd, op, req, (uint32_t)st, NULL, 0);
    case MLU_OP_NOTIFIER_RECORD:
      if (n != 16) return send_response(fd, op, req, MLU_REMOTE_BAD_LENGTH, NULL, 0);
      st = mlu_backend_notifier_record(s, get64(p), get64(p + 8)); return send_response(fd, op, req, (uint32_t)st, NULL, 0);
    case MLU_OP_TRANSFER_QUERY:
    case MLU_OP_TRANSFER_SYNC:
      if (n != 8) return send_response(fd, op, req, MLU_REMOTE_BAD_LENGTH, NULL, 0);
      st = op == MLU_OP_TRANSFER_QUERY ? mlu_backend_transfer_query(s, get64(p))
                                       : mlu_backend_transfer_sync(s, get64(p));
      return send_response(fd, op, req, (uint32_t)st, NULL, 0);
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
    case MLU_OP_MODULE_UNLOAD_PRESET:
      if (n != 8) return send_response(fd, op, req, MLU_REMOTE_BAD_LENGTH, NULL, 0);
      st = mlu_backend_module_unload(s, get64(p));
      return send_response(fd, op, req, (uint32_t)st, NULL, 0);
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
  int fd = *(int *)arg; free(arg);
  mlu_backend_session session; int st = mlu_backend_session_init(&session, 0);
  fprintf(stderr, "[server] session start fd=%d backend=%d\n", fd, st);
  if (st) { close(fd); return NULL; }
  uint64_t last_request = 0;
  for (;;) {
    struct mlu_remote_wire_header h; int rr = read_full(fd, &h, sizeof(h)); if (rr <= 0) break;
    uint32_t magic = ntohl(h.magic), version = ntohs(h.version), n = ntohl(h.payload_len);
    if (magic != MLU_REMOTE_MAGIC || version != MLU_REMOTE_VERSION || n > MLU_REMOTE_MAX_PAYLOAD || ntohl(h.status) != 0) { send_response(fd, ntohs(h.opcode), ntohll_u(h.request_id), magic != MLU_REMOTE_MAGIC ? MLU_REMOTE_PROTOCOL_ERROR : (version != MLU_REMOTE_VERSION ? MLU_REMOTE_BAD_VERSION : MLU_REMOTE_BAD_LENGTH), NULL, 0); break; }
    uint64_t request_id = ntohll_u(h.request_id);
    if (!request_id || request_id <= last_request) { send_response(fd, ntohs(h.opcode), request_id, MLU_REMOTE_PROTOCOL_ERROR, NULL, 0); break; }
    last_request = request_id;
    unsigned char *p = n ? (unsigned char *)malloc(n) : NULL; if (n && !p) break;
    rr = n ? read_full(fd, p, n) : 1; if (rr <= 0) { free(p); break; }
    if (handle_request(fd, &session, &h, p, n) < 0) { free(p); break; }
    free(p);
  }
  fprintf(stderr, "[server] session fd=%d disconnected; cleaning native resources\n", fd);
  mlu_backend_session_cleanup(&session); close(fd); fprintf(stderr, "[server] SESSION_CLEANUP_EMPTY fd=%d\n", fd); return NULL;
}

static void on_signal(int sig) { (void)sig; stop_server = 1; if (listen_fd >= 0) close(listen_fd); }

int main(void) {
  const char *bind_addr = getenv("MLU_REMOTE_BIND"); if (!bind_addr) bind_addr = "127.0.0.1";
  int port = getenv("MLU_REMOTE_PORT") ? atoi(getenv("MLU_REMOTE_PORT")) : 50051;
  int st = mlu_backend_global_init(); if (st) { fprintf(stderr, "backend init failed: %s (%d)\n", mlu_backend_error_string(st), st); return 1; }
  int count = 0; mlu_backend_device_count(&count); char name[128] = {0}; uint64_t mem = 0; if (count) mlu_backend_device_info(0, name, sizeof(name), &mem);
  fprintf(stderr, "[server] MLU_REMOTE_BIND=%s PORT=%d device_count=%d device0=%s memory=%llu\n", bind_addr, port, count, name, (unsigned long long)mem);
  listen_fd = socket(AF_INET, SOCK_STREAM, 0); if (listen_fd < 0) { perror("socket"); return 1; }
  int one = 1; setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  struct sockaddr_in addr; memset(&addr, 0, sizeof(addr)); addr.sin_family = AF_INET; addr.sin_port = htons((uint16_t)port); if (inet_pton(AF_INET, bind_addr, &addr.sin_addr) != 1) { fprintf(stderr, "invalid bind address\n"); return 1; }
  if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) || listen(listen_fd, 16)) { perror("bind/listen"); return 1; }
  signal(SIGINT, on_signal); signal(SIGTERM, on_signal); fprintf(stderr, "[server] listening\n");
  while (!stop_server) {
    int fd = accept(listen_fd, NULL, NULL); if (fd < 0) { if (stop_server || errno == EINTR) continue; perror("accept"); break; }
    int *arg = (int *)malloc(sizeof(*arg)); if (!arg) { close(fd); continue; } *arg = fd;
    pthread_t t; if (pthread_create(&t, NULL, session_worker, arg) == 0) pthread_detach(t); else { close(fd); free(arg); }
  }
  if (listen_fd >= 0) close(listen_fd); fprintf(stderr, "[server] stopped\n"); return 0;
}
