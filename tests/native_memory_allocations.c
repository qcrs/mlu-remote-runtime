#include <cn_api.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define OK(e) do { int st=(e); if(st) {fprintf(stderr,"line %d %s: %d\n",__LINE__,#e,st);exit(2);} }while(0)
#define CHECK(e) do {if(!(e)){fprintf(stderr,"line %d %s\n",__LINE__,#e);exit(3);}}while(0)
static CNresult allocate(int kind,CNaddr *p,size_t n,int node) {
  switch(kind) {
    case 1:return cnMallocNode(p,n,node);
    case 2:return cnZmalloc(p,n);
    case 3:return cnZmallocNode(p,n,node);
    case 4:return cnMallocConstant(p,n);
    case 5:return cnMallocNodeConstant(p,n,node);
    case 6:return cnMallocSecurity(p,n);
    default:return cnMallocFrameBuffer(p,n,node);
  }
}
int main(void) {
  OK(cnInit(0)); CNdev dev;OK(cnDeviceGet(&dev,0));CNcontext context;OK(cnCtxCreate(&context,0,dev));
  CNaddr addresses[7];unsigned char source[8192],result[8192];
  for(size_t i=0;i<sizeof(source);++i)source[i]=(i*13+7)%251;
  for(int k=1;k<=7;++k) {
    CNaddr untouched=123; OK(allocate(k,&untouched,0,0));CHECK(untouched==123);
    OK(allocate(k,&addresses[k-1],sizeof(source),0));CNaddr p=addresses[k-1];
    if(k==2||k==3) { memset(result,1,sizeof(result));OK(cnMemcpyDtoH(result,p,sizeof(result)));for(size_t i=0;i<sizeof(result);++i)CHECK(!result[i]); }
    OK(cnMemsetD8(p,0xa5,sizeof(source)));OK(cnMemcpyHtoD(p+8,source+8,sizeof(source)-16));
    OK(cnMemcpyDtoH(result,p,sizeof(result)));
    for(size_t i=0;i<sizeof(result);++i)CHECK(result[i]==(i<8||i>=sizeof(result)-8?0xa5:source[i]));
    CNaddr base=0;cn_uint64_t size=0;OK(cnMemGetAddressRange(&base,&size,p+17));CHECK(base==p&&size==sizeof(source));
    CNcontext owner=NULL;OK(cnGetMemAttribute(&owner,CN_MEM_ATTRIBUTE_CONTEXT,p));CHECK(owner==context);
    printf("PASS allocation kind=%d zero-output/offset/owner/range/data%s\n",k,(k==2||k==3)?"/zero-fill":"");
  }
  CNaddr unified=addresses[0]; unsigned char h2[128],h3[128]; memset(h2,0x5c,sizeof(h2));
  OK(cnMemcpy(unified,(CNaddr)h2,sizeof(h2))); memset(h3,0,sizeof(h3)); OK(cnMemcpy((CNaddr)h3,unified,sizeof(h3))); CHECK(!memcmp(h2,h3,sizeof(h2)));
  OK(cnMemcpy(unified+32,unified,64)); OK(cnMemcpy((CNaddr)h3,unified+32,64)); CHECK(!memcmp(h2,h3,64));
  OK(cnMemcpy((CNaddr)h3,(CNaddr)h2,64)); printf("PASS cnMemcpy unified H2D/D2H/D2D/H2H\n");
  for(int k=1;k<=7;k+=2)if(k!=5&&k!=7) { /* node and zero-node variants */
    CNaddr p=123;int st=allocate(k,&p,0,999);CHECK(st!=0&&p==123);printf("invalid node kind=%d status=%d\n",k,st);
  }
  for(int k=0;k<7;++k){OK(cnFree(addresses[k]));CNaddr base=0;cn_uint64_t size=0;CHECK(cnMemGetAddressRange(&base,&size,addresses[k])!=0);}
  OK(cnCtxDestroy(context));puts("PASS memory allocation family: 7 simultaneous kinds; stale invalidation");
  return 0;
}
