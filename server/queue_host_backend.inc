/* A separate ordinary RPC connection may read/update a random capability while
 * the owning session is blocked in native cnQueueSync. Native atomics provide
 * the compare/request ordering and cross-queue wakeups. */
#include <sys/random.h>
static pthread_mutex_t atomic_broker_lock=PTHREAD_MUTEX_INITIALIZER;
static mlu_backend_atomic *atomic_broker;
static void atomic_cleanup(mlu_backend_session *s) {
  pthread_mutex_lock(&atomic_broker_lock);
  for(unsigned i=0;i<MLU_BACKEND_MAX_OBJECTS;i++)if(s->atomics[i]) {
    mlu_backend_atomic *a=s->atomics[i],**p=&atomic_broker;
    while(*p && *p!=a)p=&(*p)->next;
    if(!cnFreeHost(a->address)){if(*p)*p=a->next;mlu_resource_release(&s->usage,MLU_RESOURCE_STAGING,8);free(a);s->atomics[i]=NULL;}
  }
  pthread_mutex_unlock(&atomic_broker_lock);
}
static int atomic_control(mlu_backend_session *s,const mlu_api_queue_host_request *in,mlu_api_queue_host_response *out) {
  pthread_mutex_lock(&atomic_broker_lock);int st=0;mlu_backend_atomic *a=NULL;
  if(in->action==1) {
    unsigned i=0;while(i<MLU_BACKEND_MAX_OBJECTS && s->atomics[i])i++;
    if(i==MLU_BACKEND_MAX_OBJECTS)st=CN_ERROR_SYSTEM_OUT_OF_MEMORY;
    else {
      a=calloc(1,sizeof(*a));if(!a)st=CN_ERROR_SYSTEM_OUT_OF_MEMORY;
      if(!st && (getrandom(&a->id,8,0)!=8 || !a->id))st=CN_ERROR_OPERATING_SYSTEM;
      int charged=0;if(!st){st=mlu_resource_reserve(&s->usage,MLU_RESOURCE_STAGING,8);if(st)st=MLU_REMOTE_RESOURCE_LIMIT;else charged=1;}
      if(!st)st=cnMallocHost((void **)&a->address,8);
      if(st){if(charged)mlu_resource_release(&s->usage,MLU_RESOURCE_STAGING,8);free(a);}
      else {*a->address=in->value1;a->next=atomic_broker;atomic_broker=a;s->atomics[i]=a;s->atomic_used=1;out->id=a->id;}
    }
  } else {
    for(a=atomic_broker;a && a->id!=in->object;a=a->next){}
    if(!a)st=CN_ERROR_INVALID_ADDRESS;
    else if(in->action==4)st=cnAtomicReadOps((uintptr_t)a->address,&out->value);
    else if(in->action==3)st=cnAtomicOperation((uintptr_t)a->address,in->value1,in->value2,in->type,in->flag);
    else if(in->action==2) {
      unsigned i=0;while(i<MLU_BACKEND_MAX_OBJECTS && s->atomics[i]!=a)i++;
      mlu_backend_queue *q=in->queue ? find_queue(s,in->queue) : NULL;
      if(i==MLU_BACKEND_MAX_OBJECTS)st=CN_ERROR_INVALID_ADDRESS;
      else if(in->queue && !q)st=CN_QUEUE_ERROR_INVALID;
      else {st=cnQueueAtomicOperation(q ? q->handle : NULL,(uintptr_t)a->address,in->value1,in->value2,in->type,in->flag);if(!st && !q)s->default_queue_used=1;}
    } else st=CN_ERROR_INVALID_VALUE;
  }
  pthread_mutex_unlock(&atomic_broker_lock);return st;
}
/* Native queue boundaries forward executable work to the originating client. */
typedef struct queue_host_arg {
  mlu_callback_service *service;
  uint64_t cookie,token;
  mlu_backend_transfer *transfer;
  void *copy;uint64_t bytes;
  mlu_resource_usage *usage;
} queue_host_arg;
static void queue_host_done(queue_host_arg *a) {
  (void)mlu_callback_exchange(a->service,MLU_CALLBACK_DONE,a->cookie,0,NULL,0,1);
  if(a->copy)mlu_resource_release(a->usage,MLU_RESOURCE_STAGING,a->bytes);
  mlu_callback_user_done(a->service);free(a->copy);free(a);
}
static void queue_host_fn(void *opaque) {
  queue_host_arg *a=opaque;
  if(a->transfer) {
    mlu_backend_transfer *t=a->transfer;
    if(t->graph_hold)graph_host_pack_output(t,a->copy);
    (void)mlu_callback_exchange(a->service,MLU_CALLBACK_D2H,a->cookie,a->token,t->graph_hold ? a->copy : t->host,a->bytes,1);
    __atomic_sub_fetch(&t->host_readers,1,__ATOMIC_RELEASE);
  } else (void)mlu_callback_exchange(a->service,MLU_CALLBACK_CALL,a->cookie,a->token,NULL,0,1);
  queue_host_done(a);
}
static void queue_host_callback(CNqueue queue,CNresult status,void *opaque) {
  queue_host_arg *a=opaque;uint32_t encoded=htonl(status);
  (void)mlu_callback_exchange(a->service,MLU_CALLBACK_QUEUE,a->cookie,a->token,&encoded,4,1);
  queue_host_done(a);
}
static int queue_host_control(mlu_backend_session *s,const mlu_api_queue_host_request *in,mlu_api_queue_host_response *out) {
  if(in->action<=4)return atomic_control(s,in,out);
  if(in->action<6 || in->action>8)return CN_ERROR_INVALID_VALUE;
  mlu_backend_queue *q=in->queue ? find_queue(s,in->queue) : NULL;
  if(in->queue && !q)return CN_QUEUE_ERROR_INVALID;
  if(!s->callbacks || !in->token)return CN_ERROR_INVALID_VALUE;
  queue_host_arg *a=calloc(1,sizeof(*a));if(!a)return CN_ERROR_SYSTEM_OUT_OF_MEMORY;
  *a=(queue_host_arg){.service=s->callbacks,.cookie=in->token,.token=in->object};
  if(in->action==8) {
    a->transfer=find_transfer(s,in->object);
    if(!a->transfer || (!a->transfer->host && !a->transfer->graph_hold) || a->transfer->queue_id!=in->queue || a->transfer->bytes>MLU_GRAPH_MAX_COPY_BYTES){free(a);return CN_ERROR_INVALID_VALUE;}
    a->bytes=a->transfer->bytes;
    if(a->transfer->graph_hold){
      if(mlu_resource_reserve(&s->usage,MLU_RESOURCE_STAGING,a->bytes)){free(a);return MLU_REMOTE_RESOURCE_LIMIT;}
      a->usage=&s->usage;a->copy=malloc(a->bytes ? a->bytes : 1);
      if(!a->copy){mlu_resource_release(&s->usage,MLU_RESOURCE_STAGING,a->bytes);free(a);return CN_ERROR_SYSTEM_OUT_OF_MEMORY;}
    }
    a->token=in->value1;__atomic_add_fetch(&a->transfer->host_readers,1,__ATOMIC_RELAXED);
  }
  mlu_callback_user_add(a->service);
  int st=in->action==7 ? cnQueueAddCallback(q ? q->handle : NULL,queue_host_callback,a,0) : cnInvokeHostFunc(q ? q->handle : NULL,queue_host_fn,a);
  if(st){if(a->transfer)__atomic_sub_fetch(&a->transfer->host_readers,1,__ATOMIC_RELEASE);if(a->copy)mlu_resource_release(a->usage,MLU_RESOURCE_STAGING,a->bytes);mlu_callback_user_done(a->service);free(a->copy);free(a);}
  else if(!in->queue)s->default_queue_used=1;
  return st;
}
