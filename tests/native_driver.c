/* Ordinary CNDrv application: compile once against the native SDK, execute
 * the identical binary with native, replacement and LD_PRELOAD libraries. */
#include <cn_api.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(e) do { CNresult r_=(e); if(r_) { fprintf(stderr,"line %d %s status=%d\n",__LINE__,#e,(int)r_); exit(2); } } while(0)
#define REQUIRE(e) do { if(!(e)) { fprintf(stderr,"line %d assertion: %s\n",__LINE__,#e); exit(3); } } while(0)
typedef struct {
  CNcontext ctx;
  CNqueue queue;
  CNmodule module;
  CNkernel write,read;
  CNaddr out;
  int seed;
} state;
static void *read_file(const char *path,size_t *size) {
  FILE *f=fopen(path,"rb"); REQUIRE(f);
  REQUIRE(!fseek(f,0,SEEK_END)); long n=ftell(f); REQUIRE(n>0);
  rewind(f); void *p=malloc((size_t)n); REQUIRE(p);
  REQUIRE(fread(p,1,(size_t)n,f)==(size_t)n); fclose(f); *size=(size_t)n; return p;
}
static void setup(state *s,const char *path,const void *image,unsigned flags,int seed) {
  CHECK(cnCtxCreate(&s->ctx,flags,0));
  unsigned got=999; CHECK(cnCtxGetFlags(&got)); REQUIRE(got==flags);
  CNdev dev=-1; CHECK(cnCtxGetDevice(&dev)); REQUIRE(dev==0);
  CHECK(cnCreateQueue(&s->queue,0)); CHECK(cnMalloc(&s->out,32));
  if(image) CHECK(cnModuleLoadFatBinary(image,&s->module));
  else CHECK(cnModuleLoad(path,&s->module));
  CHECK(cnModuleGetKernel(s->module,"_Z12module_writei",&s->write));
  CHECK(cnModuleGetKernel(s->module,"_Z11module_readPii",&s->read));
  s->seed=seed; void *args[]={&seed};
  CHECK(cnInvokeKernel(s->write,1,1,1,CN_KERNEL_CLASS_BLOCK,0,s->queue,args,NULL));
  CHECK(cnQueueSync(s->queue));
}
static void check_state(state *s) {
  CNaddr offset=s->out+4; int bias=37,host[4]={0}; void *args[]={&offset,&bias};
  CHECK(cnInvokeKernel(s->read,1,1,1,CN_KERNEL_CLASS_BLOCK,0,s->queue,args,NULL));
  CHECK(cnCtxSync()); CHECK(cnMemcpyDtoH(host,offset,sizeof(host)));
  for(int i=0;i<4;++i) REQUIRE(host[i]==s->seed+i+bias);
}
static void *thread(void *arg) {
  state *s=arg; CNcontext got=(CNcontext)(uintptr_t)1234;
  CHECK(cnCtxGetCurrent(&got)); REQUIRE(got==NULL);
  CHECK(cnCtxSetCurrent(s->ctx)); check_state(s);
  CHECK(cnCtxSetCurrent(NULL)); CHECK(cnCtxGetCurrent(&got)); REQUIRE(got==NULL);
  return NULL;
}
static void abi(const char *path,state *s) {
  CNmodule m; CNkernel mixed,tick; CHECK(cnModuleLoad(path,&m));
  CHECK(cnModuleGetKernel(m,"_Z5mixedPfPKfhsixfS1_",&mixed));
  CHECK(cnModuleGetKernel(m,"_Z4tickPi",&tick));
  CNaddr src,dst,copy; CHECK(cnMalloc(&src,48)); CHECK(cnMalloc(&dst,48)); CHECK(cnMalloc(&copy,48));
  float input[12],host[12]; for(int i=0;i<12;++i) input[i]=(float)i+1;
  CHECK(cnMemcpyHtoD(src,input,sizeof(input)));
  CNaddr dp=dst+4,sp=src+8,nullable=0;
  unsigned char c=3; short sh=-4; int n=10; long long bias=123456; float scale=2;
  void *args[]={&dp,&sp,&c,&sh,&n,&bias,&scale,&nullable};
  CHECK(cnInvokeKernel(mixed,2,2,1,CN_KERNEL_CLASS_BLOCK,0,s->queue,args,NULL));
  CHECK(cnQueueSync(s->queue)); CHECK(cnMemcpyDtoH(host,dp,n*sizeof(float)));
  for(int i=0;i<n;++i) REQUIRE(host[i]==input[i+2]*2-1);
  CNaddr address=dst; void *ticks[]={&address};
  for(int i=0;i<1200;++i) CHECK(cnInvokeKernel(tick,2,2,2,CN_KERNEL_CLASS_BLOCK,0,s->queue,ticks,NULL));
  CHECK(cnQueueSync(s->queue)); int result[8]; CHECK(cnMemcpyDtoH(result,dst,sizeof(result)));
  for(int i=0;i<8;++i) REQUIRE(result[i]==8);
  int uploads[1200];
  for(int i=0;i<1200;++i) { uploads[i]=i; CHECK(cnMemcpyHtoDAsync(src,uploads+i,sizeof(int),s->queue)); }
  CHECK(cnMemcpyDtoDAsync(copy,src,4,s->queue));
  int default_value=4242, default_read=0; CHECK(cnMemcpyHtoDAsync(s->out,&default_value,sizeof(default_value),NULL));
  CNresult default_ready=cnQueryQueue(NULL); REQUIRE(default_ready==CN_SUCCESS || default_ready==CN_ERROR_NOT_READY);
  CHECK(cnQueueSync(NULL)); CHECK(cnMemcpyDtoH(&default_read,s->out,sizeof(default_read))); REQUIRE(default_read==default_value);
  CNnotifier event; CHECK(cnCreateNotifier(&event,0)); CHECK(cnPlaceNotifier(event,s->queue));
  CNresult ready=cnQueryNotifier(event); REQUIRE(ready==CN_SUCCESS || ready==CN_ERROR_NOT_READY);
  CHECK(cnWaitNotifier(event)); CHECK(cnQueryNotifier(event)); CHECK(cnDestroyNotifier(event));
  CHECK(cnCtxSync()); int copied=-1; CHECK(cnMemcpyDtoH(&copied,copy,4)); REQUIRE(copied==1199);
  CHECK(cnMemcpyDtoD(dst,src,4)); CHECK(cnMemcpyDtoH(&copied,dst,4)); REQUIRE(copied==1199);
  CHECK(cnQueryQueue(s->queue)); CHECK(cnQueueSync(NULL)); CHECK(cnQueryQueue(NULL));
  if(getenv("MLU_TEST_SHIM")) {
    CNkernel untouched=(CNkernel)(uintptr_t)1234;
    REQUIRE(cnModuleGetKernel(m,"plain_c",&untouched)==CN_OPS_ERROR_NOT_SUPPORTED);
    REQUIRE(untouched==(CNkernel)(uintptr_t)1234);
    REQUIRE(cnModuleGetKernel(m,"_Z9aggregatePi7Payload",&untouched)==CN_OPS_ERROR_NOT_SUPPORTED);
    REQUIRE(cnMemcpyDtoHAsync(&copied,src,4,s->queue)==CN_OPS_ERROR_NOT_SUPPORTED);
  }
  CHECK(cnFree(src)); CHECK(cnFree(dst)); CHECK(cnFree(copy)); CHECK(cnModuleUnload(m));
  puts("PASS Driver mixed primitive args, pointer offsets/NULL, xyz, 1200 launches, 1200 async H2D, D2D, notifier");
}
static void pending_unload(const char *path,state *s) {
  for(int use_default=0;use_default<2;++use_default) {
  CNmodule m; CNkernel k; CHECK(cnModuleLoad(path,&m));
  CHECK(cnModuleGetKernel(m,"_Z10drain_workPji",&k));
  CNaddr output=s->out; int iterations=500000; void *args[]={&output,&iterations};
  CNqueue queue=use_default ? NULL : s->queue;
  CHECK(cnInvokeKernel(k,1,1,1,CN_KERNEL_CLASS_BLOCK,0,queue,args,NULL));
  CNresult ready=cnQueryQueue(queue); REQUIRE(ready==CN_SUCCESS || ready==CN_ERROR_NOT_READY);
  printf("Driver pending queue default=%d status=%d\n",use_default,(int)ready);
  CNnotifier event; CHECK(cnCreateNotifier(&event,0)); CHECK(cnPlaceNotifier(event,queue));
  CHECK(cnModuleUnload(m)); CHECK(cnCtxSync());
  unsigned got=0,expected=7; CHECK(cnMemcpyDtoH(&got,s->out,4));
  for(int i=0;i<iterations;++i) expected=expected*1664525u+1013904223u;
  REQUIRE(got==expected);
  CHECK(cnWaitNotifier(event)); CHECK(cnDestroyNotifier(event));
  }
  puts("PASS Driver explicit/NULL queue module unload drains finite kernel and preserves its result");
}
int main(int argc,char **argv) {
  REQUIRE(argc==4); CHECK(cnInit(0));
  int count=0; CHECK(cnDeviceGetCount(&count)); REQUIRE(count>0);
  CNdev dev=-1; CHECK(cnDeviceGet(&dev,0)); REQUIRE(dev==0);
  char name[256]; cn_uint64_t bytes=0; CHECK(cnDeviceGetName(name,sizeof(name),dev)); CHECK(cnDeviceTotalMem(&bytes,dev));
  printf("Driver device=%s bytes=%llu count=%d\n",name,(unsigned long long)bytes,count);
  size_t size=0; void *image=read_file(argv[1],&size); (void)size;
  state a={0},b={0}; setup(&a,argv[1],NULL,0,11); setup(&b,argv[1],image,1,101);
  check_state(&b); CHECK(cnCtxSetCurrent(a.ctx)); check_state(&a);
  CNkernel missing=(CNkernel)(uintptr_t)1234;
  REQUIRE(cnModuleGetKernel(a.module,"does_not_exist",&missing)==CN_ERROR_NOT_FOUND);
  REQUIRE(missing==NULL); /* Measured CNDrv clears output on missing symbol. */
  CNmodule absent=(CNmodule)(uintptr_t)1234;
  REQUIRE(cnModuleLoad("/mlu-remote-missing-image.cnfatbin",&absent)==CN_ERROR_FILE_NOT_FOUND);
  REQUIRE(absent==(CNmodule)(uintptr_t)1234);
  if(getenv("MLU_TEST_SHIM")) {
    CHECK(cnCtxSetCurrent(b.ctx));
    REQUIRE(cnQueueSync(a.queue)==CN_QUEUE_ERROR_INVALID);
    REQUIRE(cnModuleUnload(a.module)==CN_ERROR_INVALID_HANDLE);
    REQUIRE(cnFree(a.out)==CN_MEMORY_ERROR_INVALID_ADDRESS);
    REQUIRE(cnModuleGetKernel(a.module,"_Z12module_writei",&missing)==CN_ERROR_INVALID_HANDLE);
    REQUIRE(cnModuleLoadFatBinary("bad image",&absent)==CN_OPS_ERROR_NOT_SUPPORTED);
    REQUIRE(absent==(CNmodule)(uintptr_t)1234);
    CHECK(cnCtxSetCurrent(a.ctx));
  }
  pthread_t workers[2]; REQUIRE(!pthread_create(workers,NULL,thread,&a)); REQUIRE(!pthread_create(workers+1,NULL,thread,&b));
  REQUIRE(!pthread_join(workers[0],NULL)); REQUIRE(!pthread_join(workers[1],NULL));
  CNcontext got=NULL; CHECK(cnCtxGetCurrent(&got)); REQUIRE(got==a.ctx);
  puts("PASS Driver file/fatbinary load, two kernels share globals, two contexts isolate state, TLS bind/unbind");
  abi(argv[2],&a); pending_unload(argv[3],&a);
  CHECK(cnDestroyQueue(a.queue)); CHECK(cnFree(a.out)); CHECK(cnModuleUnload(a.module));
  CHECK(cnCtxDestroy(a.ctx)); CHECK(cnCtxGetCurrent(&got)); REQUIRE(got==NULL);
  CHECK(cnCtxSetCurrent(b.ctx)); check_state(&b);
  /* Leave a pending upload, loaded module, allocation and queue for destroy.
   * The server must acknowledge native cleanup before this call succeeds. */
  int v=78; CHECK(cnMemcpyHtoDAsync(b.out,&v,4,b.queue));
  CHECK(cnCtxDestroy(b.ctx)); CHECK(cnCtxGetCurrent(&got)); REQUIRE(got==NULL);
  if(getenv("MLU_TEST_SHIM")) {
    REQUIRE(cnCtxSetCurrent(b.ctx)==CN_CONTEXT_ERROR_INVALID);
    REQUIRE(cnCtxDestroy(b.ctx)==CN_CONTEXT_ERROR_INVALID);
    CHECK(cnCtxCreate(&got,0,0));
    REQUIRE(cnQueueSync(b.queue)==CN_QUEUE_ERROR_INVALID);
    REQUIRE(cnFree(b.out)==CN_MEMORY_ERROR_INVALID_ADDRESS);
    REQUIRE(cnModuleUnload(b.module)==CN_ERROR_INVALID_HANDLE);
    CHECK(cnCtxDestroy(got));
  }
  free(image); puts("PASS Driver explicit destroy drains resources and invalidates context children");
  return 0;
}
