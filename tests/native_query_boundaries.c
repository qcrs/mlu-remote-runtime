#include <cn_api.h>
#include <cnrt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(e) do { if(!(e)) { fprintf(stderr,"line %d: %s\n",__LINE__,#e); exit(2); } } while(0)
#define OK(e) CHECK((e)==0)
static void pci(int runtime) {
  for(int len=13;len<=64;len+=(len==13 ? 1 : 50)) {
    char text[80]; memset(text,0x5a,sizeof(text));
    if(runtime) OK(cnrtDeviceGetPCIBusId(text,len,0)); else OK(cnDeviceGetPCIBusId(text,len,0));
    CHECK(strlen(text)==12 && text[13]==0x5a); printf("PCI %d %s\n",len,text);
    char *forms[]={text,text+5};
    for(unsigned f=0;f<2;++f) {
      if(runtime) { int dev=-1; OK(cnrtDeviceGetByPCIBusId(&dev,forms[f])); CHECK(dev==0); }
      else { CNdev dev=99; OK(cnDeviceGetByPCIBusId(&dev,forms[f])); CHECK(dev==0); }
    }
  }
  char bad[16]; memset(bad,0x5a,sizeof(bad));
  int st=runtime ? cnrtDeviceGetPCIBusId(bad,11,0) : cnDeviceGetPCIBusId(bad,11,0);
  CHECK(st && bad[0]==0x5a); printf("PCI short %d\n",st);
  if(runtime) { int dev=99; st=cnrtDeviceGetByPCIBusId(&dev,"invalid"); CHECK(st); printf("invalid PCI device %lu\n",(unsigned long)dev); }
  else { CNdev dev=99; st=cnDeviceGetByPCIBusId(&dev,"invalid"); CHECK(st); printf("invalid PCI device %lu\n",(unsigned long)dev); }
  printf("PCI invalid %d\n",st);
}
static void driver(void) {
  OK(cnInit(0)); CNcontext ctx,other; OK(cnCtxCreate(&ctx,0,0)); pci(0);
  unsigned v=0; OK(cnCtxGetApiVersion(ctx,&v)); printf("context API %u\n",v);
  v=0x5a; int st=cnCtxGetApiVersion(NULL,&v); CHECK(st && v==0x5a); printf("context NULL version %d\n",st);
  for(int i=0;i<=4;++i) {
    long long value=-1; st=cnCtxGetConfig(&value,(CNCtxConfig)i); printf("context config %d %d %lld\n",i,st,value);
    if(!st) OK(cnCtxSetConfig((CNCtxConfig)i,value));
  }
  long long old; OK(cnCtxGetConfig(&old,CN_CTX_CONFIG_PRINTF_MAX_NUM));
  OK(cnCtxSetConfig(CN_CTX_CONFIG_PRINTF_MAX_NUM,2048)); OK(cnCtxGetConfig(&old,CN_CTX_CONFIG_PRINTF_MAX_NUM)); CHECK(old==2048);
  OK(cnCtxSetConfig(CN_CTX_CONFIG_PRINTF_MAX_NUM,1024));
  for(int klass=1;klass<=64;klass*=2) { int count=-1; st=cnGetCtxMaxParallelUnionTasks(ctx,(KernelClass)klass,&count); printf("context tasks %d %d %d\n",klass,st,count); }
  cn_uint64_t free,total; OK(cnMemGetNodeInfo(&free,&total,0)); CHECK(free>0 && free<=total);
  OK(cnMemGetNodeInfo(NULL,&total,0)); OK(cnMemGetNodeInfo(&free,NULL,0)); OK(cnMemGetNodeInfo(NULL,NULL,0));
  CHECK(cnMemGetNodeInfo(&free,&total,99)!=0); puts("node memory invariants and optional outputs PASS");
  CNaddr p,base; cn_uint64_t size; OK(cnMalloc(&p,123));
  for(unsigned off=0;off<123;off+=41) { OK(cnMemGetAddressRange(&base,&size,p+off)); CHECK(base==p && size==123); }
  OK(cnMemGetAddressRange(NULL,NULL,p)); OK(cnMemGetAddressRange(&base,NULL,p+1)); OK(cnMemGetAddressRange(NULL,&size,p+122));
  CHECK(cnMemGetAddressRange(&base,&size,p+123)!=0);
  unsigned attrs[]={1,2,3,4,6,7,17,18,21,22,23,24};
  for(unsigned i=0;i<sizeof(attrs)/sizeof(attrs[0]);++i) {
    unsigned attr=attrs[i],width=(attr==2||attr==3||attr==6||attr==17||attr==18) ? 8 : attr==22 ? 1 : 4;
    union { uint64_t u; unsigned char bytes[8]; } got; got.u=UINT64_C(0x5a5a5a5a5a5a5a5a);
    OK(cnGetMemAttribute(&got,(CNmem_attribute)attr,p+7));
    for(unsigned j=width;j<8;++j) CHECK(got.bytes[j]==0x5a);
    uint64_t val=0; memcpy(&val,&got,width);
    if(attr==2) CHECK(val==p+7);
    if(attr==3) CHECK(!val);
    if(attr==6) CHECK(val==(uintptr_t)ctx);
    if(attr==17) CHECK(val==p);
    if(attr==18) CHECK(val==123);
    printf("memory attr %u width %u value %lu\n",attr,width,(unsigned long)((attr==2||attr==6||attr==17) ? 1 : val));
  }
  CNmem_attribute aa[]={CN_MEM_ATTRIBUTE_TYPE,CN_MEM_ATTRIBUTE_RANGE_SIZE,CN_MEM_ATTRIBUTE_CONTEXT};
  int type=0; size=0; CNcontext owner=NULL; void *values[]={&type,&size,&owner};
  OK(cnGetMemAttributes(3,aa,values,p+7)); CHECK(type==CN_MEMORYTYPE_DEVICE && size==123 && owner==ctx);
  type=99; size=99; CNmem_attribute invalid[]={CN_MEM_ATTRIBUTE_TYPE,(CNmem_attribute)999,CN_MEM_ATTRIBUTE_RANGE_SIZE};
  void *partial[]={&type,&size,&size}; st=cnGetMemAttributes(3,invalid,partial,p); printf("batch partial %d type %d size %lu\n",st,type,(unsigned long)size);
  CHECK(!st && type==2 && size==123); CHECK(cnGetMemAttributes(0,NULL,NULL,p)!=0);
  CHECK(cnGetMemAttributes(1,invalid+1,partial+1,p)!=0);
  int count=-1; CHECK(cnGetCtxMaxParallelUnionTasks(NULL,1,&count)!=0 && count==-1);
  CNqueue q,q2; OK(cnCreateQueue(&q,0)); OK(cnCreateQueue(&q2,0));
  OK(cnQueueGetContext(NULL,&owner)); CHECK(owner==ctx); OK(cnQueueGetContext(q,&owner)); CHECK(owner==ctx);
  CNqueueAttrValue policy={0}; OK(cnQueueGetAttribute(q,1,&policy)); CHECK(!policy.accessPolicyWindow.baseAddr && !policy.accessPolicyWindow.numBytes);
  policy.accessPolicyWindow=(CNaccessPolicyWindow){p+16,0,.5,2,0}; OK(cnQueueSetAttribute(q,1,&policy));
  OK(cnQueueCopyAttributes(q2,q)); memset(&policy,0,sizeof(policy)); OK(cnQueueGetAttribute(q2,1,&policy));
  CHECK(policy.accessPolicyWindow.baseAddr==p+16 && policy.accessPolicyWindow.numBytes==0 && policy.accessPolicyWindow.hitRatio==.5 && policy.accessPolicyWindow.hitProp==2);
  policy.accessPolicyWindow.numBytes=16; st=cnQueueSetAttribute(q,1,&policy); CHECK(st); printf("queue nonzero window %d\n",st);
  st=cnQueueGetAttribute(q,999,&policy); CHECK(st); printf("queue invalid attr %d\n",st);
  OK(cnCtxCreate(&other,0,0)); OK(cnQueueGetContext(q,&owner)); CHECK(owner==ctx);
  OK(cnMemGetAddressRange(&base,&size,p+1)); CHECK(base==p && size==123);
  OK(cnGetMemAttribute(&owner,CN_MEM_ATTRIBUTE_CONTEXT,p)); CHECK(owner==ctx);
  long long config; OK(cnCtxGetConfig(&config,1)); CHECK(config==1024);
  OK(cnCtxGetApiVersion(ctx,&v)); OK(cnGetCtxMaxParallelUnionTasks(ctx,CN_KERNEL_CLASS_BLOCK,&type)); CHECK(type>0);
  OK(cnCtxSetCurrent(NULL)); OK(cnQueueGetContext(q,&owner)); CHECK(owner==ctx);
  CHECK(cnMemGetAddressRange(&base,&size,p)==CN_CONTEXT_ERROR_INVALID);
  CHECK(cnGetMemAttribute(&owner,CN_MEM_ATTRIBUTE_CONTEXT,p)==CN_CONTEXT_ERROR_INVALID);
  OK(cnCtxDestroy(other)); OK(cnCtxSetCurrent(ctx)); OK(cnQueueSync(NULL)); OK(cnQueryQueue(NULL)); OK(cnCtxResetPersistingL2Cache());
  OK(cnDestroyQueue(q)); OK(cnDestroyQueue(q2)); OK(cnFree(p));
  CHECK(cnMemGetAddressRange(&base,&size,p)!=0); OK(cnCtxDestroy(ctx)); puts("PASS driver Context/PCI/memory/queue boundaries");
}
static void runtime(void) {
  OK(cnrtSetDevice(0)); pci(1);
  for(int i=0;i<4;++i) { int64_t value=-1; int st=cnrtDeviceGetConfig(&value,(cnrtDeviceConfig_t)i); printf("runtime config %d %d %ld\n",i,st,(long)value); if(!st) OK(cnrtDeviceSetConfig((cnrtDeviceConfig_t)i,value)); }
  OK(cnrtDeviceSetConfig(cnrtDeviceConfigPrintfFifoNum,2048)); int64_t value=0; OK(cnrtDeviceGetConfig(&value,cnrtDeviceConfigPrintfFifoNum)); CHECK(value==2048); OK(cnrtDeviceSetConfig(cnrtDeviceConfigPrintfFifoNum,1024));
  void *p; OK(cnrtMalloc(&p,123));
  for(unsigned off=0;off<123;off+=41) { cnrtPointerAttributes_t a={0}; OK(cnrtPointerGetAttributes(&a,(char *)p+off)); CHECK(a.type==cnrtMemTypeDevice && a.device==0 && a.size==123 && a.devicePointer==(char *)p+off && !a.hostPointer && a.deviceBasePointer==p); printf("runtime pointer %u type %d size %lu cache %d\n",off,a.type,(unsigned long)a.size,a.cacheMode); }
  cnrtQueue_t q,q2; OK(cnrtQueueCreate(&q)); OK(cnrtQueueCreate(&q2)); cnrtQueueAttrValue_t policy={0};
  OK(cnrtQueueGetAttribute(q,cnrtQueueAttributeAccessPolicyWindow,&policy)); CHECK(!policy.accessPolicyWindow.baseAddr);
  policy.accessPolicyWindow=(cnrtAccessPolicyWindow_t){(char *)p+16,0,.5,cnrtAccessPolicyPersisting,cnrtAccessPolicyNormal}; OK(cnrtQueueSetAttribute(q,1,&policy));
  OK(cnrtQueueCopyAttributes(q2,q)); memset(&policy,0,sizeof(policy)); OK(cnrtQueueGetAttribute(q2,1,&policy)); CHECK(policy.accessPolicyWindow.baseAddr==(char *)p+16 && policy.accessPolicyWindow.hitRatio==.5);
  policy.accessPolicyWindow.numBytes=16; int st=cnrtQueueSetAttribute(q,1,&policy); CHECK(st); printf("runtime nonzero window %d\n",st);
  OK(cnrtDeviceResetPersistingL2Cache()); OK(cnrtSyncDevice()); OK(cnrtQueueDestroy(q)); OK(cnrtQueueDestroy(q2)); OK(cnrtFree(p)); puts("PASS runtime Context/PCI/memory/queue boundaries");
}
int main(int argc,char **argv) { if(argc>1 && !strcmp(argv[1],"driver")) driver(); else runtime(); return 0; }
