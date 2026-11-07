#include "remote_cnrt.h"
#include "remote_operator.h"
#include "remote_io.h"
#include "host_memory.h"
/* Wire value captured from CNDrv 2.5.2; the transport needs no SDK headers. */
#define CN_ERROR_SYSTEM_OUT_OF_MEMORY 100010

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

typedef struct pending_writeback {
  struct pending_writeback *next;
  uint64_t transfer, bytes, queue;
  void *dst;
  mlu_host_allocation *lease;
  int release_requested, published;
} pending_writeback;
struct mlu_remote_context {
  int fd;
  uint64_t next_request;
  pthread_mutex_t lock;
  uint32_t capabilities, kernel_abi;
  uint32_t rpc_timeout_ms;
  mlu_remote_error error;
  pending_writeback *pending, **pending_tail;
};

static uint64_t ntohll_u(uint64_t x) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  return ((uint64_t)ntohl((uint32_t)x) << 32) | ntohl((uint32_t)(x >> 32));
#else
  return x;
#endif
}
static uint64_t htonll_u(uint64_t x) { return ntohll_u(x); }
static void put32(unsigned char *p, uint32_t x) { x = htonl(x); memcpy(p, &x, 4); }
static void put64(unsigned char *p, uint64_t x) { x = htonll_u(x); memcpy(p, &x, 8); }
static uint32_t get32(const unsigned char *p) { uint32_t x; memcpy(&x, p, 4); return ntohl(x); }
static uint64_t get64(const unsigned char *p) { uint64_t x; memcpy(&x, p, 8); return ntohll_u(x); }

static void set_local_error(mlu_remote_context *ctx, uint32_t code, const char *name, const char *message) {
  ctx->error.code = code; snprintf(ctx->error.name, sizeof(ctx->error.name), "%s", name ? name : "REMOTE_CLIENT_ERROR"); snprintf(ctx->error.message, sizeof(ctx->error.message), "%s", message ? message : "remote request failed");
}

/* A failed exchange leaves execution/completion uncertain. Never reuse or
 * transparently replay that session, even if the peer sends more bytes. */
static int break_connection(mlu_remote_context *ctx, uint32_t code,
                            const char *name, const char *message) {
  set_local_error(ctx, code, name, message);
  if (ctx->fd >= 0) { close(ctx->fd); ctx->fd = -1; }
  return (int)code;
}

static int exchange_failed(mlu_remote_context *ctx, int writing, const char *message) {
  if (errno == ETIMEDOUT)
    return break_connection(ctx, MLU_REMOTE_TIMEOUT, "REMOTE_TIMEOUT",
                            "RPC deadline expired; execution may have occurred; session closed");
  return break_connection(ctx, MLU_REMOTE_PROTOCOL_ERROR,
                          writing ? "REMOTE_SEND_FAILED" : "REMOTE_RECV_FAILED", message);
}

/* Validate before any wrapper can publish handles or copy D2H bytes. Exact
 * lengths are part of MLUR v1; network success alone is not API success. */
static int valid_success(uint16_t op, const unsigned char *request,
                         uint32_t request_len, const unsigned char *p, uint32_t n) {
  switch (op) {
#include "typed_success.inc"
    case MLU_OP_HELLO:
      return n == 16 && get32(p) == MLU_REMOTE_VERSION;
    case MLU_OP_DEVICE_QUERY:
      return n >= 16 && get32(p + 12) == n - 16;
    case MLU_OP_ALLOC: case MLU_OP_QUEUE_CREATE: case MLU_OP_NOTIFIER_CREATE:
    case MLU_OP_OPERATOR_LAUNCH: case MLU_OP_KERNEL_LAUNCH_GENERIC:
    case MLU_OP_MODULE_GET_KERNEL: case MLU_OP_MODULE_LOAD_BINARY:
      return n == 8;
    case MLU_OP_MODULE_LOAD_PRESET: case MLU_OP_MODULE_LOAD_IMAGE:
    case MLU_OP_OPERATOR_CREATE:
      return n == 16;
    case MLU_OP_MODULE_QUERY_FATBINARY_MEMORY:
      return n == 8;
    case MLU_OP_MEMCPY:
      if (request_len < 56) return 0;
      if (get32(request) != MLU_MEMCPY_D2H) return n == 8;
      return n >= 16 && get64(p + 8) == get64(request + 44) &&
             get64(p + 8) == (uint64_t)n - 16;
    default:
      return n == 0;
  }
}

/* Caller owns *extra and must free it. The returned extra excludes the common
 * error name/message prefix. */
