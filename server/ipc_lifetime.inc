/* Process-wide backing capabilities. The exporter Context survives its public
 * session until the last imported view / exported descriptor closes. */
#include <sys/random.h>
#include <sys/socket.h>
#include <errno.h>
#include <endian.h>
#include <fcntl.h>
typedef struct mlu_ipc_context {CNcontext native;unsigned refs;} mlu_ipc_context;
typedef struct mlu_ipc_backing {
  struct mlu_ipc_backing *next;
  mlu_ipc_context *owner;
  mlu_resource_usage *charge, detached;
  uint64_t token,bytes;
  CNaddr address;
  CNmemGenericAllocationHandle native_handle;
  unsigned refs;
  int kind,opens,storage_released;
} mlu_ipc_backing;
typedef struct ipc_descriptor {
  struct ipc_descriptor *next;
  uint64_t token;
  mlu_ipc_backing *backing;
  mlu_backend_session *pending_owner;
  int native_fd,attached;
} ipc_descriptor;
static pthread_mutex_t ipc_lock=PTHREAD_MUTEX_INITIALIZER;
static mlu_ipc_backing *ipc_backings;
static ipc_descriptor *ipc_descriptors;
static uint64_t ipc_random(void) {
  uint64_t v=0;if(getrandom(&v,8,0)!=8)return 0;
  /* Host handles use the disjoint 0x484d client domain. */
  return (v & UINT64_C(0x0000ffffffffffff))|UINT64_C(0x4456000000000000);
}
static mlu_ipc_backing *ipc_backing_new(mlu_backend_session *s,uint64_t bytes,int kind,CNaddr address) {
  mlu_ipc_backing *b=calloc(1,sizeof(*b));if(!b)return NULL;
  b->token=ipc_random();if(!b->token){free(b);return NULL;}
  if(!s->ipc_context){s->ipc_context=calloc(1,sizeof(*s->ipc_context));if(!s->ipc_context){free(b);return NULL;}s->ipc_context->native=s->context;s->ipc_context->refs=1;}
  b->owner=s->ipc_context;b->owner->refs++;b->bytes=bytes;b->kind=kind;b->address=address;b->refs=1;b->opens=1;b->charge=&s->usage;
  b->next=ipc_backings;ipc_backings=b;return b;
}
static void ipc_detach_charge(mlu_ipc_backing *b) {
  if(b->charge!=&b->detached){mlu_resource_transfer(b->charge,&b->detached,MLU_RESOURCE_DEVICE,b->bytes);b->charge=&b->detached;}
}
static int ipc_context_drop(mlu_ipc_context *h) {
  if(--h->refs)return 0;
  int st=cnCtxDestroy(h->native);if(st){h->refs++;return st;}free(h);return 0;
}
/* ipc_lock held. Only the last backing owner can free native storage. */
static int ipc_backing_drop(mlu_ipc_backing *b) {
  if(b->refs>1){b->refs--;return 0;}
  CNcontext saved=NULL;cnCtxGetCurrent(&saved);int st=cnCtxSetCurrent(b->owner->native);
  if(!st && !b->storage_released) {
    st=b->kind ? cnMemRelease(b->native_handle) : cnFree(b->address);
    if(!st){b->storage_released=1;mlu_resource_release(b->charge,MLU_RESOURCE_DEVICE,b->bytes);}
  }
  if(!st){
    CNcontext owner=b->owner->native;int destroys=b->owner->refs==1;
    st=ipc_context_drop(b->owner);
    if(!st){
      if(destroys && saved==owner)saved=NULL;
      mlu_ipc_backing **p=&ipc_backings;while(*p && *p!=b)p=&(*p)->next;if(*p)*p=b->next;
      free(b);
    }
  }
  if(saved)cnCtxSetCurrent(saved);return st;
}
static int ipc_release_allocation(mlu_backend_session *s,mlu_backend_allocation *a) {
  pthread_mutex_lock(&ipc_lock);mlu_ipc_backing *b=a->ipc;
  if(!a->ipc_imported){b->opens=0;ipc_detach_charge(b);}
  int st=ipc_backing_drop(b);
  if(!st){mlu_object_registry_release(&s->objects,MLU_OBJECT_ALLOCATION,a->id);memset(a,0,sizeof(*a));}
  pthread_mutex_unlock(&ipc_lock);return st;
}
static void ipc_revoke_allocation(mlu_backend_allocation *a) {
  if(a->ipc && !a->ipc_imported){pthread_mutex_lock(&ipc_lock);a->ipc->opens=0;pthread_mutex_unlock(&ipc_lock);}
}
static int ipc_vmm_drop(mlu_backend_session *s,mlu_vmm_handle *h) {
  if(!h->ipc){mlu_resource_release(&s->usage,MLU_RESOURCE_DEVICE,h->bytes);return 0;}
  pthread_mutex_lock(&ipc_lock);if(h->ipc->charge==&s->usage)ipc_detach_charge(h->ipc);int st=ipc_backing_drop(h->ipc);pthread_mutex_unlock(&ipc_lock);return st;
}
/* Native import rejects a descriptor exported inside this same server process.
 * A native retain of the mapped allocation supplies the equivalent independent
 * physical reference, usable in another Context on this device. */
