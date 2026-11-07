typedef struct mlu_graph_host_endpoint {
  mlu_graph_payload *payload;
  CNtaskTopoNode node;
  size_t offset;
} mlu_graph_host_endpoint;
/* Replace only inside the private clone. A zero-edge graph must not call
 * GetEdges with aliased output arrays (the native API rejects that call). */
static int graph_host_replace_child(mlu_graph_view *v,size_t i,mlu_graph_view *source) {
  size_t count=0;int st=cnTaskTopoGetEdges(v->graph,NULL,NULL,&count);if(st) return st;
  if(count>MLU_GRAPH_MAX_ARRAY/2) return MLU_REMOTE_RESOURCE_LIMIT;
  CNtaskTopoNode *edges=calloc(2*count+2*v->count+1,sizeof(*edges));if(!edges) return CN_ERROR_SYSTEM_OUT_OF_MEMORY;
  CNtaskTopoNode *before=edges+2*count,*after=before+v->count;size_t nb=0,na=0,actual=count;
  if(count) st=cnTaskTopoGetEdges(v->graph,edges,edges+count,&actual);
  if(!st && actual!=count) st=CN_ERROR_OPERATING_SYSTEM;
  CNtaskTopoNode old=v->entries[i].node;
  for(size_t j=0;!st && j<count;j++) {if(edges[count+j]==old) before[nb++]=edges[j];if(edges[j]==old) after[na++]=edges[count+j];}
  if(!st) st=cnTaskTopoDestroyNode(old);
  if(!st) {graph_view_release(v->entries[i].child);v->entries[i].child=NULL;v->entries[i].node=NULL;}
  if(!st) st=cnTaskTopoAddChildTopoNode(&v->entries[i].node,v->graph,before,nb,source->graph);
  for(size_t j=0;!st && j<na;j++) st=cnTaskTopoAddDependencies(v->graph,&v->entries[i].node,&after[j],1);
  CNtaskTopo embedded=NULL;
  if(!st) st=cnTaskTopoChildTopoNodeGetTopo(v->entries[i].node,&embedded);
  if(!st) st=graph_view_duplicate(source,embedded,1,&v->entries[i].child);
  free(edges);return st;
}
static int graph_host_revision(mlu_backend_session *s,mlu_graph_entity *e,mlu_graph_view **out) {
  int st=graph_view_copy(e->revision,out);if(st) return st;
  mlu_graph_view *v=*out;
  for(size_t i=0;i<e->count;i++) if(e->overrides[i]) {
    if(e->host_slots[i]<0) return MLU_REMOTE_UNSUPPORTED;
    size_t j=(size_t)e->host_slots[i];mlu_graph_payload *p=e->overrides[i];
    if(p->child_view) st=graph_host_replace_child(v,j,p->child_view);
    else if(p->kernel) st=cnTaskTopoKernelNodeSetParams(v->entries[j].node,&p->kernel->params);
    else if(p->memset.elementSize) st=cnTaskTopoMemsetNodeSetParams(v->entries[j].node,&p->memset);
    else st=cnTaskTopoMemcpyNodeSetParams(v->entries[j].node,&p->memcpy);
    if(st) return st;
    graph_payload_retain(p);graph_payload_release(v->entries[j].payload);v->entries[j].payload=p;
  }
  return 0;
}
static int graph_host_endpoints(mlu_graph_view *v,mlu_graph_invocation *run,size_t *input,size_t *output) {
  for(size_t i=0;i<v->count;i++) {
    mlu_graph_payload *p=v->entries[i].payload;
    if(p && p->host_kind) {
      if(run->host_count==MLU_GRAPH_MAX_NODES) return MLU_REMOTE_RESOURCE_LIMIT;
      size_t bytes=p->memcpy.extent.width;
      if(bytes>MLU_GRAPH_MAX_HOST_BYTES-run->host_bytes) return MLU_REMOTE_RESOURCE_LIMIT;
      run->endpoints[run->host_count++]=(mlu_graph_host_endpoint){p,v->entries[i].node,run->host_bytes};
      run->host_bytes+=bytes;
      if(p->host_kind==1) *input+=bytes;else *output+=bytes;
    }
    if(v->entries[i].child) {int st=graph_host_endpoints(v->entries[i].child,run,input,output);if(st) return st;}
  }
  return 0;
}
/* Preserve Host aliases within one invocation, including D2H -> H2D
 * feedback and ordered overlapping outputs. Distinct invocations never share
 * staging. The sum bound above also bounds the packed interval union. */