static int rpc_locked(mlu_remote_context *ctx, uint16_t opcode, const void *payload, uint32_t payload_len,
                      unsigned char **extra, uint32_t *extra_len) {
  if (extra) *extra = NULL;
  if (extra_len) *extra_len = 0;
  if (ctx->fd < 0) {
    set_local_error(ctx, MLU_REMOTE_PROTOCOL_ERROR, "REMOTE_CONTEXT_POISONED", "previous exchange failed; create a new session, do not replay requests");
    return (int)MLU_REMOTE_PROTOCOL_ERROR;
  }
  struct mlu_remote_wire_header h; memset(&h, 0, sizeof(h));
  uint64_t req = ++ctx->next_request; h.magic = htonl(MLU_REMOTE_MAGIC); h.version = htons(MLU_REMOTE_VERSION); h.opcode = htons(opcode); h.request_id = htonll_u(req); h.payload_len = htonl(payload_len); h.status = 0;
  int64_t deadline = mlu_deadline_after(ctx->rpc_timeout_ms);
  if (mlu_io_full(ctx->fd, &h, sizeof(h), 1, deadline) != 1 ||
      (payload_len && mlu_io_full(ctx->fd, (void *)payload, payload_len, 1, deadline) != 1))
    return exchange_failed(ctx, 1, "request send incomplete");
  if (mlu_io_full(ctx->fd, &h, sizeof(h), 0, deadline) != 1)
    return exchange_failed(ctx, 0, "response header incomplete");
  uint32_t n = ntohl(h.payload_len), status = ntohl(h.status); if (ntohl(h.magic) != MLU_REMOTE_MAGIC || ntohs(h.version) != MLU_REMOTE_VERSION || ntohs(h.opcode) != opcode || ntohll_u(h.request_id) != req || n > MLU_REMOTE_MAX_PAYLOAD) return break_connection(ctx, MLU_REMOTE_PROTOCOL_ERROR, "REMOTE_BAD_RESPONSE", "invalid response frame");
  unsigned char *p = n ? (unsigned char *)malloc(n) : NULL; if (n && !p) return break_connection(ctx, CN_ERROR_SYSTEM_OUT_OF_MEMORY, "HOST_OUT_OF_MEMORY", "response allocation failed");
  if (n && mlu_io_full(ctx->fd, p, n, 0, deadline) != 1) {
    int saved = errno; free(p); errno = saved;
    return exchange_failed(ctx, 0, "response payload incomplete");
  }
  if (n < 8) { free(p); return break_connection(ctx, MLU_REMOTE_BAD_LENGTH, "REMOTE_BAD_LENGTH", "response lacks error prefix"); }
  uint32_t nl = get32(p), ml = get32(p + 4); if (nl > n - 8 || ml > n - 8 - nl) { free(p); return break_connection(ctx, MLU_REMOTE_BAD_LENGTH, "REMOTE_BAD_LENGTH", "invalid response error strings"); }
  uint32_t rest = n - 8 - nl - ml;
  if (!status && !valid_success(opcode, payload, payload_len, p + 8 + nl + ml, rest)) {
    free(p); return break_connection(ctx, MLU_REMOTE_BAD_LENGTH, "REMOTE_BAD_LENGTH", "success response has an invalid operation result");
  }
  if (status) { size_t l = nl < sizeof(ctx->error.name) - 1 ? nl : sizeof(ctx->error.name) - 1; memcpy(ctx->error.name, p + 8, l); ctx->error.name[l] = 0; l = ml < sizeof(ctx->error.message) - 1 ? ml : sizeof(ctx->error.message) - 1; memcpy(ctx->error.message, p + 8 + nl, l); ctx->error.message[l] = 0; ctx->error.code = status; }
  else { ctx->error.code = 0; ctx->error.name[0] = 0; ctx->error.message[0] = 0; }
  if (extra) { *extra_len = rest; *extra = rest ? (unsigned char *)malloc(rest) : NULL; if (rest && !*extra) { free(p); return break_connection(ctx, CN_ERROR_SYSTEM_OUT_OF_MEMORY, "HOST_OUT_OF_MEMORY", "response allocation failed"); } if (rest) memcpy(*extra, p + 8 + nl + ml, rest); } free(p); return (int)status;
}

