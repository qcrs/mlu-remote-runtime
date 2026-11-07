#include <cn_api.h>
#include <cnrt.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#define OBS(call) do { int st=(call); printf("%s = %d\n",#call,st); fflush(stdout); } while(0)
static void state(const char *tag) {
  unsigned f=99;int s=99;int r=cnSharedContextGetState(0,&f,&s);
  printf("state %s status=%d flags=%u active=%d\n",tag,r,f,s);
}
int main(int argc,char **argv) {
  if(argc!=2)return 2;setbuf(stdout,NULL);
  if(!strcmp(argv[1],"reshape")) {
    OBS(cnrtSetDevice(0)); unsigned char src[65536],dst[65536];for(int i=0;i<65536;i++)src[i]=(unsigned char)i;
    for(int type=0;type<0x62;type++) {memset(dst,0xee,sizeof(dst));int r=cnrtFilterReshape(dst,src,2,2,3,5,(cnrtDataType_t)type);
      printf("reshape type=%d status=%d bytes=",type,r);for(int i=0;i<128;i++)printf("%02x",dst[i]);puts("");}return 0;
  }
  OBS(cnInit(0));
  if(!strcmp(argv[1],"shared-life")) {
    CNcontext a=0,b=0; CNaddr p=0; CNdev d=99;
    OBS(cnSharedContextAcquire(&a,0));OBS(cnCtxSetCurrent(a));OBS(cnMalloc(&p,4096));OBS(cnSharedContextRelease(0));
    OBS(cnCtxGetCurrent(&b));printf("inactive current same=%d\n",a==b);OBS(cnCtxGetDevice(&d));printf("device=%llu\n",(unsigned long long)d);OBS(cnMemsetD8(p,1,32));OBS(cnMalloc(&p,4096));state("after-malloc");OBS(cnCtxDestroy(a));state("after-destroy");OBS(cnSharedContextAcquire(&b,0));printf("destroy-acquire same=%d\n",a==b);OBS(cnCtxSetCurrent(b));OBS(cnMalloc(&p,4096));OBS(cnCtxDestroy(b));state("active-destroy");OBS(cnSharedContextReset(0));OBS(cnMemsetD8(p,2,32));OBS(cnCtxGetCurrent(&a));printf("after-reset current same=%d\n",a==b);OBS(cnSharedContextRelease(0));return 0;
  }
  if(!strcmp(argv[1],"shared")) {
    CNcontext a=NULL,b=NULL,c=NULL;state("initial");OBS(cnSharedContextSetFlags(0,1));state("flags");
    OBS(cnSharedContextAcquire(&a,0));OBS(cnCtxGetCurrent(&c));printf("acquire changes current=%d\n",c==a);state("one");
    OBS(cnSharedContextAcquire(&b,0));printf("same=%d\n",a==b);OBS(cnSharedContextSetFlags(0,2));
    OBS(cnSharedContextRelease(0));state("release-one");OBS(cnSharedContextRelease(0));state("release-two");
    OBS(cnSharedContextRelease(0));OBS(cnSharedContextAcquire(&a,0));OBS(cnCtxSetCurrent(a));
    OBS(cnSharedContextReset(0));state("reset");OBS(cnCtxGetCurrent(&c));printf("current null=%d\n",c==NULL);
    OBS(cnSharedContextAcquire(&b,0));printf("reacquire same=%d\n",a==b);OBS(cnSharedContextRelease(0));return 0;
  }
  CNcontext c=NULL;OBS(cnCtxCreate(&c,0,0));
  if(!strcmp(argv[1],"vmm")) {
    int supported=0; OBS(cnDeviceGetAttribute(&supported,CN_DEVICE_ATTRIBUTE_VIRTUAL_ADDRESS_MANAGEMENT_SUPPORTED,0)); printf("vmm-supported=%d\n",supported);
    OBS(cnDeviceGetAttribute(&supported,CN_DEVICE_ATTRIBUTE_HANDLE_TYPE_POSIX_FILE_DESCRIPTOR_SUPPORTED,0)); printf("fd-supported=%d\n",supported);
    CNmemAllocationProp prop={.type=CN_MEM_ALLOCATION_TYPE_DEFAULT,.location={CN_MEM_LOCATION_TYPE_DEVICE,0}};
    cn_uint64_t g=0;int st=cnMemGetAllocationGranularity(&g,&prop,CN_MEM_ALLOC_GRANULARITY_MINIMUM); printf("granularity status=%d value=%llu\n",st,(unsigned long long)g);
    if(!g)g=2*1024*1024; CNaddr a=0; CNmemGenericAllocationHandle h=0;
    OBS(cnMemAddressReserve(&a,g,0,0,0));int created=cnMemCreate(&h,g,&prop,0);printf("create=%d\n",created);
    if(!created){ CNmemAllocationProp got;OBS(cnMemGetAllocationPropertiesFromHandle(&got,h));
      if(a){OBS(cnMemMap(a,g,0,h,0));CNmemAccessDesc d={.location=prop.location,.accessFlags=CN_MEM_ACCESS_FLAGS_PROT_READWRITE,.cacheType=CN_MEM_NON_CACHEABLE};OBS(cnMemSetAccess(a,g,&d,1));OBS(cnMemGetAccess(&d,&prop.location,(void*)(uintptr_t)a));OBS(cnSetMemAttribute(1,CN_MEM_ATTRIBUTE_ACCESS_FLAGS,a));OBS(cnSetMemAttribute(0,CN_MEM_ATTRIBUTE_CACHE_CONFIG,a));OBS(cnSetMemRangeAttribute(0,CN_MEM_ATTRIBUTE_CACHE_OPS,a,g)); CNmemGenericAllocationHandle hold=0;OBS(cnMemRetainAllocationHandle(&hold,a));if(hold)OBS(cnMemRelease(hold)); OBS(cnMemUnmap(a,g));} OBS(cnMemRelease(h));}
    if(a)OBS(cnMemAddressFree(a,g)); OBS(cnCtxDestroy(c));return 0;
  }
  if(!strcmp(argv[1],"rtmapping")) {
    OBS(cnrtSetDevice(0));void *p=NULL,*h=NULL;OBS(cnrtMalloc(&p,1048576));int r=cnrtMmap(p,&h,1048576);printf("cnrtMmap=%d\n",r);if(!r)OBS(cnrtMunmap(h,1048576));r=cnrtMmapCached(p,&h,1048576);printf("cnrtMmapCached=%d\n",r);if(!r){OBS(cnrtMcacheOperation(p,h,1048576,CNRT_FLUSH_CACHE));OBS(cnrtMunmap(h,1048576));} OBS(cnrtFree(p));return 0;
  }
  if(!strcmp(argv[1],"mapping")) {
    CNaddr p=0;OBS(cnMalloc(&p,1048576));void *h=NULL;
    int r=cnMmap(p,&h,1048576);printf("cnMmap=%d\n",r);if(!r){memset(h,42,32);unsigned char v[32];OBS(cnMemcpyDtoH(v,p,32));printf("coherent=%d\n",v[0]==42);OBS(cnMunmap(h,1048576));}
    h=NULL;r=cnMmapCached(p,&h,1048576);printf("cnMmapCached=%d\n",r);if(!r){OBS(cnCacheOperation(p,h,1048576,CN_FLUSH_CACHE));OBS(cnMunmap(h,1048576));}
    OBS(cnSetMemAttribute(CN_FLUSH_CACHE,CN_MEM_ATTRIBUTE_CACHE_OPS,p));
    OBS(cnSetMemRangeAttribute(CN_FLUSH_CACHE,CN_MEM_ATTRIBUTE_CACHE_OPS,p,32));
    for(int v=0;v<4;v++) {OBS(cnSetMemAttribute(v,CN_MEM_ATTRIBUTE_ACCESS_FLAGS,p));OBS(cnSetMemAttribute(v,CN_MEM_ATTRIBUTE_CACHE_CONFIG,p));}
    CNaddr q=0,out=0;OBS(cnMalloc(&q,1048576));CNaddr list[2]={p,q};OBS(cnMemMerge(&out,2,list));OBS(cnFree(q));OBS(cnFree(p));
  } else if(!strcmp(argv[1],"atomic")) {
    void *h=NULL;OBS(cnMallocHost(&h,4096));*(uint64_t*)h=7;cn_uint64_t v=0;CNqueue q=NULL;OBS(cnCreateQueue(&q,0));
    OBS(cnAtomicReadOps((uintptr_t)h,&v));printf("value=%llu\n",(unsigned long long)v);
    OBS(cnAtomicOperation((uintptr_t)h,19,0,CN_ATOMIC_OP_REQUEST,CN_ATOMIC_REQUEST_SET));
    OBS(cnQueueAtomicOperation(q,(uintptr_t)h,3,0,CN_ATOMIC_OP_REQUEST,CN_ATOMIC_REQUEST_ADD));
    OBS(cnQueueSync(q));OBS(cnAtomicReadOps((uintptr_t)h,&v));printf("value=%llu\n",(unsigned long long)v);OBS(cnDestroyQueue(q));OBS(cnFreeHost(h));
  } else if(!strcmp(argv[1],"export")) {
    const void *f=NULL;OBS(cnGetExportFunction("cnMalloc",&f));printf("function=%d\n",f!=NULL);OBS(cnGetExportFunction("no_such_function",&f));
  }
  OBS(cnCtxDestroy(c));return 0;
}
