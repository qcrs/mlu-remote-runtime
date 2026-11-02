#include "remote_cnrt.h"
#include "remote_operator.h"
#include <cn_api.h>

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

struct mlu_remote_context {
  int fd;
  uint64_t next_request;
  pthread_mutex_t lock;
  mlu_remote_error error;
};

static uint64_t ntohll_u(uint64_t x) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  return ((uint64_t)ntohl((uint32_t)x) << 32) | ntohl((uint32_t)(x >> 32));
#else
  return x;
#endif
}
static uint64_t htonll_u(uint64_t x) { return ntohll_u(x); }
static int io_full(int fd, void *buf, size_t n, int write_mode) {
  unsigned char *p = (unsigned char *)buf;
  while (n) {
    ssize_t r = write_mode ? send(fd, p, n, MSG_NOSIGNAL) : recv(fd, p, n, 0);
    if (r == 0) return -1; if (r < 0) { if (errno == EINTR) continue; return -1; }
    p += r; n -= (size_t)r;
  }
  return 0;
}
static void put32(unsigned char *p, uint32_t x) { x = htonl(x); memcpy(p, &x, 4); }
static void put64(unsigned char *p, uint64_t x) { x = htonll_u(x); memcpy(p, &x, 8); }
static uint32_t get32(const unsigned char *p) { uint32_t x; memcpy(&x, p, 4); return ntohl(x); }
static uint64_t get64(const unsigned char *p) { uint64_t x; memcpy(&x, p, 8); return ntohll_u(x); }

static void set_local_error(mlu_remote_context *ctx, uint32_t code, const char *name, const char *message) {
  ctx->error.code = code; snprintf(ctx->error.name, sizeof(ctx->error.name), "%s", name ? name : "REMOTE_CLIENT_ERROR"); snprintf(ctx->error.message, sizeof(ctx->error.message), "%s", message ? message : "remote request failed");
}

/* Caller owns *extra and must free it. The returned extra excludes the common
 * error name/message prefix. */
static int rpc_locked(mlu_remote_context *ctx, uint16_t opcode, const void *payload, uint32_t payload_len,
                      unsigned char **extra, uint32_t *extra_len) {
  struct mlu_remote_wire_header h; memset(&h, 0, sizeof(h));
  uint64_t req = ++ctx->next_request; h.magic = htonl(MLU_REMOTE_MAGIC); h.version = htons(MLU_REMOTE_VERSION); h.opcode = htons(opcode); h.request_id = htonll_u(req); h.payload_len = htonl(payload_len); h.status = 0;
  if (io_full(ctx->fd, &h, sizeof(h), 1) || (payload_len && io_full(ctx->fd, (void *)payload, payload_len, 1))) { set_local_error(ctx, MLU_REMOTE_PROTOCOL_ERROR, "REMOTE_SEND_FAILED", strerror(errno)); return (int)MLU_REMOTE_PROTOCOL_ERROR; }
  if (io_full(ctx->fd, &h, sizeof(h), 0)) { set_local_error(ctx, MLU_REMOTE_PROTOCOL_ERROR, "REMOTE_RECV_FAILED", strerror(errno)); return (int)MLU_REMOTE_PROTOCOL_ERROR; }
  uint32_t n = ntohl(h.payload_len), status = ntohl(h.status); if (ntohl(h.magic) != MLU_REMOTE_MAGIC || ntohs(h.version) != MLU_REMOTE_VERSION || ntohs(h.opcode) != opcode || ntohll_u(h.request_id) != req || n > MLU_REMOTE_MAX_PAYLOAD) { set_local_error(ctx, MLU_REMOTE_PROTOCOL_ERROR, "REMOTE_BAD_RESPONSE", "invalid response frame"); return (int)MLU_REMOTE_PROTOCOL_ERROR; }
  unsigned char *p = n ? (unsigned char *)malloc(n) : NULL; if (n && !p) { set_local_error(ctx, CN_ERROR_SYSTEM_OUT_OF_MEMORY, "HOST_OUT_OF_MEMORY", "response allocation failed"); return (int)CN_ERROR_SYSTEM_OUT_OF_MEMORY; }
  if (n && io_full(ctx->fd, p, n, 0)) { free(p); set_local_error(ctx, MLU_REMOTE_PROTOCOL_ERROR, "REMOTE_RECV_FAILED", strerror(errno)); return (int)MLU_REMOTE_PROTOCOL_ERROR; }
  if (n < 8) { free(p); set_local_error(ctx, MLU_REMOTE_BAD_LENGTH, "REMOTE_BAD_LENGTH", "response lacks error prefix"); return (int)MLU_REMOTE_BAD_LENGTH; }
  uint32_t nl = get32(p), ml = get32(p + 4); if (nl > n - 8 || ml > n - 8 - nl) { free(p); set_local_error(ctx, MLU_REMOTE_BAD_LENGTH, "REMOTE_BAD_LENGTH", "invalid response error strings"); return (int)MLU_REMOTE_BAD_LENGTH; }
  if (status) { size_t l = nl < sizeof(ctx->error.name) - 1 ? nl : sizeof(ctx->error.name) - 1; memcpy(ctx->error.name, p + 8, l); ctx->error.name[l] = 0; l = ml < sizeof(ctx->error.message) - 1 ? ml : sizeof(ctx->error.message) - 1; memcpy(ctx->error.message, p + 8 + nl, l); ctx->error.message[l] = 0; ctx->error.code = status; }
  else { ctx->error.code = 0; ctx->error.name[0] = 0; ctx->error.message[0] = 0; }
  uint32_t rest = n - 8 - nl - ml; if (extra) { *extra_len = rest; *extra = rest ? (unsigned char *)malloc(rest) : NULL; if (rest && !*extra) { free(p); set_local_error(ctx, CN_ERROR_SYSTEM_OUT_OF_MEMORY, "HOST_OUT_OF_MEMORY", "response allocation failed"); return (int)CN_ERROR_SYSTEM_OUT_OF_MEMORY; } if (rest) memcpy(*extra, p + 8 + nl + ml, rest); } free(p); return (int)status;
}

