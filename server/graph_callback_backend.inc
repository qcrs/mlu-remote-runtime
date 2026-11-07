/* G4: all executable code stays in the client. Native DAG callbacks only
 * exchange bounded events with the paired channel and wait for its ACK. */
#include "remote_callback.h"
typedef struct mlu_graph_callback_arg {
  mlu_callback_service *service;unsigned kind;uint64_t run,token;
  void *data;uint32_t bytes;
} mlu_graph_callback_arg;
static void graph_callback_thunk(void *opaque) {
  mlu_graph_callback_arg *a=opaque;
  (void)mlu_callback_exchange(a->service,a->kind,a->run,a->token,a->data,a->bytes,1);
}
static int graph_view_has_callback(mlu_graph_view *v) {
  if(v) for(size_t i=0;i<v->count;i++) if((v->entries[i].payload && v->entries[i].payload->callback_token) || graph_view_has_callback(v->entries[i].child)) return 1;
  return 0;
}
static int graph_callback_nodes(mlu_graph_view *v,mlu_graph_invocation *run,unsigned char *plan,size_t *count,int install) {
  for(size_t i=0;i<v->count;i++) {
    mlu_graph_payload *p=v->entries[i].payload;
    if(p && p->callback_token) {
      if(*count>=MLU_GRAPH_MAX_NODES) return MLU_REMOTE_RESOURCE_LIMIT;
      if(plan) {gk_put64(plan+24**count,p->callback_token);gk_put64(plan+24**count+8,0);gk_put64(plan+24**count+16,3);}(*count)++;
      if(install) {
        mlu_graph_callback_arg *arg=&run->callback_args[run->callback_count++];
        *arg=(mlu_graph_callback_arg){run->callback_service,MLU_CALLBACK_CALL,run->callback_cookie,p->callback_token,NULL,0};
        CN_HOST_NODE_PARAMS params={graph_callback_thunk,arg};int st=cnTaskTopoHostNodeSetParams(v->entries[i].node,&params);if(st)return st;
      }
    }
    if(v->entries[i].child) {int st=graph_callback_nodes(v->entries[i].child,run,plan,count,install);if(st)return st;}
  }
  return 0;
}
/* Keep each memcpy identity and add a Host boundary. H2D requests current
 * source bytes after its predecessors; D2H publishes before its successors. */