static int graph_host_pack_aliases(mlu_graph_invocation *run) {
  size_t *order=malloc((run->host_count ? run->host_count : 1)*sizeof(*order));
  if(!order) return CN_ERROR_SYSTEM_OUT_OF_MEMORY;
  for(size_t i=0;i<run->host_count;i++) {
    size_t j=i;uint64_t token=run->endpoints[i].payload->host_token;
    while(j && run->endpoints[order[j-1]].payload->host_token>token) {order[j]=order[j-1];j--;}
    order[j]=i;
  }
  uint64_t begin=0,end=0;size_t base=0,total=0;
  for(size_t i=0;i<run->host_count;i++) {
    mlu_graph_host_endpoint *ep=&run->endpoints[order[i]];mlu_graph_payload *p=ep->payload;
    uint64_t token=p->host_token,limit=token+p->memcpy.extent.width;
    if(!i || token>=end) {begin=token;end=limit;base=total;total+=limit-token;}
    else if(limit>end) {total+=limit-end;end=limit;}
    ep->offset=base+(size_t)(token-begin);
  }
  run->host_bytes=total;free(order);return 0;
}
static int graph_host_fetch(mlu_backend_session *s,mlu_backend_transfer *t,void *out,uint64_t bytes) {
  mlu_graph_invocation *run=t->graph_hold;
  if(t->release_requested || !run || !run->host || bytes!=t->bytes) return CN_ERROR_INVALID_HANDLE;
  int st=mlu_backend_transfer_query(s,t->id);if(st) return st;
  size_t offset=0;
  for(size_t i=0;i<run->host_count;i++) if(run->endpoints[i].payload->host_kind==2) {
    mlu_graph_host_endpoint *ep=&run->endpoints[i];size_t n=ep->payload->memcpy.extent.width;
    memcpy((char *)out+offset,(char *)run->host+ep->offset,n);offset+=n;
  }
  return 0;
}
/* Describe returns opaque client endpoint tokens, lengths and directions.
 * Execute echoes that descriptor and adds current H2D bytes, so a stale plan
 * cannot silently bind a changed revision to old Host destinations. */
