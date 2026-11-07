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
typedef void *Address;
typedef cnrtQueue_t Queue;
#define ALLOC(p,n) cnrtMalloc(p,n)
#define FREE(p) cnrtFree(p)
#define READ(p,a,n) cnrtMemcpy(p,a,n,cnrtMemcpyDevToHost)
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
typedef CNaddr Address;
typedef CNqueue Queue;
#define ALLOC(p,n) cnMalloc(p,n)
#define FREE(p) cnFree(p)
#define READ(p,a,n) cnMemcpyDtoH(p,a,n)
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
  if(argc>1) {
    dag(&g,n,a,11); OK(F(Create)(&h,0));
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
