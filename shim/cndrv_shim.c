#define _GNU_SOURCE
#include <cn_api.h>
#include "remote_cnrt.h"
#include "bang_image.h"
#include "bang_args.h"
#include "fake_va.h"
#include "host_memory.h"
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "local_cndrv_errors.inc"

/* One native remote session per Driver context. A process lock protects every
 * registry/RPC against destroy, and TLS represents native current context.
 * Opaque handles and reserved VAs never recycle before process exit. */
typedef struct context context;
typedef struct object {
  struct object *next;
  context *owner;
  uint64_t token,id,parent;
  unsigned kind;
  int live;
  unsigned char *image;
  size_t size;
  bang_param_layout layout;
  bang_param_types types;
} object;
struct context {
  context *next;
  uint64_t token;
  mlu_remote_context *remote;
  CNdev device;
  unsigned flags;
  int live;
};
typedef struct allocation {
  struct allocation *next;
  context *owner;
  void *base;
  size_t bytes,mapped;
  uint64_t id;
  int live;
} allocation;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static context *contexts;
static object *objects;
static allocation *allocations;
static mlu_remote_context *control;
static uint64_t next_token=0x10000000;
static _Thread_local uint64_t current;
enum { QUEUE=1, NOTIFIER, MODULE, KERNEL };
#define ENTER() pthread_mutex_lock(&lock)
static CNresult leave(CNresult status) { pthread_mutex_unlock(&lock); return status; }
#define RETURN(s) return leave((CNresult)(s))
#define INIT() do { if (!control) RETURN(CN_ERROR_NOT_INITIALIZED); } while(0)
static CNresult result(int st) {
  switch ((uint32_t)st) {
    case MLU_REMOTE_BAD_LENGTH: return CN_ERROR_INVALID_VALUE;
    case MLU_REMOTE_BAD_OBJECT: return CN_ERROR_INVALID_HANDLE;
    case MLU_REMOTE_UNSUPPORTED: return CN_OPS_ERROR_NOT_SUPPORTED;
    case MLU_REMOTE_RESOURCE_LIMIT: return CN_MEMORY_ERROR_OUT_OF_PROCESS_MAX_MEMORY;
    case MLU_REMOTE_PROTOCOL_ERROR: case MLU_REMOTE_BAD_VERSION:
    case MLU_REMOTE_TIMEOUT: return CN_ERROR_OPERATING_SYSTEM;
    default: return (CNresult)st;
  }
}
static CNresult connect_session(unsigned dev,unsigned flags,mlu_remote_context **out) {
  const char *p=getenv("MLU_REMOTE_PORT"), *host=getenv("MLU_REMOTE_HOST");
  if (p && (!*p || strspn(p,"0123456789")!=strlen(p))) return CN_ERROR_INVALID_VALUE;
  errno=0; unsigned long port=p ? strtoul(p,NULL,10) : 50051;
  if (errno || !port || port>65535 || (host && !*host)) return CN_ERROR_INVALID_VALUE;
  *out=mlu_remote_connect_driver(host ? host : "127.0.0.1",(uint16_t)port,dev,flags);
  if (*out) return CN_SUCCESS;
  const mlu_remote_error *e=mlu_remote_last_connect_error();
  return e && e->code ? result((int)e->code) : CN_ERROR_OPERATING_SYSTEM;
}
static context *find_context(uint64_t token) {
  for (context *c=contexts;c;c=c->next) if (c->live && c->token==token) return c;
  return NULL;
}
#define CURRENT() INIT(); context *c=find_context(current); if(!c) RETURN(CN_CONTEXT_ERROR_INVALID)
static object *find_object(context *c,unsigned kind,uint64_t token) {
  for (object *o=objects;o;o=o->next)
    if (o->live && o->owner==c && o->kind==kind && o->token==token) return o;
  return NULL;
}
static object *new_object(context *c,unsigned kind) {
  if (next_token==UINT64_MAX) return NULL;
  object *o=calloc(1,sizeof(*o)); if (!o) return NULL;
  o->owner=c; o->kind=kind; o->token=++next_token;
  o->next=objects; objects=o; return o;
}
static void discard_object(object *o) {
  object **link=&objects;
  while(*link && *link!=o) link=&(*link)->next;
  if(*link) *link=o->next;
  free(o->image); free(o);
}
static allocation *lookup(context *c,CNaddr value,uint64_t *offset) {
  for (allocation *a=allocations;a;a=a->next)
    if (a->live && a->owner==c && value>=(uintptr_t)a->base && value-(uintptr_t)a->base<=a->bytes) {
      *offset=value-(uintptr_t)a->base; return a;
    }
  return NULL;
}
static int bounds(context *c,CNaddr value,uint64_t bytes,uint64_t *id,uint64_t *off) {
  allocation *a=lookup(c,value,off);
  if (!a || bytes>a->bytes-*off) return -1;
  *id=a->id; return 0;
}
static int queue_id(context *c,CNqueue handle,uint64_t *id) {
  if (!handle) { *id=0; return 0; }
  object *o=find_object(c,QUEUE,(uintptr_t)handle);
  if (!o) return -1;
  *id=o->id; return 0;
}
static int release(context *c,int st,uint64_t transfer) {
  return st ? st : mlu_remote_transfer_release(c->remote,transfer);
}

