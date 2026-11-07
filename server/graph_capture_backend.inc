/* Remote capture owns an ordinary native DAG. Never submit work while a
 * participant is capturing; application thread identity is explicit on wire. */
typedef struct mlu_capture {
  struct mlu_capture *next;
  uint64_t graph,root,thread;
  unsigned mode,invalid;
} mlu_capture;
typedef struct mlu_capture_queue {
  struct mlu_capture_queue *next;
  mlu_capture *capture;
  uint64_t queue,count,frontier[MLU_GRAPH_MAX_NODES];
} mlu_capture_queue;
typedef struct mlu_capture_notifier {
  struct mlu_capture_notifier *next;
  mlu_capture *capture;
  uint64_t notifier,count,frontier[MLU_GRAPH_MAX_NODES];
} mlu_capture_notifier;
static void *capture_alloc(mlu_backend_session *s,size_t bytes) {
  if(mlu_resource_reserve(&s->usage,MLU_RESOURCE_STAGING,bytes)) return NULL;
  void *p=calloc(1,bytes);
  if(!p) mlu_resource_release(&s->usage,MLU_RESOURCE_STAGING,bytes);
  return p;
}
static void capture_free(mlu_backend_session *s,void *p,size_t bytes) {
  if(p) {free(p);mlu_resource_release(&s->usage,MLU_RESOURCE_STAGING,bytes);}
}
static mlu_capture_queue *capture_queue(mlu_backend_session *s,uint64_t queue) {
  for(mlu_capture_queue *q=s->capture_queues;q;q=q->next) if(q->queue==queue) return q;
  return NULL;
}
static mlu_capture_notifier *capture_notifier(mlu_backend_session *s,uint64_t notifier) {
  for(mlu_capture_notifier *n=s->capture_notifiers;n;n=n->next) if(n->notifier==notifier) return n;
  return NULL;
}
static void capture_forget_notifier(mlu_backend_session *s,uint64_t notifier) {
  mlu_capture_notifier **p=&s->capture_notifiers;
  while(*p) {mlu_capture_notifier *n=*p;if(n->notifier!=notifier) {p=&n->next;continue;}*p=n->next;capture_free(s,n,sizeof(*n));}
}
static void capture_drop(mlu_backend_session *s,mlu_capture *c) {
  mlu_capture_queue **q=&s->capture_queues;
  while(*q) {mlu_capture_queue *v=*q;if(v->capture!=c) {q=&v->next;continue;}*q=v->next;capture_free(s,v,sizeof(*v));}
  mlu_capture_notifier **n=&s->capture_notifiers;
  while(*n) {mlu_capture_notifier *v=*n;if(v->capture!=c) {n=&v->next;continue;}*n=v->next;capture_free(s,v,sizeof(*v));}
  mlu_capture **p=&s->captures;while(*p && *p!=c) p=&(*p)->next;
  if(*p) *p=c->next;
  capture_free(s,c,sizeof(*c));
}
static void graph_capture_cleanup(mlu_backend_session *s) {
  while(s->captures) capture_drop(s,s->captures);
}
static int graph_capture_owns(mlu_backend_session *s,uint64_t graph) {
  for(mlu_capture *c=s->captures;c;c=c->next) if(c->graph==graph) return 1;
  return 0;
}
/* Only the capture RPC may add DAG work. Raw ordinary submissions must not
 * bypass capture, including clients racing Begin against an ordinary copy.
 * Existing entity invokes intentionally execute normally (SDK 2.5.2 contract). */