static int rpc(mlu_remote_context *ctx, uint16_t opcode, const void *payload, uint32_t payload_len, unsigned char **extra, uint32_t *extra_len) {
  pthread_mutex_lock(&ctx->lock); int r = rpc_locked(ctx, opcode, payload, payload_len, extra, extra_len); pthread_mutex_unlock(&ctx->lock); return r;
}

static mlu_remote_context *connect_impl(const char *host, uint16_t port, uint32_t requested_device) {
  if (!host) host = "127.0.0.1";
  char service[16]; snprintf(service, sizeof(service), "%u", port);
  struct addrinfo hints, *list = NULL; memset(&hints, 0, sizeof(hints));
  hints.ai_socktype = SOCK_STREAM; hints.ai_family = AF_UNSPEC;
  if (getaddrinfo(host, service, &hints, &list) != 0) return NULL;
  int fd = -1;
  for (struct addrinfo *it = list; it; it = it->ai_next) {
    fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
    if (fd >= 0 && connect(fd, it->ai_addr, it->ai_addrlen) == 0) break;
    if (fd >= 0) { close(fd); fd = -1; }
  }
  freeaddrinfo(list); if (fd < 0) return NULL;
  mlu_remote_context *ctx = (mlu_remote_context *)calloc(1, sizeof(*ctx));
  if (!ctx) { close(fd); return NULL; }
  ctx->fd = fd; pthread_mutex_init(&ctx->lock, NULL);
  unsigned char hello[12]; put32(hello, MLU_REMOTE_VERSION); put32(hello + 4, 0); put32(hello + 8, requested_device);
  unsigned char *extra = NULL; uint32_t n = 0;
  int st = rpc(ctx, MLU_OP_HELLO, hello, sizeof(hello), &extra, &n);
  if (st || n < 16) { free(extra); mlu_remote_disconnect(ctx); return NULL; }
  free(extra); return ctx;
}
mlu_remote_context *mlu_remote_connect(const char *host, uint16_t port) {
  uint32_t requested = UINT32_MAX;
  const char *env = getenv("MLU_REMOTE_DEVICE");
  if (env && *env) { char *end = NULL; unsigned long value = strtoul(env, &end, 10); if (end && *end == 0 && value <= UINT32_MAX) requested = (uint32_t)value; }
  return connect_impl(host, port, requested);
}
mlu_remote_context *mlu_remote_connect_device(const char *host, uint16_t port, uint32_t device) {
  return connect_impl(host, port, device);
}
void mlu_remote_disconnect(mlu_remote_context *ctx) { if (!ctx) return; if (ctx->fd >= 0) close(ctx->fd); pthread_mutex_destroy(&ctx->lock); free(ctx); }
const mlu_remote_error *mlu_remote_last_error(const mlu_remote_context *ctx) { return ctx ? &ctx->error : NULL; }

