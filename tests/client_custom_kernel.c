#include "remote_cnrt.h"
#include <stdio.h>
#include <stdlib.h>

int main(void) {
  const char *host = getenv("MLU_REMOTE_HOST"); int port = getenv("MLU_REMOTE_PORT") ? atoi(getenv("MLU_REMOTE_PORT")) : 50051;
  mlu_remote_context *ctx = mlu_remote_connect(host ? host : "127.0.0.1", (uint16_t)port); if (!ctx) return 1;
  uint64_t module = 0, kernel = 0; int st = mlu_remote_module_load_preset(ctx, "vector_add", &module, &kernel);
  if (st) { const mlu_remote_error *e = mlu_remote_last_error(ctx); fprintf(stderr, "PRESET_MODULE status=%d %s: %s\n", st, e->name, e->message); mlu_remote_disconnect(ctx); return 77; }
  uint64_t q = 0, a = 0, b = 0, dst = 0; const uint32_t n = 1024; uint64_t bytes = n * sizeof(float); float *ha = malloc(bytes), *hb = malloc(bytes), *out = calloc(n, sizeof(float)); for (uint32_t i=0;i<n;i++){ha[i]=i;hb[i]=2*i;}
  if ((st=mlu_remote_queue_create(ctx,&q)) || (st=mlu_remote_alloc(ctx,bytes,&a)) || (st=mlu_remote_alloc(ctx,bytes,&b)) || (st=mlu_remote_alloc(ctx,bytes,&dst)) || (st=mlu_remote_memcpy_h2d(ctx,a,0,ha,bytes,q,NULL)) || (st=mlu_remote_memcpy_h2d(ctx,b,0,hb,bytes,q,NULL)) || (st=mlu_remote_kernel_launch_vector_add(ctx,kernel,q,dst,a,b,n,1)) || (st=mlu_remote_queue_sync(ctx,q)) || (st=mlu_remote_memcpy_d2h(ctx,out,dst,0,bytes,q,NULL))) { const mlu_remote_error *e=mlu_remote_last_error(ctx); fprintf(stderr,"KERNEL status=%d %s: %s\n",st,e->name,e->message); return 1; }
  for (uint32_t i=0;i<n;i++) if (out[i] != ha[i] + hb[i]) { fprintf(stderr,"kernel mismatch %u\n",i); return 1; }
  printf("PASS preset BANG vector_add module=%llu kernel=%llu\n",(unsigned long long)module,(unsigned long long)kernel); mlu_remote_free(ctx,a); mlu_remote_free(ctx,b); mlu_remote_free(ctx,dst); mlu_remote_queue_destroy(ctx,q); if ((st = mlu_remote_module_unload_preset(ctx, module)) != 0) { const mlu_remote_error *e=mlu_remote_last_error(ctx); fprintf(stderr,"MODULE_UNLOAD status=%d %s: %s\n",st,e->name,e->message); return 1; } free(ha);free(hb);free(out);mlu_remote_disconnect(ctx);return 0;
}
