/* Logical application threads outlive an individual RPC. The process-wide
 * Global epoch also covers other Driver contexts sharing this client library. */
#include <stdatomic.h>
typedef struct mlu_client_capture {
  struct mlu_client_capture *next;
  uint64_t queue,graph;
  unsigned mode;
} mlu_client_capture;
typedef struct mlu_capture_cache {
  struct mlu_capture_cache *next;
  uint64_t queue,graph,ids[MLU_GRAPH_MAX_NODES];
} mlu_capture_cache;
static atomic_uint capture_global_count;
static atomic_uint_fast64_t capture_global_epoch,capture_thread_counter;
static _Thread_local uint64_t capture_thread_id;
static uint64_t capture_thread(void) {
  if(!capture_thread_id) capture_thread_id=atomic_fetch_add(&capture_thread_counter,1)+1;
  return capture_thread_id;
}
static int capture_exchange(mlu_remote_context *ctx,unsigned action,uint64_t queue,unsigned mode,unsigned opcode,
    const void *data,uint64_t bytes,uint64_t capacity,unsigned char **reply) {
  if(bytes>2097152 || capacity>32768) return MLU_REMOTE_BAD_LENGTH;
  unsigned char *p=calloc(44+bytes,1);if(!p) return CN_ERROR_SYSTEM_OUT_OF_MEMORY;
  put32(p,action);put64(p+4,queue);put64(p+12,capture_thread());put32(p+20,mode);put32(p+24,opcode);put64(p+28,bytes);put64(p+36,capacity);
  if(bytes) memcpy(p+44,data,bytes);
  uint32_t n=0;int st=wire_rpc_locked(ctx,MLU_OP_API_GRAPH_CAPTURE,p,44+bytes,reply,&n);free(p);return st;
}
static void capture_client_cleanup(mlu_remote_context *ctx) {
  while(ctx->captures) {mlu_client_capture *c=ctx->captures;ctx->captures=c->next;if(c->mode==0) atomic_fetch_sub(&capture_global_count,1);free(c);}
  while(ctx->capture_cache) {mlu_capture_cache *c=ctx->capture_cache;ctx->capture_cache=c->next;free(c);}
}
static int capture_refresh(mlu_remote_context *ctx) {
  uint64_t epoch=atomic_load(&capture_global_epoch);
  if(!ctx->captures || ctx->capture_epoch==epoch) return 0;
  int st=capture_exchange(ctx,6,0,0,0,NULL,0,0,NULL);if(!st) ctx->capture_epoch=epoch;return st;
}
static int capture_unsafe(uint16_t op,const unsigned char *p,uint32_t n) {
  switch(op) {
    case MLU_OP_ALLOC:case MLU_OP_FREE:
    case MLU_OP_QUEUE_SYNC:case MLU_OP_QUEUE_QUERY:
    case MLU_OP_NOTIFIER_SYNC:case MLU_OP_NOTIFIER_QUERY:
    case MLU_OP_API_COPY2D:case MLU_OP_API_COPY3D:return 1;
    case MLU_OP_API_MEMSET:return n==44 && !get32(p+40);
    case MLU_OP_MEMCPY:return n>=60 && !get32(p+56);
    default:return 0;
  }
}
static int capture_local_error(mlu_remote_context *ctx,int st) {
  if(st) set_local_error(ctx,st,"QUEUE_CAPTURE_ERROR","operation violates the active queue capture contract");
  return st;
}
static int rpc_locked(mlu_remote_context *ctx,uint16_t op,const void *payload,uint32_t n,unsigned char **extra,uint32_t *len) {
  const unsigned char *p=payload;
  if(extra) *extra=NULL;if(len) *len=0;
  if(op!=MLU_OP_API_GRAPH_CAPTURE && op!=MLU_OP_SESSION_RESET && op!=MLU_OP_SESSION_CLOSE) {
    if(capture_unsafe(op,p,n) && atomic_load(&capture_global_count)) {
      atomic_fetch_add(&capture_global_epoch,1);return capture_local_error(ctx,100360);
    }
    int st=capture_refresh(ctx);if(st) return st;
    if(ctx->captures) {
      uint64_t queue=0;int dispatch=0;
      switch(op) {
        case MLU_OP_QUEUE_DESTROY:case MLU_OP_QUEUE_SYNC:case MLU_OP_QUEUE_QUERY:
          if(n==8) {queue=get64(p);dispatch=1;}break;
        case MLU_OP_NOTIFIER_DESTROY:case MLU_OP_NOTIFIER_SYNC:case MLU_OP_NOTIFIER_QUERY:
          if(n==8) dispatch=1;break;
        case MLU_OP_NOTIFIER_RECORD:if(n==16) {queue=get64(p+8);dispatch=1;}break;
        case MLU_OP_API_QUEUE_WAIT:if(n==16) {queue=get64(p);dispatch=1;}break;
        case MLU_OP_API_MEMSET:if(n==44 && get32(p+40)) {queue=get64(p+32);dispatch=1;}break;
        case MLU_OP_KERNEL_LAUNCH_GENERIC:if(n>=44) {queue=get64(p+16);dispatch=1;}break;
        default:break;
      }
      if(dispatch) {
        unsigned char *reply=NULL;st=capture_exchange(ctx,5,queue,0,op,p,n,0,&reply);
        if(st) {free(reply);return st;}
        int handled=get32(reply);free(reply);
        if(handled) {
          if(op==MLU_OP_KERNEL_LAUNCH_GENERIC) {
            if(extra) {*extra=calloc(8,1);if(!*extra) return CN_ERROR_SYSTEM_OUT_OF_MEMORY;}
            if(len) *len=8;
          }
          return 0;
        }
      }
    }
  }
  return wire_rpc_locked(ctx,op,payload,n,extra,len);
}
/* Must be called before sampling H2D bytes or registering a D2H writeback. */
static int capture_copy(mlu_remote_context *ctx,unsigned dir,uint64_t dst,uint64_t doff,uint64_t src,uint64_t soff,
    const void *host_src,void *host_dst,uint64_t bytes,uint64_t queue,int async,int *handled) {
  *handled=0; /* Caller holds the lock through ordinary submit and Host sampling. */
  int st=0;
  if(!async && atomic_load(&capture_global_count)) {atomic_fetch_add(&capture_global_epoch,1);st=capture_local_error(ctx,100360);*handled=1;}
  if(!st) st=capture_refresh(ctx);
  if(!st && async && ctx->captures) {
    unsigned char p[44],*reply=NULL;
    put64(p,dst);put64(p+8,dir==MLU_MEMCPY_D2H ? (uintptr_t)host_dst : doff);
    put64(p+16,src);put64(p+24,dir==MLU_MEMCPY_H2D ? (uintptr_t)host_src : soff);put64(p+32,bytes);
    put32(p+40,dir==MLU_MEMCPY_H2D ? 2 : dir==MLU_MEMCPY_D2H ? 3 : 0);
    st=capture_exchange(ctx,5,queue,0,MLU_OP_API_GRAPH_MEMCPY,p,44,0,&reply);
    if(!st) *handled=get32(reply);else *handled=1;
    free(reply);
  }
  return st;
}
int mlu_remote_capture(mlu_remote_context *ctx,unsigned action,uint64_t queue,unsigned mode,const uint64_t *deps,size_t count,
    unsigned *state,uint64_t *id,uint64_t *graph,const uint64_t **frontier,size_t *frontier_count) {
  if(!ctx || count>MLU_GRAPH_MAX_NODES || (count && !deps)) return MLU_REMOTE_BAD_LENGTH;
  if(!(ctx->capabilities & MLU_CAP_GRAPH_CAPTURE)) return MLU_REMOTE_UNSUPPORTED;
  pthread_mutex_lock(&ctx->lock);int st=capture_refresh(ctx);
  mlu_client_capture *fresh=NULL;
  uint64_t epoch=atomic_load(&capture_global_epoch);
  if(!st && action==1) {
    fresh=calloc(1,sizeof(*fresh));if(!fresh) st=CN_ERROR_SYSTEM_OUT_OF_MEMORY;
    else {fresh->queue=queue;fresh->mode=mode;if(mode==0) atomic_fetch_add(&capture_global_count,1);}
  }
  unsigned char data[8*MLU_GRAPH_MAX_NODES],*reply=NULL;
  for(size_t i=0;i<count;i++) put64(data+8*i,deps[i]);
  if(!st) st=capture_exchange(ctx,action,queue,mode,0,data,8*count,action==3 && frontier ? sizeof(data) : 0,&reply);
  if(fresh) {
    if(!st) {fresh->graph=get64(reply+16);fresh->next=ctx->captures;ctx->captures=fresh;ctx->capture_epoch=epoch;}
    else {if(fresh->mode==0) atomic_fetch_sub(&capture_global_count,1);free(fresh);}
  }
  if(action==2 && (!st || st==100361 || st==100362 || st==100364)) {
    mlu_client_capture **p=&ctx->captures;
    while(*p && (*p)->queue!=queue) p=&(*p)->next;
    if(*p) {
      mlu_client_capture *v=*p;*p=v->next;
      mlu_capture_cache **cache=&ctx->capture_cache;
      while(*cache) {mlu_capture_cache *entry=*cache;if(entry->graph!=v->graph) {cache=&entry->next;continue;}*cache=entry->next;free(entry);}
      if(v->mode==0) atomic_fetch_sub(&capture_global_count,1);free(v);
    }
    /* Cache pointers are owned by the capture session, and remain valid until
     * End/reset/disconnect. Other captures have independent storage. */
    if(!ctx->captures) {while(ctx->capture_cache) {mlu_capture_cache *v=ctx->capture_cache;ctx->capture_cache=v->next;free(v);}}
    if(graph && st!=100362) *graph=st ? 0 : get64(reply+16);
  }
  if(!st && action==3) {
    unsigned current=get32(reply+4);if(state) *state=current;
    if(current==1) {
      if(id) *id=get64(reply+8);if(graph) *graph=get64(reply+16);
      size_t amount=get64(reply+24);
      if(amount>MLU_GRAPH_MAX_NODES) st=break_connection(ctx,MLU_REMOTE_BAD_LENGTH,"REMOTE_BAD_LENGTH","capture frontier exceeds quota");
      if(!st && frontier) {
        mlu_capture_cache *v;for(v=ctx->capture_cache;v;v=v->next) if(v->queue==queue) break;
        if(!v) {v=calloc(1,sizeof(*v));if(!v) st=CN_ERROR_SYSTEM_OUT_OF_MEMORY;else {v->queue=queue;v->next=ctx->capture_cache;ctx->capture_cache=v;}}
        if(v) {v->graph=get64(reply+16);for(size_t i=0;i<amount;i++) v->ids[i]=get64(reply+32+8*i);*frontier=v->ids;}
      }
      if(!st && frontier_count) *frontier_count=amount;
    }
  }
  free(reply);pthread_mutex_unlock(&ctx->lock);return st;
}
