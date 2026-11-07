#include "remote_callback.h"
typedef struct client_callback {
  struct client_callback *next;uint64_t id;void (*fn)(void *);void *data;int destructor;
} client_callback;
typedef struct client_callback_run {
  struct client_callback_run *next;uint64_t cookie;size_t count;
  unsigned char *plan;mlu_host_allocation **leases;
} client_callback_run;
typedef struct mlu_client_callbacks {
  pthread_mutex_t lock;pthread_t thread;int fd,primary_fd,started,stopping;
  uint32_t timeout_ms;uint64_t next_token,next_run;unsigned identities,run_count;
  client_callback *identities_head;client_callback_run *runs;
} mlu_client_callbacks;
static _Thread_local int callback_active;
int mlu_remote_in_callback(void) {return callback_active;}
static void callback_run_free(client_callback_run *run) {
  if(!run)return;if(run->leases)for(size_t i=0;i<run->count;i++)mlu_host_release(run->leases[i]);
  free(run->leases);free(run->plan);free(run);
}
static void callback_run_remove(mlu_client_callbacks *cb,uint64_t cookie) {
  pthread_mutex_lock(&cb->lock);client_callback_run **p=&cb->runs;
  while(*p && (*p)->cookie!=cookie)p=&(*p)->next;
  client_callback_run *run=*p;if(run){*p=run->next;cb->run_count--;}
  pthread_mutex_unlock(&cb->lock);callback_run_free(run);
}
static void callback_client_abort(mlu_remote_context *ctx) {
  mlu_client_callbacks *cb=ctx->callbacks;if(!cb)return;
  pthread_mutex_lock(&cb->lock);cb->stopping=1;shutdown(cb->fd,SHUT_RDWR);shutdown(cb->primary_fd,SHUT_RDWR);pthread_mutex_unlock(&cb->lock);
}
static void *callback_reader(void *opaque) {
  mlu_client_callbacks *cb=opaque;uint64_t seq=1;
  for(;;) {
    struct pollfd pollfd={.fd=cb->fd,.events=POLLIN};int ready;
    do{ready=poll(&pollfd,1,-1);}while(ready<0 && errno==EINTR);
    if(ready<=0)break;
    int64_t deadline=mlu_deadline_after(cb->timeout_ms);struct mlu_remote_wire_header h;
    if(mlu_io_full(cb->fd,&h,sizeof(h),0,deadline)!=1 || !cb_header_valid(&h,++seq) || ntohl(h.status))break;
    uint32_t length=ntohl(h.payload_len);if(length<32 || length>32+MLU_GRAPH_MAX_COPY_BYTES)break;
    unsigned char *event=malloc(length);if(!event)break;
    if(mlu_io_full(cb->fd,event,length,0,deadline)!=1){free(event);break;}
    uint64_t kind=cb_get64(event),cookie=cb_get64(event+8),token=cb_get64(event+16),bytes=cb_get64(event+24);
    int valid=bytes<=MLU_GRAPH_MAX_COPY_BYTES && length==32+(kind==MLU_CALLBACK_D2H ? bytes : 0);
    void (*fn)(void *)=NULL;void *data=NULL;unsigned char *input=NULL;
    pthread_mutex_lock(&cb->lock);
    if(cb->stopping)valid=0;
    client_callback_run *run=cb->runs;while(run && run->cookie!=cookie)run=run->next;
    if(kind==MLU_CALLBACK_DESTROY || kind==MLU_CALLBACK_CLOSE) {if(cookie || bytes)valid=0;}
    else if(!run)valid=0;
    if(valid && (kind==MLU_CALLBACK_H2D || kind==MLU_CALLBACK_D2H || kind==MLU_CALLBACK_CALL)) {
      size_t i=0;uint64_t expected=kind==MLU_CALLBACK_CALL ? 3 : kind==MLU_CALLBACK_H2D ? 1 : 2;
      while(i<run->count && !(cb_get64(run->plan+24*i)==token && cb_get64(run->plan+24*i+8)==bytes && cb_get64(run->plan+24*i+16)==expected))i++;
      if(i==run->count)valid=0;
    }
    if(valid && (kind==MLU_CALLBACK_CALL || kind==MLU_CALLBACK_DESTROY)) {
      client_callback **p=&cb->identities_head;while(*p && (*p)->id!=token)p=&(*p)->next;
      client_callback *identity=*p;
      if(!identity || identity->destructor!=(kind==MLU_CALLBACK_DESTROY) || bytes)valid=0;
      else {fn=identity->fn;data=identity->data;if(identity->destructor){*p=identity->next;cb->identities--;free(identity);}}
    } else if(valid && kind==MLU_CALLBACK_H2D) {
      input=malloc(bytes ? bytes : 1);if(!input)valid=0;else memcpy(input,(void *)(uintptr_t)token,bytes);
    } else if(valid && kind==MLU_CALLBACK_D2H)memcpy((void *)(uintptr_t)token,event+32,bytes);
    else if(kind!=MLU_CALLBACK_DONE && kind!=MLU_CALLBACK_CALL && kind!=MLU_CALLBACK_DESTROY && kind!=MLU_CALLBACK_CLOSE)valid=0;
    if(kind==MLU_CALLBACK_DONE && (bytes || token))valid=0;
    if(valid && kind==MLU_CALLBACK_CLOSE)cb->stopping=1;
    pthread_mutex_unlock(&cb->lock);
    if(valid && fn){callback_active=1;fn(data);callback_active=0;}
    if(valid && kind==MLU_CALLBACK_DONE)callback_run_remove(cb,cookie);
    h=cb_header(seq,valid && kind==MLU_CALLBACK_H2D ? (uint32_t)bytes : 0,valid ? 0 : MLU_REMOTE_BAD_OBJECT);
    int sent=mlu_io_full(cb->fd,&h,sizeof(h),1,deadline)==1 && (!valid || kind!=MLU_CALLBACK_H2D || mlu_io_full(cb->fd,input,bytes,1,deadline)==1);
    free(input);free(event);
    if(!valid || !sent || kind==MLU_CALLBACK_CLOSE)break;
  }
  pthread_mutex_lock(&cb->lock);if(!cb->stopping)shutdown(cb->primary_fd,SHUT_RDWR);pthread_mutex_unlock(&cb->lock);return NULL;
}
static void callback_client_cleanup(mlu_remote_context *ctx) {
  mlu_client_callbacks *cb=ctx->callbacks;if(!cb)return;
  pthread_mutex_lock(&cb->lock);cb->stopping=1;shutdown(cb->fd,SHUT_RDWR);pthread_mutex_unlock(&cb->lock);
  if(cb->started)pthread_join(cb->thread,NULL);
  close(cb->fd);close(cb->primary_fd);
  while(cb->runs){client_callback_run *r=cb->runs;cb->runs=r->next;callback_run_free(r);}
  while(cb->identities_head){client_callback *p=cb->identities_head;cb->identities_head=p->next;free(p);}
  pthread_mutex_destroy(&cb->lock);free(cb);ctx->callbacks=NULL;
}
static int callback_control_locked(mlu_remote_context *ctx,unsigned action,uint64_t graph,uint64_t node,uint64_t entity,uint64_t token,uint64_t value,unsigned flags,const void *data,size_t bytes,size_t capacity,unsigned char **out) {
  if(!(ctx->capabilities & MLU_CAP_GRAPH_CALLBACKS))return MLU_REMOTE_UNSUPPORTED;
  if(bytes>1048576 || capacity>1048576)return MLU_REMOTE_BAD_LENGTH;
  unsigned char *request=calloc(64+bytes,1);if(!request)return CN_ERROR_SYSTEM_OUT_OF_MEMORY;
  put32(request,action);put64(request+4,graph);put64(request+12,node);put64(request+20,entity);put64(request+28,token);put64(request+36,value);put32(request+44,flags);put64(request+48,bytes);put64(request+56,capacity);
  if(bytes)memcpy(request+64,data,bytes);uint32_t n=0;
  int st=rpc_locked(ctx,MLU_OP_API_GRAPH_CONTROL,request,(uint32_t)(64+bytes),out,&n);free(request);return st;
}
static int callback_ensure_locked(mlu_remote_context *ctx) {
  if(ctx->callbacks)return 0;
  unsigned char *reply=NULL;int st=callback_control_locked(ctx,1,0,0,0,0,0,0,NULL,0,0,&reply);if(st)return st;
  mlu_client_callbacks *cb=calloc(1,sizeof(*cb));if(!cb){free(reply);return CN_ERROR_SYSTEM_OUT_OF_MEMORY;}
  cb->fd=-1;cb->primary_fd=-1;pthread_mutex_init(&cb->lock,NULL);cb->timeout_ms=ctx->rpc_timeout_ms;
  struct sockaddr_storage peer;socklen_t size=sizeof(peer);
  if(getpeername(ctx->fd,(struct sockaddr *)&peer,&size))st=MLU_REMOTE_PROTOCOL_ERROR;
  if(!st) {cb->fd=socket(peer.ss_family,SOCK_STREAM|SOCK_NONBLOCK,0);if(cb->fd<0)st=MLU_REMOTE_PROTOCOL_ERROR;}
  int64_t deadline=mlu_deadline_after(ctx->rpc_timeout_ms);
  if(!st && connect(cb->fd,(struct sockaddr *)&peer,size)) {
    int error=0;socklen_t n=sizeof(error);
    if(errno!=EINPROGRESS || mlu_wait_socket(cb->fd,POLLOUT,deadline) || getsockopt(cb->fd,SOL_SOCKET,SO_ERROR,&error,&n) || error)st=MLU_REMOTE_PROTOCOL_ERROR;
  }
  if(!st) {
    int one=1;setsockopt(cb->fd,IPPROTO_TCP,TCP_NODELAY,&one,sizeof(one));
    struct mlu_remote_wire_header h=cb_header(1,16,0);unsigned char prefix[8];
    if(mlu_io_full(cb->fd,&h,sizeof(h),1,deadline)!=1 || mlu_io_full(cb->fd,reply,16,1,deadline)!=1 || mlu_io_full(cb->fd,&h,sizeof(h),0,deadline)!=1 ||
       !cb_header_valid(&h,1) || ntohl(h.status) || ntohl(h.payload_len)!=8 || mlu_io_full(cb->fd,prefix,8,0,deadline)!=1 || cb_get64(prefix))st=MLU_REMOTE_PROTOCOL_ERROR;
  }
  free(reply);
  if(!st){cb->primary_fd=dup(ctx->fd);if(cb->primary_fd<0)st=MLU_REMOTE_PROTOCOL_ERROR;}
  if(!st){ctx->callbacks=cb;if(pthread_create(&cb->thread,NULL,callback_reader,cb))st=MLU_REMOTE_PROTOCOL_ERROR;else cb->started=1;}
  if(st){ctx->callbacks=cb;callback_client_cleanup(ctx);return break_connection(ctx,MLU_REMOTE_PROTOCOL_ERROR,"REMOTE_CALLBACK_CHANNEL","callback channel setup failed");}
  return 0;
}
/* Bounded immutable identity registry. Repeated fn/userData pairs reuse their
 * token; destructors are one-shot entries and disappear upon execution. */