static int ipc_retain_vmm(CNmemGenericAllocationHandle handle,uint64_t bytes,CNmemGenericAllocationHandle *out) {
  CNaddr temporary=0;int st=cnMemAddressReserve(&temporary,bytes,0,0,0);
  if(st)return st;
  st=cnMemMap(temporary,bytes,0,handle,0);
  if(!st){st=cnMemRetainAllocationHandle(out,temporary);int unmap=cnMemUnmap(temporary,bytes);if(!st)st=unmap;}
  int released=cnMemAddressFree(temporary,bytes);if(!st)st=released;
  return st;
}
static int ipc_session_detach(mlu_backend_session *s) {
  pthread_mutex_lock(&ipc_lock);
  ipc_descriptor **p=&ipc_descriptors;
  while(*p){ipc_descriptor *d=*p;if(d->pending_owner==s && !d->attached){*p=d->next;close(d->native_fd);ipc_backing_drop(d->backing);free(d);}else p=&d->next;}
  int st=0;
  if(s->ipc_context){
    for(mlu_ipc_backing *b=ipc_backings;b;b=b->next)if(b->owner==s->ipc_context && b->charge==&s->usage)ipc_detach_charge(b);
    st=ipc_context_drop(s->ipc_context);if(!st){s->ipc_context=NULL;s->context=NULL;}
  }
  pthread_mutex_unlock(&ipc_lock);return st;
}
/* The TCP socket itself is the transferable POSIX descriptor. An immutable
 * capability stays unread at its receive head (MSG_PEEK); final close produces
 * EOF here, releasing the native exported FD and the backing lease. */
int mlu_backend_ipc_attach(int fd,uint64_t token) {
  pthread_mutex_lock(&ipc_lock);ipc_descriptor *d=ipc_descriptors;
  while(d && (d->token!=token || d->attached))d=d->next;
  if(!d){pthread_mutex_unlock(&ipc_lock);return CN_ERROR_INVALID_HANDLE;}
  d->attached=1;d->pending_owner=NULL;
  uint64_t encoded=htobe64(d->token);pthread_mutex_unlock(&ipc_lock);
  int st=send(fd,&encoded,8,MSG_NOSIGNAL)==8 ? 0 : CN_ERROR_OPERATING_SYSTEM;
  if(!st){int flags=fcntl(fd,F_GETFL);if(flags>=0)fcntl(fd,F_SETFL,flags & ~O_NONBLOCK);char byte;while(recv(fd,&byte,1,0)<0 && errno==EINTR){} }
  pthread_mutex_lock(&ipc_lock);ipc_descriptor **p=&ipc_descriptors;while(*p && *p!=d)p=&(*p)->next;if(*p)*p=d->next;
  close(d->native_fd);int released=ipc_backing_drop(d->backing);free(d);pthread_mutex_unlock(&ipc_lock);return st ? st : released;
}
