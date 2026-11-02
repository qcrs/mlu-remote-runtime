#include <cnrt.h>
#include <stdio.h>
#include <string.h>
#define OK(expr) do { cnrtRet_t s=(expr); if(s) { printf("FAIL %s=%d\n",#expr,s); return 1; } } while(0)
int main(void) {
  OK(cnrtSetDevice(0));
  void *p = NULL; cnrtRet_t zero=cnrtMalloc(&p,0);
  printf("malloc_zero=%d pointer_is_null=%d\n",zero,p==NULL);
  if (!zero && p) OK(cnrtFree(p));
  printf("free_null=%d\n",cnrtFree(NULL));
  cnrtGetLastError();
  printf("invalid_device=%d\n",cnrtSetDevice(-1));
  unsigned count=0; OK(cnrtGetDeviceCount(&count));
  printf("last_after_success=%d\n",cnrtPeekAtLastError());
  cnrtGetLastError();
  int input[4]={1,2,3,4},output[4]={0};
  void *copy=NULL;
  OK(cnrtMalloc(&p,sizeof(input))); OK(cnrtMalloc(&copy,sizeof(input))); cnrtQueue_t q;
  OK(cnrtQueueCreate(&q));
  OK(cnrtMemcpyAsync(p,input,sizeof(input),q,cnrtMemcpyHostToDev));
  OK(cnrtMemcpyAsync(copy,p,sizeof(input),q,cnrtMemcpyDevToDev));
  OK(cnrtQueueSync(q));
  printf("completed_queue_query=%d\n",cnrtQueueQuery(q));
  OK(cnrtMemcpy(output,copy,sizeof(output),cnrtMemcpyDevToHost));
  if(memcmp(input,output,sizeof(input))) return 2;
  OK(cnrtFree(p)); OK(cnrtFree(copy)); OK(cnrtQueueDestroy(q));
  puts("PASS native API H2D/D2D/D2H/queue/error semantics family");
}