static int complete_writebacks(mlu_remote_context *ctx) {
  pending_writeback **link=&ctx->pending;
  while(*link) {
    pending_writeback *w=*link;
    /* A query can race completion: never publish a later same-queue or
     * overlapping write while an earlier result is still pending. Independent
     * queues and destinations remain nonblocking. */
    pending_writeback *prior=ctx->pending;
    for(;prior!=w;prior=prior->next) {
      uintptr_t a=(uintptr_t)prior->dst,b=(uintptr_t)w->dst;
      if(prior->queue==w->queue || (a<=b ? b-a<prior->bytes : a-b<w->bytes)) break;
    }
    if(prior!=w) { link=&w->next; continue; }
    unsigned char request[16], *data=NULL; uint32_t n=0;
    put64(request,w->transfer);
    int st=0;
    if(!w->published) {
      st=rpc_locked(ctx,MLU_OP_TRANSFER_QUERY,request,8,NULL,&n);
      if(st==1) { link=&w->next; continue; } /* CN_ERROR_NOT_READY */
      if(st) return st;
      put64(request+8,w->bytes);
      st=rpc_locked(ctx,MLU_OP_API_TRANSFER_FETCH,request,16,&data,&n);
      if(st) { free(data); return st; }
      memcpy(w->dst,data,(size_t)w->bytes); free(data);
      w->published=1;
    }
    if(w->release_requested) st=rpc_locked(ctx,MLU_OP_TRANSFER_RELEASE,request,8,NULL,&n);
    if(st) return st;
    *link=w->next; mlu_host_release(w->lease); free(w);
  }
  ctx->pending_tail=link;
  return 0;
}
static int completion_frontier(uint16_t op) {
  switch(op) {
    case MLU_OP_QUEUE_SYNC: case MLU_OP_QUEUE_QUERY:
    case MLU_OP_NOTIFIER_SYNC: case MLU_OP_NOTIFIER_QUERY:
    case MLU_OP_TRANSFER_SYNC: case MLU_OP_TRANSFER_QUERY:
    case MLU_OP_CONTEXT_SYNC: case MLU_OP_FREE:
    /* Publish older D2H results before memcpy_call copies a newer synchronous
     * D2H response into the application's destination. Queries never wait. */
    case MLU_OP_MEMCPY:
    case MLU_OP_MODULE_UNLOAD_PRESET: return 1;
    default: return 0;
  }
}
static int rpc(mlu_remote_context *ctx, uint16_t opcode, const void *payload, uint32_t payload_len, unsigned char **extra, uint32_t *extra_len) {
  pthread_mutex_lock(&ctx->lock);
  int r=0;
  /* Resolve pending destinations before the server invalidates transfer IDs.
   * Failure preserves them for retry or disconnect, without replaying work. */
  if(ctx->pending && (opcode==MLU_OP_SESSION_RESET || opcode==MLU_OP_SESSION_CLOSE)) {
    r=rpc_locked(ctx,MLU_OP_CONTEXT_SYNC,NULL,0,NULL,&(uint32_t){0});
    if(!r) r=complete_writebacks(ctx);
  }
  if(!r && opcode==MLU_OP_TRANSFER_RELEASE && payload_len==8) {
    for(pending_writeback *w=ctx->pending;w;w=w->next) if(w->transfer==get64(payload)) {
      r=w->release_requested ? (int)MLU_REMOTE_BAD_OBJECT : 0;
      w->release_requested=1;
      if(r) set_local_error(ctx,(uint32_t)r,"REMOTE_BAD_OBJECT","transfer release already requested");
      else memset(&ctx->error,0,sizeof(ctx->error));
      if(extra) *extra=NULL;
      if(extra_len) *extra_len=0;
      pthread_mutex_unlock(&ctx->lock); return r;
    }
  }
  if(!r) r=rpc_locked(ctx,opcode,payload,payload_len,extra,extra_len);
  if(!r && ctx->pending && completion_frontier(opcode)) {
    mlu_remote_error saved=ctx->error;
    r=complete_writebacks(ctx);
    if(!r) ctx->error=saved;
  }
  pthread_mutex_unlock(&ctx->lock); return r;
}

static _Thread_local mlu_remote_error connect_error;
const mlu_remote_error *mlu_remote_last_connect_error(void) { return &connect_error; }
static mlu_remote_context *connect_failure(uint32_t code, const char *name, const char *message, int error) {
  connect_error.code = code;
  snprintf(connect_error.name, sizeof(connect_error.name), "%s", name);
  snprintf(connect_error.message, sizeof(connect_error.message), "%s", message);
  errno = error; return NULL;
}

