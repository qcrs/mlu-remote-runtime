#include "remote_cnrt.h"
#include "remote_cnnl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(mlu_remote_context *ctx, const char *where, int status) {
  const mlu_remote_error *e = mlu_remote_last_error(ctx);
  fprintf(stderr, "FAIL %s status=%d %s: %s\n", where, status, e ? e->name : "", e ? e->message : "");
  return 1;
}
#define CHECK(c, what) do { int _s = (c); if (_s) return fail(ctx, what, _s); } while (0)

int main(void) {
  const char *host = getenv("MLU_REMOTE_HOST"); int port = getenv("MLU_REMOTE_PORT") ? atoi(getenv("MLU_REMOTE_PORT")) : 50051;
  mlu_remote_context *ctx = mlu_remote_connect(host ? host : "127.0.0.1", (uint16_t)port);
  if (!ctx) { fprintf(stderr, "connect failed to %s:%d\n", host ? host : "127.0.0.1", port); return 1; }
  uint32_t count = 0; CHECK(mlu_remote_device_count(ctx, &count), "device_count"); if (!count) { fprintf(stderr, "no remote devices\n"); mlu_remote_disconnect(ctx); return 1; }
  char name[128] = {0}; uint64_t memory = 0; CHECK(mlu_remote_device_query(ctx, 0, name, sizeof(name), &memory), "device_query");
  printf("HELLO device_count=%u device0=%s memory=%llu\n", count, name, (unsigned long long)memory);
  CHECK(mlu_remote_select_device(ctx, 0), "device_select");
  int invalid_status = mlu_remote_free(ctx, 0xdeadbeef); const mlu_remote_error *invalid_error = mlu_remote_last_error(ctx);
  if (!invalid_status || !invalid_error || !invalid_error->name[0] || !invalid_error->message[0]) return fail(ctx, "error_propagation", invalid_status);
  printf("PASS error code=%d name=%s\n", invalid_status, invalid_error->name);
  mlu_remote_queue_id q = 0; mlu_remote_notifier_id n = 0; CHECK(mlu_remote_queue_create(ctx, &q), "queue_create"); CHECK(mlu_remote_notifier_create(ctx, &n), "notifier_create");
  const uint32_t elements = 1024; const uint64_t bytes = elements * sizeof(float); float *input = (float *)malloc(bytes), *output = (float *)calloc(elements, sizeof(float)); for (uint32_t i = 0; i < elements; ++i) input[i] = (float)i;
  mlu_remote_allocation_id a = 0, b = 0; CHECK(mlu_remote_alloc(ctx, bytes, &a), "alloc_a"); CHECK(mlu_remote_alloc(ctx, bytes, &b), "alloc_b"); mlu_remote_transfer_id t = 0;
  CHECK(mlu_remote_memcpy_h2d(ctx, a, 0, input, bytes, q, &t), "h2d"); CHECK(mlu_remote_memcpy_d2d(ctx, b, 0, a, 0, bytes, q, &t), "d2d"); CHECK(mlu_remote_notifier_record(ctx, n, q), "notifier_record"); CHECK(mlu_remote_queue_sync(ctx, q), "queue_sync"); CHECK(mlu_remote_queue_query(ctx, q), "queue_query"); CHECK(mlu_remote_notifier_sync(ctx, n), "notifier_sync"); CHECK(mlu_remote_notifier_query(ctx, n), "notifier_query"); CHECK(mlu_remote_memcpy_d2h(ctx, output, b, 0, bytes, q, &t), "d2h");
  for (uint32_t i = 0; i < elements; ++i) if (output[i] != input[i]) { fprintf(stderr, "mismatch at %u: %.2f != %.2f\n", i, output[i], input[i]); return 1; }
  printf("PASS memory H2D/D2D/D2H queue=%llu notifier=%llu transfer=%llu\n", (unsigned long long)q, (unsigned long long)n, (unsigned long long)t);
  int cnnl = mlu_remote_cnnl_elementwise_add_f32(ctx, q, b, a, b, elements); printf("CNNL elementwise_add_f32 status=%d (expected GROUND_TRUTH_BLOCKED when CNNL is absent)\n", cnnl);
  CHECK(mlu_remote_free(ctx, a), "free_a"); CHECK(mlu_remote_free(ctx, b), "free_b"); CHECK(mlu_remote_notifier_destroy(ctx, n), "notifier_destroy"); CHECK(mlu_remote_queue_destroy(ctx, q), "queue_destroy"); free(input); free(output); mlu_remote_disconnect(ctx); return 0;
}
