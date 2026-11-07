#include <cnrt.h>
#include <cn_api.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define OK(e) do { int st=(e); if(st) { fprintf(stderr,"line %d %s: %d\n",__LINE__,#e,st); exit(2); } } while(0)
#define CHECK(e) do { if(!(e)) { fprintf(stderr,"line %d %s\n",__LINE__,#e); exit(3); } } while(0)
enum { N=4096, OFFSET=37, COUNT=1003 };
static unsigned char input[N],output[N],host[N];
static void verify(void) { CHECK(!memcmp(input+OFFSET,output+OFFSET,COUNT)); }
static void runtime(int ownership) {
  OK(cnrtSetDevice(0)); unsigned char *a=NULL,*b=NULL; cnrtQueue_t queue=NULL;
  OK(cnrtMalloc((void **)&a,N)); OK(cnrtMalloc((void **)&b,N)); OK(cnrtQueueCreate(&queue));
  if(ownership) {
    OK(cnrtFree(a));
    CHECK(cnrtMemcpy(host,a,8,cnrtMemcpyNoDirection)!=cnrtSuccess);
    CHECK(cnrtMemcpy(host,b+N,8,cnrtMemcpyNoDirection)!=cnrtSuccess);
    CHECK(cnrtMemcpy(host,b+N-4,8,cnrtMemcpyNoDirection)!=cnrtSuccess);
    CHECK(cnrtMemcpy(host,a,8,cnrtMemcpyHostToHost)!=cnrtSuccess);
    OK(cnrtMemcpyAsync(b,input,N,queue,cnrtMemcpyNoDirection));
    memset(input,0,N); OK(cnrtQueueSync(queue));
    OK(cnrtMemcpy(output,b,N,cnrtMemcpyNoDirection)); CHECK(output[OFFSET]==(unsigned char)((OFFSET*13+7)%251));
    puts("PASS runtime stale/guard/range rejection and H2D staging snapshot");
  } else {
    OK(cnrtMemcpy(host+OFFSET,input+OFFSET,COUNT,cnrtMemcpyNoDirection));
    CHECK(!memcmp(host+OFFSET,input+OFFSET,COUNT));
    OK(cnrtMemcpy(a+OFFSET,host+OFFSET,COUNT,cnrtMemcpyNoDirection));
    OK(cnrtMemcpy(b+OFFSET,a+OFFSET,COUNT,cnrtMemcpyNoDirection));
    OK(cnrtMemcpy(output+OFFSET,b+OFFSET,COUNT,cnrtMemcpyNoDirection)); verify();
    puts("PASS runtime inferred sync H2H/H2D/D2D/D2H offsets");
    for(int def=0;def<2;++def) {
      cnrtQueue_t q=def ? NULL : queue; memset(output,0,N);
      OK(cnrtMemcpyAsync(a+OFFSET,input+OFFSET,COUNT,q,cnrtMemcpyNoDirection));
      OK(cnrtMemcpyAsync(b+OFFSET,a+OFFSET,COUNT,q,cnrtMemcpyNoDirection));
      OK(cnrtMemcpyAsync(output+OFFSET,b+OFFSET,COUNT,q,cnrtMemcpyNoDirection));
      OK(cnrtQueueSync(q)); verify();
      printf("PASS runtime inferred async H2D/D2D/D2H queue=%s\n",def ? "NULL" : "explicit");
    }
    OK(cnrtFree(a));
  }
  OK(cnrtFree(b)); OK(cnrtQueueDestroy(queue));
}
static void driver(int ownership) {
  OK(cnInit(0)); CNdev dev; CNcontext context; CNaddr a=0,b=0; CNqueue queue=NULL;
  OK(cnDeviceGet(&dev,0)); OK(cnCtxCreate(&context,0,dev));
  OK(cnMalloc(&a,N)); OK(cnMalloc(&b,N)); OK(cnCreateQueue(&queue,0));
  if(ownership) {
    CNcontext other; OK(cnCtxCreate(&other,0,dev));
    CHECK(cnMemcpy((CNaddr)host,a,8)==CN_MEMORY_ERROR_INVALID_ADDRESS);
    CHECK(cnMemcpyAsync(a,(CNaddr)input,8,NULL)==CN_MEMORY_ERROR_INVALID_ADDRESS);
    OK(cnCtxDestroy(other)); OK(cnCtxSetCurrent(context)); OK(cnFree(a));
    CHECK(cnMemcpy((CNaddr)host,a,8)==CN_MEMORY_ERROR_INVALID_ADDRESS);
    CHECK(cnMemcpyAsync(b,a,8,queue)==CN_MEMORY_ERROR_INVALID_ADDRESS);
    CHECK(cnMemcpy((CNaddr)host,b+N,8)==CN_MEMORY_ERROR_INVALID_ADDRESS);
    OK(cnMemcpyAsync(b,(CNaddr)input,N,queue)); memset(input,0,N); OK(cnQueueSync(queue));
    OK(cnMemcpy((CNaddr)output,b,N)); CHECK(output[OFFSET]==(unsigned char)((OFFSET*13+7)%251));
    CHECK(cnMemcpyAsync((CNaddr)host,(CNaddr)input,8,queue)==CN_OPS_ERROR_NOT_SUPPORTED);
    OK(cnMemcpyAsync((CNaddr)host,b,8,queue)); OK(cnQueueSync(queue)); CHECK(!memcmp(host,output,8));
    puts("PASS driver foreign/stale/guard rejection, H2D snapshot and async D2H writeback and unsupported H2H");
  } else {
    OK(cnMemcpy((CNaddr)(host+OFFSET),(CNaddr)(input+OFFSET),COUNT));
    CHECK(!memcmp(host+OFFSET,input+OFFSET,COUNT));
    OK(cnMemcpy(a+OFFSET,(CNaddr)(host+OFFSET),COUNT));
    OK(cnMemcpy(b+OFFSET,a+OFFSET,COUNT)); OK(cnMemcpy((CNaddr)(output+OFFSET),b+OFFSET,COUNT)); verify();
    puts("PASS driver inferred sync H2H/H2D/D2D/D2H offsets");
    for(int def=0;def<2;++def) {
      CNqueue q=def ? NULL : queue; memset(output,0,N);
      OK(cnMemcpyAsync(a+OFFSET,(CNaddr)(input+OFFSET),COUNT,q));
      OK(cnMemcpyAsync(b+OFFSET,a+OFFSET,COUNT,q)); OK(cnQueueSync(q));
      OK(cnMemcpy((CNaddr)(output+OFFSET),b+OFFSET,COUNT)); verify();
      printf("PASS driver inferred async H2D/D2D queue=%s\n",def ? "NULL" : "explicit");
    }
    OK(cnFree(a));
  }
  OK(cnFree(b)); OK(cnDestroyQueue(queue)); OK(cnCtxDestroy(context));
}
int main(int argc,char **argv) {
  for(size_t i=0;i<N;++i) input[i]=(i*13+7)%251;
  int ownership=argc>2;
  if(argc>1 && !strcmp(argv[1],"driver")) driver(ownership); else runtime(ownership);
  return 0;
}