static mlu_remote_context *connect_impl(const char *host, uint16_t port, uint32_t requested_device, int driver, uint32_t flags) {
  memset(&connect_error, 0, sizeof(connect_error));
  uint32_t connect_ms, rpc_ms;
  if (mlu_timeout_env("MLU_REMOTE_CONNECT_TIMEOUT_MS", 5000, &connect_ms) ||
      mlu_timeout_env("MLU_REMOTE_RPC_TIMEOUT_MS", 120000, &rpc_ms))
    return connect_failure(MLU_REMOTE_BAD_LENGTH, "REMOTE_BAD_TIMEOUT",
                           "timeout must be an integer from 1 to 3600000 milliseconds", EINVAL);
  if (!host) host = "127.0.0.1";
  char service[16]; snprintf(service, sizeof(service), "%u", port);
  struct addrinfo hints, *list = NULL; memset(&hints, 0, sizeof(hints));
  hints.ai_socktype = SOCK_STREAM; hints.ai_family = AF_UNSPEC;
  int gai = getaddrinfo(host, service, &hints, &list);
  if (gai) return connect_failure(MLU_REMOTE_PROTOCOL_ERROR, "REMOTE_RESOLVE_FAILED", gai_strerror(gai), EHOSTUNREACH);
  /* DNS is outside this budget. All resolved addresses share one deadline. */
  int64_t deadline = mlu_deadline_after(connect_ms);
  int fd = -1, saved = ECONNREFUSED;
  for (struct addrinfo *it = list; it; it = it->ai_next) {
    if (mlu_deadline_remaining(deadline) < 0) { saved = errno; break; }
    fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
    if (fd < 0) { saved = errno; continue; }
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0) {
      int r = connect(fd, it->ai_addr, it->ai_addrlen);
      if (!r) break;
      if (errno == EINPROGRESS || errno == EINTR) {
        if (!mlu_wait_socket(fd, POLLOUT, deadline)) {
          int error = 0; socklen_t size = sizeof(error);
          if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size) == 0) {
            if (!error) break;
            errno = error;
          }
        }
      }
    }
    saved = errno; close(fd); fd = -1;
    if (saved == ETIMEDOUT) break;
  }
  freeaddrinfo(list);
  if (fd < 0) return connect_failure(saved == ETIMEDOUT ? MLU_REMOTE_TIMEOUT : MLU_REMOTE_PROTOCOL_ERROR,
                                    saved == ETIMEDOUT ? "REMOTE_CONNECT_TIMEOUT" : "REMOTE_CONNECT_FAILED", strerror(saved), saved);
  int no_delay = 1; setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &no_delay, sizeof(no_delay));
  mlu_remote_context *ctx = (mlu_remote_context *)calloc(1, sizeof(*ctx));
  if (!ctx) { close(fd); return connect_failure(CN_ERROR_SYSTEM_OUT_OF_MEMORY, "HOST_OUT_OF_MEMORY", "context allocation failed", ENOMEM); }
  ctx->fd = fd; ctx->rpc_timeout_ms = rpc_ms; pthread_mutex_init(&ctx->lock, NULL);
  unsigned char hello[16]; put32(hello, MLU_REMOTE_VERSION); put32(hello + 4, 0); put32(hello + 8, requested_device); put32(hello + 12, flags);
  unsigned char *extra = NULL; uint32_t n = 0;
  int st = rpc(ctx, MLU_OP_HELLO, hello, driver ? 16 : 12, &extra, &n);
  if (st || n < 16) {
    connect_error = ctx->error; free(extra); mlu_remote_disconnect(ctx);
    errno = (uint32_t)st == MLU_REMOTE_TIMEOUT ? ETIMEDOUT : EPROTO; return NULL;
  }
  ctx->capabilities = get32(extra + 8); ctx->kernel_abi = get32(extra + 12);
  free(extra); return ctx;
}
mlu_remote_context *mlu_remote_connect(const char *host, uint16_t port) {
  uint32_t requested = UINT32_MAX;
  const char *env = getenv("MLU_REMOTE_DEVICE");
  if (env && *env) { char *end = NULL; unsigned long value = strtoul(env, &end, 10); if (end && *end == 0 && value <= UINT32_MAX) requested = (uint32_t)value; }
  return connect_impl(host, port, requested, 0, 0);
}
mlu_remote_context *mlu_remote_connect_device(const char *host, uint16_t port, uint32_t device) {
  return connect_impl(host, port, device, 0, 0);
}
mlu_remote_context *mlu_remote_connect_driver(const char *host, uint16_t port, uint32_t device, uint32_t flags) {
  mlu_remote_context *ctx=connect_impl(host,port,device,1,flags);
  if (ctx && !(ctx->capabilities & MLU_CAP_DRIVER_CONTEXT)) {
    mlu_remote_disconnect(ctx);
    return connect_failure(MLU_REMOTE_UNSUPPORTED,"REMOTE_DRIVER_UNSUPPORTED","server lacks native Driver context support",EPROTO);
  }
  return ctx;
}
int mlu_remote_context_sync(mlu_remote_context *ctx) {
  if (!ctx) return MLU_REMOTE_BAD_OBJECT;
  if (!(ctx->capabilities & MLU_CAP_DRIVER_CONTEXT)) return MLU_REMOTE_UNSUPPORTED;
  return rpc(ctx,MLU_OP_CONTEXT_SYNC,NULL,0,NULL,&(uint32_t){0});
}
int mlu_remote_session_close(mlu_remote_context *ctx) {
  if (!ctx) return MLU_REMOTE_BAD_OBJECT;
  if (!(ctx->capabilities & MLU_CAP_DRIVER_CONTEXT)) return MLU_REMOTE_UNSUPPORTED;
  return rpc(ctx,MLU_OP_SESSION_CLOSE,NULL,0,NULL,&(uint32_t){0});
}
int mlu_remote_module_load_binary(mlu_remote_context *ctx,const void *image,size_t size,uint64_t *module) {
  if (!ctx || !image || !module || !size || size>MLU_REMOTE_MAX_PAYLOAD-4) return MLU_REMOTE_BAD_LENGTH;
  if (!(ctx->capabilities & MLU_CAP_DRIVER_CONTEXT)) return MLU_REMOTE_UNSUPPORTED;
  unsigned char *p=malloc(size+4),*extra=NULL; uint32_t n=0;
  if (!p) return CN_ERROR_SYSTEM_OUT_OF_MEMORY;
  put32(p,(uint32_t)size); memcpy(p+4,image,size);
  int st=rpc(ctx,MLU_OP_MODULE_LOAD_BINARY,p,(uint32_t)(size+4),&extra,&n);
  if (!st) *module=get64(extra);
  free(p); free(extra); return st;
}
int mlu_remote_module_query_fatbinary_memory(mlu_remote_context *ctx,const void *image,size_t size,uint64_t *bytes) {
  if (!ctx || !image || !size || !bytes || size>MLU_REMOTE_MAX_PAYLOAD-4) return MLU_REMOTE_BAD_LENGTH;
  if (!(ctx->capabilities & MLU_CAP_DRIVER_CONTEXT)) return MLU_REMOTE_UNSUPPORTED;
  unsigned char *p=malloc(size+4),*extra=NULL; uint32_t n=0;
  if (!p) return CN_ERROR_SYSTEM_OUT_OF_MEMORY;
  put32(p,(uint32_t)size); memcpy(p+4,image,size);
  int st=rpc(ctx,MLU_OP_MODULE_QUERY_FATBINARY_MEMORY,p,(uint32_t)(size+4),&extra,&n);
  if (!st) *bytes=get64(extra);
  free(p); free(extra); return st;
}
void mlu_remote_disconnect(mlu_remote_context *ctx) {
  if (!ctx) return;
  if (ctx->fd >= 0) close(ctx->fd);
  /* No writes after a poisoned exchange/EOF. Server owns its separate pages. */
  while(ctx->pending) { pending_writeback *w=ctx->pending; ctx->pending=w->next; mlu_host_release(w->lease); free(w); }
  pthread_mutex_destroy(&ctx->lock); free(ctx);
}
const mlu_remote_error *mlu_remote_last_error(const mlu_remote_context *ctx) { return ctx ? &ctx->error : NULL; }

