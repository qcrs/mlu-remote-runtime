/* Native contract discovery, compiled separately against CNRT and CNDrv.
 * No shim or remote server is involved. Each scenario is a fresh process. */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <pthread.h>
#ifdef USE_CNRT
#include <cnrt.h>
#define API_(s) cnrt##s
#define LIBRARY "CNRT"
typedef void *Addr;
typedef cnrtTaskTopo_t Topo;
typedef cnrtTaskTopoNode_t Node;
typedef cnrtTaskTopoEntity_t Entity;
typedef cnrtQueue_t Queue;
typedef cnrtMemsetParams_t SetParams;
typedef cnrtMemcpy3dParam_t CopyParams;
typedef cnrtTaskTopoNodeType_t NodeType;
typedef cnrtIpcMemHandle MemHandle;
#define alloc(a,n) cnrtMalloc(a,n)
#define release(a) cnrtFree(a)
#define host_alloc(a,n) cnrtHostMalloc(a,n)
#define host_free(a) cnrtFreeHost(a)
#define fill(a,v,n) cnrtMemset(a,v,n)
#define readback(h,a,n) cnrtMemcpy(h,a,n,cnrtMemcpyDevToHost)
#define h2d_async(a,h,n,q) cnrtMemcpyAsync(a,h,n,q,cnrtMemcpyHostToDev)
#define d2h_async(h,a,n,q) cnrtMemcpyAsync(h,a,n,q,cnrtMemcpyDevToHost)
#define queue_create(q) cnrtQueueCreate(q)
#define queue_destroy(q) cnrtQueueDestroy(q)
#define queue_sync(q) cnrtQueueSync(q)
#define queue_query(q) cnrtQueueQuery(q)
#define capture_begin(q) cnrtQueueBeginCapture(q,cnrtQueueCaptureModeRelaxed)
#define capture_end(q,t) cnrtQueueEndCapture(q,t)
#define capture_status(q,s) cnrtQueueIsCapturing(q,s)
#define CaptureStatus enum cnrtQueueCaptureStatus
#define export_mem(h,a) cnrtAcquireMemHandle(h,a)
#define import_mem(a,h) cnrtMapMemHandle(a,h,0)
#define close_mem(a) cnrtUnMapMemHandle(a)
#else
#include <cn_api.h>
#define API_(s) cn##s
#define LIBRARY "CNDrv"
typedef CNaddr Addr;
typedef CNtaskTopo Topo;
typedef CNtaskTopoNode Node;
typedef CNtaskTopoEntity Entity;
typedef CNqueue Queue;
typedef CN_MEMSET_NODE_PARAMS SetParams;
typedef CNmemcpy3dParam CopyParams;
typedef CNtaskTopoNodeType NodeType;
typedef CNIpcMemHandle MemHandle;
static CNcontext context;
#define alloc(a,n) cnMalloc(a,n)
#define release(a) cnFree(a)
#define host_alloc(a,n) cnMallocHost(a,n)
#define host_free(a) cnFreeHost(a)
#define fill(a,v,n) cnMemsetD8(a,v,n)
#define readback(h,a,n) cnMemcpyDtoH(h,a,n)
#define h2d_async(a,h,n,q) cnMemcpyHtoDAsync(a,h,n,q)
#define d2h_async(h,a,n,q) cnMemcpyDtoHAsync(h,a,n,q)
#define queue_create(q) cnCreateQueue(q,0)
#define queue_destroy(q) cnDestroyQueue(q)
#define queue_sync(q) cnQueueSync(q)
#define queue_query(q) cnQueryQueue(q)
#define capture_begin(q) cnQueueBeginCapture(q,CN_QUEUE_CAPTURE_MODE_RELAXED)
#define capture_end(q,t) cnQueueEndCapture(q,t)
#define capture_status(q,s) cnQueueIsCapturing(q,s)
#define CaptureStatus CNqueueCaptureStatus
#define export_mem(h,a) cnIpcGetMemHandle(h,a)
#define import_mem(a,h) cnIpcOpenMemHandle(a,h,0)
#define close_mem(a) cnIpcCloseMemHandle(a)
#endif
#define API(s) API_(s)
#define N 4096
static int observe(const char *name, int status) {
  printf("{\"call\":\"%s\",\"status\":%d}\n",name,status);
  fflush(stdout);
  return status;
}
#define CHECK(x) do { if(observe(#x,(x))) exit(2); } while(0)
static void expect(const char *name, int ok) {
  printf("{\"check\":\"%s\",\"pass\":%s}\n",name,ok?"true":"false");
  fflush(stdout);
  if(!ok) exit(3);
}
static int bytes_are(const void *data, size_t n, unsigned char v) {
  const unsigned char *p=data;
  for(size_t i=0;i<n;i++) if(p[i]!=v) return 0;
  return 1;
}
static void init(void) {
  printf("{\"library\":\"%s\",\"pid\":%ld,\"pointer_bytes\":%zu,"
         "\"memory_handle_bytes\":%zu}\n",LIBRARY,(long)getpid(),sizeof(void *),sizeof(MemHandle));
#ifdef USE_CNRT
  CHECK(cnrtSetDevice(0));
#else
  CHECK(cnInit(0)); CHECK(cnCtxCreate(&context,0,0));
#endif
  int major=0,minor=0,patch=0;
  CHECK(API(GetLibVersion)(&major,&minor,&patch));
  printf("{\"sdk_library_version\":\"%d.%d.%d\"}\n",major,minor,patch);
}
static void finish(void) {
#ifdef USE_CNRT
  CHECK(cnrtDeviceReset());
#else
  CHECK(cnCtxDestroy(context));
#endif
}

