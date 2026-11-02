#include "remote_cnrt.h"

#include <stdio.h>
#include <stdlib.h>

static int fail(mlu_remote_context *ctx, const char *where, int status) {
  const mlu_remote_error *e = mlu_remote_last_error(ctx);
  fprintf(stderr, "FAIL %s status=%d %s: %s\n", where, status, e ? e->name : "", e ? e->message : "");
  return 1;
}

int main(void) {
  const char *host = getenv("MLU_REMOTE_HOST"); int port = getenv("MLU_REMOTE_PORT") ? atoi(getenv("MLU_REMOTE_PORT")) : 50051;
  mlu_remote_context *ctx = mlu_remote_connect(host ? host : "127.0.0.1", (uint16_t)port);
  if (!ctx) { fprintf(stderr, "connect failed\n"); return 1; }
  mlu_remote_queue_id old_queue = 0; mlu_remote_allocation_id old_allocation = 0;
  int st = mlu_remote_queue_create(ctx, &old_queue); if (st) return fail(ctx, "queue_create", st);
  st = mlu_remote_alloc(ctx, 256, &old_allocation); if (st) return fail(ctx, "alloc", st);
  st = mlu_remote_session_reset(ctx); if (st) return fail(ctx, "session_reset", st);
  int stale_queue = mlu_remote_queue_sync(ctx, old_queue);
  int stale_allocation = mlu_remote_free(ctx, old_allocation);
  if (!stale_queue || !stale_allocation) return fail(ctx, "stale_ids_rejected", 0);
  mlu_remote_queue_id fresh_queue = 0; st = mlu_remote_queue_create(ctx, &fresh_queue);
  if (st) return fail(ctx, "queue_after_reset", st);
  mlu_remote_queue_destroy(ctx, fresh_queue); mlu_remote_disconnect(ctx);
  printf("PASS session_reset old_queue=%llu old_allocation=%llu stale_queue=%d stale_allocation=%d\n",
         (unsigned long long)old_queue, (unsigned long long)old_allocation,
         stale_queue, stale_allocation);
  return 0;
}