int mlu_backend_capture_guard(mlu_backend_session *s,uint16_t op,const unsigned char *p,uint32_t n) {
  if(!s->captures) return 0;
  int unsafe=0,has_queue=0;uint64_t queue=0;
  switch(op) {
    case MLU_OP_ALLOC:case MLU_OP_FREE:case MLU_OP_QUEUE_SYNC:case MLU_OP_QUEUE_QUERY:
    case MLU_OP_NOTIFIER_SYNC:case MLU_OP_NOTIFIER_QUERY:
    case MLU_OP_API_COPY2D:case MLU_OP_API_COPY3D:unsafe=1;break;
    case MLU_OP_API_MEMSET:unsafe=n==44 && !gk_u32(p+40);break;
    case MLU_OP_MEMCPY:unsafe=n>=60 && !gk_u32(p+56);break;
    default:break;
  }
  int rejected=0;
  if(unsafe) for(mlu_capture *c=s->captures;c;c=c->next) if(c->mode==0) {c->invalid=1;rejected=1;}
  if(rejected) return 100360;
  switch(op) {
    case MLU_OP_QUEUE_DESTROY:case MLU_OP_QUEUE_SYNC:case MLU_OP_QUEUE_QUERY:
      if(n==8) {has_queue=1;queue=gk_u64(p);}break;
    case MLU_OP_NOTIFIER_RECORD:if(n==16) {has_queue=1;queue=gk_u64(p+8);}break;
    case MLU_OP_API_QUEUE_WAIT:
      if(n==16) {has_queue=1;queue=gk_u64(p);mlu_capture_notifier *v=capture_notifier(s,gk_u64(p+8));if(v) {v->capture->invalid=1;return 100360;}}break;
    case MLU_OP_NOTIFIER_SYNC:case MLU_OP_NOTIFIER_QUERY:
      if(n==8) {mlu_capture_notifier *v=capture_notifier(s,gk_u64(p));if(v) {v->capture->invalid=1;return 100360;}}break;
    case MLU_OP_NOTIFIER_DESTROY:if(n==8) capture_forget_notifier(s,gk_u64(p));break;
    case MLU_OP_MEMCPY:if(n>=60) {has_queue=1;queue=gk_u64(p+4);}break;
    case MLU_OP_API_D2H_SUBMIT:if(n==32) {has_queue=1;queue=gk_u64(p+24);}break;
    case MLU_OP_API_MEMSET:if(n==44) {has_queue=1;queue=gk_u64(p+32);}break;
    case MLU_OP_KERNEL_LAUNCH_GENERIC:if(n>=44) {has_queue=1;queue=gk_u64(p+16);}break;
    case MLU_OP_OPERATOR_LAUNCH:if(n>=64) {has_queue=1;queue=gk_u64(p+16);}break;
    case MLU_OP_KERNEL_LAUNCH:if(n==56) {has_queue=1;queue=gk_u64(p+32);}break;
    default:break;
  }
  mlu_capture_queue *q=has_queue ? capture_queue(s,queue) : NULL;
  if(q) {int st=q->capture->invalid ? 100361 : 100360;q->capture->invalid=1;return st;}
  return 0;
}
static int capture_union(uint64_t *dst,uint64_t *count,const uint64_t *src,uint64_t n) {
  uint64_t needed=*count;
  for(uint64_t i=0;i<n;i++) {uint64_t j=0;for(;j<*count;j++) if(dst[j]==src[i]) break;if(j==*count) needed++;}
  if(needed>MLU_GRAPH_MAX_NODES) return MLU_REMOTE_RESOURCE_LIMIT;
  for(uint64_t i=0;i<n;i++) {uint64_t j=0;for(;j<*count;j++) if(dst[j]==src[i]) break;if(j==*count) dst[(*count)++]=src[i];}
  return 0;
}
/* End accepts ancestors of the root frontier, including a joined branch that
 * the root has subsequently advanced past. SDK GetDependentNodes is upstream. */
