#include <cn_api.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#define OK(x) do {CNresult s=(x);if(s){printf("FAIL %s=%d line=%d\n",#x,s,__LINE__);return 1;}}while(0)
#define CHECK(x) do{if(!(x)){printf("FAIL %s line=%d\n",#x,__LINE__);return 1;}}while(0)
int main(void) {
 OK(cnInit(0));CNcontext c;OK(cnCtxCreate(&c,0,0));
 CNmemAllocationProp p={.type=CN_MEM_ALLOCATION_TYPE_DEFAULT,.location={CN_MEM_LOCATION_TYPE_DEVICE,0}},got;
 cn_uint64_t g;OK(cnMemGetAllocationGranularity(&g,&p,CN_MEM_ALLOC_GRANULARITY_MINIMUM));printf("granularity=%llu\n",(unsigned long long)g);
 CNaddr r;CNmemGenericAllocationHandle h,retained;OK(cnMemAddressReserve(&r,2*g,0,0,0));OK(cnMemCreate(&h,g,&p,0));OK(cnMemGetAllocationPropertiesFromHandle(&got,h));CHECK(got.type==p.type && got.location.id==0);
 CNmemGenericAllocationHandle large;OK(cnMemCreate(&large,2*g,&p,0));int offset_status=cnMemMap(r,g,g,large,0);printf("nonzero_backing_offset=%d\n",offset_status);CHECK(offset_status==CN_OPS_ERROR_NOT_SUPPORTED);OK(cnMemRelease(large));
 CNmemAccessDesc d={.location=p.location,.accessFlags=CN_MEM_ACCESS_FLAGS_PROT_READWRITE,.cacheType=CN_MEM_NON_CACHEABLE};
 OK(cnMemMap(r,g,0,h,0));OK(cnMemMap(r+g,g,0,h,0));OK(cnMemSetAccess(r,g,&d,1));OK(cnMemSetAccess(r+g,g,&d,1));OK(cnMemGetAccess(&d,&p.location,(void*)(uintptr_t)(r+64)));CHECK(d.accessFlags==CN_MEM_ACCESS_FLAGS_PROT_READWRITE);
 OK(cnMemsetD8(r,37,256));unsigned char dst[256];OK(cnMemcpyDtoH(dst,r+g,256));for(int i=0;i<256;i++)CHECK(dst[i]==37);
 OK(cnMemRetainAllocationHandle(&retained,r+17));CHECK(retained==h);OK(cnMemRelease(h));OK(cnMemRelease(retained));OK(cnMemRetainAllocationHandle(&retained,r+g+32));CHECK(retained==h);
 OK(cnMemUnmap(r,g));OK(cnMemMap(r,g,0,retained,0));OK(cnMemSetAccess(r,g,&d,1));OK(cnMemsetD8(r,71,256));OK(cnMemcpyDtoH(dst,r,256));CHECK(dst[0]==71);
 OK(cnMemUnmap(r+g,g));OK(cnMemRelease(retained));OK(cnMemUnmap(r,g));OK(cnMemAddressFree(r,2*g));
 /* Destroy with live views and zero handle references must clean up exactly once. */
 OK(cnMemCreate(&h,g,&p,0));OK(cnMemAddressReserve(&r,g,0,0,0));OK(cnMemMap(r,g,0,h,0));OK(cnMemSetAccess(r,g,&d,1));OK(cnMemRelease(h));OK(cnCtxDestroy(c));
 puts("PASS VMM aliases, access, copies, retain, remap and context cleanup");return 0;
}