#ifdef USE_CNRT
#define begin(q,m) cnrtQueueBeginCapture(q,(enum cnrtQueueCaptureMode)m)
#define info(q,s,id,t,d,n) cnrtQueueGetCaptureInfo(q,s,id,t,d,n)
#define update(q,d,n,f) cnrtQueueUpdateCaptureDependencies(q,d,n,f)
#define set_async(a,v,n,q) cnrtMemsetAsync(a,v,n,q)
#define Notifier cnrtNotifier_t
#define create_notifier(n) cnrtNotifierCreate(n)
#define destroy_notifier(n) cnrtNotifierDestroy(n)
#define record(n,q) cnrtPlaceNotifier(n,q)
#define wait(q,n) cnrtQueueWaitNotifier(n,q,0)
#else
#define begin(q,m) cnQueueBeginCapture(q,(CNqueueCaptureMode)m)
#define info(q,s,id,t,d,n) cnQueueGetCaptureInfo(q,s,id,t,d,n)
#define update(q,d,n,f) cnQueueUpdateCaptureDependencies(q,d,n,f)
#define set_async(a,v,n,q) cnMemsetD8Async(a,v,n,q)
#define Notifier CNnotifier
#define create_notifier(n) cnCreateNotifier(n,0)
#define destroy_notifier(n) cnDestroyNotifier(n)
#define record(n,q) cnPlaceNotifier(n,q)
#define wait(q,n) cnQueueWaitNotifier(q,n)
#endif
static void inspect(Queue q,const char *label) {
  CaptureStatus s=(CaptureStatus)99;uint64_t id=123;Topo t=(Topo)123;
  const Node *d=(const Node*)123;size_t n=123;
  int st=info(q,&s,&id,&t,&d,&n);
  printf("{\"info\":\"%s\",\"status\":%d,\"state\":%d,\"id\":%llu,\"graph\":%d,\"count\":%zu,\"deps_written\":%d}\n",label,st,(int)s,(unsigned long long)id,t!=(Topo)123,n,d!=(const Node*)123);
  fflush(stdout);
  if(!st && s==1 && t) {size_t nodes=0,edges=0;CHECK(API(TaskTopoGetNodes)(t,NULL,&nodes));CHECK(API(TaskTopoGetEdges)(t,NULL,NULL,&edges));printf("{\"nodes\":%zu,\"edges\":%zu}\n",nodes,edges);}
}
struct thread_arg {Queue q;Topo t;int result;};
static void *end_other(void *v) {
#ifndef USE_CNRT
  CHECK(cnCtxSetCurrent(context));
#endif
  struct thread_arg *a=v;a->result=observe("end_other_thread",capture_end(a->q,&a->t));return NULL;
}
static void *unsafe_other(void *v) {
  (void)v;
#ifndef USE_CNRT
  CHECK(cnCtxSetCurrent(context));
#endif
  Addr other=0;observe("allocate_other_thread",alloc(&other,N));if(other) CHECK(release(other));return NULL;
}
int main(int argc,char **argv) {
  if(argc<2) return 2;
  init();int mode=argc>2?atoi(argv[2]):2;Queue q,r;CHECK(queue_create(&q));CHECK(queue_create(&r));
  Addr a;CHECK(alloc(&a,N));CHECK(fill(a,7,N));Topo t=NULL;
#ifndef USE_CNRT
  CNcontext second=NULL;
  if(!strcmp(argv[1],"contexts")) {CHECK(cnCtxCreate(&second,0,0));CHECK(cnCtxSetCurrent(context));}
#endif
  inspect(q,"idle");
  int st=observe("begin",begin(q,mode));if(st) {expect("unsupported_mode_observed",st!=0);finish();return 0;}
  inspect(q,"empty");
  if(!strcmp(argv[1],"thread")) {struct thread_arg arg={.q=q,.t=(Topo)123};pthread_t th;pthread_create(&th,NULL,end_other,&arg);pthread_join(th,NULL);if(arg.result) printf("{\"thread_error_output_preserved\":%d}\n",arg.t==(Topo)123);if(!arg.result) t=arg.t;else observe("end_origin",capture_end(q,&t));}
  else if(!strcmp(argv[1],"unsafe_thread")) {pthread_t th;pthread_create(&th,NULL,unsafe_other,NULL);pthread_join(th,NULL);inspect(q,"after_unsafe");observe("end",capture_end(q,&t));}
#ifndef USE_CNRT
  else if(!strcmp(argv[1],"contexts")) {CHECK(cnCtxSetCurrent(second));Addr other=0;observe("allocate_other_context",alloc(&other,N));if(other) CHECK(release(other));CHECK(cnCtxSetCurrent(context));inspect(q,"after_unsafe");observe("end",capture_end(q,&t));CHECK(cnCtxDestroy(second));}
#endif
  else if(!strcmp(argv[1],"contextsync")) {CHECK(set_async(a,17,N,q));
#ifdef USE_CNRT
    observe("context_sync",cnrtSyncDevice());
#else
    observe("context_sync",cnCtxSync());
#endif
    inspect(q,"after_sync");observe("end",capture_end(q,&t));
  }
  else if(!strcmp(argv[1],"query")) {observe("query_captured",queue_query(q));inspect(q,"after_sync");observe("end",capture_end(q,&t));}
  else if(!strcmp(argv[1],"unsafe")) {Addr other=0;observe("allocate_during_capture",alloc(&other,N));if(other) observe("free_during_capture",release(other));observe("sync_other",queue_sync(r));observe("sync_default",queue_sync(0));inspect(q,"after_unsafe");observe("end",capture_end(q,&t));}
  else if(!strcmp(argv[1],"entity")) {Topo other;Node empty;Entity e;CHECK(API(TaskTopoCreate)(&other,0));CHECK(API(TaskTopoAddEmptyNode)(&empty,other,NULL,0));CHECK(API(TaskTopoInstantiate)(&e,other,NULL,NULL,0));observe("capture_entity_invoke",API(TaskTopoEntityInvoke)(e,q));inspect(q,"after_entity");observe("end",capture_end(q,&t));CHECK(API(TaskTopoEntityDestroy)(e));CHECK(API(TaskTopoDestroy)(other));}
  else if(!strcmp(argv[1],"isolation")) {Notifier n;CHECK(create_notifier(&n));CHECK(record(n,r));observe("captured_wait_external",wait(q,n));inspect(q,"after_external_wait");observe("end",capture_end(q,&t));CHECK(destroy_notifier(n));}
  else if(!strcmp(argv[1],"merge")) {Notifier n;CHECK(create_notifier(&n));CHECK(begin(r,mode));CHECK(set_async(a,11,N,q));CHECK(record(n,q));observe("merge_wait",wait(r,n));inspect(q,"merge_root");inspect(r,"merge_other");Topo other=0;observe("end_other",capture_end(r,&other));if(other) CHECK(API(TaskTopoDestroy)(other));observe("end",capture_end(q,&t));CHECK(destroy_notifier(n));}
  else if(!strcmp(argv[1],"sync")) {observe("set_async",set_async(a,17,N,q));observe("sync_captured",queue_sync(q));inspect(q,"after_sync");observe("end",capture_end(q,&t));}
  else if(!strcmp(argv[1],"fork") || !strcmp(argv[1],"unjoined") || !strcmp(argv[1],"wrong_root")) {
    Notifier n,m;CHECK(create_notifier(&n));CHECK(create_notifier(&m));
    CHECK(set_async(a,17,N,q));inspect(q,"first");CHECK(record(n,q));inspect(q,"record");
    CHECK(wait(r,n));inspect(r,"forked");CHECK(set_async(a,29,N,r));inspect(r,"branch");
    if(!strcmp(argv[1],"fork")) {CHECK(record(m,r));CHECK(wait(q,m));inspect(q,"joined");}
    if(!strcmp(argv[1],"wrong_root")) {Topo sentinel=(Topo)123;int error=observe("end_branch",capture_end(r,&sentinel));expect("wrong_root_output_preserved",error==100366 && sentinel==(Topo)123);inspect(q,"invalid_root");observe("enqueue_invalid",set_async(a,41,N,q));}
    observe("end",capture_end(q,&t));inspect(r,"after_end");
    CHECK(destroy_notifier(n));CHECK(destroy_notifier(m));
  } else {
    CHECK(set_async(a,17,N,q));inspect(q,"first");CaptureStatus s;uint64_t id;const Node *d;size_t n;
    CHECK(info(q,&s,&id,&t,&d,&n));Node first=d[0],empty,other;
    CHECK(API(TaskTopoAddEmptyNode)(&empty,t,&first,1));CHECK(update(q,&empty,1,1));inspect(q,"updated");
    CHECK(API(TaskTopoAddEmptyNode)(&other,t,&first,1));CHECK(update(q,&other,1,0));inspect(q,"added");
    CHECK(info(q,&s,&id,&t,&d,&n));expect("add_preserves_frontier",n==2 && ((d[0]==empty && d[1]==other)||(d[1]==empty && d[0]==other)));
    CHECK(set_async(a,29,N,q));inspect(q,"second");
    CHECK(info(q,&s,&id,&t,&d,&n));Node parents[2]={0};size_t count=2;
    CHECK(API(TaskTopoNodeGetDependentNodes)(d[0],parents,&count));
    expect("next_node_depends_on_both_frontiers",count==2 && ((parents[0]==empty && parents[1]==other)||(parents[1]==empty && parents[0]==other)));
    CHECK(capture_end(q,&t));
  }
  inspect(q,"after_end");if(t) CHECK(API(TaskTopoDestroy)(t));
  CHECK(release(a));CHECK(queue_destroy(q));CHECK(queue_destroy(r));expect("scenario_completed",1);finish();return 0;
}
