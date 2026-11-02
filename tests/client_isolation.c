#include "remote_cnrt.h"

#include <stdio.h>
#include <stdlib.h>

int main(void) {
  const char *host = getenv("MLU_REMOTE_HOST"); int port = getenv("MLU_REMOTE_PORT") ? atoi(getenv("MLU_REMOTE_PORT")) : 50051;
  mlu_remote_context *a = mlu_remote_connect(host ? host : "127.0.0.1", (uint16_t)port);
  mlu_remote_context *b = mlu_remote_connect(host ? host : "127.0.0.1", (uint16_t)port);
  if (!a || !b) { fprintf(stderr, "connect failed\n"); return 1; }
  mlu_remote_queue_id qa = 0, qb = 0; if (mlu_remote_queue_create(a, &qa) || mlu_remote_queue_create(b, &qb)) return 1;
  int cross = mlu_remote_queue_sync(b, qa); if (!cross) { fprintf(stderr, "cross-session queue ID accepted\n"); return 1; }
  mlu_remote_queue_destroy(a, qa); mlu_remote_queue_destroy(b, qb); mlu_remote_disconnect(a); mlu_remote_disconnect(b);
  printf("PASS session isolation queue_a=%llu queue_b=%llu cross_status=%d\n", (unsigned long long)qa, (unsigned long long)qb, cross); return 0;
}
