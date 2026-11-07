#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#ifdef TEST_CNRT
#include <cnrt.h>
#define G(n) cnrtTaskTopo##n
#define U(n) cnrtUserObject##n
typedef cnrtTaskTopo_t Graph;
typedef cnrtTaskTopoNode_t Node;
typedef cnrtTaskTopoEntity_t Entity;
typedef cnrtTaskTopoEntityUpdateResult_t Update;
typedef cnrtHostNodeParams_t Host;
typedef cnrtMemcpy3dParam_t Copy;
typedef cnrtQueue_t Queue;
typedef cnrtUserObject_t Object;
typedef void *Address;
#define ALLOC(p,n) cnrtMalloc(p,n)
#define FREE(p) cnrtFree(p)
#define QCREATE(p) cnrtQueueCreate(p)
#define QSYNC(q) cnrtQueueSync(q)
#define QFREE(q) cnrtQueueDestroy(q)
#else
#include <cn_api.h>
#define G(n) cnTaskTopo##n
#define U(n) cnUserObject##n
typedef CNtaskTopo Graph;
typedef CNtaskTopoNode Node;
typedef CNtaskTopoEntity Entity;
typedef CNtaskTopoEntityUpdateResult Update;
typedef CN_HOST_NODE_PARAMS Host;
typedef CNmemcpy3dParam Copy;
typedef CNqueue Queue;
typedef CNuserObject Object;
typedef CNaddr Address;
#define ALLOC(p,n) cnMalloc(p,n)
#define FREE(p) cnFree(p)
#define QCREATE(p) cnCreateQueue(p,0)
#define QSYNC(q) cnQueueSync(q)
#define QFREE(q) cnDestroyQueue(q)
#endif
#define OK(c) do {int status_=(int)(c);if(status_){fprintf(stderr,"%s=%d line=%d\n",#c,status_,__LINE__);exit(1);}}while(0)
#define CHECK(c) do {if(!(c)){fprintf(stderr,"failed %s line=%d\n",#c,__LINE__);exit(2);}}while(0)
typedef struct {
  pthread_mutex_t lock;
  pthread_cond_t cond;
  unsigned calls,order[16];
  int gate,entered,release;
  unsigned char in[64],out[64];
  pid_t pid;
} State;
typedef struct {State *s;unsigned value;} Arg;
static void init(State *s) {memset(s,0,sizeof(*s));pthread_mutex_init(&s->lock,NULL);pthread_cond_init(&s->cond,NULL);s->pid=getpid();}
static void fini(State *s) {pthread_cond_destroy(&s->cond);pthread_mutex_destroy(&s->lock);}
static void callback(void *p) {
  Arg *a=p;State *s=a->s;CHECK(s->pid==getpid());
  pthread_mutex_lock(&s->lock);
  if(s->gate) {s->entered=1;pthread_cond_broadcast(&s->cond);while(!s->release)pthread_cond_wait(&s->cond,&s->lock);}
  CHECK(s->calls<16);s->order[s->calls++]=a->value;
  if(a->value==1) memset(s->in,41+s->calls,64);
  if(a->value==2) {for(unsigned i=0;i<64;i++) CHECK(s->out[i]==42);memset(s->out,73,64);}
  if(a->value==3) for(unsigned i=0;i<64;i++) CHECK(s->in[i]==73);
  pthread_cond_broadcast(&s->cond);pthread_mutex_unlock(&s->lock);
}
static void destructor(void *p) {State *s=p;CHECK(s->pid==getpid());pthread_mutex_lock(&s->lock);s->calls++;pthread_cond_broadcast(&s->cond);pthread_mutex_unlock(&s->lock);}
static void wait_calls(State *s,unsigned count) {
  struct timespec until;clock_gettime(CLOCK_REALTIME,&until);until.tv_sec+=10;
  pthread_mutex_lock(&s->lock);while(s->calls<count) CHECK(!pthread_cond_timedwait(&s->cond,&s->lock,&until));CHECK(s->calls==count);pthread_mutex_unlock(&s->lock);
}
static Node host(Graph g,Node *dep,size_t n,Arg *a) {Node node;Host p={callback,a};OK(G(AddHostNode)(&node,g,dep,n,&p));return node;}
static Node copy(Graph g,Node dep,void *dst,void *src,int dir) {
  Copy p={0};p.dst=p.dstPtr.ptr=dst;p.src=p.srcPtr.ptr=src;
  p.dstPtr.pitch=p.srcPtr.pitch=64;p.dstPtr.xsize=p.srcPtr.xsize=64;p.dstPtr.ysize=p.srcPtr.ysize=1;
  p.extent.width=64;p.extent.height=p.extent.depth=1;p.dir=dir;
  Node node;OK(G(AddMemcpyNode)(&node,g,&dep,1,&p));return node;
}
static void order(Queue q,Address d) {
  State s;init(&s);Arg a[]={{&s,1},{&s,2},{&s,3}};Graph g;Entity e;
  OK(G(Create)(&g,0));Node n=host(g,NULL,0,&a[0]);
  n=copy(g,n,(void *)(uintptr_t)d,s.in,2);n=copy(g,n,s.out,(void *)(uintptr_t)d,3);
  n=host(g,&n,1,&a[1]);n=copy(g,n,(void *)(uintptr_t)d,s.out,2);n=copy(g,n,s.in,(void *)(uintptr_t)d,3);
  host(g,&n,1,&a[2]);OK(G(Instantiate)(&e,g,NULL,NULL,0));CHECK(!s.calls);
  OK(G(EntityInvoke)(e,q));OK(QSYNC(q));CHECK(s.calls==3 && s.order[0]==1 && s.order[1]==2 && s.order[2]==3);
  OK(G(EntityDestroy)(e));OK(G(Destroy)(g));fini(&s);puts("PASS callback -> H2D -> D2H -> callback -> H2D observes current client Host bytes");
}
static void snapshots(Queue q) {
  State s;init(&s);Arg a[]={{&s,10},{&s,11},{&s,12},{&s,13}};
  Graph g,clone,parent,embedded;Node n,m,c;Entity e,ec,ep;
  OK(G(Create)(&g,0));n=host(g,NULL,0,&a[0]);Host got={0};OK(G(HostNodeGetParams)(n,&got));CHECK(got.fn==callback && got.userData==a);
  OK(G(Clone)(&clone,g));OK(G(NodeFindInClone)(&m,n,clone));
  Host p={callback,&a[1]};OK(G(HostNodeSetParams)(n,&p));OK(G(HostNodeGetParams)(m,&got));CHECK(got.userData==a);
  OK(G(Instantiate)(&e,g,NULL,NULL,0));OK(G(Instantiate)(&ec,clone,NULL,NULL,0));
  p.userData=&a[2];OK(G(HostNodeSetParams)(n,&p));
  OK(G(Create)(&parent,0));OK(G(AddChildTopoNode)(&c,parent,NULL,0,g));OK(G(ChildTopoNodeGetTopo)(c,&embedded));
  OK(G(Instantiate)(&ep,parent,NULL,NULL,0));p.userData=&a[3];OK(G(EntityHostNodeSetParams)(e,n,&p));
  OK(G(Destroy)(g));OK(G(Destroy)(clone));OK(G(Destroy)(parent));
  OK(G(EntityInvoke)(e,q));OK(G(EntityInvoke)(ec,q));OK(G(EntityInvoke)(ep,q));OK(QSYNC(q));
  CHECK(s.calls==3 && s.order[0]==13 && s.order[1]==10 && s.order[2]==12);
  OK(G(EntityDestroy)(e));OK(G(EntityDestroy)(ec));OK(G(EntityDestroy)(ep));fini(&s);
  puts("PASS callback identity, clone, setters, instantiate snapshot and nested child");
}
static void async(Queue q) {
  State s;init(&s);s.gate=1;Arg a={&s,10};Graph g;Entity e;
  OK(G(Create)(&g,0));host(g,NULL,0,&a);OK(G(Instantiate)(&e,g,NULL,NULL,0));
  OK(G(EntityInvoke)(e,q));
  struct timespec until;clock_gettime(CLOCK_REALTIME,&until);until.tv_sec+=10;
  pthread_mutex_lock(&s.lock);while(!s.entered) CHECK(!pthread_cond_timedwait(&s.cond,&s.lock,&until));
  pthread_mutex_unlock(&s.lock);OK(G(EntityDestroy)(e));OK(G(Destroy)(g));
  pthread_mutex_lock(&s.lock);s.release=1;pthread_cond_broadcast(&s.cond);pthread_mutex_unlock(&s.lock);
  OK(QSYNC(q));CHECK(s.calls==1);fini(&s);puts("PASS callback progresses without sync; pending entity/graph destruction retains invocation");
}
static void users(Queue q) {
  State s;init(&s);Object o;Graph g,clone;Entity e;
  OK(U(Create)(&o,&s,destructor,2,1));OK(U(Acquire)(o,1));OK(G(Create)(&g,0));
  OK(G(AcquireUserObject)(g,o,1,1));OK(G(AcquireUserObject)(g,o,2,0));
  OK(U(Release)(o,2));OK(G(ReleaseUserObject)(g,o,1));CHECK(!s.calls);
  OK(G(Clone)(&clone,g));OK(G(Instantiate)(&e,clone,NULL,NULL,0));OK(G(Destroy)(g));OK(G(Destroy)(clone));CHECK(!s.calls);
  OK(G(EntityInvoke)(e,q));OK(G(EntityDestroy)(e));OK(QSYNC(q));wait_calls(&s,1);fini(&s);
  puts("PASS user caller/acquire/MOVE/graph release/clone/entity/invocation references and one destructor");
  State s2;init(&s2);OK(U(Create)(&o,&s2,destructor,1,1));OK(U(Release)(o,1));wait_calls(&s2,1);fini(&s2);
  puts("PASS unassociated user object destructor progresses without API synchronization");
}
static void dot(const char *path) {
  Graph g;State s;init(&s);Arg a={&s,10};OK(G(Create)(&g,0));Node n=host(g,NULL,0,&a);Node empty;
  OK(G(AddEmptyNode)(&empty,g,&n,1));
  const unsigned flags[]={0,1,2,4,64,255,256};
  for(unsigned i=0;i<sizeof(flags)/sizeof(flags[0]);i++) {
    int st=G(DebugDotPrint)(g,path,flags[i]);printf("dot flags=%u status=%d\n",flags[i],st);CHECK(!st);
    FILE *f=fopen(path,"r");CHECK(f);char buf[4096]={0};CHECK(fread(buf,1,sizeof(buf)-1,f)>0);fclose(f);CHECK(strstr(buf,"digraph"));
  }
  int st=G(DebugDotPrint)(g,"/does-not-exist/mlur.dot",0);printf("dot missing-parent=%d\n",st);CHECK(st);
  OK(G(Destroy)(g));fini(&s);puts("PASS DOT flags, real local output and missing-parent error");
}
static void repeat(Queue q) {
  State s;init(&s);Arg a={&s,10};Graph g;Entity e;
  OK(G(Create)(&g,0));host(g,NULL,0,&a);OK(G(Instantiate)(&e,g,NULL,NULL,0));
  for(unsigned i=0;i<12;i++)OK(G(EntityInvoke)(e,q));
  wait_calls(&s,12);OK(QSYNC(q));
  OK(G(EntityDestroy)(e));OK(G(Destroy)(g));fini(&s);
  puts("PASS repeated invocations complete without synchronization driving callbacks");
}
static void childset(Queue q) {
  State s;init(&s);Arg a[]={{&s,10},{&s,11}};Graph g,child,replacement;Entity e;Node n;
  OK(G(Create)(&child,0));host(child,NULL,0,&a[0]);
  OK(G(Create)(&replacement,0));host(replacement,NULL,0,&a[1]);
  OK(G(Create)(&g,0));OK(G(AddChildTopoNode)(&n,g,NULL,0,child));OK(G(Instantiate)(&e,g,NULL,NULL,0));
  OK(G(EntityChildTopoNodeSetParams)(e,n,replacement));
  OK(G(Destroy)(g));OK(G(Destroy)(child));OK(G(Destroy)(replacement));
  OK(G(EntityInvoke)(e,q));OK(QSYNC(q));CHECK(s.calls==1 && s.order[0]==11);
  OK(G(EntityDestroy)(e));fini(&s);puts("PASS entity child replacement routes callback snapshots");
}
static void update(Queue q) {
  State s;init(&s);Arg a[]={{&s,10},{&s,11},{&s,12}};Graph g,h;Entity e;Node error=NULL;Update result;
  OK(G(Create)(&g,0));Node n=host(g,NULL,0,&a[0]);OK(G(Instantiate)(&e,g,NULL,NULL,0));
  OK(G(Create)(&h,0));host(h,NULL,0,&a[1]);OK(G(EntityUpdate)(e,h,&error,&result));CHECK(!error && result==0);
  OK(G(EntityInvoke)(e,q));OK(QSYNC(q));
  Host params={callback,&a[2]};OK(G(EntityHostNodeSetParams)(e,n,&params));
  OK(G(Destroy)(g));OK(G(Destroy)(h));OK(G(EntityInvoke)(e,q));OK(QSYNC(q));
  CHECK(s.calls==2 && s.order[0]==11 && s.order[1]==12);OK(G(EntityDestroy)(e));fini(&s);
  puts("PASS entity update replaces callback snapshot and retains original setter identity");
}
static void pending_user(Queue q) {
  State s,u;init(&s);init(&u);s.gate=1;Arg a={&s,10};Object o;Graph g;Entity e;
  OK(U(Create)(&o,&u,destructor,1,1));OK(G(Create)(&g,0));host(g,NULL,0,&a);
  OK(G(AcquireUserObject)(g,o,1,1));OK(G(Instantiate)(&e,g,NULL,NULL,0));OK(G(EntityInvoke)(e,q));
  struct timespec until;clock_gettime(CLOCK_REALTIME,&until);until.tv_sec+=10;
  pthread_mutex_lock(&s.lock);while(!s.entered)CHECK(!pthread_cond_timedwait(&s.cond,&s.lock,&until));pthread_mutex_unlock(&s.lock);
  OK(G(EntityDestroy)(e));OK(G(Destroy)(g));CHECK(!u.calls);
  pthread_mutex_lock(&s.lock);s.release=1;pthread_cond_broadcast(&s.cond);pthread_mutex_unlock(&s.lock);
  OK(QSYNC(q));wait_calls(&u,1);fini(&s);fini(&u);
  puts("PASS in-flight callback invocation retains user object until completion");
}
static void reentry_callback(void *p) {
#ifdef TEST_CNRT
  int device;CHECK(cnrtGetDevice(&device)==cnrtErrorNotSupport);
  CHECK(cnrtQueueSync(NULL)==cnrtErrorNotSupport);
#else
  CNdev device;CHECK(cnCtxGetDevice(&device)==CN_OPS_ERROR_NOT_SUPPORTED);
  CHECK(cnQueueSync(NULL)==CN_OPS_ERROR_NOT_SUPPORTED);
#endif
  destructor(p);
}
static void reentry(Queue q) {
  State s;init(&s);Graph g;Node n;Entity e;Object o;Host h={reentry_callback,&s};
  OK(G(Create)(&g,0));OK(G(AddHostNode)(&n,g,NULL,0,&h));OK(G(Instantiate)(&e,g,NULL,NULL,0));
  OK(G(EntityInvoke)(e,q));OK(QSYNC(q));wait_calls(&s,1);
  OK(G(EntityDestroy)(e));OK(G(Destroy)(g));
  OK(U(Create)(&o,&s,reentry_callback,1,1));OK(U(Release)(o,1));wait_calls(&s,2);fini(&s);
  puts("PASS callback and user destructor API reentry fails promptly before shim locks");
}
static void lifecycle(Queue q,const char *mode) {
  State s,u;init(&s);init(&u);Arg a={&s,10};Graph g;Entity e;Object o;
  OK(G(Create)(&g,0));host(g,NULL,0,&a);OK(U(Create)(&o,&u,destructor,1,1));
  OK(G(AcquireUserObject)(g,o,1,1));OK(G(Instantiate)(&e,g,NULL,NULL,0));OK(G(EntityInvoke)(e,q));
  if(!strcmp(mode,"abrupt"))_exit(0);
#ifdef TEST_CNRT
  OK(cnrtDeviceReset());
#else
  CNcontext current;OK(cnCtxGetCurrent(&current));OK(cnCtxDestroy(current));
#endif
  wait_calls(&s,1);wait_calls(&u,1);fini(&s);fini(&u);
#ifdef TEST_CNRT
  /* A second callback channel after reset exercises graceful-close ownership. */
  OK(cnrtSetDevice(0));OK(QCREATE(&q));repeat(q);OK(QFREE(q));OK(cnrtDeviceReset());
#endif
  puts("PASS reset/context close drains callback, user destructor and channel");
}
static void lease(Queue q,Address d) {
  State s;init(&s);s.gate=1;Arg a={&s,10};Graph g;Entity e;void *src,*dst;
#ifdef TEST_CNRT
  OK(cnrtHostMalloc(&src,64));OK(cnrtHostMalloc(&dst,64));
#else
  OK(cnMallocHost(&src,64));OK(cnMallocHost(&dst,64));
#endif
  memset(src,37,64);memset(dst,0,64);OK(G(Create)(&g,0));Node n=host(g,NULL,0,&a);
  n=copy(g,n,(void *)(uintptr_t)d,src,2);copy(g,n,dst,(void *)(uintptr_t)d,3);
  OK(G(Instantiate)(&e,g,NULL,NULL,0));OK(G(EntityInvoke)(e,q));
  struct timespec until;clock_gettime(CLOCK_REALTIME,&until);until.tv_sec+=10;
  pthread_mutex_lock(&s.lock);while(!s.entered)CHECK(!pthread_cond_timedwait(&s.cond,&s.lock,&until));pthread_mutex_unlock(&s.lock);
#ifdef TEST_CNRT
  OK(cnrtFreeHost(src));OK(cnrtFreeHost(dst));
#else
  OK(cnFreeHost(src));OK(cnFreeHost(dst));
#endif
  pthread_mutex_lock(&s.lock);s.release=1;pthread_cond_broadcast(&s.cond);pthread_mutex_unlock(&s.lock);
  OK(QSYNC(q));OK(G(EntityDestroy)(e));OK(G(Destroy)(g));fini(&s);
  puts("PASS Remote Host retirement preserves callback invocation endpoint leases");
}
int main(int argc,char **argv) {
#ifdef TEST_CNRT
  OK(cnrtSetDevice(0));
#else
  CNcontext ctx;OK(cnInit(0));OK(cnCtxCreate(&ctx,0,0));
#endif
  Queue q;Address d;OK(QCREATE(&q));OK(ALLOC(&d,64));
  const char *mode=argc>1 ? argv[1] : "all";
  if(!strcmp(mode,"reset") || !strcmp(mode,"abrupt")){lifecycle(q,mode);return 0;}
  if(!strcmp(mode,"reentry"))reentry(q);
  if(!strcmp(mode,"lease"))lease(q,d);
  if(!strcmp(mode,"all") || !strcmp(mode,"order")) order(q,d);
  if(!strcmp(mode,"all") || !strcmp(mode,"snapshot")) snapshots(q);
  if(!strcmp(mode,"all") || !strcmp(mode,"async")) async(q);
  if(!strcmp(mode,"all") || !strcmp(mode,"user")) users(q);
  if(!strcmp(mode,"all") || !strcmp(mode,"dot")) dot(argc>2 ? argv[2] : "/tmp/mlur-callbacks.dot");
  if(!strcmp(mode,"all") || !strcmp(mode,"repeat"))repeat(q);
  if(!strcmp(mode,"all") || !strcmp(mode,"childset"))childset(q);
  if(!strcmp(mode,"all") || !strcmp(mode,"update"))update(q);
  if(!strcmp(mode,"all") || !strcmp(mode,"pending_user"))pending_user(q);
  OK(FREE(d));OK(QFREE(q));
#ifndef TEST_CNRT
  OK(cnCtxDestroy(ctx));
#endif
  return 0;
}