static int graph_callback_copy(mlu_graph_invocation *run,mlu_graph_host_endpoint *ep) {
  mlu_graph_payload *p=ep->payload;CNtaskTopo graph=ep->view->graph;size_t count=0;
  int st=cnTaskTopoGetEdges(graph,NULL,NULL,&count);if(st)return st;
  if(count>MLU_GRAPH_MAX_ARRAY/2) return MLU_REMOTE_RESOURCE_LIMIT;
  CNtaskTopoNode *edges=calloc(2*count+1,sizeof(*edges));if(!edges)return CN_ERROR_SYSTEM_OUT_OF_MEMORY;
  size_t actual=count;if(count)st=cnTaskTopoGetEdges(graph,edges,edges+count,&actual);
  if(!st && actual!=count)st=CN_ERROR_OPERATING_SYSTEM;
  mlu_graph_callback_arg *a=&run->callback_args[run->callback_count++];
  *a=(mlu_graph_callback_arg){run->callback_service,p->host_kind==1 ? MLU_CALLBACK_H2D : MLU_CALLBACK_D2H,
    run->callback_cookie,p->host_token,(char *)run->host+ep->offset,(uint32_t)p->memcpy.extent.width};
  CN_HOST_NODE_PARAMS params={graph_callback_thunk,a};CNtaskTopoNode callback=NULL;
  if(!st) st=cnTaskTopoAddHostNode(&callback,graph,p->host_kind==2 ? &ep->node : NULL,p->host_kind==2 ? 1 : 0,&params);
  if(!st && p->host_kind==1) {
    for(size_t i=0;!st && i<count;i++) if(edges[count+i]==ep->node) st=cnTaskTopoAddDependencies(graph,&edges[i],&callback,1);
    if(!st) st=cnTaskTopoAddDependencies(graph,&callback,&ep->node,1);
  } else if(!st) for(size_t i=0;!st && i<count;i++) if(edges[i]==ep->node) {
    st=cnTaskTopoRemoveDependencies(graph,&ep->node,&edges[count+i],1);
    if(!st) st=cnTaskTopoAddDependencies(graph,&callback,&edges[count+i],1);
  }
  free(edges);return st;
}
static int graph_callbacks(mlu_backend_session *s,const mlu_api_graph_control_request *in,mlu_api_graph_control_response *out) {
  mlu_graph_entity *e=graph_find_entity(s,in->entity);if(!e || !e->handle)return CN_ERROR_INVALID_VALUE;
  int has_callback=graph_view_has_callback(e->revision);
  for(size_t i=0;i<e->count;i++)if(e->overrides[i] && (e->overrides[i]->callback_token || graph_view_has_callback(e->overrides[i]->child_view)))has_callback=1;
  if(!has_callback)return 0;
  mlu_graph_invocation *run=calloc(1,sizeof(*run));if(!run)return CN_ERROR_SYSTEM_OUT_OF_MEMORY;
  run->session=s;int st=graph_host_revision(s,e,&run->view);
  if(!st && !graph_view_has_callback(run->view)) {graph_run_discard(s,run);(void)graph_views_reap(s);return 0;}
  out->id=1;size_t inputs=0,outputs=0,count=0;
  run->endpoints=calloc(MLU_GRAPH_MAX_NODES,sizeof(*run->endpoints));
  if(!st)st=run->endpoints ? graph_host_endpoints(run->view,run,&inputs,&outputs) : CN_ERROR_SYSTEM_OUT_OF_MEMORY;
  if(!st)st=graph_callback_nodes(run->view,run,NULL,&count,0);
  if(!st && count>MLU_GRAPH_MAX_NODES-run->host_count)st=MLU_REMOTE_RESOURCE_LIMIT;
  size_t plan_bytes=24*(run->host_count+count);
  unsigned char *plan=calloc(plan_bytes ? plan_bytes : 1,1);if(!plan && !st)st=CN_ERROR_SYSTEM_OUT_OF_MEMORY;
  for(size_t i=0;!st && i<run->host_count;i++) {mlu_graph_payload *p=run->endpoints[i].payload;
    gk_put64(plan+24*i,p->host_token);gk_put64(plan+24*i+8,p->memcpy.extent.width);gk_put64(plan+24*i+16,p->host_kind);}
  size_t all=run->host_count;if(!st)st=graph_callback_nodes(run->view,run,plan,&all,0);
  out->token=all;out->value=inputs;out->bytes=outputs;
  if(in->action==12) {
    if(!st && in->input_bytes)st=CN_ERROR_INVALID_VALUE;
    if(!st && in->capacity) {if(in->capacity<plan_bytes)st=CN_ERROR_INVALID_VALUE;else memcpy(out->data,plan,plan_bytes);}
    free(plan);graph_run_discard(s,run);(void)graph_views_reap(s);return st;
  }
  if(!st && (!s->callbacks || !in->token || in->capacity || in->input_bytes!=plan_bytes || memcmp(in->data,plan,plan_bytes)))st=CN_ERROR_INVALID_VALUE;
  free(plan);
  mlu_backend_queue *q=in->graph ? find_queue(s,in->graph) : NULL;
  if(!st && in->graph && !q)st=CN_ERROR_INVALID_HANDLE;
  if(!st)st=transfer_capacity(s);
  /* Helper nodes count against the same snapshot quota. */
  if(!st && run->host_count+1>MLU_GRAPH_MAX_NODES-s->graph_snapshot_nodes)st=MLU_REMOTE_RESOURCE_LIMIT;
  if(!st) {run->callback_cost=run->host_count+1;s->graph_snapshot_nodes+=(unsigned)run->callback_cost;
    run->callback_service=s->callbacks;mlu_callback_retain(s->callbacks);run->callback_cookie=in->token;
    run->callback_args=calloc(all+1,sizeof(*run->callback_args));if(!run->callback_args)st=CN_ERROR_SYSTEM_OUT_OF_MEMORY;}
  if(!st && run->host_bytes) {
    if(mlu_resource_reserve(&s->usage,MLU_RESOURCE_STAGING,run->host_bytes))st=MLU_REMOTE_RESOURCE_LIMIT;
    else {st=cnMallocHost(&run->host,run->host_bytes);if(st)mlu_resource_release(&s->usage,MLU_RESOURCE_STAGING,run->host_bytes);}
  }
  count=0;if(!st)st=graph_callback_nodes(run->view,run,NULL,&count,1);
  for(size_t i=0;!st && i<run->host_count;i++) {
    mlu_graph_host_endpoint *ep=&run->endpoints[i];CNmemcpy3dParam params=ep->payload->memcpy;
    if(ep->payload->host_kind==1)params.src=params.srcPtr.ptr=(char *)run->host+ep->offset;
    else params.dst=params.dstPtr.ptr=(char *)run->host+ep->offset;
    st=cnTaskTopoMemcpyNodeSetParams(ep->node,&params);if(!st)st=graph_callback_copy(run,ep);
  }
  size_t top=0;if(!st)st=cnTaskTopoGetNodes(run->view->graph,NULL,&top);
  CNtaskTopoNode *deps=NULL;
  if(!st) {deps=calloc(top ? top : 1,sizeof(*deps));if(!deps)st=CN_ERROR_SYSTEM_OUT_OF_MEMORY;}
  if(!st && top)st=cnTaskTopoGetNodes(run->view->graph,deps,&top);
  if(!st) {
    mlu_graph_callback_arg *arg=&run->callback_args[run->callback_count++];
    *arg=(mlu_graph_callback_arg){s->callbacks,MLU_CALLBACK_DONE,in->token,0,NULL,0};CN_HOST_NODE_PARAMS params={graph_callback_thunk,arg};CNtaskTopoNode done;
    st=cnTaskTopoAddHostNode(&done,run->view->graph,deps,top,&params);
  }
  free(deps);
  if(!st)st=cnTaskTopoInstantiate(&run->private_entity,run->view->graph,NULL,NULL,0);
  int submitted=0;
  if(!st) {if(!in->graph)s->default_queue_used=1;submitted=1;st=cnTaskTopoEntityInvoke(run->private_entity,q ? q->handle : NULL);}
  if(!st)st=record_transfer(s,in->graph,1,&out->id);
  if(!st) {find_transfer(s,out->id)->graph_hold=run;out->bytes=0;}
  else if(!submitted || !backend_queue_wait(s,q ? q->handle : NULL))graph_run_discard(s,run);
  else {mlu_callback_abort(s->callbacks,1);run->next=s->graph_orphans;s->graph_orphans=run;}
  (void)graph_views_reap(s);return st;
}
