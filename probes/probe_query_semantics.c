#include <cn_api.h>
#include <cnrt.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define SHOW(e) printf("%s = %d\n",#e,(int)(e))
int main(int argc,char **argv) {
  setvbuf(stdout,NULL,_IOLBF,0);
  CNcontext ctx; CNaddr p; CNqueue q,q2; cnInit(0); cnCtxCreate(&ctx,0,0);
  /* Deliberately opt-in, separate process: this SDK crashes for NULL queue.
   * The remote handler rejects it; ordinary probe and suites never call it. */
  if(argc>1 && !strcmp(argv[1],"--native-null-attribute")) {
    CNqueueAttrValue value={0};
    SHOW(cnQueueGetAttribute(NULL,CN_QUEUE_ATTRIBUTE_ACCESS_POLICY_WINDOW,&value));
    cnCtxDestroy(ctx); return 0;
  }
  cnMalloc(&p,123); cnCreateQueue(&q,0); cnCreateQueue(&q2,0);
  char pci[64]; memset(pci,0xa5,sizeof(pci)); SHOW(cnDeviceGetPCIBusId(pci,64,0)); printf("pci %s\n",pci);
  for(int n=0;n<=14;++n) { memset(pci,0xa5,sizeof(pci)); int st=cnDeviceGetPCIBusId(pci,n,0); printf("pci length %d status %d bytes",n,st); for(int i=0;i<14;++i) printf(" %02x",(unsigned char)pci[i]); puts(""); }
  for(int t=0;t<5;++t) { long long v=-1; int st=cnCtxGetConfig(&v,(CNCtxConfig)t); printf("ctx config %d %d %lld\n",t,st,v); if(!st) SHOW(cnCtxSetConfig((CNCtxConfig)t,v)); }
  unsigned ver=0; SHOW(cnCtxGetApiVersion(ctx,&ver)); printf("api version %u\n",ver);
  for(int k=0;k<7;++k) { int n=-1; int st=cnGetCtxMaxParallelUnionTasks(ctx,(KernelClass)(1<<k),&n); printf("tasks %d %d %d\n",1<<k,st,n); }
  for(int off=0;off<=123;off+=41) { CNaddr base=0; cn_uint64_t bytes=0; int st=cnMemGetAddressRange(&base,&bytes,p+off); printf("range %d %d %d %lu\n",off,st,base==p,(unsigned long)bytes); }
  SHOW(cnMemGetAddressRange(NULL,NULL,p));
  for(int attr=1;attr<=25;++attr) { uint64_t v=0; int st=cnGetMemAttribute(&v,(CNmem_attribute)attr,p+7); printf("mem attr %d %d %lu ptr %d base %d ctx %d\n",attr,st,(unsigned long)((attr==2||attr==3||attr==6||attr==17)?0:v),v==p+7,v==p,v==(uintptr_t)ctx); }
  CNcontext qc=NULL; SHOW(cnQueueGetContext(NULL,&qc)); printf("default ctx matches %d\n",qc==ctx);
  CNqueueAttrValue value={0}; SHOW(cnQueueGetAttribute(q,CN_QUEUE_ATTRIBUTE_ACCESS_POLICY_WINDOW,&value));
  printf("queue attr default %lu %lu %f %d %d\n",(unsigned long)value.accessPolicyWindow.baseAddr,(unsigned long)value.accessPolicyWindow.numBytes,value.accessPolicyWindow.hitRatio,value.accessPolicyWindow.hitProp,value.accessPolicyWindow.missProp);
  value.accessPolicyWindow=(CNaccessPolicyWindow){p,0,0,CN_ACCESS_PROPERTY_NORMAL,CN_ACCESS_PROPERTY_NORMAL};
  SHOW(cnQueueSetAttribute(q,CN_QUEUE_ATTRIBUTE_ACCESS_POLICY_WINDOW,&value)); SHOW(cnQueueCopyAttributes(q2,q)); SHOW(cnQueueGetAttribute(q2,CN_QUEUE_ATTRIBUTE_ACCESS_POLICY_WINDOW,&value));
  printf("queue attr set %d %lu %f %d %d\n",value.accessPolicyWindow.baseAddr==p,(unsigned long)value.accessPolicyWindow.numBytes,value.accessPolicyWindow.hitRatio,value.accessPolicyWindow.hitProp,value.accessPolicyWindow.missProp);
  SHOW(cnCtxResetPersistingL2Cache());
  cnDestroyQueue(q); cnDestroyQueue(q2); cnFree(p); cnCtxDestroy(ctx);
  cnrtSetDevice(0); void *rp; cnrtMalloc(&rp,123);
  for(int t=0;t<5;++t) { int64_t v=-1; int st=cnrtDeviceGetConfig(&v,(cnrtDeviceConfig_t)t); printf("runtime config %d %d %ld\n",t,st,(long)v); if(!st) SHOW(cnrtDeviceSetConfig((cnrtDeviceConfig_t)t,v)); }
  cnrtPointerAttributes_t a={0}; SHOW(cnrtPointerGetAttributes(&a,(char *)rp+7));
  printf("runtime ptr %d %d %lu dev %d host %d cache %d base %d\n",a.type,a.device,(unsigned long)a.size,a.devicePointer==(char *)rp+7,a.hostPointer==NULL,a.cacheMode,a.deviceBasePointer==rp);
  cnrtQueue_t rq; cnrtQueueCreate(&rq); cnrtQueueAttrValue_t rv={0}; SHOW(cnrtQueueGetAttribute(rq,cnrtQueueAttributeAccessPolicyWindow,&rv));
  rv.accessPolicyWindow=(cnrtAccessPolicyWindow_t){rp,64,.5,cnrtAccessPolicyPersisting,cnrtAccessPolicyNormal}; SHOW(cnrtQueueSetAttribute(rq,cnrtQueueAttributeAccessPolicyWindow,&rv));
  SHOW(cnrtDeviceResetPersistingL2Cache()); cnrtQueueDestroy(rq); cnrtFree(rp);
  return 0;
}
