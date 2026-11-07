/* Native contract discovery, compiled separately against CNRT and CNDrv.
 * No shim or remote server is involved. Each IPC peer is a fresh exec process. */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
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
static CopyParams copy_params(Addr dst, Addr src) {
  CopyParams p={0}; p.dst=(void *)(uintptr_t)dst; p.src=(void *)(uintptr_t)src;
  p.dstPtr.pitch=N; p.dstPtr.ysize=1; p.srcPtr.pitch=N; p.srcPtr.ysize=1;
  p.extent.depth=1; p.extent.height=1; p.extent.width=N;
  return p;
}
static void graph(void) {
  Addr a,b; CHECK(alloc(&a,N)); CHECK(alloc(&b,N));
  Topo t,clone; Node set,copy,empty,cloned; Entity e1,e2,e3; Queue q;
  CHECK(queue_create(&q)); CHECK(API(TaskTopoCreate)(&t,0));
  SetParams p={0}; p.dst=a; p.value=17; p.elementSize=1; p.width=N; p.height=1;
  CHECK(API(TaskTopoAddMemsetNode)(&set,t,NULL,0,&p));
  CopyParams c=copy_params(b,a);
  CHECK(API(TaskTopoAddMemcpyNode)(&copy,t,&set,1,&c));
  CHECK(API(TaskTopoAddEmptyNode)(&empty,t,&copy,1));
  size_t count=0;
  CHECK(API(TaskTopoGetNodes)(t,NULL,&count)); expect("dag_nodes_3",count==3);
  Node nodes[5]={set,set,set,set,set}; count=5;
  CHECK(API(TaskTopoGetNodes)(t,nodes,&count));
  expect("nodes_capacity_and_null_tail",count==3 && !nodes[3] && !nodes[4]);
  count=0; CHECK(API(TaskTopoGetEdges)(t,NULL,NULL,&count)); expect("dag_edges_2",count==2);
  Node from[2],to[2]; count=2; CHECK(API(TaskTopoGetEdges)(t,from,to,&count));
  expect("dag_edge_identity",count==2 &&
    ((from[0]==set && to[0]==copy && from[1]==copy && to[1]==empty) ||
     (from[1]==set && to[1]==copy && from[0]==copy && to[0]==empty)));
  count=5; CHECK(API(TaskTopoGetRootNodes)(t,nodes,&count)); expect("dag_root",count==1 && nodes[0]==set);
  NodeType kind; CHECK(API(TaskTopoNodeGetType)(set,&kind)); expect("memset_type",kind==4);
  CHECK(API(TaskTopoRemoveDependencies)(t,&copy,&empty,1));
  count=0; CHECK(API(TaskTopoGetEdges)(t,NULL,NULL,&count)); expect("remove_edge",count==1);
  CHECK(API(TaskTopoAddDependencies)(t,&copy,&empty,1));
  CHECK(API(TaskTopoClone)(&clone,t)); CHECK(API(TaskTopoNodeFindInClone)(&cloned,set,clone));
  p.value=34; CHECK(API(TaskTopoMemsetNodeSetParams)(cloned,&p));
  SetParams got={0}; CHECK(API(TaskTopoMemsetNodeGetParams)(set,&got));
  expect("clone_has_independent_params",got.value==17);
  CHECK(API(TaskTopoInstantiate)(&e1,t,NULL,NULL,0));
  p.value=51; CHECK(API(TaskTopoMemsetNodeSetParams)(set,&p));
  CHECK(API(TaskTopoInstantiate)(&e2,t,NULL,NULL,0));
  CHECK(API(TaskTopoInstantiate)(&e3,clone,NULL,NULL,0));
  CHECK(API(TaskTopoDestroy)(t)); CHECK(API(TaskTopoDestroy)(clone));
  Entity entities[4]={e1,e2,e3,e1}; unsigned char values[4]={17,51,34,17};
  unsigned char host[N];
  for(int i=0;i<4;i++) {
    CHECK(fill(b,0,N)); CHECK(API(TaskTopoEntityInvoke)(entities[i],q)); CHECK(queue_sync(q));
    CHECK(readback(host,b,N)); expect("entity_snapshot_after_source_destroy",bytes_are(host,N,values[i]));
  }
  CHECK(API(TaskTopoEntityDestroy)(e1)); CHECK(API(TaskTopoEntityDestroy)(e2));
  CHECK(API(TaskTopoEntityDestroy)(e3)); CHECK(queue_destroy(q)); CHECK(release(a)); CHECK(release(b));
}
static void capture(void) {
  Addr a; Queue q; Topo t; Entity e; void *src,*dst;
  CHECK(alloc(&a,N)); CHECK(fill(a,9,N)); CHECK(host_alloc(&src,N)); CHECK(host_alloc(&dst,N));
  memset(src,21,N); memset(dst,7,N); CHECK(queue_create(&q));
  CaptureStatus status;
  CHECK(capture_status(q,&status)); expect("capture_initial_none",status==0);
  CHECK(capture_begin(q)); CHECK(capture_status(q,&status)); expect("capture_active",status==1);
  CHECK(h2d_async(a,src,N,q)); CHECK(d2h_async(dst,a,N,q));
  CHECK(capture_end(q,&t)); CHECK(capture_status(q,&status)); expect("capture_ended",status==0);
  unsigned char before[N]; CHECK(readback(before,a,N));
  expect("capture_does_not_execute",bytes_are(before,N,9) && bytes_are(dst,N,7));
  size_t count=0; CHECK(API(TaskTopoGetNodes)(t,NULL,&count)); expect("capture_copy_nodes",count==2);
  CHECK(API(TaskTopoInstantiate)(&e,t,NULL,NULL,0)); CHECK(API(TaskTopoDestroy)(t));
  for(int i=0;i<2;i++) {
    memset(src,42+i,N); memset(dst,7,N);
    CHECK(API(TaskTopoEntityInvoke)(e,q)); CHECK(queue_sync(q));
    expect("replay_reads_current_host_bytes",bytes_are(dst,N,42+i));
  }
  CHECK(API(TaskTopoEntityDestroy)(e)); CHECK(queue_destroy(q));
  CHECK(host_free(src)); CHECK(host_free(dst)); CHECK(release(a));
}
static void notifier(void) {
#ifdef USE_CNRT
  cnrtNotifier_t n=NULL;
  int r=observe("cnrtNotifierCreateWithFlags(interprocess|disable_timing_all)",
      cnrtNotifierCreateWithFlags(&n,CNRT_NOTIFIER_INTERPROCESS|CNRT_NOTIFIER_DISABLE_TIMING_ALL));
  expect("ipc_notifier_creation_not_supported",r==cnrtErrorNotSupport);
#else
  int supported=-1; CHECK(cnDeviceGetAttribute(&supported,CN_DEVICE_ATTRIBUTE_IPCNOTIFIER_SUPPORTED,0));
  expect("ipc_notifier_attribute_zero",supported==0);
  CNnotifier n=NULL;
  int r=observe("cnCreateNotifier(interprocess|disable_timing_all)",
      cnCreateNotifier(&n,CN_NOTIFIER_INTERPROCESS|CN_NOTIFIER_DISABLE_TIMING_ALL));
  expect("ipc_notifier_creation_not_supported",r==CN_OPS_ERROR_NOT_SUPPORTED);
  int vmm=-1,fd=-1;
  CHECK(cnDeviceGetAttribute(&vmm,CN_DEVICE_ATTRIBUTE_VIRTUAL_ADDRESS_MANAGEMENT_SUPPORTED,0));
  CHECK(cnDeviceGetAttribute(&fd,CN_DEVICE_ATTRIBUTE_HANDLE_TYPE_POSIX_FILE_DESCRIPTOR_SUPPORTED,0));
  printf("{\"vmm_supported\":%d,\"posix_fd_supported\":%d}\n",vmm,fd);
#endif
}
static void ipc_export(int host) {
  Addr a=0; void *h=NULL; MemHandle handle=NULL,again=NULL;
  if(host) { CHECK(host_alloc(&h,N)); a=(Addr)(uintptr_t)h; memset(h,17,N); }
  else { CHECK(alloc(&a,N)); CHECK(fill(a,17,N)); }
  CHECK(export_mem(&handle,a));
  int repeated=observe("repeated_export",export_mem(&again,a));
#ifdef USE_CNRT
  expect("sdk_repeated_export_rejects",repeated==cnrtErrorCndrvFuncCall);
#else
  expect("sdk_repeated_export_rejects",repeated==CN_MEMORY_ERROR_INVALID_ADDRESS);
#endif
  expect("first_export_is_nonzero",handle!=NULL);
  printf("{\"ready\":true,\"handle\":\"%" PRIxPTR "\",\"host\":%s}\n",
         (uintptr_t)handle,host?"true":"false"); fflush(stdout);
  char command[32]; expect("exporter_waited_for_importers",fgets(command,sizeof(command),stdin) && !strcmp(command,"done\n"));
  unsigned char result[N];
  if(host) memcpy(result,h,N); else CHECK(readback(result,a,N));
  expect("exporter_sees_imported_offset_write",bytes_are(result,64,17) && bytes_are(result+64,N-128,85) && bytes_are(result+N-64,64,17));
  if(host) CHECK(host_free(h)); else CHECK(release(a));
}
static void ipc_import(const char *text, int host) {
  MemHandle handle=(MemHandle)(uintptr_t)strtoull(text,NULL,16); Addr a=0;
  CHECK(import_mem(&a,handle)); unsigned char before[N];
  if(host) memcpy(before,(void *)(uintptr_t)a,N); else CHECK(readback(before,a,N));
  expect("import_sees_exported_bytes",bytes_are(before,N,17));
  if(host) memset((char *)(uintptr_t)a+64,85,N-128);
  else CHECK(fill((Addr)((uintptr_t)a+64),85,N-128));
  CHECK(close_mem(a));
}
#ifndef USE_CNRT
static void ipc_same_process(void) {
  Addr a=0,b=0; MemHandle handle; CNcontext importer;
  CHECK(alloc(&a,N)); CHECK(fill(a,17,N)); CHECK(export_mem(&handle,a));
  CHECK(cnCtxCreate(&importer,0,0)); int r=observe("same_process_cross_context_open",import_mem(&b,handle));
  expect("sdk_rejects_same_process_ipc_open",r==CN_MEMORY_ERROR_INVALID_ADDRESS);
  CHECK(cnCtxDestroy(importer)); CHECK(cnCtxSetCurrent(context));
  CHECK(release(a));
}
static CNmemAllocationProp vmm_props(void) {
  CNmemAllocationProp p={0}; p.type=CN_MEM_ALLOCATION_TYPE_DEFAULT;
  p.requestedHandleTypes=CN_MEM_HANDLE_TYPE_POSIX_FILE_DESCRIPTOR;
  p.location.type=CN_MEM_LOCATION_TYPE_DEVICE; p.location.id=0;
  return p;
}
static CNaddr vmm_map(CNmemGenericAllocationHandle handle, cn_uint64_t bytes) {
  CNaddr a=0; CHECK(cnMemAddressReserve(&a,bytes,0,0,0));
  CHECK(cnMemMap(a,bytes,0,handle,0));
  CNmemAccessDesc desc={0}; desc.location.type=CN_MEM_LOCATION_TYPE_DEVICE;
  desc.location.id=0; desc.accessFlags=CN_MEM_ACCESS_FLAGS_PROT_READWRITE;
  CHECK(cnMemSetAccess(a,bytes,&desc,1)); return a;
}
static void vmm_cleanup(CNaddr a, CNmemGenericAllocationHandle h, cn_uint64_t bytes) {
  CHECK(cnMemUnmap(a,bytes)); CHECK(cnMemAddressFree(a,bytes)); CHECK(cnMemRelease(h));
}
static void vmm_export(int sock,int retire) {
  CNmemAllocationProp p=vmm_props(); cn_uint64_t grain=0; CNmemGenericAllocationHandle h=0;
  CHECK(cnMemGetAllocationGranularity(&grain,&p,CN_MEM_ALLOC_GRANULARITY_MINIMUM));
  expect("vmm_granularity_bounded",grain>=N && grain<=64*1024*1024);
  CHECK(cnMemCreate(&h,grain,&p,0)); CNaddr a=vmm_map(h,grain); CHECK(fill(a,17,N));
  /* Probe the void* output's write width without a four-byte stack overwrite. */
  struct { uint64_t before,value,after; } exported={0x12345678,UINT64_C(0xa5a5a5a5a5a5a5a5),0x87654321};
  CHECK(cnMemExportToShareableHandle(&exported.value,h,CN_MEM_HANDLE_TYPE_POSIX_FILE_DESCRIPTOR,0));
  int fd=(int)(uint32_t)exported.value;
  printf("{\"vmm_export_output_hex\":\"%016" PRIx64 "\",\"granularity\":%" PRIu64 "}\n",exported.value,(uint64_t)grain);
  expect("vmm_export_output_guards",exported.before==0x12345678 && exported.after==0x87654321);
  expect("vmm_export_returns_fd",fd>=0);
  char message[32]; int len=snprintf(message,sizeof(message),"%" PRIu64,(uint64_t)grain);
  struct iovec iov={message,(size_t)len};
  union { struct cmsghdr align; char bytes[CMSG_SPACE(sizeof(int))]; } control={0};
  struct msghdr msg={0}; msg.msg_iov=&iov; msg.msg_iovlen=1;
  msg.msg_control=control.bytes; msg.msg_controllen=sizeof(control.bytes);
  struct cmsghdr *c=CMSG_FIRSTHDR(&msg); c->cmsg_level=SOL_SOCKET; c->cmsg_type=SCM_RIGHTS;
  c->cmsg_len=CMSG_LEN(sizeof(int)); memcpy(CMSG_DATA(c),&fd,sizeof(fd));
  expect("vmm_fd_sent_with_scm_rights",sendmsg(sock,&msg,0)==len);
  close(fd); close(sock);
  if(retire){vmm_cleanup(a,h,grain);finish();return;}
  char command[32]; expect("vmm_exporter_waited_for_importer",fgets(command,sizeof(command),stdin) && !strcmp(command,"done\n"));
  unsigned char result[N]; CHECK(readback(result,a,N));
  expect("vmm_shared_write_visible",bytes_are(result,64,17) && bytes_are(result+64,N-128,85) && bytes_are(result+N-64,64,17));
  vmm_cleanup(a,h,grain);
}
static void vmm_import(int fd, cn_uint64_t bytes) {
  CNmemGenericAllocationHandle h=0;
  printf("{\"import_mapping_bytes\":%" PRIu64 "}\n",(uint64_t)bytes);
  CHECK(cnMemImportFromShareableHandle(&h,(void *)(uintptr_t)fd,CN_MEM_HANDLE_TYPE_POSIX_FILE_DESCRIPTOR));
  CNaddr a=vmm_map(h,bytes); unsigned char before[N];
  CHECK(readback(before,a,N)); expect("vmm_import_sees_exported_bytes",bytes_are(before,N,17));
  CHECK(fill(a+64,85,N-128)); vmm_cleanup(a,h,bytes); close(fd);
}
#endif
int main(int argc,char **argv) {
  setvbuf(stdout,NULL,_IOLBF,0);
  if(argc<2) return 1;
  init();
  if(!strcmp(argv[1],"graph")) graph();
  else if(!strcmp(argv[1],"capture")) capture();
  else if(!strcmp(argv[1],"notifier")) notifier();
  else if(!strcmp(argv[1],"ipc-export")) ipc_export(argc>2 && !strcmp(argv[2],"host"));
  else if(!strcmp(argv[1],"ipc-import") && argc>2) ipc_import(argv[2],argc>3 && !strcmp(argv[3],"host"));
#ifndef USE_CNRT
  else if(!strcmp(argv[1],"ipc-same-process")) ipc_same_process();
  else if(!strcmp(argv[1],"vmm-export") && argc>2) {vmm_export(atoi(argv[2]),argc>3);if(argc>3)return 0;}
  else if(!strcmp(argv[1],"vmm-import") && argc>3) vmm_import(atoi(argv[2]),strtoull(argv[3],NULL,10));
#endif
  else return 1;
  finish(); return 0;
}