int mlu_remote_device_count(mlu_remote_context *ctx, uint32_t *count) { unsigned char p[4]; put32(p, 0xffffffffu); unsigned char *e = NULL; uint32_t n = 0; int st = rpc(ctx, MLU_OP_DEVICE_QUERY, p, 4, &e, &n); if (!st && n >= 16) *count = get32(e); free(e); return st; }
int mlu_remote_device_query(mlu_remote_context *ctx, uint32_t device, char *name, size_t cap, uint64_t *memory) { unsigned char p[4]; put32(p, device); unsigned char *e = NULL; uint32_t n = 0; int st = rpc(ctx, MLU_OP_DEVICE_QUERY, p, 4, &e, &n); if (!st && n >= 16) { uint32_t nl = get32(e + 12); if (memory) *memory = get64(e + 4); if (name && cap) { size_t c = nl < cap - 1 ? nl : cap - 1; if (16 + c <= n) { memcpy(name, e + 16, c); name[c] = 0; } } } free(e); return st; }
int mlu_remote_select_device(mlu_remote_context *ctx, uint32_t device) { unsigned char p[4]; put32(p, device); return rpc(ctx, MLU_OP_DEVICE_SELECT, p, 4, NULL, &(uint32_t){0}); }

int mlu_remote_alloc(mlu_remote_context *ctx, uint64_t bytes, uint64_t *allocation) { unsigned char p[8]; put64(p, bytes); unsigned char *e = NULL; uint32_t n = 0; int st = rpc(ctx, MLU_OP_ALLOC, p, 8, &e, &n); if (!st && n >= 8) *allocation = get64(e); free(e); return st; }
int mlu_remote_free(mlu_remote_context *ctx, uint64_t allocation) { unsigned char p[8]; put64(p, allocation); return rpc(ctx, MLU_OP_FREE, p, 8, NULL, &(uint32_t){0}); }

static int memcpy_call(mlu_remote_context *ctx, uint32_t dir, uint64_t dst, uint64_t dstoff, uint64_t src, uint64_t srcoff, const void *in, void *outbuf, uint64_t bytes, uint64_t queue, uint64_t *transfer) {
  if (bytes > MLU_REMOTE_MAX_PAYLOAD - 56 || (dir == MLU_MEMCPY_H2D && !in) || (dir == MLU_MEMCPY_D2H && !outbuf)) return MLU_REMOTE_BAD_LENGTH; uint32_t n = 56 + (dir == MLU_MEMCPY_H2D ? (uint32_t)bytes : 0); unsigned char *p = (unsigned char *)calloc(1, n); if (!p) return CN_ERROR_SYSTEM_OUT_OF_MEMORY; put32(p, dir); put64(p + 4, queue); put64(p + 12, dst); put64(p + 20, dstoff); put64(p + 28, src); put64(p + 36, srcoff); put64(p + 44, bytes); put32(p + 52, dir == MLU_MEMCPY_H2D ? (uint32_t)bytes : 0); if (dir == MLU_MEMCPY_H2D) memcpy(p + 56, in, (size_t)bytes); unsigned char *e = NULL; uint32_t en = 0; int st = rpc(ctx, MLU_OP_MEMCPY, p, n, &e, &en); free(p); if (!st && en >= 8 && transfer) *transfer = get64(e); if (!st && dir == MLU_MEMCPY_D2H && en >= 16 + bytes && get64(e + 8) == bytes) memcpy(outbuf, e + 16, (size_t)bytes); free(e); return st;
}
int mlu_remote_memcpy_h2d(mlu_remote_context *ctx, uint64_t dst, uint64_t off, const void *src, uint64_t bytes, uint64_t q, uint64_t *t) { return memcpy_call(ctx, MLU_MEMCPY_H2D, dst, off, 0, 0, src, NULL, bytes, q, t); }
int mlu_remote_memcpy_d2h(mlu_remote_context *ctx, void *dst, uint64_t src, uint64_t off, uint64_t bytes, uint64_t q, uint64_t *t) { return memcpy_call(ctx, MLU_MEMCPY_D2H, 0, 0, src, off, NULL, dst, bytes, q, t); }
int mlu_remote_memcpy_d2d(mlu_remote_context *ctx, uint64_t dst, uint64_t doff, uint64_t src, uint64_t soff, uint64_t bytes, uint64_t q, uint64_t *t) { return memcpy_call(ctx, MLU_MEMCPY_D2D, dst, doff, src, soff, NULL, NULL, bytes, q, t); }