int mlu_remote_device_count(mlu_remote_context *ctx, uint32_t *count) { unsigned char p[4]; put32(p, 0xffffffffu); unsigned char *e = NULL; uint32_t n = 0; int st = rpc(ctx, MLU_OP_DEVICE_QUERY, p, 4, &e, &n); if (!st && n >= 16) *count = get32(e); free(e); return st; }
int mlu_remote_device_query(mlu_remote_context *ctx, uint32_t device, char *name, size_t cap, uint64_t *memory) { unsigned char p[4]; put32(p, device); unsigned char *e = NULL; uint32_t n = 0; int st = rpc(ctx, MLU_OP_DEVICE_QUERY, p, 4, &e, &n); if (!st && n >= 16) { uint32_t nl = get32(e + 12); if (memory) *memory = get64(e + 4); if (name && cap) { size_t c = nl < cap - 1 ? nl : cap - 1; if (16 + c <= n) { memcpy(name, e + 16, c); name[c] = 0; } } } free(e); return st; }
int mlu_remote_select_device(mlu_remote_context *ctx, uint32_t device) { unsigned char p[4]; put32(p, device); return rpc(ctx, MLU_OP_DEVICE_SELECT, p, 4, NULL, &(uint32_t){0}); }

int mlu_remote_alloc(mlu_remote_context *ctx, uint64_t bytes, uint64_t *allocation) { unsigned char p[8]; put64(p, bytes); unsigned char *e = NULL; uint32_t n = 0; int st = rpc(ctx, MLU_OP_ALLOC, p, 8, &e, &n); if (!st && n >= 8) *allocation = get64(e); free(e); return st; }
int mlu_remote_free(mlu_remote_context *ctx, uint64_t allocation) { unsigned char p[8]; put64(p, allocation); return rpc(ctx, MLU_OP_FREE, p, 8, NULL, &(uint32_t){0}); }

