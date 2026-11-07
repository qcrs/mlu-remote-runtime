/* One lock covers describe + current-input capture + execute + publication
 * registration. No callback or server pointer is dereferenced on the client. */
int mlu_remote_graph_invoke(mlu_remote_context *ctx,uint64_t entity,uint64_t queue) {
  if(!ctx) return MLU_REMOTE_BAD_LENGTH;
  unsigned char req[36],*reply=NULL,*plan=NULL,*request=NULL;uint32_t n=0;
  pending_writeback *w=NULL;mlu_host_allocation **leases=NULL;size_t count=0,inputs=0,outputs=0;
  pthread_mutex_lock(&ctx->lock);
  put32(req,1);put64(req+4,entity);put64(req+12,queue);put64(req+20,0);put64(req+28,0);
  int st=rpc_locked(ctx,MLU_OP_API_GRAPH_HOST,req,sizeof(req),&reply,&n);
  if(st) goto done;
  count=(size_t)get64(reply+8);inputs=(size_t)get64(reply+16);outputs=(size_t)get64(reply+24);
  if(count>MLU_GRAPH_MAX_NODES || inputs>MLU_GRAPH_MAX_HOST_BYTES || outputs>MLU_GRAPH_MAX_HOST_BYTES-inputs) {st=MLU_REMOTE_BAD_LENGTH;goto done;}
  free(reply);reply=NULL;
  if(count) {
    put64(req+28,24*count);
    st=rpc_locked(ctx,MLU_OP_API_GRAPH_HOST,req,sizeof(req),&reply,&n);if(st) goto done;
    if(get64(reply+8)!=count || get64(reply+16)!=inputs || get64(reply+24)!=outputs) {st=MLU_REMOTE_BAD_LENGTH;goto done;}
    plan=reply+32;
  }
  size_t total=36+24*count+inputs;
  request=malloc(total);leases=calloc(count ? count : 1,sizeof(*leases));
  if(!request || !leases) {st=CN_ERROR_SYSTEM_OUT_OF_MEMORY;goto done;}
  if(outputs) {
    w=calloc(1,sizeof(*w));if(w) w->targets=calloc(count,sizeof(*w->targets));
    if(!w || !w->targets) {st=CN_ERROR_SYSTEM_OUT_OF_MEMORY;goto done;}
    w->bytes=outputs;w->queue=queue;w->release_requested=1;
  }
  put32(request,2);put64(request+4,entity);put64(request+12,queue);put64(request+20,24*count+inputs);put64(request+28,0);
  if(count) memcpy(request+36,plan,24*count);
  size_t in_offset=36+24*count,out_offset=0;
  for(size_t i=0;i<count;i++) {
    uint64_t token=get64(plan+24*i),bytes=get64(plan+24*i+8),kind=get64(plan+24*i+16);
    if(!token || !bytes || bytes>MLU_GRAPH_MAX_COPY_BYTES || token>UINTPTR_MAX-bytes || (kind!=1 && kind!=2)) {st=MLU_REMOTE_BAD_LENGTH;goto done;}
    void *host=(void *)(uintptr_t)token;
    if(mlu_host_acquire(host,(size_t)bytes,&leases[i])<0) {st=MLU_REMOTE_BAD_LENGTH;goto done;}
    if(kind==1) {
      if(bytes>total-in_offset) {st=MLU_REMOTE_BAD_LENGTH;goto done;}
      memcpy(request+in_offset,host,bytes);in_offset+=bytes;
    } else {
      if(!w || bytes>outputs-out_offset) {st=MLU_REMOTE_BAD_LENGTH;goto done;}
      w->targets[w->target_count++]=(graph_writeback_target){host,bytes,out_offset,leases[i]};leases[i]=NULL;out_offset+=bytes;
    }
  }
  if(in_offset!=total || out_offset!=outputs) {st=MLU_REMOTE_BAD_LENGTH;goto done;}
  free(reply);reply=NULL;
  st=rpc_locked(ctx,MLU_OP_API_GRAPH_HOST,request,(uint32_t)total,&reply,&n);
  if(st) goto done;
  uint64_t transfer=get64(reply);
  if(!transfer || (count && (get64(reply+8)!=count || get64(reply+24)!=outputs))) {st=MLU_REMOTE_BAD_LENGTH;goto done;}
  if(w) {
    w->transfer=transfer;
    if(ctx->pending_tail) *ctx->pending_tail=w;else ctx->pending=w;
    ctx->pending_tail=&w->next;w=NULL;
  } else {unsigned char id[8];put64(id,transfer);st=rpc_locked(ctx,MLU_OP_TRANSFER_RELEASE,id,8,NULL,&n);}
done:
  if(leases) for(size_t i=0;i<count;i++) mlu_host_release(leases[i]);
  free(leases);if(w) writeback_free(w);free(reply);free(request);
  pthread_mutex_unlock(&ctx->lock);return st;
}
