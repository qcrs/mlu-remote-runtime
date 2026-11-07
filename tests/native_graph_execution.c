#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef TEST_CNRT
#include <cnrt.h>
#define F(n) cnrtTaskTopo##n
typedef cnrtTaskTopo_t Graph;
typedef cnrtTaskTopoNode_t Node;
typedef cnrtTaskTopoEntity_t Entity;
typedef cnrtTaskTopoEntityUpdateResult_t Update;
typedef cnrtMemsetParams_t Params;
typedef cnrtMemcpy3dParam_t CopyParams;
typedef void *Address;
typedef cnrtQueue_t Queue;
#define ALLOC(p,n) cnrtMalloc(p,n)
#define FREE(p) cnrtFree(p)
#define READ(p,a,n) cnrtMemcpy(p,a,n,cnrtMemcpyDevToHost)
#define WRITE(a,p,n) cnrtMemcpy(a,p,n,cnrtMemcpyHostToDev)
#define QCREATE(p) cnrtQueueCreate(p)
#define QSYNC(q) cnrtQueueSync(q)
#define QDESTROY(q) cnrtQueueDestroy(q)
#else
#include <cn_api.h>
#define F(n) cnTaskTopo##n
typedef CNtaskTopo Graph;
typedef CNtaskTopoNode Node;
typedef CNtaskTopoEntity Entity;
typedef CNtaskTopoEntityUpdateResult Update;
typedef CN_MEMSET_NODE_PARAMS Params;
typedef CNmemcpy3dParam CopyParams;
typedef CNaddr Address;
typedef CNqueue Queue;
#define ALLOC(p,n) cnMalloc(p,n)
#define FREE(p) cnFree(p)
#define READ(p,a,n) cnMemcpyDtoH(p,a,n)
#define WRITE(a,p,n) cnMemcpyHtoD(a,p,n)
#define QCREATE(p) cnCreateQueue(p,0)
#define QSYNC(q) cnQueueSync(q)
#define QDESTROY(q) cnDestroyQueue(q)
#endif
#define OK(c) do { int s=(int)(c); if(s) { fprintf(stderr,"%s=%d line=%d\n",#c,s,__LINE__); exit(1); } } while(0)
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"failed %s line=%d\n",#c,__LINE__); exit(2); } } while(0)
static Params params(Address a,unsigned slot,unsigned value) {
  Params p={0}; p.dst=(Address)((uintptr_t)a+slot*64); p.pitch=777;
  p.value=value; p.elementSize=1u<<slot; p.width=8; p.height=1; return p;
}
static void dag(Graph *g,Node n[4],Address a,unsigned value) {
  OK(F(Create)(g,0));
  for(unsigned i=0;i<3;i++) {
    Params p=params(a,i,value+i);
    OK(F(AddMemsetNode)(&n[i],*g,i ? n : NULL,i ? 1 : 0,&p));
    Params got={0}; OK(F(MemsetNodeGetParams)(n[i],&got));
    CHECK(got.dst==p.dst && got.pitch==p.pitch && got.value==p.value &&
          got.elementSize==p.elementSize && got.width==p.width && got.height==1);
  }
  OK(F(AddEmptyNode)(&n[3],*g,n+1,2));
}
static void values(Address a,unsigned x,unsigned y,unsigned z) {
  unsigned char b[256]; OK(READ(b,a,sizeof(b)));
  for(unsigned i=0;i<8;i++) { unsigned short v; unsigned w;
    memcpy(&v,b+64+2*i,2); memcpy(&w,b+128+4*i,4);
    CHECK(b[i]==(unsigned char)x && v==(unsigned short)y && w==z);
  }
}
static void children(Address a,Queue q,Queue q2) {
  Graph source,parent,inside,clone,cloned_inside,updated; Node n[4],m[4],child,cloned_child,inner;
  Entity e,ec; size_t count=0;
  dag(&source,n,a,10); OK(F(Create)(&parent,0));
  OK(F(AddChildTopoNode)(&child,parent,NULL,0,source));
  OK(F(ChildTopoNodeGetTopo)(child,&inside)); CHECK(inside!=source);
  OK(F(NodeFindInClone)(&inner,n[0],inside));
  OK(F(GetNodes)(inside,NULL,&count)); CHECK(count==4);
  OK(F(Instantiate)(&e,parent,NULL,NULL,0));
  Params p=params(a,0,20); OK(F(MemsetNodeSetParams)(inner,&p));
  OK(F(Clone)(&clone,parent)); OK(F(NodeFindInClone)(&cloned_child,child,clone));
  OK(F(ChildTopoNodeGetTopo)(cloned_child,&cloned_inside)); CHECK(cloned_inside!=inside);
  OK(F(Instantiate)(&ec,clone,NULL,NULL,0));
  OK(F(Destroy)(source));
  OK(F(EntityInvoke)(e,q)); OK(QSYNC(q)); values(a,10,11,12);
  OK(F(EntityInvoke)(ec,q2)); OK(QSYNC(q2)); values(a,20,11,12);
  dag(&updated,m,a,30);
  OK(F(EntityChildTopoNodeSetParams)(e,child,updated));
  OK(F(EntityInvoke)(e,q)); OK(QSYNC(q)); values(a,30,31,32);
  OK(F(Destroy)(updated));
  /* Both the source and the embedded graph can disappear before replay. */
  OK(F(Destroy)(parent)); OK(F(Destroy)(clone));
  OK(F(EntityInvoke)(ec,q)); OK(F(EntityDestroy)(ec)); OK(QSYNC(q)); values(a,20,11,12);
  OK(F(EntityInvoke)(e,q2)); OK(F(EntityDestroy)(e)); OK(QSYNC(q2)); values(a,30,31,32);
  puts("PASS child graph ownership/edit/clone/entity replacement");
  /* A nested child creates another independent identity/lease subtree. */
  dag(&source,n,a,40); OK(F(Create)(&parent,0));
  OK(F(AddChildTopoNode)(&child,parent,NULL,0,source));
  OK(F(Create)(&clone,0)); OK(F(AddChildTopoNode)(&cloned_child,clone,NULL,0,parent));
  OK(F(Instantiate)(&e,clone,NULL,NULL,0));
  OK(F(Destroy)(source)); OK(F(Destroy)(parent)); OK(F(Destroy)(clone));
  OK(F(EntityInvoke)(e,q)); OK(F(EntityDestroy)(e)); OK(QSYNC(q)); values(a,40,41,42);
  puts("PASS nested child snapshot leases");
}
static CopyParams copy_params(Address dst,unsigned doff,Address src,unsigned soff,unsigned bytes) {
  CopyParams p={0}; p.dst=(void *)((uintptr_t)dst+doff); p.src=(void *)((uintptr_t)src+soff);
  p.dstPtr.pitch=p.srcPtr.pitch=bytes; p.dstPtr.ysize=p.srcPtr.ysize=1;
  p.extent.width=bytes; p.extent.height=p.extent.depth=1; return p;
}
static void copy_get(Node node,const CopyParams *p) {
  CopyParams got={0}; OK(F(MemcpyNodeGetParams)(node,&got));
  CHECK(got.dst==p->dst && got.src==p->src && got.extent.width==p->extent.width);
  CHECK(got.dstPtr.pitch==p->dstPtr.pitch && got.srcPtr.pitch==p->srcPtr.pitch &&
        got.dstPtr.ysize==1 && got.srcPtr.ysize==1 && got.extent.height==1 && got.extent.depth==1 && got.dir==p->dir);
}
static void copy_values(Address dst,unsigned doff,const unsigned char *src,unsigned soff,unsigned bytes,unsigned char *expected) {
  unsigned char got[512]; OK(READ(got,dst,sizeof(got)));
  memcpy(expected+doff,src+soff,bytes);
  CHECK(!memcmp(got,expected,sizeof(got)));
}
static void copies(Queue q,Queue q2) {
  Address src,dst; unsigned char bytes[512],expected[512];
  for(unsigned i=0;i<sizeof(bytes);i++) bytes[i]=(unsigned char)(i*37+11);
  memset(expected,0xa6,sizeof(expected));
  OK(ALLOC(&src,sizeof(bytes))); OK(ALLOC(&dst,sizeof(bytes))); OK(WRITE(src,bytes,sizeof(bytes)));
  OK(WRITE(dst,expected,sizeof(expected)));
  Graph g,clone,h,parent,inside; Node n,c,m,child,inner; Entity e,ec,en; Update result; Node error=NULL;
  OK(F(Create)(&g,0)); CopyParams p=copy_params(dst,32,src,16,64);
  OK(F(AddMemcpyNode)(&n,g,NULL,0,&p)); copy_get(n,&p);
  OK(F(Clone)(&clone,g)); OK(F(NodeFindInClone)(&c,n,clone));
  OK(F(Instantiate)(&e,g,NULL,NULL,0));
  p=copy_params(dst,112,src,96,64); OK(F(MemcpyNodeSetParams)(c,&p)); copy_get(c,&p);
  OK(F(Instantiate)(&ec,clone,NULL,NULL,0));
  p=copy_params(dst,192,src,176,64); p.dir=5; OK(F(MemcpyNodeSetParams)(n,&p)); copy_get(n,&p);
  OK(F(Instantiate)(&en,g,NULL,NULL,0));
  OK(F(EntityInvoke)(e,q)); OK(QSYNC(q)); copy_values(dst,32,bytes,16,64,expected);
  OK(F(EntityInvoke)(ec,q2)); OK(QSYNC(q2)); copy_values(dst,112,bytes,96,64,expected);
  OK(F(EntityInvoke)(en,q)); OK(QSYNC(q)); copy_values(dst,192,bytes,176,64,expected);
  OK(F(EntityDestroy)(en));
  p=copy_params(dst,272,src,240,64); OK(F(EntityMemcpyNodeSetParams)(e,n,&p));
  OK(F(EntityInvoke)(e,q)); OK(QSYNC(q)); copy_values(dst,272,bytes,240,64,expected);
  for(unsigned i=0;i<sizeof(bytes);i++) bytes[i]^=0xa5;
  OK(WRITE(src,bytes,sizeof(bytes)));
  OK(F(EntityInvoke)(e,q2)); OK(QSYNC(q2)); copy_values(dst,272,bytes,240,64,expected);
  OK(F(Create)(&h,0)); p=copy_params(dst,352,src,320,64);
  OK(F(AddMemcpyNode)(&m,h,NULL,0,&p));
  OK(F(EntityUpdate)(e,h,&error,&result)); CHECK(result==0 && !error);
  OK(F(EntityInvoke)(e,q)); OK(QSYNC(q)); copy_values(dst,352,bytes,320,64,expected);
  p=copy_params(dst,432,src,400,64); OK(F(EntityMemcpyNodeSetParams)(e,n,&p));
  OK(F(EntityInvoke)(e,q)); OK(QSYNC(q)); copy_values(dst,432,bytes,400,64,expected);
  OK(F(Destroy)(g)); OK(F(Destroy)(h)); OK(F(Destroy)(clone));
  OK(F(EntityInvoke)(ec,q)); OK(F(EntityDestroy)(ec)); OK(QSYNC(q)); copy_values(dst,112,bytes,96,64,expected);
  OK(F(EntityInvoke)(e,q2)); OK(F(EntityDestroy)(e)); OK(QSYNC(q2)); copy_values(dst,432,bytes,400,64,expected);
  puts("PASS graph memcpy get/set/clone/snapshot/update/current bytes/pending destroy");
  /* Non-overlapping endpoints in the same allocation own two balanced leases. */
  OK(F(Create)(&g,0)); p=copy_params(src,128,src,0,64); OK(F(AddMemcpyNode)(&n,g,NULL,0,&p));
  OK(F(Create)(&parent,0)); OK(F(AddChildTopoNode)(&child,parent,NULL,0,g));
  OK(F(ChildTopoNodeGetTopo)(child,&inside)); OK(F(NodeFindInClone)(&inner,n,inside)); copy_get(inner,&p);
  OK(F(Instantiate)(&e,parent,NULL,NULL,0)); OK(F(Destroy)(g)); OK(F(Destroy)(parent));
  OK(F(EntityInvoke)(e,q)); OK(F(EntityDestroy)(e)); OK(QSYNC(q)); copy_values(src,128,bytes,0,64,bytes);
  puts("PASS child memcpy same-allocation endpoints");
  if(getenv("MLU_TEST_SHIM")) {
    OK(F(Create)(&g,0)); p=copy_params(dst,32,src,16,64); OK(F(AddMemcpyNode)(&n,g,NULL,0,&p));
    OK(F(Clone)(&clone,g)); OK(F(NodeFindInClone)(&c,n,clone)); OK(F(Instantiate)(&e,g,NULL,NULL,0));
    OK(F(EntityInvoke)(e,q)); OK(FREE(src)); src=0; OK(FREE(dst)); dst=0;
    copy_get(n,&p); copy_get(c,&p);
    OK(F(Instantiate)(&ec,clone,NULL,NULL,0));
    OK(F(Destroy)(g)); OK(F(Destroy)(clone)); OK(F(EntityDestroy)(e));
    OK(F(EntityInvoke)(ec,q2)); OK(F(EntityDestroy)(ec)); OK(QSYNC(q)); OK(QSYNC(q2));
    /* Keep safety-only output out of the Native comparison. */
  }
  if(src) OK(FREE(src));
  if(dst) OK(FREE(dst));
}
int main(int argc,char **argv) {
#ifdef TEST_CNRT
  OK(cnrtSetDevice(0));
#else
  CNcontext ctx; OK(cnInit(0)); OK(cnCtxCreate(&ctx,0,0));
#endif
  Address a; Queue q,q2; OK(ALLOC(&a,256)); OK(QCREATE(&q)); OK(QCREATE(&q2));
  Graph g,h,clone; Node n[4],m[4],c; Entity e,ec; Update result; Node error=NULL;
  dag(&g,n,a,1); OK(F(Clone)(&clone,g)); OK(F(NodeFindInClone)(&c,n[0],clone));
  OK(F(Instantiate)(&e,g,NULL,NULL,0)); OK(F(Instantiate)(&ec,clone,NULL,NULL,0));
  Params p=params(a,0,9); OK(F(MemsetNodeSetParams)(n[0],&p));
  Params got; OK(F(MemsetNodeGetParams)(c,&got)); CHECK(got.value==1);
  OK(F(EntityInvoke)(e,q)); OK(QSYNC(q)); values(a,1,2,3);
  p=params(a,1,0x3456); OK(F(EntityMemsetNodeSetParams)(e,n[1],&p));
  OK(F(EntityInvoke)(e,q2)); OK(QSYNC(q2)); values(a,1,0x3456,3);
  OK(F(Destroy)(clone)); OK(F(EntityInvoke)(ec,q)); OK(QSYNC(q)); values(a,1,2,3);
  OK(F(EntityDestroy)(ec));
  dag(&h,m,a,5); OK(F(EntityUpdate)(e,h,&error,&result)); CHECK(result==0 && !error);
  OK(F(EntityInvoke)(e,q)); OK(QSYNC(q)); values(a,5,6,7);
  p=params(a,2,0x12345678); OK(F(EntityMemsetNodeSetParams)(e,n[2],&p));
  OK(F(EntityInvoke)(e,q)); OK(QSYNC(q)); values(a,5,6,0x12345678);
  Node extra; OK(F(AddEmptyNode)(&extra,h,NULL,0));
  int failed=(int)F(EntityUpdate)(e,h,&error,&result);
  CHECK(failed && result!=0); printf("update_failure=%d result=%d error_null=%d\n",failed,(int)result,error==NULL);
  OK(F(EntityInvoke)(e,q2)); OK(QSYNC(q2)); values(a,5,6,0x12345678);
  OK(F(Destroy)(g)); OK(F(Destroy)(h));
  OK(F(EntityInvoke)(e,q)); OK(F(EntityDestroy)(e)); OK(QSYNC(q)); values(a,5,6,0x12345678);
  puts("PASS graph memset snapshots/update/pending destroy");
  children(a,q,q2);
  copies(q,q2);
  if(argc>1) {
    dag(&g,n,a,11); OK(F(Create)(&h,0));
    CopyParams copy=copy_params(a,192,a,0,32);
    OK(F(AddMemcpyNode)(&c,g,n,1,&copy));
    OK(F(AddChildTopoNode)(&c,h,NULL,0,g));
    OK(F(Instantiate)(&e,h,NULL,NULL,0)); OK(F(EntityInvoke)(e,q));
    if(!strcmp(argv[1],"lease")) {
      /* Remote safety contract: public free revokes the ID while graph/entity
       * and invocation leases preserve backing. Native early free is undefined. */
      OK(FREE(a)); a=0; OK(F(Destroy)(g)); OK(F(Destroy)(h)); OK(F(EntityDestroy)(e)); OK(QSYNC(q));
      puts("PASS remote allocation retirement");
    } else if(!strcmp(argv[1],"abrupt")) { fflush(stdout); _exit(0); }
    else if(!strcmp(argv[1],"reset")) {
#ifdef TEST_CNRT
      OK(cnrtDeviceReset()); OK(cnrtSetDevice(0));
#else
      OK(cnCtxDestroy(ctx)); OK(cnCtxCreate(&ctx,0,0));
#endif
      OK(F(Create)(&g,0)); OK(F(Destroy)(g)); puts("PASS graph execution reset"); return 0;
    }
  }
  OK(QDESTROY(q)); OK(QDESTROY(q2)); if(a) OK(FREE(a));
#ifndef TEST_CNRT
  OK(cnCtxDestroy(ctx));
#endif
  return 0;
}