CNresult cnInit(unsigned flags) {
  /* Measured native flags=0 and 1 are accepted; flag is reserved by SDK. */
  (void)flags; ENTER();
  CNresult st=control ? CN_SUCCESS : connect_session(0,0,&control);
  RETURN(st);
}
CNresult cnDeviceGetCount(int *count) {
  ENTER(); INIT(); if(!count) RETURN(CN_ERROR_INVALID_VALUE);
  uint32_t n=0; int st=mlu_remote_device_count(control,&n); if(!st) *count=(int)n;
  RETURN(result(st));
}
CNresult cnDeviceGet(CNdev *dev,int ordinal) {
  ENTER(); INIT(); if(!dev) RETURN(CN_ERROR_INVALID_VALUE);
  uint32_t n=0; int st=mlu_remote_device_count(control,&n);
  if(st) RETURN(result(st));
  if(ordinal<0 || (unsigned)ordinal>=n) RETURN(CN_ERROR_INVALID_DEVICE);
  *dev=ordinal; RETURN(CN_SUCCESS);
}
CNresult cnDeviceGetName(char *name,unsigned len,CNdev dev) {
  ENTER(); INIT(); if(!name || !len) RETURN(CN_ERROR_INVALID_VALUE);
  if((uint32_t)dev>INT_MAX) RETURN(CN_ERROR_INVALID_DEVICE);
  RETURN(result(mlu_remote_device_query(control,(uint32_t)dev,name,len,NULL)));
}
CNresult cnDeviceTotalMem(cn_uint64_t *bytes,CNdev dev) {
  ENTER(); INIT(); if(!bytes) RETURN(CN_ERROR_INVALID_VALUE);
  if((uint32_t)dev>INT_MAX) RETURN(CN_ERROR_INVALID_DEVICE);
  uint64_t value=0; int st=mlu_remote_device_query(control,(uint32_t)dev,NULL,0,&value);
  if(!st) *bytes=value; RETURN(result(st));
}
CNresult cnCtxCreate(CNcontext *out,unsigned flags,CNdev dev) {
  ENTER(); INIT(); if(!out || flags>=CN_CTX_SCHED_SYNC_NUM) RETURN(CN_ERROR_INVALID_VALUE);
  /* This SDK exposes a 64-bit CNdev but native uses its low 32-bit ordinal. */
  dev=(uint32_t)dev;
  if(dev>INT_MAX) RETURN(CN_ERROR_INVALID_DEVICE);
  if(next_token==UINT64_MAX) RETURN(CN_ERROR_SYSTEM_OUT_OF_MEMORY);
  context *c=calloc(1,sizeof(*c)); if(!c) RETURN(CN_ERROR_SYSTEM_OUT_OF_MEMORY);
  CNresult st=connect_session((unsigned)dev,flags,&c->remote);
  if(st) { free(c); RETURN(st); }
  c->token=++next_token; c->device=dev; c->flags=flags; c->live=1;
  c->next=contexts; contexts=c; current=c->token; *out=(CNcontext)(uintptr_t)c->token;
  RETURN(CN_SUCCESS);
}
CNresult cnCtxGetCurrent(CNcontext *out) {
  ENTER(); INIT(); if(!out) RETURN(CN_ERROR_INVALID_VALUE);
  context *c=find_context(current); *out=c ? (CNcontext)(uintptr_t)c->token : NULL;
  RETURN(CN_SUCCESS);
}
CNresult cnCtxSetCurrent(CNcontext handle) {
  ENTER(); INIT(); uint64_t token=(uintptr_t)handle;
  if(token && !find_context(token)) RETURN(CN_CONTEXT_ERROR_INVALID);
  current=token; RETURN(CN_SUCCESS);
}
CNresult cnCtxGetDevice(CNdev *dev) { ENTER(); CURRENT(); if(!dev) RETURN(CN_ERROR_INVALID_VALUE); *dev=c->device; RETURN(CN_SUCCESS); }
CNresult cnCtxGetFlags(unsigned *flags) { ENTER(); CURRENT(); if(!flags) RETURN(CN_ERROR_INVALID_VALUE); *flags=c->flags; RETURN(CN_SUCCESS); }
CNresult cnCtxSync(void) { ENTER(); CURRENT(); RETURN(result(mlu_remote_context_sync(c->remote))); }
CNresult cnCtxDestroy(CNcontext handle) {
  ENTER(); INIT(); context *c=find_context((uintptr_t)handle); if(!c) RETURN(CN_CONTEXT_ERROR_INVALID);
  int st=mlu_remote_session_close(c->remote);
  /* Even on transport failure the old context can never be reused/replayed. */
  mlu_remote_disconnect(c->remote); c->remote=NULL; c->live=0;
  for (object *o=objects;o;o=o->next) if(o->owner==c) { o->live=0; free(o->image); o->image=NULL; }
  for (allocation *a=allocations;a;a=a->next) if(a->owner==c) a->live=0;
  if(current==c->token) current=0;
  RETURN(result(st));
}
static CNresult allocate_kind(context *c,CNaddr *out,cn_uint64_t bytes,unsigned kind,int node) {
  if(!out || bytes>SIZE_MAX-8191) return CN_ERROR_INVALID_VALUE;
  allocation *a=NULL;
  if(bytes) {
    a=calloc(1,sizeof(*a)); if(!a) return CN_ERROR_SYSTEM_OUT_OF_MEMORY;
    /* A guard page keeps one-past addresses separate from adjacent mappings. */
    a->mapped=(((size_t)bytes+4095)&~(size_t)4095)+4096;
    a->base=mmap(NULL,a->mapped,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if(a->base==MAP_FAILED) { free(a); return CN_ERROR_SYSTEM_OUT_OF_MEMORY; }
  }
  uint64_t id=0; int st;
  if(!kind) st=bytes ? mlu_remote_alloc(c->remote,bytes,&id) : 0;
  else {
    mlu_api_allocation_kind_request in={.bytes=bytes,.kind=kind,.node=node};
    mlu_api_allocation_kind_response value={0};
    st=mlu_remote_api_allocation_kind(c->remote,&in,&value); id=value.allocation;
  }
  if(st) { if(a) { munmap(a->base,a->mapped); free(a); } return result(st); }
  if(!bytes) return CN_SUCCESS; /* Native leaves *out unchanged. */
  if(!id) { munmap(a->base,a->mapped); free(a); return CN_ERROR_OPERATING_SYSTEM; }
  a->owner=c; a->bytes=bytes; a->id=id; a->live=1; a->next=allocations; allocations=a;
  *out=(CNaddr)(uintptr_t)a->base; return CN_SUCCESS;
}
CNresult cnMalloc(CNaddr *out,cn_uint64_t bytes) {
  ENTER(); CURRENT(); RETURN(allocate_kind(c,out,bytes,0,0));
}
CNresult cnMallocNode(CNaddr *out,cn_uint64_t bytes,int node) {
  ENTER(); CURRENT(); RETURN(allocate_kind(c,out,bytes,1,node));
}
CNresult cnZmalloc(CNaddr *out,cn_uint64_t bytes) {
  ENTER(); CURRENT(); RETURN(allocate_kind(c,out,bytes,2,0));
}
CNresult cnZmallocNode(CNaddr *out,cn_uint64_t bytes,int node) {
  ENTER(); CURRENT(); RETURN(allocate_kind(c,out,bytes,3,node));
}
CNresult cnMallocConstant(CNaddr *out,cn_uint64_t bytes) {
  ENTER(); CURRENT(); RETURN(allocate_kind(c,out,bytes,4,0));
}
CNresult cnMallocNodeConstant(CNaddr *out,cn_uint64_t bytes,int node) {
  ENTER(); CURRENT(); RETURN(allocate_kind(c,out,bytes,5,node));
}
CNresult cnMallocSecurity(CNaddr *out,cn_uint64_t bytes) {
  ENTER(); CURRENT(); RETURN(allocate_kind(c,out,bytes,6,0));
}
CNresult cnMallocFrameBuffer(CNaddr *out,cn_uint64_t bytes,int node) {
  ENTER(); CURRENT(); RETURN(allocate_kind(c,out,bytes,7,node));
}

CNresult cnFree(CNaddr value) {
  ENTER(); CURRENT(); if(!value) RETURN(CN_SUCCESS);
  uint64_t off; allocation *a=lookup(c,value,&off);
  if(!a || off) RETURN(CN_MEMORY_ERROR_INVALID_ADDRESS);
  int st=mlu_remote_free(c->remote,a->id); if(!st) a->live=0;
  RETURN(result(st));
}
static CNresult copy(context *c,int direction,CNaddr dst,CNaddr src,const void *in,void *out,
                       cn_uint64_t bytes,CNqueue queue,int async) {
  uint64_t q=0,di=0,si=0,doff=0,soff=0,transfer=0;
  if(queue_id(c,queue,&q)) return CN_QUEUE_ERROR_INVALID;
  if(!bytes) return CN_SUCCESS;
  if((direction!=MLU_MEMCPY_D2H && bounds(c,dst,bytes,&di,&doff)) ||
     (direction!=MLU_MEMCPY_H2D && bounds(c,src,bytes,&si,&soff))) return CN_MEMORY_ERROR_INVALID_ADDRESS;
  int st;
  if(direction==MLU_MEMCPY_H2D) {
    if(!in || mlu_host_range(in,bytes)<0) return CN_ERROR_INVALID_VALUE;
    st=async ? mlu_remote_memcpy_h2d_async(c->remote,di,doff,in,bytes,q,&transfer)
             : mlu_remote_memcpy_h2d(c->remote,di,doff,in,bytes,q,&transfer);
  } else if(direction==MLU_MEMCPY_D2H) {
    if(!out) return CN_ERROR_INVALID_VALUE;
    if(mlu_host_range(out,bytes)<0) return CN_ERROR_INVALID_VALUE;
    st=async ? mlu_remote_memcpy_d2h_async(c->remote,out,si,soff,bytes,q,&transfer)
             : mlu_remote_memcpy_d2h(c->remote,out,si,soff,bytes,q,&transfer);
  } else st=async ? mlu_remote_memcpy_d2d_async(c->remote,di,doff,si,soff,bytes,q,&transfer)
                  : mlu_remote_memcpy_d2d(c->remote,di,doff,si,soff,bytes,q,&transfer);
  if(!st && !async) st=mlu_remote_transfer_sync(c->remote,transfer);
  return result(release(c,st,transfer));
}
/* Unified-copy direction comes from registry ownership, never native device
 * pointer bits. Tombstones/guard pages must not be treated as host memory. */
static int unified_device(context *c,CNaddr p,cn_uint64_t bytes) {
  for(allocation *a=allocations;a;a=a->next) {
    int kind=mlu_fake_va_endpoint(p,bytes,(uintptr_t)a->base,a->bytes,a->mapped,a->live,a->owner==c);
    if(kind!=MLU_ENDPOINT_HOST) return kind;
  }
  return mlu_host_range((void *)(uintptr_t)p,bytes)<0 ? MLU_ENDPOINT_INVALID : 0;
}
CNresult cnMemcpy(CNaddr dst,CNaddr src,cn_uint64_t bytes) {
  ENTER(); CURRENT(); if(!bytes) RETURN(CN_SUCCESS);
  if(!dst || !src) RETURN(CN_ERROR_INVALID_VALUE);
  int d=unified_device(c,dst,bytes),s=unified_device(c,src,bytes);
  if(d<0 || s<0) RETURN(CN_MEMORY_ERROR_INVALID_ADDRESS);
  if(d && s) RETURN(copy(c,MLU_MEMCPY_D2D,dst,src,NULL,NULL,bytes,NULL,0));
  if(d) RETURN(copy(c,MLU_MEMCPY_H2D,dst,0,(void *)(uintptr_t)src,NULL,bytes,NULL,0));
  if(s) RETURN(copy(c,MLU_MEMCPY_D2H,0,src,NULL,(void *)(uintptr_t)dst,bytes,NULL,0));
  memmove((void *)(uintptr_t)dst,(void *)(uintptr_t)src,(size_t)bytes); RETURN(CN_SUCCESS);
}
CNresult cnMemcpyHtoD(CNaddr dst,const void *src,cn_uint64_t n) { ENTER(); CURRENT(); RETURN(copy(c,1,dst,0,src,NULL,n,NULL,0)); }
CNresult cnMemcpyDtoH(void *dst,CNaddr src,cn_uint64_t n) { ENTER(); CURRENT(); RETURN(copy(c,2,0,src,NULL,dst,n,NULL,0)); }
CNresult cnMemcpyDtoD(CNaddr dst,CNaddr src,cn_uint64_t n) { ENTER(); CURRENT(); RETURN(copy(c,3,dst,src,NULL,NULL,n,NULL,0)); }
CNresult cnMemcpyHtoDAsync(CNaddr dst,const void *src,cn_uint64_t n,CNqueue q) { ENTER(); CURRENT(); RETURN(copy(c,1,dst,0,src,NULL,n,q,1)); }
CNresult cnMemcpyDtoDAsync(CNaddr dst,CNaddr src,cn_uint64_t n,CNqueue q) { ENTER(); CURRENT(); RETURN(copy(c,3,dst,src,NULL,NULL,n,q,1)); }
CNresult cnMemcpyAsync(CNaddr dst,CNaddr src,cn_uint64_t n,CNqueue q) {
  ENTER(); CURRENT(); uint64_t queue;
  if(queue_id(c,q,&queue)) RETURN(CN_QUEUE_ERROR_INVALID);
  if(!n) RETURN(CN_SUCCESS);
  if(!dst || !src) RETURN(CN_ERROR_INVALID_VALUE);
  int d=unified_device(c,dst,n),s=unified_device(c,src,n);
  if(d<0 || s<0) RETURN(CN_MEMORY_ERROR_INVALID_ADDRESS);
  if(d && s) RETURN(copy(c,MLU_MEMCPY_D2D,dst,src,NULL,NULL,n,q,1));
  if(d) RETURN(copy(c,MLU_MEMCPY_H2D,dst,0,(void *)(uintptr_t)src,NULL,n,q,1));
  if(s) RETURN(copy(c,MLU_MEMCPY_D2H,0,src,NULL,(void *)(uintptr_t)dst,n,q,1));
  RETURN(CN_OPS_ERROR_NOT_SUPPORTED);
}
CNresult cnMemcpyDtoHAsync(void *dst,CNaddr src,cn_uint64_t n,CNqueue q) { ENTER(); CURRENT(); RETURN(copy(c,2,0,src,NULL,dst,n,q,1)); }
CNresult cnMallocHost(void **p,cn_uint64_t n) { ENTER(); INIT(); RETURN(result(mlu_host_alloc(p,n))); }
CNresult cnFreeHost(void *p) { ENTER(); INIT(); RETURN(result(mlu_host_free(p))); }

CNresult cnCreateQueue(CNqueue *out,unsigned flags) {
  ENTER(); CURRENT(); if(!out) RETURN(CN_ERROR_INVALID_VALUE);
  object *o=new_object(c,QUEUE); if(!o) RETURN(CN_ERROR_SYSTEM_OUT_OF_MEMORY);
  mlu_api_queue_create_request in={.flags=flags}; mlu_api_queue_create_response value={0};
  int st=mlu_remote_api_queue_create(c->remote,&in,&value); if(!st) o->id=value.id; if(!st) { o->live=1; *out=(CNqueue)(uintptr_t)o->token; }
  else discard_object(o);
  RETURN(result(st));
}
CNresult cnDestroyQueue(CNqueue handle) {
  ENTER(); CURRENT(); object *o=find_object(c,QUEUE,(uintptr_t)handle); if(!o) RETURN(CN_QUEUE_ERROR_INVALID);
  int st=mlu_remote_queue_destroy(c->remote,o->id); if(!st) o->live=0; RETURN(result(st));
}
CNresult cnQueueSync(CNqueue handle) {
  ENTER(); CURRENT(); if(!handle) RETURN(result(mlu_remote_queue_sync(c->remote,0)));
  object *o=find_object(c,QUEUE,(uintptr_t)handle); RETURN(o ? result(mlu_remote_queue_sync(c->remote,o->id)) : CN_QUEUE_ERROR_INVALID);
}
CNresult cnQueryQueue(CNqueue handle) {
  ENTER(); CURRENT(); if(!handle) RETURN(result(mlu_remote_queue_query(c->remote,0)));
  object *o=find_object(c,QUEUE,(uintptr_t)handle); RETURN(o ? result(mlu_remote_queue_query(c->remote,o->id)) : CN_QUEUE_ERROR_INVALID);
}
CNresult cnCreateNotifier(CNnotifier *out,unsigned flags) {
  ENTER(); CURRENT(); if(!out) RETURN(CN_ERROR_INVALID_VALUE);
  object *o=new_object(c,NOTIFIER); if(!o) RETURN(CN_ERROR_SYSTEM_OUT_OF_MEMORY);
  mlu_api_notifier_create_request in={.flags=flags}; mlu_api_notifier_create_response value={0};
  int st=mlu_remote_api_notifier_create(c->remote,&in,&value); if(!st) o->id=value.id; if(!st) { o->live=1; *out=(CNnotifier)(uintptr_t)o->token; }
  else discard_object(o);
  RETURN(result(st));
}
CNresult cnDestroyNotifier(CNnotifier handle) {
  ENTER(); CURRENT(); object *o=find_object(c,NOTIFIER,(uintptr_t)handle); if(!o) RETURN(CN_ERROR_INVALID_HANDLE);
  int st=mlu_remote_notifier_destroy(c->remote,o->id); if(!st) o->live=0; RETURN(result(st));
}
CNresult cnPlaceNotifier(CNnotifier handle,CNqueue queue) {
  ENTER(); CURRENT(); object *o=find_object(c,NOTIFIER,(uintptr_t)handle); uint64_t q=0;
  if(!o || queue_id(c,queue,&q)) RETURN(CN_ERROR_INVALID_HANDLE);
  RETURN(result(mlu_remote_notifier_record(c->remote,o->id,q)));
}
CNresult cnWaitNotifier(CNnotifier handle) { ENTER(); CURRENT(); object *o=find_object(c,NOTIFIER,(uintptr_t)handle); RETURN(o ? result(mlu_remote_notifier_sync(c->remote,o->id)) : CN_ERROR_INVALID_HANDLE); }
CNresult cnQueryNotifier(CNnotifier handle) { ENTER(); CURRENT(); object *o=find_object(c,NOTIFIER,(uintptr_t)handle); RETURN(o ? result(mlu_remote_notifier_query(c->remote,o->id)) : CN_ERROR_INVALID_HANDLE); }

/* Never inspect bytes outside a readable mapping. This is a Linux host ABI
 * adapter, not a general arbitrary-pointer validator or security sandbox. */
static size_t readable(const void *p) {
  FILE *f=fopen("/proc/self/maps","r"); if(!f) return 0;
  char line[512],perm[5]; unsigned long lo,hi; uintptr_t value=(uintptr_t)p; size_t n=0;
  while(fgets(line,sizeof(line),f)) if(sscanf(line,"%lx-%lx %4s",&lo,&hi,perm)==3 &&
      perm[0]=='r' && value>=lo && value<hi) { n=hi-value; break; }
  fclose(f); return n;
}
static CNresult load(context *c,const void *image,size_t size,CNmodule *out) {
  if(bang_image_validate(image,size)) return CN_OPS_ERROR_NOT_SUPPORTED;
  object *o=new_object(c,MODULE); if(!o) return CN_ERROR_SYSTEM_OUT_OF_MEMORY;
  o->image=malloc(size); if(!o->image) { discard_object(o); return CN_ERROR_SYSTEM_OUT_OF_MEMORY; }
  memcpy(o->image,image,size); o->size=size;
  int st=mlu_remote_module_load_binary(c->remote,o->image,size,&o->id);
  if(st) { discard_object(o); return result(st); }
  o->live=1; *out=(CNmodule)(uintptr_t)o->token; return CN_SUCCESS;
}
static CNresult query_fatbinary_memory(context *c,const void *image,size_t size,size_t *out) {
  size_t exact=0;
  if(!image || !size || !out || bang_image_span(image,size,&exact) || exact!=size)
    return CN_OPS_ERROR_NOT_SUPPORTED;
  uint64_t usage=0;
  int st=mlu_remote_module_query_fatbinary_memory(c->remote,image,size,&usage);
  if(!st) *out=(size_t)usage;
  return result(st);
}
CNresult cnModuleQueryFatBinaryMemoryUsage(const void *fatbin,size_t *pbytes) {
  ENTER(); CURRENT(); if(!fatbin || !pbytes) RETURN(CN_ERROR_INVALID_VALUE);
  size_t size=0,available=readable(fatbin);
  if(bang_image_span(fatbin,available,&size)) RETURN(CN_OPS_ERROR_NOT_SUPPORTED);
  RETURN(query_fatbinary_memory(c,fatbin,size,pbytes));
}
CNresult cnModuleQueryMemoryUsage(const char *path,size_t *pbytes) {
  ENTER(); CURRENT(); if(!path || !pbytes) RETURN(CN_ERROR_INVALID_VALUE);
  FILE *f=fopen(path,"rb"); if(!f) RETURN(errno==ENOENT ? CN_ERROR_FILE_NOT_FOUND : CN_ERROR_OPERATING_SYSTEM);
  if(fseek(f,0,SEEK_END)) { fclose(f); RETURN(CN_ERROR_OPERATING_SYSTEM); }
  long length=ftell(f);
  if(length<=0 || (unsigned long)length>MLU_REMOTE_MAX_PAYLOAD-4) { fclose(f); RETURN(CN_OPS_ERROR_NOT_SUPPORTED); }
  rewind(f); void *image=malloc((size_t)length);
  if(!image) { fclose(f); RETURN(CN_ERROR_SYSTEM_OUT_OF_MEMORY); }
  int ok=fread(image,1,(size_t)length,f)==(size_t)length; fclose(f);
  CNresult st=ok ? query_fatbinary_memory(c,image,(size_t)length,pbytes) : CN_ERROR_OPERATING_SYSTEM;
  free(image); RETURN(st);
}
CNresult cnModuleLoadFatBinary(const void *image,CNmodule *out) {
  ENTER(); CURRENT(); if(!image || !out) RETURN(CN_ERROR_INVALID_VALUE);
  size_t size=0,available=readable(image);
  if(bang_image_span(image,available,&size)) RETURN(CN_OPS_ERROR_NOT_SUPPORTED);
  RETURN(load(c,image,size,out));
}
CNresult cnModuleLoad(const char *path,CNmodule *out) {
  ENTER(); CURRENT(); if(!path || !out) RETURN(CN_ERROR_INVALID_VALUE);
  FILE *f=fopen(path,"rb"); if(!f) RETURN(errno==ENOENT ? CN_ERROR_FILE_NOT_FOUND : CN_ERROR_OPERATING_SYSTEM);
  if(fseek(f,0,SEEK_END)) { fclose(f); RETURN(CN_ERROR_OPERATING_SYSTEM); }
  long size=ftell(f);
  if(size<=0 || (unsigned long)size>MLU_REMOTE_MAX_PAYLOAD-4) { fclose(f); RETURN(CN_OPS_ERROR_NOT_SUPPORTED); }
  rewind(f); void *image=malloc((size_t)size); if(!image) { fclose(f); RETURN(CN_ERROR_SYSTEM_OUT_OF_MEMORY); }
  int ok=fread(image,1,(size_t)size,f)==(size_t)size; fclose(f);
  CNresult st=ok ? load(c,image,(size_t)size,out) : CN_ERROR_OPERATING_SYSTEM;
  free(image); RETURN(st);
}
CNresult cnModuleUnload(CNmodule handle) {
  ENTER(); CURRENT(); object *m=find_object(c,MODULE,(uintptr_t)handle); if(!m) RETURN(CN_ERROR_INVALID_HANDLE);
  int st=mlu_remote_module_unload_preset(c->remote,m->id);
  if(!st) { m->live=0; free(m->image); m->image=NULL;
    for(object *o=objects;o;o=o->next) if(o->owner==c && o->kind==KERNEL && o->parent==m->token) o->live=0;
  }
  RETURN(result(st));
}
CNresult cnModuleGetKernel(CNmodule handle,const char *name,CNkernel *out) {
  ENTER(); CURRENT(); if(!out || !name || !*name || strlen(name)>=256) RETURN(CN_ERROR_INVALID_VALUE);
  object *m=find_object(c,MODULE,(uintptr_t)handle); if(!m) RETURN(CN_ERROR_INVALID_HANDLE);
  uint64_t kid=0; int st=mlu_remote_module_get_kernel(c->remote,m->id,name,&kid);
  if(st) { if(st==CN_ERROR_NOT_FOUND) *out=NULL; RETURN(result(st)); }
  for(object *o=objects;o;o=o->next) if(o->live && o->owner==c && o->kind==KERNEL && o->id==kid) { *out=(CNkernel)(uintptr_t)o->token; RETURN(CN_SUCCESS); }
  bang_param_layout layout; bang_param_types types={0};
  if(bang_image_params(m->image,m->size,name,&layout)) RETURN(CN_OPS_ERROR_NOT_SUPPORTED);
  int metadata=bang_metadata_classify_image(m->image,m->size,name,&layout,&types);
  if(metadata<0 || (metadata==1 && bang_classify_params(name,&layout,types.kind))) RETURN(CN_OPS_ERROR_NOT_SUPPORTED);
  object *k=new_object(c,KERNEL); if(!k) RETURN(CN_ERROR_SYSTEM_OUT_OF_MEMORY);
  k->id=kid; k->parent=m->token; k->layout=layout; k->types=types;
  k->live=1; *out=(CNkernel)(uintptr_t)k->token; RETURN(CN_SUCCESS);
}
static int resolve_kernel_pointer(void *owner,uint64_t ptr,uint64_t *id,uint64_t *offset) {
  return bounds(owner,(CNaddr)ptr,0,id,offset);
}
CNresult cnInvokeKernel(CNkernel handle,unsigned x,unsigned y,unsigned z,KernelClass cls,
                          unsigned reserved,CNqueue queue,void **params,void **extra) {
  ENTER(); CURRENT(); object *k=find_object(c,KERNEL,(uintptr_t)handle);
  if(!k) RETURN(CN_ERROR_INVALID_KERNEL);
  object *m=find_object(c,MODULE,k->parent);
  if(!m) RETURN(CN_ERROR_INVALID_KERNEL);
  if(reserved || extra) RETURN(CN_OPS_ERROR_NOT_SUPPORTED);
  uint64_t q; if(queue_id(c,queue,&q)) RETURN(CN_QUEUE_ERROR_INVALID);
  if(!x || !y || !z || (k->layout.count && !params)) RETURN(CN_ERROR_INVALID_VALUE);
  mlu_remote_kernel_arg args[BANG_MAX_ARGS]={0};
  mlu_remote_kernel_relocation relocations[BANG_MAX_RELOCS];
  int packed=bang_pack_args(&k->layout,&k->types,params,resolve_kernel_pointer,c,args,relocations);
  if(packed) RETURN(packed==-1 ? CN_ERROR_INVALID_VALUE : CN_MEMORY_ERROR_INVALID_ADDRESS);
  uint64_t transfer=0; int st=mlu_remote_kernel_launch_generic(c->remote,m->id,k->id,q,x,y,z,cls,args,k->layout.count,&transfer);
  RETURN(result(release(c,st,transfer)));
}
__attribute__((destructor)) static void finish(void) {
  ENTER(); for(context *c=contexts;c;c=c->next) if(c->remote) mlu_remote_disconnect(c->remote);
  if(control) mlu_remote_disconnect(control);
  pthread_mutex_unlock(&lock);
}
#include "driver_queries.inc"
#include "graph_memset.inc"
#include "cndrv_api.inc"

CNresult cnCreateQueueWithPriority(CNqueue *out,unsigned flags,int priority) {
  ENTER(); CURRENT(); if(!out) RETURN(CN_ERROR_INVALID_VALUE);
  object *q=new_object(c,QUEUE); if(!q) RETURN(CN_ERROR_SYSTEM_OUT_OF_MEMORY);
  mlu_api_queue_create_request in={.flags=flags,.priority=priority}; mlu_api_queue_create_response value={0};
  int st=mlu_remote_api_queue_create(c->remote,&in,&value);
  if(!st) { q->id=value.id; q->live=1; *out=(CNqueue)(uintptr_t)q->token; } else discard_object(q);
  RETURN(result(st));
}
static CNresult fill(context *c,CNaddr ptr,uint32_t value,uint32_t width,uint64_t count,CNqueue queue,int async) {
  mlu_api_memset_request in={.count=count,.value=value,.width=width,.async=async}; mlu_api_memset_response out={0};
  if(count>UINT64_MAX/width || bounds(c,ptr,count*width,&in.allocation,&in.offset)) return CN_MEMORY_ERROR_INVALID_ADDRESS;
  if(queue_id(c,queue,&in.queue)) return CN_QUEUE_ERROR_INVALID;
  return result(mlu_remote_api_memset(c->remote,&in,&out));
}
#define FILL(bits,type) \
CNresult cnMemsetD##bits(CNaddr ptr,type value,cn_uint64_t count) { ENTER(); CURRENT(); RETURN(fill(c,ptr,value,bits/8,count,NULL,0)); } \
CNresult cnMemsetD##bits##Async(CNaddr ptr,type value,cn_uint64_t count,CNqueue queue) { ENTER(); CURRENT(); RETURN(fill(c,ptr,value,bits/8,count,queue,1)); }
FILL(8,unsigned char)
FILL(16,unsigned short)
FILL(32,unsigned int)
#undef FILL
CNresult cnMemcpyDtoD2D(CNaddr dst,cn_uint64_t dpitch,CNaddr src,cn_uint64_t spitch,cn_uint64_t width,cn_uint64_t height) {
  ENTER(); CURRENT(); mlu_api_copy2d_request in={.dpitch=dpitch,.spitch=spitch,.width=width,.height=height}; mlu_api_copy2d_response out={0};
  if(bounds(c,dst,0,&in.dst,&in.dst_offset) || bounds(c,src,0,&in.src,&in.src_offset)) RETURN(CN_MEMORY_ERROR_INVALID_ADDRESS);
  RETURN(result(mlu_remote_api_copy2d(c->remote,&in,&out)));
}
CNresult cnMemcpy2D(CNaddr dst,cn_uint64_t dpitch,CNaddr src,cn_uint64_t spitch,cn_uint64_t width,cn_uint64_t height) {
  return cnMemcpyDtoD2D(dst,dpitch,src,spitch,width,height);
}
CNresult cnMemcpyDtoD3D(CNmemcpy3dParam *p) {
  ENTER(); CURRENT(); if(!p) RETURN(CN_ERROR_INVALID_VALUE);
  mlu_api_copy3d_request in={.dpitch=p->dstPtr.pitch,.dysize=p->dstPtr.ysize,.dx=p->dstPos.x,.dy=p->dstPos.y,.dz=p->dstPos.z,
    .spitch=p->srcPtr.pitch,.sysize=p->srcPtr.ysize,.sx=p->srcPos.x,.sy=p->srcPos.y,.sz=p->srcPos.z,
    .width=p->extent.width,.height=p->extent.height,.depth=p->extent.depth}; mlu_api_copy3d_response out={0};
  if(bounds(c,(uintptr_t)(p->dst ? p->dst : p->dstPtr.ptr),0,&in.dst,&in.dst_offset) ||
     bounds(c,(uintptr_t)(p->src ? p->src : p->srcPtr.ptr),0,&in.src,&in.src_offset)) RETURN(CN_MEMORY_ERROR_INVALID_ADDRESS);
  RETURN(result(mlu_remote_api_copy3d(c->remote,&in,&out)));
}
CNresult cnMemcpy3D(CNmemcpy3dParam *p) { return cnMemcpyDtoD3D(p); }

CNresult cnGetCtxConfigParam_pt(CNcontext handle,CNctxConfigParamType type,CNctxConfigParam *param) {
  ENTER(); INIT(); if(!param) RETURN(CN_ERROR_INVALID_VALUE);
  context *c=find_context((uintptr_t)handle); if(!c) RETURN(CN_CONTEXT_ERROR_INVALID);
  mlu_api_ctx_config_request in={.type=(uint32_t)type,.set=0}; mlu_api_ctx_config_response out={0};
  int st=mlu_remote_api_ctx_config(c->remote,&in,&out);
  if(!st) param->visibleCluster=out.value;
  RETURN(result(st));
}
CNresult cnSetCtxConfigParam_pt(CNcontext handle,CNctxConfigParamType type,CNctxConfigParam *param) {
  ENTER(); INIT(); if(!param) RETURN(CN_ERROR_INVALID_VALUE);
  context *c=find_context((uintptr_t)handle); if(!c) RETURN(CN_CONTEXT_ERROR_INVALID);
  mlu_api_ctx_config_request in={.type=(uint32_t)type,.value=param->visibleCluster,.set=1}; mlu_api_ctx_config_response out={0};
  RETURN(result(mlu_remote_api_ctx_config(c->remote,&in,&out)));
}
