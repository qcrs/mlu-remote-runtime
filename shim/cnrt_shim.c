#define _GNU_SOURCE
#include <cnrt.h>
#include "bang_registration.h"
#include "remote_cnrt.h"
#include "bang_image.h"
#include <dlfcn.h>
#include <elf.h>
#include <link.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

/* State and RPCs share a lock: reset/free cannot race a launch or a copy. */
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static mlu_remote_context *ctx;
static int device;
static __thread cnrtRet_t last_error;
#define ENTER() pthread_mutex_lock(&lock)
static cnrtRet_t leave(cnrtRet_t r) {
  if (r) last_error=r;
  pthread_mutex_unlock(&lock); return r;
}
#define RETURN(r) return leave((cnrtRet_t)(r))
static cnrtRet_t status(int st) {
  switch ((uint32_t)st) {
    case 0: case 1: case 100002: case 100004: case 100005: case 100006:
    case 100007: case 100010: case 100014: case 100015: case 100050:
    case 100051: case 100060: case 100100: case 100128: case 100132:
    case 100133: case 100301: case 100302: return (cnrtRet_t)st;
    case MLU_REMOTE_UNSUPPORTED: return cnrtErrorNotSupport;
    case MLU_REMOTE_BAD_LENGTH: return cnrtErrorArgsInvalid;
    case MLU_REMOTE_BAD_OBJECT: return cnrtErrorInvalidResourceHandle;
    case MLU_REMOTE_RESOURCE_LIMIT: return cnrtErrorNoMem;
    case MLU_REMOTE_PROTOCOL_ERROR: case MLU_REMOTE_BAD_VERSION:
    case MLU_REMOTE_TIMEOUT: return cnrtErrorSys;
    default: return cnrtErrorCndrvFuncCall;
  }
}
static cnrtRet_t connect_remote(void) {
  if (ctx) return cnrtSuccess;
  const char *port=getenv("MLU_REMOTE_PORT"), *host=getenv("MLU_REMOTE_HOST");
  char *end=NULL; unsigned long n=port ? strtoul(port,&end,10) : 50051;
  if (!n || n>65535 || (port && (!*port || *end))) return cnrtErrorArgsInvalid;
  ctx=mlu_remote_connect(host ? host : "127.0.0.1",(uint16_t)n);
  if (!ctx) return cnrtErrorInit;
  const char *d=getenv("MLU_REMOTE_DEVICE"); device=d ? atoi(d) : 0;
  return cnrtSuccess;
}
#define CONNECT() do { cnrtRet_t s=connect_remote(); if(s) RETURN(s); } while(0)

typedef struct allocation {
  struct allocation *next;
  void *base;
  size_t bytes, mapped;
  uint64_t id;
  int live;
} allocation;
static allocation *allocations;
static allocation *lookup(const void *p, uint64_t *off) {
  uintptr_t v=(uintptr_t)p;
  for (allocation *a=allocations;a;a=a->next) if(a->live && v>=(uintptr_t)a->base && v-(uintptr_t)a->base<=a->bytes) {
    *off=v-(uintptr_t)a->base; return a;
  }
  return NULL;
}
/* Handles are monotonic tokens, never dereferenced and never recycled. */
typedef struct { uint64_t token,id; int live; } object;
static object queues[256], notifiers[256];
static uint64_t next_token=0x400000, default_queue;
static object *get_object(object *table, uint64_t token) {
  for(unsigned i=0;i<256;++i) if(table[i].live && table[i].token==token) return table+i;
  return NULL;
}
static object *new_object(object *table) {
  for(unsigned i=0;i<256;++i) if(!table[i].live) return table+i;
  return NULL;
}
static cnrtRet_t create_queue(uint64_t *token) {
  object *q=new_object(queues); if(!q) return cnrtErrorSysNoMem;
  uint64_t id; int st=mlu_remote_queue_create(ctx,&id); if(st) return status(st);
  *q=(object){++next_token,id,1}; *token=q->token; return cnrtSuccess;
}
static object *get_queue(cnrtQueue_t handle) {
  uint64_t token=(uintptr_t)handle;
  if(!token) {
    if(!default_queue && create_queue(&default_queue)) return NULL;
    token=default_queue;
  }
  return get_object(queues,token);
}
static cnrtRet_t release_transfer(uint64_t id) {
  return status(mlu_remote_transfer_release(ctx,id));
}