static int capture_joined(mlu_backend_session *s,mlu_capture_queue *root) {
  if(!root->count) {
    for(mlu_capture_queue *q=s->capture_queues;q;q=q->next)
      if(q->capture==root->capture && q->count) return 100364;
    return 0;
  }
  unsigned char seen[MLU_GRAPH_MAX_NODES]={0};
  unsigned stack[MLU_GRAPH_MAX_NODES],size=0;
  for(uint64_t i=0;i<root->count;i++) {int n=graph_node_index(s,root->frontier[i]);if(n<0) return CN_ERROR_INVALID_VALUE;if(!seen[n]) {seen[n]=1;stack[size++]=n;}}
  CNtaskTopoNode *deps=capture_alloc(s,MLU_GRAPH_MAX_NODES*sizeof(*deps));
  if(!deps) return MLU_REMOTE_RESOURCE_LIMIT;
  int st=0;
  while(size && !st) {
    unsigned n=stack[--size];size_t count=MLU_GRAPH_MAX_NODES;
    st=cnTaskTopoNodeGetDependentNodes(s->graph_nodes[n].handle,deps,&count);
    if(st || count>MLU_GRAPH_MAX_NODES) {if(!st) st=MLU_REMOTE_RESOURCE_LIMIT;break;}
    for(size_t i=0;i<count;i++) {
      unsigned d=0;
      for(;d<MLU_GRAPH_MAX_NODES;d++) if(s->graph_nodes[d].live && s->graph_nodes[d].handle==deps[i] && s->graph_nodes[d].parent==root->capture->graph) break;
      if(d==MLU_GRAPH_MAX_NODES) {st=CN_ERROR_INVALID_VALUE;break;}
      if(!seen[d]) {seen[d]=1;stack[size++]=d;}
    }
  }
  capture_free(s,deps,MLU_GRAPH_MAX_NODES*sizeof(*deps));
  if(st) return st;
  for(mlu_capture_queue *q=s->capture_queues;q;q=q->next) if(q->capture==root->capture)
    for(uint64_t i=0;i<q->count;i++) {int n=graph_node_index(s,q->frontier[i]);if(n<0 || !seen[n]) return 100364;}
  return 0;
}
static int capture_wait(mlu_backend_session *s,mlu_capture_queue *q,uint64_t queue,uint64_t notifier) {
  if(!find_notifier(s,notifier) || (queue && !find_queue(s,queue))) return CN_ERROR_INVALID_HANDLE;
  mlu_capture_notifier *n=capture_notifier(s,notifier);
  if(!n) return q ? (q->capture->invalid ? 100361 : 100365) : 0;
  if(q && q->capture!=n->capture) return 100363;
  if(n->capture->invalid) return 100361;
  if(!queue) return 100365;
  if(!q) {
    q=capture_alloc(s,sizeof(*q));if(!q) return MLU_REMOTE_RESOURCE_LIMIT;
    q->queue=queue;q->capture=n->capture;q->next=s->capture_queues;s->capture_queues=q;
  }
  return capture_union(q->frontier,&q->count,n->frontier,n->count);
}
static int capture_dispatch(mlu_backend_session *s,const mlu_api_graph_capture_request *in,mlu_api_graph_capture_response *out) {
  mlu_capture_queue *q=capture_queue(s,in->queue);
  const unsigned char *p=in->data;uint64_t n=in->input_bytes;unsigned op=in->opcode;
  out->handled=0;
  if(op==MLU_OP_API_QUEUE_WAIT && n==16) {
    uint64_t queue=gk_u64(p),notifier=gk_u64(p+8);
    if(queue!=in->queue) return CN_ERROR_INVALID_VALUE;
    if(q || capture_notifier(s,notifier)) {out->handled=1;return capture_wait(s,q,queue,notifier);}
  }
  if(op==MLU_OP_NOTIFIER_RECORD && n==16) {
    uint64_t id=gk_u64(p);
    if(gk_u64(p+8)!=in->queue || !find_notifier(s,id)) return CN_ERROR_INVALID_VALUE;
    if(!q) {capture_forget_notifier(s,id);return 0;}
    out->handled=1;if(q->capture->invalid) return 100361;
    mlu_capture_notifier *v=capture_notifier(s,id);
    if(!v) {v=capture_alloc(s,sizeof(*v));if(!v) return MLU_REMOTE_RESOURCE_LIMIT;v->notifier=id;v->next=s->capture_notifiers;s->capture_notifiers=v;}
    v->capture=q->capture;v->count=q->count;memcpy(v->frontier,q->frontier,q->count*8);return 0;
  }
  if((op==MLU_OP_NOTIFIER_SYNC || op==MLU_OP_NOTIFIER_QUERY) && n==8) {
    mlu_capture_notifier *v=capture_notifier(s,gk_u64(p));
    if(v) {out->handled=1;v->capture->invalid=1;return 100360;}
  }
  if(op==MLU_OP_NOTIFIER_DESTROY && n==8) {capture_forget_notifier(s,gk_u64(p));return 0;}
  if(!q) return 0;
  out->handled=1;
  if(op==MLU_OP_QUEUE_SYNC || op==MLU_OP_QUEUE_QUERY || op==MLU_OP_QUEUE_DESTROY) {q->capture->invalid=1;return 100360;}
  if(q->capture->invalid) return 100361;
  int st=0;uint64_t node=0;
  if(op==MLU_OP_API_MEMSET && n==44 && gk_u64(p+32)==in->queue && gk_u32(p+40)==1) {
    mlu_api_graph_memset_request a={.action=MLU_GRAPH_PARAM_ADD,.graph=q->capture->graph,.ids=q->frontier,.count=q->count,
      .allocation=gk_u64(p),.offset=gk_u64(p+8),.width=gk_u64(p+16),.value=gk_u32(p+24),.element_size=gk_u32(p+28),.height=1};
    mlu_api_graph_memset_response b={0};st=graph_memset(s,&a,&b);node=b.id;
  } else if(op==MLU_OP_API_GRAPH_MEMCPY && n==44) {
    mlu_api_graph_memcpy_request a={.action=MLU_GRAPH_PARAM_ADD,.graph=q->capture->graph,.ids=q->frontier,.count=q->count,
      .dst_allocation=gk_u64(p),.dst_offset=gk_u64(p+8),.src_allocation=gk_u64(p+16),.src_offset=gk_u64(p+24),.width=gk_u64(p+32),.direction=gk_u32(p+40)};
    mlu_api_graph_memcpy_response b={0};st=graph_memcpy(s,&a,&b);node=b.id;
  } else if(op==MLU_OP_KERNEL_LAUNCH_GENERIC && n>=44) {
    size_t bytes=8*q->count+n-44;
    unsigned char *data=capture_alloc(s,bytes ? bytes : 1);if(!data) return MLU_REMOTE_RESOURCE_LIMIT;
    for(uint64_t i=0;i<q->count;i++) gk_put64(data+8*i,q->frontier[i]);
    memcpy(data+8*q->count,p+44,n-44);size_t cursor=8*q->count;
    unsigned argc=gk_u32(p+40);
    if(argc>BANG_MAX_ARGS || gk_u64(p+16)!=in->queue) st=CN_ERROR_INVALID_VALUE;
    for(unsigned i=0;i<argc && !st;i++) {
      if(bytes-cursor<32) {st=CN_ERROR_INVALID_VALUE;break;}
      unsigned char *v=data+cursor;uint64_t relocs=gk_u64(v+24),argbytes=gk_u32(v+4);
      if(relocs>BANG_MAX_RELOCS || argbytes>bytes-cursor-32 || relocs>(bytes-cursor-32-argbytes)/24) {st=CN_ERROR_INVALID_VALUE;break;}
      gk_put32(v+24,(uint32_t)relocs);gk_put32(v+28,0);cursor+=32+argbytes+24*relocs;
    }
    if(cursor!=bytes) st=CN_ERROR_INVALID_VALUE;
    if(!st) {
      mlu_api_graph_kernel_request a={.action=MLU_GRAPH_PARAM_ADD,.graph=q->capture->graph,.count=q->count,.module=gk_u64(p),.kernel=gk_u64(p+8),
        .x=gk_u32(p+24),.y=gk_u32(p+28),.z=gk_u32(p+32),.klass=gk_u32(p+36),.arg_count=argc,.input_bytes=bytes,.data=data};
      mlu_api_graph_kernel_response b={0};st=graph_kernel(s,&a,&b);node=b.id;
    }
    capture_free(s,data,bytes ? bytes : 1);
  } else {q->capture->invalid=1;return 100360;}
  if(!st && node) {q->count=1;q->frontier[0]=node;}
  return st;
}
static int graph_capture(mlu_backend_session *s,const mlu_api_graph_capture_request *in,mlu_api_graph_capture_response *out) {
  if(in->action==6) {for(mlu_capture *c=s->captures;c;c=c->next) if(c->mode==0) c->invalid=1;return 0;}
  if(in->action==5) return capture_dispatch(s,in,out);
  if(!in->queue || !find_queue(s,in->queue)) return CN_ERROR_INVALID_VALUE;
  mlu_capture_queue *q=capture_queue(s,in->queue);
  if(in->action==1) {
    if(q) return CN_ERROR_ILLEGAL_STATE;
    if(in->mode>2 || !in->thread || in->input_bytes) return CN_ERROR_INVALID_VALUE;
    mlu_capture *c=capture_alloc(s,sizeof(*c));if(!c) return MLU_REMOTE_RESOURCE_LIMIT;
    q=capture_alloc(s,sizeof(*q));if(!q) {capture_free(s,c,sizeof(*c));return MLU_REMOTE_RESOURCE_LIMIT;}
    mlu_api_graph_action_request a={.action=MLU_GRAPH_CREATE};mlu_api_graph_action_response b={0};int st=graph_action(s,&a,&b);
    if(st) {capture_free(s,q,sizeof(*q));capture_free(s,c,sizeof(*c));return st;}
    c->graph=b.id;c->root=in->queue;c->thread=in->thread;c->mode=in->mode;c->next=s->captures;s->captures=c;
    q->capture=c;q->queue=in->queue;q->next=s->capture_queues;s->capture_queues=q;out->graph=c->graph;return 0;
  }
  if(in->action==3) {
    out->state=q ? (q->capture->invalid ? 2 : 1) : 0;
    if(out->state!=1) return 0;
    out->id=q->capture->graph;out->graph=q->capture->graph;out->count=q->count;
    if(in->capacity && in->capacity<q->count*8) return CN_ERROR_INVALID_VALUE;
    if(in->capacity) for(uint64_t i=0;i<q->count;i++) gk_put64(out->data+8*i,q->frontier[i]);
    return 0;
  }
  if(!q) return CN_ERROR_ILLEGAL_STATE;
  mlu_capture *c=q->capture;
  if(in->action==2) {
    if(c->root!=in->queue) {c->invalid=1;return 100366;}
    int st=c->mode==0 && c->thread!=in->thread ? 100362 : c->invalid ? 100361 : capture_joined(s,q);
    /* Resource/native query failure retains the capture for a retry. */
    if(st && st!=100361 && st!=100362 && st!=100364) return st;
    uint64_t graph=c->graph;capture_drop(s,c);
    if(st) {int g=graph_index(s,graph);if(g>=0) (void)graph_destroy(s,g);(void)graph_reap_allocations(s);return st;}
    out->graph=graph;return 0;
  }
  if(in->action==4) {
    if(c->invalid) return 100361;
    if(in->mode>1 || in->input_bytes%8 || in->input_bytes/8>MLU_GRAPH_MAX_NODES) return CN_ERROR_INVALID_VALUE;
    uint64_t ids[MLU_GRAPH_MAX_NODES],count=in->input_bytes/8;
    for(uint64_t i=0;i<count;i++) {ids[i]=gk_u64(in->data+8*i);int n=graph_node_index(s,ids[i]);if(n<0 || s->graph_nodes[n].parent!=c->graph) return CN_ERROR_INVALID_VALUE;}
    if(in->mode==1) {q->count=0;}
    return capture_union(q->frontier,&q->count,ids,count);
  }
  return CN_ERROR_INVALID_VALUE;
}