static int callback_register_locked(mlu_remote_context *ctx,void (*fn)(void *),void *data,int destructor,uint64_t *token) {
  if(!fn)return MLU_REMOTE_BAD_LENGTH;int st=callback_ensure_locked(ctx);if(st)return st;mlu_client_callbacks *cb=ctx->callbacks;
  pthread_mutex_lock(&cb->lock);
  if(!destructor)for(client_callback *p=cb->identities_head;p;p=p->next)if(!p->destructor && p->fn==fn && p->data==data){*token=p->id;pthread_mutex_unlock(&cb->lock);return 0;}
  if(cb->identities==MLU_CALLBACK_MAX_IDENTITIES){pthread_mutex_unlock(&cb->lock);return MLU_REMOTE_RESOURCE_LIMIT;}
  client_callback *p=calloc(1,sizeof(*p));if(!p){pthread_mutex_unlock(&cb->lock);return CN_ERROR_SYSTEM_OUT_OF_MEMORY;}
  *p=(client_callback){cb->identities_head,++cb->next_token,fn,data,destructor};cb->identities_head=p;cb->identities++;*token=p->id;
  pthread_mutex_unlock(&cb->lock);return 0;
}
int mlu_remote_graph_host_node(mlu_remote_context *ctx,unsigned action,uint64_t graph,uint64_t node,uint64_t entity,const uint64_t *deps,size_t count,void (**fn)(void *),void **data,uint64_t *id) {
  if(!ctx || !fn || !data || count>MLU_GRAPH_MAX_NODES || (count && !deps))return MLU_REMOTE_BAD_LENGTH;
  if(callback_active)return MLU_REMOTE_UNSUPPORTED;
  pthread_mutex_lock(&ctx->lock);uint64_t token=0;unsigned char *reply=NULL,*encoded=NULL;
  int st=action==3 ? 0 : callback_register_locked(ctx,*fn,*data,0,&token);
  if(!st && count){encoded=malloc(8*count);if(!encoded)st=CN_ERROR_SYSTEM_OUT_OF_MEMORY;else for(size_t i=0;i<count;i++)put64(encoded+8*i,deps[i]);}
  if(!st)st=callback_control_locked(ctx,action,graph,node,entity,token,0,0,encoded,8*count,0,&reply);
  if(!st && action==3) {
    mlu_client_callbacks *cb=ctx->callbacks;client_callback *p=NULL;
    if(cb){pthread_mutex_lock(&cb->lock);for(p=cb->identities_head;p;p=p->next)if(p->id==get64(reply+8))break;
      if(p){*fn=p->fn;*data=p->data;}pthread_mutex_unlock(&cb->lock);}
    if(!p)st=MLU_REMOTE_BAD_OBJECT;
  }
  if(!st && id)*id=get64(reply);free(encoded);free(reply);pthread_mutex_unlock(&ctx->lock);return st;
}
int mlu_remote_graph_user(mlu_remote_context *ctx,unsigned action,uint64_t graph,uint64_t object,void (*fn)(void *),void *data,unsigned count,unsigned flags,uint64_t *id) {
  if(!ctx || callback_active)return MLU_REMOTE_UNSUPPORTED;
  pthread_mutex_lock(&ctx->lock);uint64_t token=0;unsigned char *reply=NULL;
  int st=action==6 ? callback_register_locked(ctx,fn,data,1,&token) : 0;
  if(!st)st=callback_control_locked(ctx,action,graph,object,0,token,count,flags,NULL,0,0,&reply);
  if(!st && id)*id=get64(reply);
  if(st && token && ctx->callbacks) {mlu_client_callbacks *cb=ctx->callbacks;pthread_mutex_lock(&cb->lock);
    client_callback **p=&cb->identities_head;while(*p && (*p)->id!=token)p=&(*p)->next;if(*p){client_callback *old=*p;*p=old->next;free(old);cb->identities--;}pthread_mutex_unlock(&cb->lock);}
  free(reply);pthread_mutex_unlock(&ctx->lock);return st;
}
int mlu_remote_graph_dot(mlu_remote_context *ctx,uint64_t graph,const char *path,unsigned flags) {
  if(!ctx || !path || !*path)return MLU_REMOTE_BAD_LENGTH;if(callback_active)return MLU_REMOTE_UNSUPPORTED;
  pthread_mutex_lock(&ctx->lock);unsigned char *reply=NULL;
  int st=callback_control_locked(ctx,11,graph,0,0,0,0,flags,NULL,0,1048576,&reply);
  if(!st) {size_t bytes=(size_t)get64(reply+24);if(bytes>1048576)st=MLU_REMOTE_BAD_LENGTH;
    else {FILE *f=fopen(path,"wb");if(!f)st=100007;else {if(fwrite(reply+32,1,bytes,f)!=bytes)st=100007;if(fclose(f))st=100007;}}}
  free(reply);pthread_mutex_unlock(&ctx->lock);return st;
}