static int memcpy_call(mlu_remote_context *ctx, uint32_t dir, uint64_t dst, uint64_t dstoff, uint64_t src, uint64_t srcoff, const void *in, void *outbuf, uint64_t bytes, uint64_t queue, int asynchronous, uint64_t *transfer) {
  if (bytes > MLU_REMOTE_MAX_PAYLOAD - 60 || (dir == MLU_MEMCPY_H2D && !in) || (dir == MLU_MEMCPY_D2H && !outbuf)) return MLU_REMOTE_BAD_LENGTH;
  uint32_t n = 60 + (dir == MLU_MEMCPY_H2D ? (uint32_t)bytes : 0);
  unsigned char *p = (unsigned char *)calloc(1, n); if (!p) return CN_ERROR_SYSTEM_OUT_OF_MEMORY;
  put32(p, dir); put64(p + 4, queue); put64(p + 12, dst); put64(p + 20, dstoff); put64(p + 28, src); put64(p + 36, srcoff); put64(p + 44, bytes);
  put32(p + 52, dir == MLU_MEMCPY_H2D ? (uint32_t)bytes : 0); put32(p + 56, asynchronous ? 1u : 0u);
  if (dir == MLU_MEMCPY_H2D) memcpy(p + 60, in, (size_t)bytes);
  unsigned char *e = NULL; uint32_t en = 0; int st = rpc(ctx, MLU_OP_MEMCPY, p, n, &e, &en); free(p);
  if (!st && en >= 8 && transfer) *transfer = get64(e);
  if (!st && dir == MLU_MEMCPY_D2H && en >= 16 + bytes && get64(e + 8) == bytes) memcpy(outbuf, e + 16, (size_t)bytes);
  free(e); return st;
}
int mlu_remote_memcpy_h2d(mlu_remote_context *ctx, uint64_t dst, uint64_t off, const void *src, uint64_t bytes, uint64_t q, uint64_t *t) { return memcpy_call(ctx, MLU_MEMCPY_H2D, dst, off, 0, 0, src, NULL, bytes, q, 0, t); }
int mlu_remote_memcpy_h2d_async(mlu_remote_context *ctx, uint64_t dst, uint64_t off, const void *src, uint64_t bytes, uint64_t q, uint64_t *t) { return memcpy_call(ctx, MLU_MEMCPY_H2D, dst, off, 0, 0, src, NULL, bytes, q, 1, t); }
int mlu_remote_memcpy_d2h(mlu_remote_context *ctx, void *dst, uint64_t src, uint64_t off, uint64_t bytes, uint64_t q, uint64_t *t) { return memcpy_call(ctx, MLU_MEMCPY_D2H, 0, 0, src, off, NULL, dst, bytes, q, 0, t); }
int mlu_remote_memcpy_d2d(mlu_remote_context *ctx, uint64_t dst, uint64_t doff, uint64_t src, uint64_t soff, uint64_t bytes, uint64_t q, uint64_t *t) { return memcpy_call(ctx, MLU_MEMCPY_D2D, dst, doff, src, soff, NULL, NULL, bytes, q, 0, t); }
int mlu_remote_memcpy_d2d_async(mlu_remote_context *ctx, uint64_t dst, uint64_t doff, uint64_t src, uint64_t soff, uint64_t bytes, uint64_t q, uint64_t *t) { return memcpy_call(ctx, MLU_MEMCPY_D2D, dst, doff, src, soff, NULL, NULL, bytes, q, 1, t); }