static int id_call(mlu_remote_context *ctx, uint16_t op, uint64_t id) { unsigned char p[8]; put64(p, id); return rpc(ctx, op, p, 8, NULL, &(uint32_t){0}); }
int mlu_remote_queue_create(mlu_remote_context *ctx, uint64_t *q) { unsigned char *e = NULL; uint32_t n = 0; int st = rpc(ctx, MLU_OP_QUEUE_CREATE, NULL, 0, &e, &n); if (!st && n >= 8) *q = get64(e); free(e); return st; }
int mlu_remote_queue_destroy(mlu_remote_context *ctx, uint64_t q) { return id_call(ctx, MLU_OP_QUEUE_DESTROY, q); }
int mlu_remote_queue_sync(mlu_remote_context *ctx, uint64_t q) { return id_call(ctx, MLU_OP_QUEUE_SYNC, q); }
int mlu_remote_queue_query(mlu_remote_context *ctx, uint64_t q) { return id_call(ctx, MLU_OP_QUEUE_QUERY, q); }
int mlu_remote_notifier_create(mlu_remote_context *ctx, uint64_t *n) { unsigned char *e = NULL; uint32_t len = 0; int st = rpc(ctx, MLU_OP_NOTIFIER_CREATE, NULL, 0, &e, &len); if (!st && len >= 8) *n = get64(e); free(e); return st; }
int mlu_remote_notifier_destroy(mlu_remote_context *ctx, uint64_t n) { return id_call(ctx, MLU_OP_NOTIFIER_DESTROY, n); }
int mlu_remote_notifier_record(mlu_remote_context *ctx, uint64_t n, uint64_t q) { unsigned char p[16]; put64(p, n); put64(p + 8, q); return rpc(ctx, MLU_OP_NOTIFIER_RECORD, p, 16, NULL, &(uint32_t){0}); }
int mlu_remote_notifier_query(mlu_remote_context *ctx, uint64_t n) { return id_call(ctx, MLU_OP_NOTIFIER_QUERY, n); }
int mlu_remote_notifier_sync(mlu_remote_context *ctx, uint64_t n) { return id_call(ctx, MLU_OP_NOTIFIER_SYNC, n); }
int mlu_remote_session_reset(mlu_remote_context *ctx) { return rpc(ctx, MLU_OP_SESSION_RESET, NULL, 0, NULL, &(uint32_t){0}); }
int mlu_remote_module_load_preset(mlu_remote_context *ctx, const char *name, uint64_t *module, uint64_t *kernel) { if (!name) return MLU_REMOTE_BAD_LENGTH; size_t sl = strlen(name); if (sl > 128) return MLU_REMOTE_BAD_LENGTH; uint32_t l = (uint32_t)sl; unsigned char *p = (unsigned char *)malloc(4 + l); if (!p) return CN_ERROR_SYSTEM_OUT_OF_MEMORY; put32(p, l); memcpy(p + 4, name, l); unsigned char *e = NULL; uint32_t n = 0; int st = rpc(ctx, MLU_OP_MODULE_LOAD_PRESET, p, 4 + l, &e, &n); free(p); if (!st && n >= 16) { if (module) *module = get64(e); if (kernel) *kernel = get64(e + 8); } free(e); return st; }
int mlu_remote_module_unload_preset(mlu_remote_context *ctx, uint64_t module) { unsigned char p[8]; put64(p, module); return rpc(ctx, MLU_OP_MODULE_UNLOAD_PRESET, p, sizeof(p), NULL, &(uint32_t){0}); }
int mlu_remote_kernel_launch_vector_add(mlu_remote_context *ctx, uint64_t kernel, uint64_t q, uint64_t dst, uint64_t a, uint64_t b, uint32_t elements, uint32_t grid_x) { unsigned char p[56] = {0}; put64(p, kernel); put64(p + 8, q); put64(p + 16, dst); put64(p + 24, a); put64(p + 32, b); put32(p + 40, elements); put32(p + 44, grid_x); return rpc(ctx, MLU_OP_KERNEL_LAUNCH, p, 56, NULL, &(uint32_t){0}); }

