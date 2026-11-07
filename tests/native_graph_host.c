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
#define HOSTFN(q,f,p) cnrtInvokeHostFunc(q,f,p)
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
#define HOSTFN(q,f,p) cnInvokeHostFunc(q,f,p)
#define QDESTROY(q) cnDestroyQueue(q)
#endif
#define OK(c) do { int s=(int)(c); if(s) { fprintf(stderr,"%s=%d line=%d\n",#c,s,__LINE__); exit(1); } } while(0)
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"failed %s line=%d\n",#c,__LINE__); exit(2); } } while(0)
#ifdef TEST_CNRT
#define HALLOC(p,n) cnrtHostMalloc(p,n)
#define HFREE(p) cnrtFreeHost(p)
#define NSYNC(n) cnrtWaitNotifier(n)
#define NCREATE(n) cnrtNotifierCreate(n)
#define NPLACE(n,q) cnrtPlaceNotifier(n,q)
#define NFREE(n) cnrtNotifierDestroy(n)
#define CSYNC() cnrtSyncDevice()
typedef cnrtNotifier_t Notifier;
#else
#define HALLOC(p,n) cnMallocHost(p,n)
#define HFREE(p) cnFreeHost(p)
#define NSYNC(n) cnWaitNotifier(n)
#define NCREATE(n) cnCreateNotifier(n,0)
#define NPLACE(n,q) cnPlaceNotifier(n,q)
#define NFREE(n) cnDestroyNotifier(n)
#define CSYNC() cnCtxSync()
typedef CNnotifier Notifier;
#endif
static CopyParams cp(void *dst,void *src,int direction) {
  CopyParams p={0};p.dst=dst;p.src=src;p.dstPtr.ptr=dst;p.srcPtr.ptr=src;
  p.dstPtr.pitch=p.srcPtr.pitch=64;p.dstPtr.xsize=p.srcPtr.xsize=64;
  p.dstPtr.ysize=p.srcPtr.ysize=1;p.extent.width=64;p.extent.height=p.extent.depth=1;p.dir=direction;return p;
}
static void check(const unsigned char *out,unsigned value) {
  for(unsigned i=0;i<128;i++) CHECK(out[i]==(i>=16 && i<80 ? value : 0xa5));
}
static void observe_output(void *out) { check(out,42); }
static void graph(Graph *g,Node n[2],Address d,unsigned char *src,unsigned char *dst) {
  OK(F(Create)(g,0));CopyParams p=cp((void *)(uintptr_t)d,src+16,2);
  OK(F(AddMemcpyNode)(n,*g,NULL,0,&p));p=cp(dst+16,(void *)(uintptr_t)d,3);
  OK(F(AddMemcpyNode)(n+1,*g,n,1,&p));
}
static void test(Address d,Queue q,int pinned,int remote) {
  unsigned char *in=NULL,*out=NULL,alternate[128];
  if(pinned) {OK(HALLOC((void **)&in,128));OK(HALLOC((void **)&out,128));}
  else {in=malloc(128);out=malloc(128);}
  memset(in,17,128);memset(out,0xa5,128);memset(alternate,0xa5,128);
  Graph g,h,clone;Node n[2],m[2],mapped;Entity e,ec;Node error=NULL;Update result;
  graph(&g,n,d,in,out);CopyParams got={0};OK(F(MemcpyNodeGetParams)(n[0],&got));CHECK(got.src==in+16 && got.dst==(void *)(uintptr_t)d);
  OK(F(Clone)(&clone,g));OK(F(NodeFindInClone)(&mapped,n[1],clone));
  OK(F(MemcpyNodeGetParams)(mapped,&got));CHECK(got.dst==out+16);
  OK(F(Instantiate)(&e,g,NULL,NULL,0));OK(F(Instantiate)(&ec,clone,NULL,NULL,0));
  for(unsigned i=0;i<128;i++) CHECK(out[i]==0xa5);
  memset(in,42,128);OK(F(EntityInvoke)(e,q));OK(HOSTFN(q,observe_output,out));OK(QSYNC(q));check(out,42);
  memset(in,43,128);OK(F(EntityInvoke)(e,q));Notifier mark;OK(NCREATE(&mark));OK(NPLACE(mark,q));OK(NSYNC(mark));check(out,43);OK(NFREE(mark));
  CopyParams p=cp(alternate+16,(void *)(uintptr_t)d,3);OK(F(EntityMemcpyNodeSetParams)(e,n[1],&p));
  memset(in,44,128);OK(F(EntityInvoke)(e,q));OK(CSYNC());check(alternate,44);check(out,43);
  graph(&h,m,d,in,out);OK(F(EntityUpdate)(e,h,&error,&result));CHECK(!error && result==0);
  memset(in,45,128);OK(F(EntityInvoke)(e,q));OK(QSYNC(q));check(out,45);
  p=cp(alternate+16,(void *)(uintptr_t)d,3);OK(F(EntityMemcpyNodeSetParams)(e,n[1],&p));
  memset(in,46,128);OK(F(EntityInvoke)(e,q));OK(QSYNC(q));check(alternate,46);check(out,45);
  OK(F(Destroy)(g));OK(F(Destroy)(h));OK(F(Destroy)(clone));
  memset(in,47,128);OK(F(EntityInvoke)(ec,q));OK(F(EntityDestroy)(ec));OK(QSYNC(q));check(out,47);
  OK(F(EntityDestroy)(e));
  if(remote) {
    graph(&g,n,d,in,out);OK(F(Instantiate)(&e,g,NULL,NULL,0));
    memset(in,51,128);OK(F(EntityInvoke)(e,q));
    p=cp(alternate+16,(void *)(uintptr_t)d,3);OK(F(EntityMemcpyNodeSetParams)(e,n[1],&p));
    memset(in,52,128);OK(F(EntityInvoke)(e,q));OK(F(Destroy)(g));OK(F(EntityDestroy)(e));OK(QSYNC(q));
    check(out,51);check(alternate,52);
  }
  if(pinned) {OK(HFREE(in));OK(HFREE(out));}else {free(in);free(out);}
  printf("PASS Host graph %s replay/getter/clone/setter/update/completion\n",pinned ? "pinned" : "pageable");
}
static void child(Address d,Queue q) {
  unsigned char in[128],out[128],other[128];memset(in,61,128);memset(out,0xa5,128);memset(other,0xa5,128);
  Graph g,parent,outer,replacement,clone;Node n[2],m[2],c,c2;Entity e;
  graph(&g,n,d,in,out);OK(F(Create)(&parent,0));OK(F(AddChildTopoNode)(&c,parent,NULL,0,g));
  OK(F(Create)(&outer,0));OK(F(AddChildTopoNode)(&c2,outer,NULL,0,parent));OK(F(Clone)(&clone,outer));
  OK(F(Instantiate)(&e,clone,NULL,NULL,0));OK(F(Destroy)(clone));OK(F(Destroy)(outer));OK(F(Destroy)(parent));OK(F(Destroy)(g));
  memset(in,62,128);OK(F(EntityInvoke)(e,q));OK(QSYNC(q));check(out,62);OK(F(EntityDestroy)(e));
  graph(&g,n,d,in,out);OK(F(Create)(&parent,0));OK(F(AddChildTopoNode)(&c,parent,NULL,0,g));OK(F(Instantiate)(&e,parent,NULL,NULL,0));
  graph(&replacement,m,d,in,other);OK(F(EntityChildTopoNodeSetParams)(e,c,replacement));
  OK(F(Destroy)(replacement));OK(F(Destroy)(g));OK(F(Destroy)(parent));
  memset(in,63,128);OK(F(EntityInvoke)(e,q));OK(F(EntityDestroy)(e));OK(QSYNC(q));check(other,63);check(out,62);
  puts("PASS Host nested child clone and entity child replacement");
}
/* Same Host ranges retain their aliases inside one invocation. The final
 * scatter must preserve native D2H -> H2D feedback and overlapping writes. */
