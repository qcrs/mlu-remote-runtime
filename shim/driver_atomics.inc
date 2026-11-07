typedef struct atomic_binding {
  struct atomic_binding *next;
  context *owner;cn_uint64_t *ptr;uint64_t id;
  mlu_remote_context *side;
  mlu_host_allocation *lease;
} atomic_binding;
static pthread_mutex_t atomic_lock=PTHREAD_MUTEX_INITIALIZER;
static atomic_binding *atomic_bindings;
static atomic_binding *atomic_find(uint64_t owner,cn_uint64_t ptr) {
  for(atomic_binding *a=atomic_bindings;a;a=a->next)if(a->side && a->owner->token==owner && (uintptr_t)a->ptr==ptr)return a;
  return NULL;
}
/* Called under both registry locks. The side connection is independent of the
 * primary RPC lock and the process shim lock held by queue/context waits. */
static int atomic_bind(context *c,cn_uint64_t ptr,atomic_binding **out) {
  atomic_binding *a=atomic_find(c->token,ptr);if(a){*out=a;return 0;}
  if(ptr%8 || mlu_host_range((void *)(uintptr_t)ptr,8)!=1)return CN_ERROR_INVALID_ADDRESS;
  a=calloc(1,sizeof(*a));if(!a)return CN_ERROR_SYSTEM_OUT_OF_MEMORY;
  int st=mlu_host_acquire((void *)(uintptr_t)ptr,8,&a->lease)==1 ? 0 : CN_ERROR_INVALID_ADDRESS;
  if(!st && !c->atomic_side)st=connect_session(c->device,c->flags,&c->atomic_side);
  a->side=c->atomic_side;
  mlu_api_queue_host_request in={.action=1,.value1=*(cn_uint64_t *)(uintptr_t)ptr};mlu_api_queue_host_response value={0};
  if(!st)st=mlu_remote_api_queue_host(c->remote,&in,&value);
  if(st){mlu_host_release(a->lease);free(a);return st;}
  a->owner=c;a->ptr=(void *)(uintptr_t)ptr;a->id=value.id;a->next=atomic_bindings;atomic_bindings=a;*out=a;return 0;
}
static void atomic_retire(context *c) {
  pthread_mutex_lock(&atomic_lock);
  for(atomic_binding *a=atomic_bindings;a;a=a->next)if(a->owner==c && a->side){a->side=NULL;mlu_host_release(a->lease);a->lease=NULL;}
  if(c->atomic_side){mlu_remote_disconnect(c->atomic_side);c->atomic_side=NULL;}
  pthread_mutex_unlock(&atomic_lock);
}
static int atomic_publish(context *c) {
  pthread_mutex_lock(&atomic_lock);int st=0;
  for(atomic_binding *a=atomic_bindings;!st && a;a=a->next)if(a->owner==c && a->side && mlu_host_range(a->ptr,8)==1) {
    mlu_api_queue_host_request in={.action=4,.object=a->id};mlu_api_queue_host_response value={0};
    st=mlu_remote_api_queue_host(a->side,&in,&value);if(!st)*a->ptr=value.value;
  }
  pthread_mutex_unlock(&atomic_lock);return st;
}
CNresult cnQueueAtomicOperation(CNqueue queue,cn_uint64_t ptr,cn_uint64_t data1,cn_uint64_t data2,CNAtomicOpType_t type,cn_uint64_t flag) {
  ENTER();CURRENT();uint64_t q;if(queue_id(c,queue,&q))RETURN(CN_QUEUE_ERROR_INVALID);
  pthread_mutex_lock(&atomic_lock);atomic_binding *a=NULL;int st=atomic_bind(c,ptr,&a);
  mlu_api_queue_host_request in={.action=2,.queue=q,.value1=data1,.value2=data2,.type=type,.flag=flag};mlu_api_queue_host_response out={0};
  if(!st){in.object=a->id;st=mlu_remote_api_queue_host(c->remote,&in,&out);}
  pthread_mutex_unlock(&atomic_lock);RETURN(result(st));
}
static int atomic_immediate(cn_uint64_t ptr,cn_uint64_t data1,cn_uint64_t data2,CNAtomicOpType_t type,cn_uint64_t flag,cn_uint64_t *value) {
  if(mlu_remote_in_callback())return CN_OPS_ERROR_NOT_SUPPORTED;
  pthread_mutex_lock(&atomic_lock);atomic_binding *a=atomic_find(current,ptr);
  if(!a) {
    pthread_mutex_unlock(&atomic_lock);pthread_mutex_lock(&lock);
    context *c=find_context(current);int st=!control ? CN_ERROR_NOT_INITIALIZED : !c || !c->remote ? CN_CONTEXT_ERROR_INVALID : 0;
    pthread_mutex_lock(&atomic_lock);if(!st)st=atomic_bind(c,ptr,&a);pthread_mutex_unlock(&lock);
    if(st){pthread_mutex_unlock(&atomic_lock);return st;}
  }
  int st=mlu_host_range((void *)(uintptr_t)ptr,8)==1 ? 0 : CN_ERROR_INVALID_ADDRESS;
  mlu_api_queue_host_request in={.action=value ? 4 : 3,.object=a->id,.value1=data1,.value2=data2,.type=type,.flag=flag};mlu_api_queue_host_response out={0};
  if(!st)st=mlu_remote_api_queue_host(a->side,&in,&out);
  if(!st && value){*value=out.value;*a->ptr=out.value;}
  pthread_mutex_unlock(&atomic_lock);return st;
}
CNresult cnAtomicOperation(cn_uint64_t ptr,cn_uint64_t data1,cn_uint64_t data2,CNAtomicOpType_t type,cn_uint64_t flag) {
  return result(atomic_immediate(ptr,data1,data2,type,flag,NULL));
}
CNresult cnAtomicReadOps(cn_uint64_t ptr,cn_uint64_t *value) {
  if(!value)return CN_ERROR_INVALID_VALUE;return result(atomic_immediate(ptr,0,0,0,0,value));
}
