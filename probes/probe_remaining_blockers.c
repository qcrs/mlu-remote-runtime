/* Focused Native evidence for APIs without a working MLU370 / Neuware 3.5.2 path. */
#include <cn_api.h>
#include <cnrt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define SHOW(call) do{int r=(call);printf("%s = %d\n",#call,r);fflush(stdout);}while(0)
int main(int argc,char **argv) {
 setbuf(stdout,NULL);
 if(argc>1 && !strcmp(argv[1],"reshape")) {
  int n=argc>2?atoi(argv[2]):64,c=argc>3?atoi(argv[3]):64,type=argc>4?atoi(argv[4]):CNRT_FLOAT16;
  SHOW(cnrtSetDevice(0));size_t size=(size_t)n*3*3*c*4+1048576;
  unsigned char *src=calloc(1,size),*dst=calloc(1,size);if(!src || !dst)return 2;
  printf("reshape n=%d h=3 w=3 c=%d type=%d buffer_bytes=%zu\n",n,c,type,size);
  SHOW(cnrtFilterReshape(dst,src,n,3,3,c,(cnrtDataType_t)type));free(src);free(dst);return 0;
 }
 CNcontext context;SHOW(cnInit(0));SHOW(cnCtxCreate(&context,0,0));
 if(argc>1 && !strcmp(argv[1],"export")) {
  const void *function=NULL;SHOW(cnGetExportFunction("cnQueueAbort",&function));printf("valid private extension present=%d\n",function!=NULL);
 } else {
  CNaddr device=0;void *host=NULL;SHOW(cnMalloc(&device,1048576));SHOW(cnMallocHost(&host,1048576));
  for(int domain=0;domain<2;domain++) {
   CNaddr p=domain?(uintptr_t)host:device;printf("memory_domain=%s\n",domain?"pinned_host":"device");
   for(unsigned op=CN_FLUSH_CACHE;op<=CN_INVALID_CACHE;op++) {SHOW(cnSetMemAttribute(op,CN_MEM_ATTRIBUTE_CACHE_OPS,p));SHOW(cnSetMemRangeAttribute(op,CN_MEM_ATTRIBUTE_CACHE_OPS,p,1048576));}
   for(unsigned v=0;v<2;v++)SHOW(cnSetMemAttribute(v,CN_MEM_ATTRIBUTE_ACCESS_FLAGS,p));
   for(unsigned v=0;v<4;v++)SHOW(cnSetMemAttribute(v,CN_MEM_ATTRIBUTE_CACHE_CONFIG,p));
  }
  SHOW(cnFreeHost(host));SHOW(cnFree(device));
 }
 SHOW(cnCtxDestroy(context));return 0;
}