typedef struct registration {
  struct registration *next;
  unsigned char *image;
  size_t size;
  uint64_t mid;
  int live, unsupported;
} registration;
typedef struct kernel {
  struct kernel *next;
  registration *reg;
  const void *host;
  char *name;
  bang_param_layout layout;
  unsigned char pointer[BANG_MAX_ARGS];
  uint64_t kid;
  int valid;
} kernel;
static registration *registrations;
static kernel *kernels;

/* Match pointer identity only against our registry. Never read an arbitrary
 * application-supplied handle. */
static registration *get_registration(void *handle) {
  for(registration *r=registrations;r;r=r->next) if(r==handle && r->live) return r;
  return NULL;
}
static int mapped_cb(struct dl_phdr_info *info,size_t unused,void *arg) {
  (void)unused; uintptr_t *q=arg;
  for(unsigned i=0;i<info->dlpi_phnum;++i) {
    const ElfW(Phdr) *p=info->dlpi_phdr+i;
    uintptr_t start=info->dlpi_addr+p->p_vaddr;
    if(p->p_type==PT_LOAD && (p->p_flags&PF_R) && q[0]>=start && q[0]-start<p->p_filesz) {
      q[1]=p->p_filesz-(q[0]-start); return 1;
    }
  }
  return 0;
}
static size_t readable_span(const void *p) {
  uintptr_t q[2]={(uintptr_t)p,0}; dl_iterate_phdr(mapped_cb,q); return q[1];
}

/* Type classification is deliberately closed: complete demangled primitive
 * signatures only; parameter sizes/counts must match device ELF metadata.
 * No scalar value is ever heuristically treated as a pointer. */