static int id_call(mlu_remote_context *ctx, uint16_t op, uint64_t id) { unsigned char p[8]; put64(p, id); return rpc(ctx, op, p, 8, NULL, &(uint32_t){0}); }
int mlu_remote_memcpy_d2h_async(mlu_remote_context *ctx,void *dst,uint64_t src,
    uint64_t offset,uint64_t bytes,uint64_t queue,uint64_t *transfer) {
  if(!ctx || !dst || !transfer || !bytes || bytes>MLU_REMOTE_MAX_PAYLOAD-128) return MLU_REMOTE_BAD_LENGTH;
  if(!(ctx->capabilities & MLU_CAP_DEFERRED_D2H)) return MLU_REMOTE_UNSUPPORTED;
  pending_writeback *w=calloc(1,sizeof(*w)); if(!w) return CN_ERROR_SYSTEM_OUT_OF_MEMORY;
  if(mlu_host_acquire(dst,(size_t)bytes,&w->lease)<0) { free(w); return MLU_REMOTE_BAD_LENGTH; }
  unsigned char request[32],*out=NULL; uint32_t n=0;
  put64(request,src); put64(request+8,offset); put64(request+16,bytes); put64(request+24,queue);
  pthread_mutex_lock(&ctx->lock);
  int st=rpc_locked(ctx,MLU_OP_API_D2H_SUBMIT,request,32,&out,&n);
  if(!st) {
    w->transfer=get64(out); w->dst=dst; w->bytes=bytes; w->queue=queue; *transfer=w->transfer;
    if(ctx->pending_tail) *ctx->pending_tail=w; else ctx->pending=w;
    ctx->pending_tail=&w->next;
  } else { mlu_host_release(w->lease); free(w); }
  pthread_mutex_unlock(&ctx->lock); free(out); return st;
}
#include "simple_rpc_client.inc"
#include "typed_rpc_client.inc"
int mlu_remote_queue_create(mlu_remote_context *ctx, uint64_t *q) { unsigned char *e = NULL; uint32_t n = 0; int st = rpc(ctx, MLU_OP_QUEUE_CREATE, NULL, 0, &e, &n); if (!st && n >= 8) *q = get64(e); free(e); return st; }
int mlu_remote_notifier_create(mlu_remote_context *ctx, uint64_t *n) { unsigned char *e = NULL; uint32_t len = 0; int st = rpc(ctx, MLU_OP_NOTIFIER_CREATE, NULL, 0, &e, &len); if (!st && len >= 8) *n = get64(e); free(e); return st; }
int mlu_remote_notifier_record(mlu_remote_context *ctx, uint64_t n, uint64_t q) { unsigned char p[16]; put64(p, n); put64(p + 8, q); return rpc(ctx, MLU_OP_NOTIFIER_RECORD, p, 16, NULL, &(uint32_t){0}); }
int mlu_remote_session_reset(mlu_remote_context *ctx) { return rpc(ctx, MLU_OP_SESSION_RESET, NULL, 0, NULL, &(uint32_t){0}); }
int mlu_remote_module_load_preset(mlu_remote_context *ctx, const char *name, uint64_t *module, uint64_t *kernel) { if (!name) return MLU_REMOTE_BAD_LENGTH; size_t sl = strlen(name); if (sl > 128) return MLU_REMOTE_BAD_LENGTH; uint32_t l = (uint32_t)sl; unsigned char *p = (unsigned char *)malloc(4 + l); if (!p) return CN_ERROR_SYSTEM_OUT_OF_MEMORY; put32(p, l); memcpy(p + 4, name, l); unsigned char *e = NULL; uint32_t n = 0; int st = rpc(ctx, MLU_OP_MODULE_LOAD_PRESET, p, 4 + l, &e, &n); free(p); if (!st && n >= 16) { if (module) *module = get64(e); if (kernel) *kernel = get64(e + 8); } free(e); return st; }

int mlu_remote_module_load_image(mlu_remote_context *ctx, const void *image,
                                 size_t image_size, const char *kernel_name,
                                 uint64_t *module, uint64_t *kernel) {
  if (!ctx || !image || !image_size || image_size > MLU_REMOTE_MAX_PAYLOAD - 8 || !kernel_name)
    return MLU_REMOTE_BAD_LENGTH;
  if (!(ctx->capabilities & MLU_CAP_MODULE_IMAGE) || ctx->kernel_abi != MLU_REMOTE_KERNEL_ABI)
    return MLU_REMOTE_UNSUPPORTED;
  size_t name_size = strlen(kernel_name);
  if (!name_size || name_size > 1024 || image_size > MLU_REMOTE_MAX_PAYLOAD - 8 - name_size)
    return MLU_REMOTE_BAD_LENGTH;
  uint32_t total = (uint32_t)(8 + image_size + name_size);
  unsigned char *p = (unsigned char *)malloc(total);
  if (!p) return CN_ERROR_SYSTEM_OUT_OF_MEMORY;
  put32(p, (uint32_t)image_size); put32(p + 4, (uint32_t)name_size);
  memcpy(p + 8, image, image_size); memcpy(p + 8 + image_size, kernel_name, name_size);
  unsigned char *e = NULL; uint32_t n = 0;
  int st = rpc(ctx, MLU_OP_MODULE_LOAD_IMAGE, p, total, &e, &n);
  free(p);
  if (!st && n >= 16) { if (module) *module = get64(e); if (kernel) *kernel = get64(e + 8); }
  free(e); return st;
}

