static int graph_callbacks_invoke_locked(mlu_remote_context *ctx,uint64_t entity,uint64_t queue,int *handled) {
  if(!(ctx->capabilities & MLU_CAP_GRAPH_CALLBACKS))return 0;
  unsigned char *reply=NULL;client_callback_run *run=NULL;int registered=0;
  int st=callback_control_locked(ctx,12,0,0,entity,0,0,0,NULL,0,0,&reply);if(st)return st;
  *handled=get64(reply)!=0;if(!*handled){free(reply);return 0;}
  size_t count=(size_t)get64(reply+8);free(reply);reply=NULL;
  if(!count || count>MLU_GRAPH_MAX_NODES)return MLU_REMOTE_BAD_LENGTH;
  if(!ctx->callbacks)return MLU_REMOTE_BAD_OBJECT;
  /* A deferred ordinary copy must publish before callback-side reads. */
  if(ctx->pending) {st=rpc_locked(ctx,MLU_OP_CONTEXT_SYNC,NULL,0,NULL,&(uint32_t){0});if(!st)st=complete_writebacks(ctx);if(st)return st;}
  st=callback_control_locked(ctx,12,0,0,entity,0,0,0,NULL,0,24*count,&reply);if(st)return st;
  if(get64(reply)!=1 || get64(reply+8)!=count){free(reply);return MLU_REMOTE_BAD_LENGTH;}
  run=calloc(1,sizeof(*run));if(!run){free(reply);return CN_ERROR_SYSTEM_OUT_OF_MEMORY;}
  run->count=count;run->plan=malloc(24*count);run->leases=calloc(count,sizeof(*run->leases));
  if(!run->plan || !run->leases){st=CN_ERROR_SYSTEM_OUT_OF_MEMORY;goto done;}
  memcpy(run->plan,reply+32,24*count);free(reply);reply=NULL;
  for(size_t i=0;!st && i<count;i++) {
    uint64_t token=get64(run->plan+24*i),bytes=get64(run->plan+24*i+8),kind=get64(run->plan+24*i+16);
    if(kind==3) {if(!token || bytes)st=MLU_REMOTE_BAD_LENGTH;}
    else if(!token || !bytes || bytes>MLU_GRAPH_MAX_COPY_BYTES || token>UINTPTR_MAX-bytes || (kind!=1 && kind!=2))st=MLU_REMOTE_BAD_LENGTH;
    else if(mlu_host_acquire((void *)(uintptr_t)token,bytes,&run->leases[i])<0)st=MLU_REMOTE_BAD_LENGTH;
  }
  if(st)goto done;
  mlu_client_callbacks *cb=ctx->callbacks;uint64_t cookie;
  pthread_mutex_lock(&cb->lock);
  if(cb->run_count==MLU_CALLBACK_MAX_RUNS || cb->stopping)st=MLU_REMOTE_RESOURCE_LIMIT;
  cookie=run->cookie=++cb->next_run;
  if(!st) {run->next=cb->runs;cb->runs=run;cb->run_count++;registered=1;}
  pthread_mutex_unlock(&cb->lock);if(st)goto done;
  /* The executor may finish and free run before the primary reply arrives. */
  unsigned char *plan=malloc(24*count);if(!plan){st=CN_ERROR_SYSTEM_OUT_OF_MEMORY;callback_run_remove(cb,cookie);run=NULL;goto done;}
  pthread_mutex_lock(&cb->lock);memcpy(plan,run->plan,24*count);pthread_mutex_unlock(&cb->lock);
  st=callback_control_locked(ctx,13,queue,0,entity,cookie,0,0,plan,24*count,0,&reply);free(plan);run=NULL;
  if(st) {if(ctx->fd>=0)callback_run_remove(cb,cookie);}
  else {uint64_t transfer=get64(reply);if(!transfer)st=MLU_REMOTE_BAD_LENGTH;
    else {unsigned char request[8];put64(request,transfer);st=rpc_locked(ctx,MLU_OP_TRANSFER_RELEASE,request,8,NULL,&(uint32_t){0});}}
done:
  free(reply);if(!registered)callback_run_free(run);return st;
}
