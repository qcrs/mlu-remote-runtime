#include "remote_cnrt.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

/* Deliberately exits without sending SESSION_RESET or freeing IDs. The server
 * must observe EOF and destroy the session-owned native context/resources. */
int main(void) {
  const char *host = getenv("MLU_REMOTE_HOST"); int port = getenv("MLU_REMOTE_PORT") ? atoi(getenv("MLU_REMOTE_PORT")) : 50051;
  mlu_remote_context *ctx = mlu_remote_connect(host ? host : "127.0.0.1", (uint16_t)port); if (!ctx) return 1;
  uint64_t q = 0, a = 0; if (mlu_remote_queue_create(ctx, &q) || mlu_remote_alloc(ctx, 4096, &a)) return 1;
  fprintf(stdout, "ABRUPT_LIVE queue=%llu allocation=%llu\n", (unsigned long long)q, (unsigned long long)a); fflush(stdout);
  _exit(0);
}
