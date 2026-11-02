#include "remote_cnnl.h"
#include <stdio.h>
#include <stdlib.h>

int main(void) {
  const char *host = getenv("MLU_REMOTE_HOST"); int port = getenv("MLU_REMOTE_PORT") ? atoi(getenv("MLU_REMOTE_PORT")) : 50051;
  mlu_remote_context *ctx = mlu_remote_connect(host ? host : "127.0.0.1", (uint16_t)port);
  if (!ctx) return 1;
  int st = mlu_remote_cnnl_elementwise_add_f32(ctx, 0, 0, 0, 0, 1);
  printf("CNNL_STATUS=%d\n", st); mlu_remote_disconnect(ctx); return st == MLU_REMOTE_UNSUPPORTED ? 0 : 1;
}