static int graph_host(mlu_backend_session *s,const mlu_api_graph_host_request *in,mlu_api_graph_host_response *out) {
  if((in->action!=1 && in->action!=2) || (in->action==1 && in->input_bytes)) return CN_ERROR_INVALID_VALUE;
  mlu_graph_entity *e=graph_find_entity(s,in->entity);if(!e || !e->handle) return CN_ERROR_INVALID_HANDLE;
  mlu_backend_queue *q=in->queue ? find_queue(s,in->queue) : NULL;
  if(in->queue && !q) return CN_ERROR_INVALID_HANDLE;
  /* Keep the mature non-Host path, including its native setter semantics. */
  int has_host=graph_view_has_host(e->revision);
  for(size_t i=0;i<e->count;i++) if(e->overrides[i] && (e->overrides[i]->host_kind || graph_view_has_host(e->overrides[i]->child_view))) has_host=1;
  if(!has_host) {
    if(in->action==2) {
      if(in->input_bytes) return CN_ERROR_INVALID_VALUE;
      mlu_api_graph_entity_request req={.action=MLU_GRAPH_ENTITY_INVOKE,.entity=e->id,.queue=in->queue};mlu_api_graph_entity_response res={0};
      int st=graph_entity(s,&req,&res);out->id=res.id;return st;
    }
    return 0;
  }
  mlu_graph_invocation *run=calloc(1,sizeof(*run));if(!run) return CN_ERROR_SYSTEM_OUT_OF_MEMORY;
  run->session=s;run->endpoints=calloc(MLU_GRAPH_MAX_NODES,sizeof(*run->endpoints));
  int st=run->endpoints ? graph_host_revision(s,e,&run->view) : CN_ERROR_SYSTEM_OUT_OF_MEMORY;
  size_t input=0,output=0;
  if(!st) st=graph_host_endpoints(run->view,run,&input,&output);
  if(!st) st=graph_host_pack_aliases(run);
  size_t plan=24*run->host_count;
  if(!st && in->action==1) {
    out->count=run->host_count;out->input_bytes=input;out->output_bytes=output;
    if(in->capacity && in->capacity<plan) st=CN_ERROR_INVALID_VALUE;
    else if(in->capacity) for(size_t i=0;i<run->host_count;i++) {
      mlu_graph_payload *p=run->endpoints[i].payload;
      gk_put64(out->data+24*i,p->host_token);gk_put64(out->data+24*i+8,p->memcpy.extent.width);gk_put64(out->data+24*i+16,p->host_kind);
    }
    graph_run_discard(s,run);(void)graph_views_reap(s);return st;
  }
  if(!st && (in->input_bytes!=plan+input || in->capacity)) st=CN_ERROR_INVALID_VALUE;
  for(size_t i=0;!st && i<run->host_count;i++) {
    mlu_graph_payload *p=run->endpoints[i].payload;
    if(gk_u64(in->data+24*i)!=p->host_token || gk_u64(in->data+24*i+8)!=p->memcpy.extent.width || gk_u64(in->data+24*i+16)!=p->host_kind) st=CN_ERROR_INVALID_VALUE;
  }
  if(!st) st=transfer_capacity(s);
  if(!st && run->host_bytes) {
    if(mlu_resource_reserve(&s->usage,MLU_RESOURCE_STAGING,run->host_bytes)) st=MLU_REMOTE_RESOURCE_LIMIT;
    else {st=cnMallocHost(&run->host,run->host_bytes);if(st) mlu_resource_release(&s->usage,MLU_RESOURCE_STAGING,run->host_bytes);}
  }
  size_t off=plan;
  for(size_t i=0;!st && i<run->host_count;i++) {
    mlu_graph_host_endpoint *ep=&run->endpoints[i];mlu_graph_payload *p=ep->payload;CNmemcpy3dParam params=p->memcpy;
    void *host=(char *)run->host+ep->offset;
    if(p->host_kind==1) {memcpy(host,in->data+off,params.extent.width);off+=params.extent.width;params.src=params.srcPtr.ptr=host;}
    else params.dst=params.dstPtr.ptr=host;
    st=cnTaskTopoMemcpyNodeSetParams(ep->node,&params);
  }
  if(!st) st=cnTaskTopoInstantiate(&run->private_entity,run->view->graph,NULL,NULL,0);
  int submitted=0;
  if(!st) {if(!in->queue) s->default_queue_used=1;submitted=1;st=cnTaskTopoEntityInvoke(run->private_entity,q ? q->handle : NULL);}
  if(!st) st=record_transfer(s,in->queue,1,&out->id);
  if(!st) {mlu_backend_transfer *t=find_transfer(s,out->id);t->graph_hold=run;t->bytes=output;out->count=run->host_count;out->output_bytes=output;}
  else if(!submitted || !cnQueueSync(q ? q->handle : NULL)) graph_run_discard(s,run);
  else {run->next=s->graph_orphans;s->graph_orphans=run;}
  (void)graph_views_reap(s);return st;
}