int mlu_remote_module_get_kernel(mlu_remote_context *ctx, uint64_t module,
                                 const char *name, uint64_t *kernel) {
  if (!ctx || !name || !kernel || !*name || strlen(name) >= 256)
    return MLU_REMOTE_BAD_LENGTH;
  if (!(ctx->capabilities & MLU_CAP_MODULE_KERNELS) || ctx->kernel_abi != MLU_REMOTE_KERNEL_ABI)
    return MLU_REMOTE_UNSUPPORTED;
  size_t length = strlen(name);
  unsigned char p[12 + 255]; put64(p, module); put32(p + 8, (uint32_t)length);
  memcpy(p + 12, name, length);
  unsigned char *extra = NULL; uint32_t n = 0;
  int st = rpc(ctx, MLU_OP_MODULE_GET_KERNEL, p, (uint32_t)(12 + length), &extra, &n);
  if (!st) *kernel = get64(extra);
  free(extra); return st;
}

int mlu_remote_kernel_launch_generic(mlu_remote_context *ctx, uint64_t module,
                                     uint64_t kernel, uint64_t queue,
                                     uint32_t dim_x, uint32_t dim_y,
                                     uint32_t dim_z, uint32_t kernel_class,
                                     const mlu_remote_kernel_arg *args,
                                     uint32_t arg_count, uint64_t *transfer) {
  if (!ctx || arg_count > 256 || (arg_count && !args)) return MLU_REMOTE_BAD_LENGTH;
  if (!(ctx->capabilities & MLU_CAP_GENERIC_LAUNCH) || ctx->kernel_abi != MLU_REMOTE_KERNEL_ABI)
    return MLU_REMOTE_UNSUPPORTED;
  size_t total = 44;
  uint32_t relocation_total=0;
  for (uint32_t i = 0; i < arg_count; ++i) {
    if (args[i].kind < 1 || args[i].kind > 3) return MLU_REMOTE_BAD_LENGTH;
    if (!args[i].size || args[i].size > 4096 || (args[i].kind != 1 && !args[i].scalar)) return MLU_REMOTE_BAD_LENGTH;
    if (args[i].kind == 1 && args[i].size != 8) return MLU_REMOTE_BAD_LENGTH;
    if(args[i].kind!=1 && (args[i].allocation || args[i].offset)) return MLU_REMOTE_BAD_LENGTH;
    uint32_t count=args[i].relocation_count;
    if(args[i].kind==3) {
      if(!(ctx->capabilities & MLU_CAP_ARGUMENT_RELOCATIONS)) return MLU_REMOTE_UNSUPPORTED;
      if(!count || count>512-relocation_total || !args[i].relocations) return MLU_REMOTE_BAD_LENGTH;
      for(uint32_t j=0;j<count;++j) {
        const mlu_remote_kernel_relocation *r=&args[i].relocations[j];
        if(r->offset>args[i].size || args[i].size-r->offset<8 || r->offset%8 ||
           (j && r->offset<args[i].relocations[j-1].offset+8) || (!r->allocation && r->allocation_offset)) return MLU_REMOTE_BAD_LENGTH;
      }
    } else if(count) return MLU_REMOTE_BAD_LENGTH;
    relocation_total+=count;
    size_t need=32u+args[i].size+24u*count;
    if (total > MLU_REMOTE_MAX_PAYLOAD - need) return MLU_REMOTE_BAD_LENGTH;
    total += need;
  }
  unsigned char *p = (unsigned char *)calloc(1, total);
  if (!p) return CN_ERROR_SYSTEM_OUT_OF_MEMORY;
  put64(p, module); put64(p + 8, kernel); put64(p + 16, queue);
  put32(p + 24, dim_x); put32(p + 28, dim_y); put32(p + 32, dim_z);
  put32(p + 36, kernel_class); put32(p + 40, arg_count);
  unsigned char *cursor = p + 44;
  for (uint32_t i = 0; i < arg_count; ++i) {
    put32(cursor, args[i].kind); put32(cursor + 4, args[i].size);
    put64(cursor + 8, args[i].allocation); put64(cursor + 16, args[i].offset);
    put64(cursor + 24, args[i].relocation_count); cursor += 32;
    if (args[i].kind != 1) memcpy(cursor, args[i].scalar, args[i].size);
    for(uint32_t j=0;j<args[i].relocation_count;++j) memset(cursor+args[i].relocations[j].offset,0,8);
    cursor += args[i].size;
    for(uint32_t j=0;j<args[i].relocation_count;++j) {
      const mlu_remote_kernel_relocation *r=&args[i].relocations[j];
      put32(cursor,r->offset); put32(cursor+4,0);
      put64(cursor+8,r->allocation); put64(cursor+16,r->allocation_offset); cursor+=24;
    }
  }
  unsigned char *e = NULL; uint32_t n = 0;
  int st = rpc(ctx, MLU_OP_KERNEL_LAUNCH_GENERIC, p, (uint32_t)total, &e, &n);
  free(p); if (!st && n >= 8 && transfer) *transfer = get64(e); free(e); return st;
}
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