cnrtRet_t cnrtInit(unsigned flags) { ENTER(); if(flags) RETURN(cnrtErrorArgsInvalid); CONNECT(); RETURN(cnrtSuccess); }
cnrtRet_t cnrtGetDeviceCount(unsigned *n) { ENTER(); if(!n) RETURN(cnrtErrorArgsInvalid); CONNECT(); RETURN(status(mlu_remote_device_count(ctx,n))); }
cnrtRet_t cnrtGetDevice(int *d) { ENTER(); if(!d) RETURN(cnrtErrorArgsInvalid); CONNECT(); *d=device; RETURN(cnrtSuccess); }
cnrtRet_t cnrtSetDevice(int d) { ENTER(); if(d<0) RETURN(cnrtErrorArgsInvalid); CONNECT(); int st=mlu_remote_select_device(ctx,d); if(!st) device=d; RETURN(status(st)); }
cnrtRet_t cnrtSyncDevice(void) {
  ENTER(); CONNECT();
  for(unsigned i=0;i<256;++i) if(queues[i].live) { int st=mlu_remote_queue_sync(ctx,queues[i].id); if(st) RETURN(status(st)); }
  RETURN(cnrtSuccess);
}
cnrtRet_t cnrtDeviceReset(void) {
  ENTER(); CONNECT(); int st=mlu_remote_session_reset(ctx); if(st) RETURN(status(st));
  for(allocation *a=allocations;a;a=a->next) a->live=0;
  memset(queues,0,sizeof(queues)); memset(notifiers,0,sizeof(notifiers)); default_queue=0;
  for(registration *r=registrations;r;r=r->next) r->mid=0;
  for(kernel *k=kernels;k;k=k->next) k->kid=0;
  RETURN(cnrtSuccess);
}
cnrtRet_t cnrtMalloc(void **ptr,size_t bytes) {
  ENTER(); if(!ptr || bytes>SIZE_MAX-4095) RETURN(cnrtErrorArgsInvalid);
  if(!bytes) { *ptr=NULL; RETURN(cnrtSuccess); }
  CONNECT();
  allocation *a=calloc(1,sizeof(*a)); if(!a) RETURN(cnrtErrorSysNoMem);
  a->mapped=(bytes+4095)&~(size_t)4095;
  a->base=mmap(NULL,a->mapped,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
  if(a->base==MAP_FAILED) { free(a); RETURN(cnrtErrorSysNoMem); }
  int st=mlu_remote_alloc(ctx,bytes,&a->id);
  if(st) { munmap(a->base,a->mapped); free(a); RETURN(status(st)); }
  a->bytes=bytes; a->live=1; a->next=allocations; allocations=a; *ptr=a->base; RETURN(cnrtSuccess);
}
cnrtRet_t cnrtFree(void *p) {
  ENTER(); if(!p) RETURN(cnrtSuccess); CONNECT(); uint64_t off=0; allocation *a=lookup(p,&off);
  if(!a || off) RETURN(cnrtErrorInvalidResourceHandle);
  int st=mlu_remote_free(ctx,a->id); if(!st) a->live=0;
  /* Keep a PROT_NONE tombstone until process exit: no stale VA alias. */
  RETURN(status(st));
}
static cnrtRet_t copy(void *dst,void *src,size_t bytes,cnrtQueue_t handle,cnrtMemTransDir_t dir,int sync) {
  cnrtRet_t st=connect_remote(); if(st) return st;
  if(!bytes) return cnrtSuccess;
  if(!dst || !src) return cnrtErrorArgsInvalid;
  if(dir==cnrtMemcpyHostToHost) { memmove(dst,src,bytes); return cnrtSuccess; }
  object *q=get_queue(handle); if(!q) return cnrtErrorInvalidResourceHandle;
  uint64_t doff=0,soff=0,tid=0;
  allocation *da=lookup(dst,&doff),*sa=lookup(src,&soff);
  int r;
  if(dir==cnrtMemcpyHostToDev && da && bytes<=da->bytes-doff && !sa)
    r=mlu_remote_memcpy_h2d(ctx,da->id,doff,src,bytes,q->id,&tid);
  else if(dir==cnrtMemcpyDevToHost && sa && bytes<=sa->bytes-soff && !da)
    r=mlu_remote_memcpy_d2h(ctx,dst,sa->id,soff,bytes,q->id,&tid);
  else if(dir==cnrtMemcpyDevToDev && da && sa && bytes<=da->bytes-doff && bytes<=sa->bytes-soff)
    r=mlu_remote_memcpy_d2d(ctx,da->id,doff,sa->id,soff,bytes,q->id,&tid);
  else return cnrtErrorArgsInvalid;
  if(r) return status(r);
  if(sync && (r=mlu_remote_queue_sync(ctx,q->id))) return status(r);
  /* D2H is currently blocking. H2D owns a server staging snapshot. */
  return release_transfer(tid);
}
cnrtRet_t cnrtMemcpy(void *d,void *s,size_t n,cnrtMemTransDir_t dir) { ENTER(); RETURN(copy(d,s,n,NULL,dir,1)); }
cnrtRet_t cnrtMemcpyAsync(void *d,void *s,size_t n,cnrtQueue_t q,cnrtMemTransDir_t dir) { ENTER(); RETURN(copy(d,s,n,q,dir,0)); }
cnrtRet_t cnrtQueueCreate(cnrtQueue_t *q) { ENTER(); if(!q) RETURN(cnrtErrorArgsInvalid); CONNECT(); uint64_t t; cnrtRet_t st=create_queue(&t); if(!st) *q=(cnrtQueue_t)(uintptr_t)t; RETURN(st); }
cnrtRet_t cnrtQueueDestroy(cnrtQueue_t h) {
  ENTER(); CONNECT(); object *q=get_object(queues,(uintptr_t)h); if(!q) RETURN(cnrtErrorInvalidResourceHandle);
  int st=mlu_remote_queue_destroy(ctx,q->id); if(!st) q->live=0; RETURN(status(st));
}
cnrtRet_t cnrtQueueSync(cnrtQueue_t h) { ENTER(); CONNECT(); object *q=get_queue(h); RETURN(q ? status(mlu_remote_queue_sync(ctx,q->id)) : cnrtErrorInvalidResourceHandle); }
cnrtRet_t cnrtQueueQuery(cnrtQueue_t h) { ENTER(); CONNECT(); object *q=get_queue(h); RETURN(q ? status(mlu_remote_queue_query(ctx,q->id)) : cnrtErrorInvalidResourceHandle); }
cnrtRet_t cnrtNotifierCreate(cnrtNotifier_t *n) {
  ENTER(); if(!n) RETURN(cnrtErrorArgsInvalid); CONNECT(); object *o=new_object(notifiers); if(!o) RETURN(cnrtErrorSysNoMem);
  uint64_t id; int st=mlu_remote_notifier_create(ctx,&id); if(st) RETURN(status(st));
  *o=(object){++next_token,id,1}; *n=(cnrtNotifier_t)(uintptr_t)o->token; RETURN(cnrtSuccess);
}
cnrtRet_t cnrtNotifierDestroy(cnrtNotifier_t h) {
  ENTER(); CONNECT(); object *o=get_object(notifiers,(uintptr_t)h); if(!o) RETURN(cnrtErrorInvalidResourceHandle);
  int st=mlu_remote_notifier_destroy(ctx,o->id); if(!st) o->live=0; RETURN(status(st));
}
cnrtRet_t cnrtPlaceNotifier(cnrtNotifier_t h,cnrtQueue_t handle) {
  ENTER(); CONNECT(); object *o=get_object(notifiers,(uintptr_t)h),*q=get_queue(handle);
  RETURN(o && q ? status(mlu_remote_notifier_record(ctx,o->id,q->id)) : cnrtErrorInvalidResourceHandle);
}
cnrtRet_t cnrtQueryNotifier(cnrtNotifier_t h) { ENTER(); CONNECT(); object *o=get_object(notifiers,(uintptr_t)h); RETURN(o ? status(mlu_remote_notifier_query(ctx,o->id)) : cnrtErrorInvalidResourceHandle); }
cnrtRet_t cnrtWaitNotifier(cnrtNotifier_t h) { ENTER(); CONNECT(); object *o=get_object(notifiers,(uintptr_t)h); RETURN(o ? status(mlu_remote_notifier_sync(ctx,o->id)) : cnrtErrorInvalidResourceHandle); }

static __thread struct { cnrtDim3_t dim; cnrtFunctionType_t type; cnrtQueue_t queue; int live; } config;
/* Struct by value is essential: AArch64 packs x/y in x0 and z in x1. */
cnrtRet_t __bangPushCallConfiguration(cnrtDim3_t dim,cnrtFunctionType_t type,cnrtQueue_t queue) {
  if(config.live) { last_error=cnrtErrorNotSupport; return last_error; }
  config.dim=dim; config.type=type; config.queue=queue; config.live=1; return 0;
}
cnrtRet_t __bangPopCallConfiguration(cnrtDim3_t *dim,cnrtFunctionType_t *type,cnrtQueue_t *queue) {
  if(dim) *dim=config.dim;
  if(type) *type=config.type;
  if(queue) *queue=config.queue;
  config.live=0;
  return cnrtSuccess;
}
void **__bangRegisterFatBinary(void *wrapper) {
  ENTER(); unsigned char *w=wrapper; void *image=NULL;
  if(readable_span(w)<24) { pthread_mutex_unlock(&lock); return NULL; }
  uint32_t magic,version; memcpy(&magic,w,4); memcpy(&version,w+4,4); memcpy(&image,w+8,8);
  size_t size=0,available=readable_span(image);
  if(magic!=0x466243b1 || version!=1 || bang_image_span(image,available,&size) || size>MLU_REMOTE_MAX_PAYLOAD) { last_error=cnrtErrorNotSupport; pthread_mutex_unlock(&lock); return NULL; }
  registration *r=calloc(1,sizeof(*r));
  if(r) r->image=malloc(size);
  if(!r || !r->image) { free(r); last_error=cnrtErrorSysNoMem; pthread_mutex_unlock(&lock); return NULL; }
  memcpy(r->image,image,size); r->size=size; r->live=1; r->next=registrations; registrations=r;
  pthread_mutex_unlock(&lock); return (void **)r;
}
void __bangRegisterFunction(void **handle,const char *host,char *symbol,const char *unused,int limit,void *tid,void *bid,cnrtDim3_t *bdim,cnrtDim3_t *gdim,int *wsize) {
  (void)unused; (void)limit; (void)tid; (void)bid; (void)bdim; (void)gdim; (void)wsize;
  ENTER(); registration *r=get_registration(handle);
  if(r && host && symbol && strlen(symbol)<256) {
    kernel *k=calloc(1,sizeof(*k));
    if(k) {
      k->reg=r; k->host=host; k->name=strdup(symbol);
      k->valid=k->name && !bang_image_params(r->image,r->size,symbol,&k->layout) && !bang_classify_params(k->name,&k->layout,k->pointer);
      k->next=kernels; kernels=k;
      if(!k->valid) fprintf(stderr,"[mlu-shim] unsupported kernel ABI: %s\n",symbol);
    }
  }
  pthread_mutex_unlock(&lock);
}
void __bangRegisterVar(void **handle,char *host,char *address,const char *name,int ext,int size,int constant,int global) {
  ENTER(); registration *r=get_registration(handle);
  /* Native probe: ext/constant/global are all zero for ordinary uninitialized
   * __mlu_device__ objects. Native module loading owns their storage and
   * relocations. Host symbol-copy/address APIs remain outside this subset. */
  if (!r || !host || !address || !name || strcmp(address,name) || ext || constant || global || size<=0 ||
      bang_image_device_variable(r->image,r->size,name,(size_t)size)) {
    if(r) r->unsupported=1;
    last_error=cnrtErrorNotSupport;
  }
  pthread_mutex_unlock(&lock);
}
void __bangUnregisterFatBinary(void **handle) {
  ENTER(); registration *r=get_registration(handle);
  if(r) {
    if(r->mid && ctx) {
      int st=mlu_remote_module_unload_preset(ctx,r->mid);
      if(st) last_error=status(st);
    }
    r->mid=0;
    kernel **link=&kernels;
    while(*link) {
      kernel *k=*link;
      if(k->reg!=r) { link=&k->next; continue; }
      *link=k->next; free(k->name); free(k);
    }
    /* Keep the small registration identity as a tombstone: a stale compiler
     * handle must never become a newly loaded DSO's registration. */
    r->live=0; free(r->image); r->image=NULL;
  }
  pthread_mutex_unlock(&lock);
}
cnrtRet_t cnrtInvokeKernel(const void *host,cnrtDim3_t dim,cnrtFunctionType_t type,void **args,size_t reserved,cnrtQueue_t handle) {
  ENTER(); CONNECT(); kernel *k;
  for(k=kernels;k;k=k->next) if(k->host==host && k->reg->live) break;
  if(!k) RETURN(cnrtErrorInvalidKernel);
  /* CNCC 4.5.1 emits an uninitialized reserved slot in some host stubs.
   * Native CNRT ignores it; it must never become a protocol parameter. */
  (void)reserved;
  if(!k->valid || k->reg->unsupported) RETURN(cnrtErrorNotSupport);
  if(!dim.x || !dim.y || !dim.z || (k->layout.count && !args)) RETURN(cnrtErrorArgsInvalid);
  if(type!=cnrtFuncTypeBlock && type!=cnrtFuncTypeUnion1 && type!=cnrtFuncTypeUnion2 && type!=cnrtFuncTypeUnion4 && type!=cnrtFuncTypeUnion8 && type!=cnrtFuncTypeUnion16) RETURN(cnrtErrorNotSupport);
  object *q=get_queue(handle); if(!q) RETURN(cnrtErrorInvalidResourceHandle);
  mlu_remote_kernel_arg wire[BANG_MAX_ARGS]; memset(wire,0,sizeof(wire));
  for(unsigned i=0;i<k->layout.count;++i) {
    if(!args[i]) RETURN(cnrtErrorArgsInvalid);
    wire[i].size=k->layout.sizes[i];
    if(k->pointer[i]) {
      void *value; memcpy(&value,args[i],8);
      wire[i].kind=1;
      if(value) {
        allocation *a=lookup(value,&wire[i].offset);
        if(!a) RETURN(cnrtErrorInvalidResourceHandle);
        wire[i].allocation=a->id;
      }
    } else { wire[i].kind=2; wire[i].scalar=args[i]; }
  }
  if(!k->reg->mid) {
    int st=mlu_remote_module_load_image(ctx,k->reg->image,k->reg->size,k->name,&k->reg->mid,&k->kid);
    if(st) RETURN(status(st));
  } else if(!k->kid) {
    int st=mlu_remote_module_get_kernel(ctx,k->reg->mid,k->name,&k->kid);
    if(st) RETURN(status(st));
  }
  uint64_t transfer=0;
  int st=mlu_remote_kernel_launch_generic(ctx,k->reg->mid,k->kid,q->id,dim.x,dim.y,dim.z,(uint32_t)type,wire,k->layout.count,&transfer);
  if(st) RETURN(status(st));
  RETURN(release_transfer(transfer));
}
cnrtRet_t cnrtGetLastError(void) { cnrtRet_t r=last_error; last_error=cnrtSuccess; return r; }
cnrtRet_t cnrtPeekAtLastError(void) { return last_error; }
const char *cnrtGetErrorName(cnrtRet_t e) {
  switch(e) {
    case cnrtSuccess: return "cnrtSuccess";
    case cnrtErrorArgsInvalid: return "cnrtErrorArgsInvalid";
    case cnrtErrorInvalidResourceHandle: return "cnrtErrorInvalidResourceHandle";
    case cnrtErrorNotSupport: return "cnrtErrorNotSupport";
    case cnrtErrorInvalidKernel: return "cnrtErrorInvalidKernel";
    case cnrtErrorInit: return "cnrtErrorInit";
    case cnrtErrorNotReady: return "cnrtErrorNotReady";
    default: return "cnrtErrorUnknown";
  }
}
const char *cnrtGetErrorStr(cnrtRet_t e) { return cnrtGetErrorName(e); }
/* Unsupported declarations are exported with explicit errors, never success. */
cnrtRet_t cnrtMallocConstant(void **p,size_t n) { (void)p; (void)n; ENTER(); RETURN(cnrtErrorNotSupport); }
cnrtRet_t cnrtHostMalloc(void **p,size_t n) { (void)p; (void)n; ENTER(); RETURN(cnrtErrorNotSupport); }
cnrtRet_t cnrtFreeHost(void *p) { (void)p; ENTER(); RETURN(cnrtErrorNotSupport); }
cnrtRet_t cnrtGetDeviceProperties_V2(cnrtDeviceProp_t *p,int d) { (void)p; (void)d; ENTER(); RETURN(cnrtErrorNotSupport); }
cnrtRet_t cnrtDeviceGetAttribute(int *p,cnrtDeviceAttr_t a,int d) { (void)p; (void)a; (void)d; ENTER(); RETURN(cnrtErrorNotSupport); }
cnrtRet_t cnrtMemset(void *p,int v,size_t n) { (void)p; (void)v; (void)n; ENTER(); RETURN(cnrtErrorNotSupport); }
cnrtRet_t cnrtMemsetAsync(void *p,int v,size_t n,cnrtQueue_t q) { (void)p; (void)v; (void)n; (void)q; ENTER(); RETURN(cnrtErrorNotSupport); }
cnrtRet_t cnrtNotifierElapsedTime(cnrtNotifier_t a,cnrtNotifier_t b,float *v) { (void)a; (void)b; (void)v; ENTER(); RETURN(cnrtErrorNotSupport); }
cnrtRet_t cnrtQueueCreateWithPriority(cnrtQueue_t *q,unsigned flags,int priority) { if(!flags && !priority) return cnrtQueueCreate(q); ENTER(); RETURN(cnrtErrorNotSupport); }
cnrtRet_t cnrtQueueGetPriority(cnrtQueue_t h,int *p) { ENTER(); object *q=get_object(queues,(uintptr_t)h); if(!q || !p) RETURN(cnrtErrorArgsInvalid); *p=0; RETURN(cnrtSuccess); }
cnrtRet_t cnrtNotifierCreateWithFlags(cnrtNotifier_t *n,unsigned flags) { if(!flags) return cnrtNotifierCreate(n); ENTER(); RETURN(cnrtErrorNotSupport); }
__attribute__((destructor)) static void shutdown_remote(void) {
  ENTER(); if(ctx) { mlu_remote_disconnect(ctx); ctx=NULL; }
  while(allocations) { allocation *a=allocations; allocations=a->next; munmap(a->base,a->mapped); free(a); }
  while(kernels) { kernel *k=kernels; kernels=k->next; free(k->name); free(k); }
  while(registrations) { registration *r=registrations; registrations=r->next; free(r->image); free(r); }
  pthread_mutex_unlock(&lock);
}