static size_t operator_tensor_wire_size(const mlu_remote_operator_tensor *t) {
  if (!t || !t->rank || t->rank > MLU_REMOTE_MAX_RANK) return 0;
  return 32u + 16u * (size_t)t->rank;
}
static int operator_put_tensor(unsigned char **cursor, size_t *remaining,
                               const mlu_remote_operator_tensor *t) {
  size_t need = operator_tensor_wire_size(t);
  if (!need || !cursor || !*cursor || !remaining || *remaining < need) return MLU_REMOTE_BAD_LENGTH;
  unsigned char *p = *cursor;
  put64(p, t->allocation); put64(p + 8, t->offset); put32(p + 16, t->rank);
  put32(p + 20, t->layout); put32(p + 24, t->datatype); put32(p + 28, 0); p += 32;
  for (uint32_t i = 0; i < t->rank; ++i, p += 8) put64(p, t->shape[i]);
  for (uint32_t i = 0; i < t->rank; ++i, p += 8) put64(p, t->stride[i]);
  *cursor += need; *remaining -= need; return 0;
}

int mlu_remote_operator_create(mlu_remote_context *ctx, uint32_t operator_id,
                               uint64_t *module, uint64_t *kernel) {
  unsigned char p[4]; put32(p, operator_id); unsigned char *e = NULL; uint32_t n = 0;
  int st = rpc(ctx, MLU_OP_OPERATOR_CREATE, p, sizeof(p), &e, &n);
  if (!st && n >= 16) { if (module) *module = get64(e); if (kernel) *kernel = get64(e + 8); }
  free(e); return st;
}
int mlu_remote_operator_destroy(mlu_remote_context *ctx, uint32_t operator_id) {
  unsigned char p[4]; put32(p, operator_id); return rpc(ctx, MLU_OP_OPERATOR_DESTROY, p, sizeof(p), NULL, &(uint32_t){0});
}
int mlu_remote_operator_launch(mlu_remote_context *ctx, uint32_t operator_id,
                               uint64_t kernel, uint64_t queue,
                               const mlu_remote_operator_tensor *inputs, uint32_t input_count,
                               const mlu_remote_operator_tensor *outputs, uint32_t output_count,
                               const void *scalar_bytes, size_t scalar_size,
                               const mlu_remote_operator_grid *grid,
                               uint64_t *transfer) {
  if (input_count > 16 || output_count > 16 || input_count + output_count > 16 || scalar_size > MLU_REMOTE_MAX_PAYLOAD - 64)
    return MLU_REMOTE_BAD_LENGTH;
  if ((input_count && !inputs) || (output_count && !outputs) || (scalar_size && !scalar_bytes))
    return MLU_REMOTE_BAD_LENGTH;
  size_t size = 64 + scalar_size;
  for (uint32_t i = 0; i < input_count; ++i) { size_t x = operator_tensor_wire_size(&inputs[i]); if (!x || size > MLU_REMOTE_MAX_PAYLOAD - x) return MLU_REMOTE_BAD_LENGTH; size += x; }
  for (uint32_t i = 0; i < output_count; ++i) { size_t x = operator_tensor_wire_size(&outputs[i]); if (!x || size > MLU_REMOTE_MAX_PAYLOAD - x) return MLU_REMOTE_BAD_LENGTH; size += x; }
  unsigned char *p = calloc(1, size); if (!p) return CN_ERROR_SYSTEM_OUT_OF_MEMORY;
  put32(p, operator_id); put32(p + 4, 0); put64(p + 8, kernel); put64(p + 16, queue);
  put32(p + 24, input_count); put32(p + 28, output_count); put32(p + 32, (uint32_t)scalar_size); put32(p + 36, 0);
  put32(p + 40, grid && grid->grid_x ? grid->grid_x : 1); put32(p + 44, grid ? grid->grid_y : 1); put32(p + 48, grid ? grid->grid_z : 1);
  put32(p + 52, grid ? grid->block_x : 1); put32(p + 56, grid ? grid->block_y : 1); put32(p + 60, grid ? grid->block_z : 1);
  unsigned char *cursor = p + 64; size_t remaining = size - 64;
  for (uint32_t i = 0; i < input_count; ++i) { int st = operator_put_tensor(&cursor, &remaining, &inputs[i]); if (st) { free(p); return st; } }
  for (uint32_t i = 0; i < output_count; ++i) { int st = operator_put_tensor(&cursor, &remaining, &outputs[i]); if (st) { free(p); return st; } }
  if (scalar_size) memcpy(cursor, scalar_bytes, scalar_size);
  unsigned char *e = NULL; uint32_t n = 0; int st = rpc(ctx, MLU_OP_OPERATOR_LAUNCH, p, (uint32_t)size, &e, &n);
  free(p); if (!st && n >= 8 && transfer) *transfer = get64(e); free(e); return st;
}
int mlu_remote_transfer_query(mlu_remote_context *ctx, uint64_t transfer) { unsigned char p[8]; put64(p, transfer); return rpc(ctx, MLU_OP_TRANSFER_QUERY, p, 8, NULL, &(uint32_t){0}); }
int mlu_remote_transfer_sync(mlu_remote_context *ctx, uint64_t transfer) { unsigned char p[8]; put64(p, transfer); return rpc(ctx, MLU_OP_TRANSFER_SYNC, p, 8, NULL, &(uint32_t){0}); }
int mlu_remote_operator_query(mlu_remote_context *ctx, uint64_t transfer) { return mlu_remote_transfer_query(ctx, transfer); }
int mlu_remote_operator_sync(mlu_remote_context *ctx, uint64_t transfer) { return mlu_remote_transfer_sync(ctx, transfer); }

