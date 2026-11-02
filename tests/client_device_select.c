#include "remote_cnrt.h"

#include <stdio.h>
#include <stdlib.h>

static int show_error(mlu_remote_context *ctx, const char *where, int st) {
  const mlu_remote_error *e = mlu_remote_last_error(ctx);
  fprintf(stderr, "FAIL %s status=%d %s: %s\n", where, st, e ? e->name : "", e ? e->message : "");
  return 1;
}

int main(void) {
  const char *host = getenv("MLU_REMOTE_HOST"); int port = getenv("MLU_REMOTE_PORT") ? atoi(getenv("MLU_REMOTE_PORT")) : 50051;
  mlu_remote_context *ctx = mlu_remote_connect(host ? host : "127.0.0.1", (uint16_t)port);
  if (!ctx) { fprintf(stderr, "connect failed\n"); return 1; }
  uint32_t count = 0; int st = mlu_remote_device_count(ctx, &count); if (st) return show_error(ctx, "device_count", st);
  for (uint32_t ordinal = 0; ordinal < count; ++ordinal) {
    st = mlu_remote_select_device(ctx, ordinal); if (st) return show_error(ctx, "select_available_device", st);
  }
  st = mlu_remote_select_device(ctx, count);
  if (count && !st) return show_error(ctx, "reject_invalid_device", st);
  int invalid_status = st;
  mlu_remote_queue_id queue = 0; st = mlu_remote_queue_create(ctx, &queue); if (st) return show_error(ctx, "queue_after_select", st);
  int resource_switch_status = 0;
  if (count > 1) {
    uint32_t other = (count - 1u) ? 0u : 1u;
    resource_switch_status = mlu_remote_select_device(ctx, other);
    if (!resource_switch_status) return show_error(ctx, "reject_switch_with_live_queue", resource_switch_status);
  }
  mlu_remote_queue_destroy(ctx, queue); mlu_remote_disconnect(ctx);
  printf("PASS device_selection available=%u invalid_status=%d resource_switch_status=%d\n", count, invalid_status, resource_switch_status);
  return 0;
}
