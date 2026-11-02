#include <cn_api.h>
#include <stdio.h>
#include <stdint.h>
#define SHOW(call) printf("%s=%d\n", #call, (int)(call))
#define OK(call) do { CNresult s=(call); if(s) { fprintf(stderr,"%s=%d\n",#call,(int)s); return 1; } } while(0)
int main(int argc,char **argv) {
  int n=-1; CNcontext a=NULL,b=NULL,current=NULL; CNdev dev; CNaddr p=0x1234;
  SHOW(cnDeviceGetCount(&n));
  SHOW(cnCtxGetCurrent(&current));
  SHOW(cnInit(1));
  OK(cnInit(0)); OK(cnDeviceGet(&dev,0));
  SHOW(cnCtxGetCurrent(&current)); printf("initial_context_null=%d\n",current==NULL);
  SHOW(cnMalloc(&p,16)); printf("no_context_output=%llu\n",(unsigned long long)p);
  OK(cnCtxCreate(&a,0,dev)); unsigned flags=999; OK(cnCtxGetFlags(&flags));
  printf("default_flags=%u\n",flags);
  p=0x1234; SHOW(cnMalloc(&p,0)); printf("malloc_zero_output=%llu\n",(unsigned long long)p);
  SHOW(cnFree(0)); int value=7;
  SHOW(cnMemcpyHtoD(0,&value,0)); SHOW(cnMemcpyDtoH(&value,0,0)); SHOW(cnMemcpyDtoD(0,0,0));
  SHOW(cnQueueSync(NULL)); SHOW(cnQueryQueue(NULL));
  char name[32]="sentinel"; cn_uint64_t memory=1234;
  SHOW(cnDeviceGetName(name,sizeof(name),-1));
  printf("invalid_device_name=%s\n",name);
  SHOW(cnDeviceTotalMem(&memory,-1));
  printf("invalid_device_memory=%llu\n",(unsigned long long)memory);
  SHOW(cnDeviceTotalMem(&memory,(CNdev)1<<32));
  printf("wide_invalid_device_memory=%llu\n",(unsigned long long)memory);
  CNcontext invalid=(CNcontext)(uintptr_t)0x1234;
  SHOW(cnCtxCreate(&invalid,0,(CNdev)1<<32));
  printf("wide_context_created=%d\n",invalid!=(CNcontext)(uintptr_t)0x1234);
  if(invalid!=(CNcontext)(uintptr_t)0x1234) { OK(cnCtxDestroy(invalid)); OK(cnCtxSetCurrent(a)); }
  if(argc>1) {
    CNmodule m=NULL; OK(cnModuleLoad(argv[1],&m));
    CNkernel k=(CNkernel)(uintptr_t)0x1234;
    OK(cnModuleGetKernel(m,"_Z12module_writei",&k));
    int seed=7; void *params[]={&seed};
    SHOW(cnInvokeKernel(k,1,1,1,CN_KERNEL_CLASS_BLOCK,0,NULL,params,NULL));
    CNnotifier note=NULL; OK(cnCreateNotifier(&note,0));
    SHOW(cnPlaceNotifier(note,NULL)); SHOW(cnWaitNotifier(note));
    OK(cnDestroyNotifier(note));
    OK(cnCtxSync());
    k=(CNkernel)(uintptr_t)0x1234;
    SHOW(cnModuleGetKernel(m,"missing_symbol",&k));
    printf("missing_kernel_output=%llu\n",(unsigned long long)(uintptr_t)k);
    OK(cnModuleUnload(m));
    m=(CNmodule)(uintptr_t)0x1234;
    SHOW(cnModuleLoad("/mlu-remote-missing-image.cnfatbin",&m));
    printf("missing_file_output=%llu\n",(unsigned long long)(uintptr_t)m);
  }
  OK(cnCtxCreate(&b,CN_CTX_SCHED_SYNC_WAIT,dev));
  OK(cnCtxGetCurrent(&current)); printf("create_b_binds=%d\n",current==b);
  OK(cnCtxDestroy(b));
  SHOW(cnCtxGetCurrent(&current)); printf("destroy_b_current_null=%d\n",current==NULL);
  OK(cnCtxSetCurrent(a)); OK(cnCtxSetCurrent(NULL));
  OK(cnCtxGetCurrent(&current)); printf("unbind_context_null=%d\n",current==NULL);
  OK(cnCtxDestroy(a)); SHOW(cnCtxSetCurrent(a));
  SHOW(cnCtxDestroy(a));
  for(unsigned requested=0;requested<CN_CTX_SCHED_SYNC_NUM;++requested) {
    OK(cnCtxCreate(&a,requested,dev)); OK(cnCtxGetFlags(&flags));
    printf("context_flags_requested=%u actual=%u\n",requested,flags);
    OK(cnCtxDestroy(a));
  }
  return 0;
}