static mlu_remote_operator_tensor make_vector_tensor(uint64_t allocation, uint32_t elements) {
  mlu_remote_operator_tensor t; memset(&t, 0, sizeof(t)); t.allocation = allocation; t.rank = 1; t.layout = MLU_REMOTE_LAYOUT_ROW_MAJOR; t.datatype = MLU_REMOTE_DTYPE_F32; t.shape[0] = elements; t.stride[0] = 1; return t;
}
static mlu_remote_operator_grid make_grid(uint32_t grid_x) { mlu_remote_operator_grid g = {grid_x, 1, 1, 1, 1, 1}; return g; }
int mlu_remote_operator_launch_vector_add(mlu_remote_context *ctx, uint64_t kernel, uint64_t queue, uint64_t dst, uint64_t a, uint64_t b, uint32_t elements, uint32_t grid_x, uint64_t *transfer) {
  mlu_remote_operator_tensor in[2] = {make_vector_tensor(a, elements), make_vector_tensor(b, elements)}, out = make_vector_tensor(dst, elements); mlu_remote_operator_grid g = make_grid(grid_x);
  return mlu_remote_operator_launch(ctx, MLU_REMOTE_OPERATOR_VECTOR_ADD, kernel, queue, in, 2, &out, 1, NULL, 0, &g, transfer);
}
int mlu_remote_operator_launch_scalar_add(mlu_remote_context *ctx, uint64_t kernel, uint64_t queue, uint64_t dst, uint64_t src, uint32_t elements, float scalar, uint32_t grid_x, uint64_t *transfer) {
  mlu_remote_operator_tensor in = make_vector_tensor(src, elements), out = make_vector_tensor(dst, elements); mlu_remote_operator_grid g = make_grid(grid_x); uint32_t bits; memcpy(&bits, &scalar, 4); bits = htonl(bits);
  return mlu_remote_operator_launch(ctx, MLU_REMOTE_OPERATOR_SCALAR_ADD, kernel, queue, &in, 1, &out, 1, &bits, 4, &g, transfer);
}
int mlu_remote_operator_launch_scalar_mul(mlu_remote_context *ctx, uint64_t kernel, uint64_t queue, uint64_t dst, uint64_t src, uint32_t elements, float scalar, uint32_t grid_x, uint64_t *transfer) {
  mlu_remote_operator_tensor in = make_vector_tensor(src, elements), out = make_vector_tensor(dst, elements); mlu_remote_operator_grid g = make_grid(grid_x); uint32_t bits; memcpy(&bits, &scalar, 4); bits = htonl(bits);
  return mlu_remote_operator_launch(ctx, MLU_REMOTE_OPERATOR_SCALAR_MUL, kernel, queue, &in, 1, &out, 1, &bits, 4, &g, transfer);
}
int mlu_remote_operator_launch_relu(mlu_remote_context *ctx, uint64_t kernel, uint64_t queue, uint64_t dst, uint64_t src, uint32_t elements, uint32_t grid_x, uint64_t *transfer) {
  mlu_remote_operator_tensor in = make_vector_tensor(src, elements), out = make_vector_tensor(dst, elements); mlu_remote_operator_grid g = make_grid(grid_x);
  return mlu_remote_operator_launch(ctx, MLU_REMOTE_OPERATOR_RELU, kernel, queue, &in, 1, &out, 1, NULL, 0, &g, transfer);
}
int mlu_remote_operator_launch_tensor_add_strided(mlu_remote_context *ctx, uint64_t kernel, uint64_t queue, uint64_t dst, uint64_t a, uint64_t b, uint32_t rows, uint32_t cols, uint32_t dst_rs, uint32_t dst_cs, uint32_t a_rs, uint32_t a_cs, uint32_t b_rs, uint32_t b_cs, uint32_t grid_x, uint64_t *transfer) {
  mlu_remote_operator_tensor in[2] = {make_vector_tensor(a, 1), make_vector_tensor(b, 1)}, out = make_vector_tensor(dst, 1); memset(in, 0, sizeof(in)); memset(&out, 0, sizeof(out));
  for (int i = 0; i < 2; ++i) { in[i].allocation = i ? b : a; in[i].rank = 2; in[i].layout = MLU_REMOTE_LAYOUT_ROW_MAJOR; in[i].datatype = MLU_REMOTE_DTYPE_F32; in[i].shape[0] = rows; in[i].shape[1] = cols; in[i].stride[0] = i ? b_rs : a_rs; in[i].stride[1] = i ? b_cs : a_cs; }
  out.allocation = dst; out.rank = 2; out.layout = MLU_REMOTE_LAYOUT_ROW_MAJOR; out.datatype = MLU_REMOTE_DTYPE_F32; out.shape[0] = rows; out.shape[1] = cols; out.stride[0] = dst_rs; out.stride[1] = dst_cs; mlu_remote_operator_grid g = make_grid(grid_x);
  return mlu_remote_operator_launch(ctx, MLU_REMOTE_OPERATOR_TENSOR_ADD_STRIDED, kernel, queue, in, 2, &out, 1, NULL, 0, &g, transfer);
}
int mlu_remote_operator_launch_tensor_mul_strided(mlu_remote_context *ctx, uint64_t kernel, uint64_t queue, uint64_t dst, uint64_t a, uint64_t b, uint32_t rows, uint32_t cols, uint32_t dst_rs, uint32_t dst_cs, uint32_t a_rs, uint32_t a_cs, uint32_t b_rs, uint32_t b_cs, uint32_t grid_x, uint64_t *transfer) {
  mlu_remote_operator_tensor in[2] = {make_vector_tensor(a, 1), make_vector_tensor(b, 1)}, out = make_vector_tensor(dst, 1); memset(in, 0, sizeof(in)); memset(&out, 0, sizeof(out));
  for (int i = 0; i < 2; ++i) { in[i].allocation = i ? b : a; in[i].rank = 2; in[i].layout = MLU_REMOTE_LAYOUT_ROW_MAJOR; in[i].datatype = MLU_REMOTE_DTYPE_F32; in[i].shape[0] = rows; in[i].shape[1] = cols; in[i].stride[0] = i ? b_rs : a_rs; in[i].stride[1] = i ? b_cs : a_cs; }
  out.allocation = dst; out.rank = 2; out.layout = MLU_REMOTE_LAYOUT_ROW_MAJOR; out.datatype = MLU_REMOTE_DTYPE_F32; out.shape[0] = rows; out.shape[1] = cols; out.stride[0] = dst_rs; out.stride[1] = dst_cs; mlu_remote_operator_grid g = make_grid(grid_x);
  return mlu_remote_operator_launch(ctx, MLU_REMOTE_OPERATOR_TENSOR_MUL_STRIDED, kernel, queue, in, 2, &out, 1, NULL, 0, &g, transfer);
}

int mlu_remote_cnnl_request(mlu_remote_context *ctx, uint64_t queue, uint64_t dst,
                            uint64_t a, uint64_t b, uint64_t elements) {
  unsigned char p[40]; put64(p, queue); put64(p + 8, dst); put64(p + 16, a);
  put64(p + 24, b); put64(p + 32, elements);
  return rpc(ctx, MLU_OP_CNNL_OPERATION, p, sizeof(p), NULL, &(uint32_t){0});
}