static void aliases(Address d) {
  unsigned char in[128],mid[128],out[128];memset(in,81,128);memset(mid,0xa5,128);memset(out,0xa5,128);
  Graph g;Node n[6];Entity e;CopyParams p;
  OK(F(Create)(&g,0));p=cp((void *)(uintptr_t)d,in+16,2);OK(F(AddMemcpyNode)(&n[0],g,NULL,0,&p));
  p=cp(mid+16,(void *)(uintptr_t)d,3);OK(F(AddMemcpyNode)(&n[1],g,n,1,&p));
  p=cp((void *)((uintptr_t)d+64),mid+16,2);OK(F(AddMemcpyNode)(&n[2],g,n+1,1,&p));
  Params fill={0};fill.dst=d;fill.value=82;fill.elementSize=1;fill.width=64;fill.height=1;
  OK(F(AddMemsetNode)(&n[3],g,n+2,1,&fill));
  p=cp(mid+48,(void *)(uintptr_t)d,3);OK(F(AddMemcpyNode)(&n[4],g,n+3,1,&p));
  p=cp(out+16,(void *)((uintptr_t)d+64),3);OK(F(AddMemcpyNode)(&n[5],g,n+4,1,&p));
  OK(F(Instantiate)(&e,g,NULL,NULL,0));OK(F(EntityInvoke)(e,NULL));OK(QSYNC(NULL));check(out,81);
  for(unsigned i=0;i<128;i++) CHECK(mid[i]==(i<16 || i>=112 ? 0xa5 : i<48 ? 81 : 82));
  OK(F(EntityDestroy)(e));OK(F(Destroy)(g));
  puts("PASS Host aliases, D2H to H2D feedback, overlapping outputs, memset and default queue");
}
static void retirement(Address d,Queue q) {
  unsigned char *in,*out;OK(HALLOC((void **)&in,128));OK(HALLOC((void **)&out,128));
  memset(in,91,128);memset(out,0xa5,128);Graph g;Node n[2];Entity e;graph(&g,n,d,in,out);
  OK(F(Instantiate)(&e,g,NULL,NULL,0));OK(F(EntityInvoke)(e,q));
  OK(HFREE(in));OK(HFREE(out));OK(F(EntityDestroy)(e));OK(F(Destroy)(g));OK(QSYNC(q));
  puts("PASS Remote pinned Host retirement retains pending destination through publication");
}
int main(int argc,char **argv) {
#ifdef TEST_CNRT
  OK(cnrtSetDevice(0));
#else
  CNcontext ctx;OK(cnInit(0));OK(cnCtxCreate(&ctx,0,0));
#endif
  Address d;Queue q;OK(ALLOC(&d,128));OK(QCREATE(&q));
  int remote=getenv("MLU_TEST_SHIM")!=NULL;
  test(d,q,0,remote);test(d,q,1,remote);child(d,q);aliases(d);
  if(argc>1 && !strcmp(argv[1],"lease")) retirement(d,q);
  if(argc>1 && strcmp(argv[1],"lease")) {
    unsigned char in[128],out[128];memset(in,71,128);memset(out,0xa5,128);
    Graph g;Node n[2];Entity e;graph(&g,n,d,in,out);OK(F(Instantiate)(&e,g,NULL,NULL,0));OK(F(EntityInvoke)(e,q));
    if(!strcmp(argv[1],"abrupt")) {fflush(stdout);_exit(0);}
    if(!strcmp(argv[1],"reset")) {
#ifdef TEST_CNRT
      OK(cnrtDeviceReset());
#else
      OK(cnCtxDestroy(ctx));
#endif
      check(out,71);puts("PASS Host graph reset publication");return 0;
    }
  }
  OK(QDESTROY(q));OK(FREE(d));
#ifndef TEST_CNRT
  OK(cnCtxDestroy(ctx));
#endif
  return 0;
}
