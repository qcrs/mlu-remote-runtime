/* Native DAG is authoritative. This adapter owns every native graph/node and
 * translates identities and bounded arrays for both public libraries. */
#include "remote_graph.h"
/* Payloads are immutable once attached. Clones and executable snapshots
 * share records, each of which holds its endpoint allocation leases. */
typedef struct mlu_graph_payload {
  unsigned refs;
  mlu_backend_allocation *allocation;
  mlu_backend_allocation *source_allocation;
  CN_MEMSET_NODE_PARAMS memset;
  CNmemcpy3dParam memcpy;
  size_t child_count;
  struct mlu_graph_payload **children;
} mlu_graph_payload;
static void graph_payload_retain(mlu_graph_payload *p) { if(p) p->refs++; }
static void graph_payload_release(mlu_graph_payload *p) {
  if(!p || --p->refs) return;
  if(p->allocation) p->allocation->graph_refs--;
  if(p->source_allocation) p->source_allocation->graph_refs--;
  for(size_t i=0;i<p->child_count;i++) graph_payload_release(p->children[i]);
  free(p->children);
  free(p);
}
typedef struct mlu_graph_invocation {
  struct mlu_graph_invocation *next;
  size_t count;
  mlu_graph_payload *payloads[];
} mlu_graph_invocation;
typedef struct mlu_graph_entity {
  struct mlu_graph_entity *next;
  uint64_t id;
  CNtaskTopoEntity handle;
  CNtaskTopo snapshot;
  size_t count;
  size_t lease_count, snapshot_cost;
  uint64_t *source_ids;
  CNtaskTopoNode *nodes;
  mlu_graph_payload **payloads;
  mlu_graph_payload **overrides;
} mlu_graph_entity;
static void graph_invocation_release(mlu_graph_invocation *run) {
  if(!run) return;
  for(size_t i=0;i<run->count;i++) graph_payload_release(run->payloads[i]);
  free(run);
}
static void graph_transfer_release(mlu_backend_session *s,mlu_backend_transfer *t) {
  graph_invocation_release(t->graph_hold); t->graph_hold=NULL;
  if(s->context) (void)graph_reap_allocations(s);
}
static void graph_orphans_release(mlu_backend_session *s) {
  while(s->graph_orphans) {
    mlu_graph_invocation *run=s->graph_orphans; s->graph_orphans=run->next;
    graph_invocation_release(run);
  }
}
static mlu_graph_entity *graph_find_entity(mlu_backend_session *s,uint64_t id) {
  if(!mlu_object_registry_contains(&s->objects,MLU_OBJECT_GRAPH_ENTITY,id)) return NULL;
  for(mlu_graph_entity *e=s->graph_entities;e;e=e->next) if(e->id==id) return e;
  return NULL;
}
static int graph_entity_destroy(mlu_backend_session *s,mlu_graph_entity *e) {
  int st=0;
  if(e->handle) { st=cnTaskTopoEntityDestroy(e->handle); if(st) return st; e->handle=NULL; }
  if(e->snapshot) { st=cnTaskTopoDestroy(e->snapshot); if(st) return st; e->snapshot=NULL; }
  for(size_t i=0;i<e->lease_count;i++) graph_payload_release(e->payloads[i]);
  for(size_t i=0;i<e->count;i++) graph_payload_release(e->overrides[i]);
  mlu_graph_entity **link=&s->graph_entities; while(*link && *link!=e) link=&(*link)->next;
  if(*link) *link=e->next;
  mlu_object_registry_release(&s->objects,MLU_OBJECT_GRAPH_ENTITY,e->id);
  s->graph_snapshot_nodes-=(unsigned)e->snapshot_cost;
  free(e->source_ids); free(e->nodes); free(e->payloads); free(e->overrides); free(e);
  return 0;
}
static int graph_index(mlu_backend_session *s,uint64_t id) {
  if(!mlu_object_registry_contains(&s->objects,MLU_OBJECT_GRAPH,id)) return -1;
  for(unsigned i=0;i<MLU_BACKEND_MAX_OBJECTS;i++) if(s->graphs[i].live && s->graphs[i].id==id) return (int)i;
  return -1;
}
static int graph_node_index(mlu_backend_session *s,uint64_t id) {
  if(!mlu_object_registry_contains(&s->objects,MLU_OBJECT_GRAPH_NODE,id)) return -1;
  for(unsigned i=0;i<MLU_GRAPH_MAX_NODES;i++) if(s->graph_nodes[i].live && s->graph_nodes[i].id==id) return (int)i;
  return -1;
}
static void graph_forget_node(mlu_backend_session *s,unsigned i);
static void graph_forget_graph(mlu_backend_session *s,unsigned i) {
  uint64_t id=s->graphs[i].id;
  for(unsigned j=0;j<MLU_GRAPH_MAX_NODES;j++) if(s->graph_nodes[j].live && s->graph_nodes[j].parent==id) graph_forget_node(s,j);
  mlu_object_registry_release(&s->objects,MLU_OBJECT_GRAPH,id);
  memset(&s->graphs[i],0,sizeof(s->graphs[i]));
}
static void graph_forget_node(mlu_backend_session *s,unsigned i) {
  for(unsigned j=0;j<MLU_BACKEND_MAX_OBJECTS;j++)
    if(s->graphs[j].live && s->graphs[j].owner_node==s->graph_nodes[i].id) graph_forget_graph(s,j);
  graph_payload_release(s->graph_nodes[i].payload);
  mlu_object_registry_release(&s->objects,MLU_OBJECT_GRAPH_NODE,s->graph_nodes[i].id);
  memset(&s->graph_nodes[i],0,sizeof(s->graph_nodes[i]));
}
static int graph_destroy(mlu_backend_session *s,unsigned i) {
  CNresult st=cnTaskTopoDestroy(s->graphs[i].handle); if(st) return (int)st;
  graph_forget_graph(s,i); return 0;
}
static int graph_cleanup(mlu_backend_session *s) {
  int first=0; unsigned graphs=0,nodes=0;
  mlu_graph_entity *e=s->graph_entities;
  while(e) { mlu_graph_entity *next=e->next; int st=graph_entity_destroy(s,e); if(st && !first) first=st; e=next; }
  for(unsigned i=0;i<MLU_GRAPH_MAX_NODES;i++) nodes+=s->graph_nodes[i].live!=0;
  for(unsigned i=0;i<MLU_BACKEND_MAX_OBJECTS;i++) if(s->graphs[i].live && !s->graphs[i].owner_node) {
    graphs++; int st=graph_destroy(s,i); if(st && !first) first=st;
  }
  if(graphs) fprintf(stderr,"[server] GRAPH_CLEANUP owner=%llu graphs=%u nodes=%u status=%d\n",(unsigned long long)s->owner_token,graphs,nodes,first);
  int st=graph_reap_allocations(s); return first ? first : st;
}
static int graph_reserve(mlu_backend_session *s,int node) {
  unsigned limit=node ? MLU_GRAPH_MAX_NODES : MLU_BACKEND_MAX_OBJECTS;
  for(unsigned i=0;i<limit;i++) {
    int used=node ? s->graph_nodes[i].live : s->graphs[i].live;
    if(used) continue;
    uint64_t id; uint32_t generation;
    int st=register_object(s,node ? MLU_OBJECT_GRAPH_NODE : MLU_OBJECT_GRAPH,&id,&generation);
    if(st) return -1;
    if(node) { s->graph_nodes[i].id=id; s->graph_nodes[i].live=1; }
    else { s->graphs[i].id=id; s->graphs[i].live=1; }
    return (int)i;
  }
  return -1;
}
static void graph_unreserve(mlu_backend_session *s,unsigned i) {
  mlu_object_registry_release(&s->objects,MLU_OBJECT_GRAPH,s->graphs[i].id);
  memset(&s->graphs[i],0,sizeof(s->graphs[i]));
}
static int graph_owned_child(mlu_backend_session *s,uint64_t node) {
  for(unsigned i=0;i<MLU_BACKEND_MAX_OBJECTS;i++)
    if(s->graphs[i].live && s->graphs[i].owner_node==node) return (int)i;
  return -1;
}
/* A child graph belongs to its native node. Publish a separately owned identity
 * tree for every clone, and retain the immutable payload of each copied node. */
static int graph_adopt_nodes(mlu_backend_session *s,unsigned source,unsigned dest,unsigned depth) {
  if(depth>=64) return MLU_REMOTE_RESOURCE_LIMIT;
  uint64_t source_id=s->graphs[source].id;
  for(unsigned j=0;j<MLU_GRAPH_MAX_NODES;j++) if(s->graph_nodes[j].live && s->graph_nodes[j].parent==source_id) {
    int n=graph_reserve(s,1); if(n<0) return MLU_REMOTE_RESOURCE_LIMIT;
    s->graph_nodes[n].parent=s->graphs[dest].id;
    int st=cnTaskTopoNodeFindInClone(&s->graph_nodes[n].handle,s->graph_nodes[j].handle,s->graphs[dest].handle);
    if(st) return st;
    s->graph_nodes[n].payload=s->graph_nodes[j].payload; graph_payload_retain(s->graph_nodes[n].payload);
    int child=graph_owned_child(s,s->graph_nodes[j].id);
    if(child>=0) {
      int copy=graph_reserve(s,0); if(copy<0) return MLU_REMOTE_RESOURCE_LIMIT;
      s->graphs[copy].owner_node=s->graph_nodes[n].id;
      st=cnTaskTopoChildTopoNodeGetTopo(s->graph_nodes[n].handle,&s->graphs[copy].handle);
      if(!st) st=graph_adopt_nodes(s,child,copy,depth+1);
      if(st) return st;
    }
  }
  return 0;
}
static int graph_collect(mlu_backend_session *s,unsigned g,mlu_graph_payload **payloads,size_t *count,unsigned depth) {
  if(depth>=64) return MLU_REMOTE_RESOURCE_LIMIT;
  for(unsigned j=0;j<MLU_GRAPH_MAX_NODES;j++) if(s->graph_nodes[j].live && s->graph_nodes[j].parent==s->graphs[g].id) {
    if(*count==MLU_GRAPH_MAX_NODES) return MLU_REMOTE_RESOURCE_LIMIT;
    payloads[(*count)++]=s->graph_nodes[j].payload;
    int child=graph_owned_child(s,s->graph_nodes[j].id);
    if(child>=0) { int st=graph_collect(s,child,payloads,count,depth+1); if(st) return st; }
  }
  return 0;
}
static int graph_action(mlu_backend_session *s,const mlu_api_graph_action_request *in,mlu_api_graph_action_response *out) {
  int g=graph_index(s,in->graph),n=graph_node_index(s,in->node);
  if(in->action==MLU_GRAPH_CREATE || in->action==MLU_GRAPH_CLONE) {
    if(in->action==MLU_GRAPH_CLONE && g<0) return CN_ERROR_INVALID_VALUE;
    int dst=graph_reserve(s,0); if(dst<0) return MLU_REMOTE_RESOURCE_LIMIT;
    int st;
    if(in->action==MLU_GRAPH_CREATE) st=cnTaskTopoCreate(&s->graphs[dst].handle,in->flags);
    else {
      st=cnTaskTopoClone(&s->graphs[dst].handle,s->graphs[g].handle);
      if(!st) st=graph_adopt_nodes(s,g,dst,0);
      if(st && s->graphs[dst].handle) { int cleanup=graph_destroy(s,dst); return cleanup ? cleanup : st; }
    }
    if(st) { graph_unreserve(s,dst); return st; }
    out->id=s->graphs[dst].id; return 0;
  }
  switch(in->action) {
    case MLU_GRAPH_DESTROY: {
      if(g<0) return CN_ERROR_INVALID_VALUE;
      if(s->graphs[g].owner_node) return CN_ERROR_INVALID_VALUE;
      int st=graph_destroy(s,g); (void)graph_reap_allocations(s); return st;
    }
    case MLU_GRAPH_DESTROY_NODE: {
      if(n<0) return CN_ERROR_INVALID_VALUE;
      int st=cnTaskTopoDestroyNode(s->graph_nodes[n].handle);
      if(!st) graph_forget_node(s,n);
      (void)graph_reap_allocations(s); return st;
    }
    case MLU_GRAPH_NODE_TYPE: {
      if(n<0) return CN_ERROR_INVALID_VALUE;
      CNtaskTopoNodeType type; int st=cnTaskTopoNodeGetType(s->graph_nodes[n].handle,&type); if(!st) out->value=(uint32_t)type; return st;
    }
    case MLU_GRAPH_FIND_CLONE: {
      if(n<0 || g<0) return CN_ERROR_INVALID_VALUE;
      CNtaskTopoNode handle=NULL; int st=cnTaskTopoNodeFindInClone(&handle,s->graph_nodes[n].handle,s->graphs[g].handle); if(st) return st;
      for(unsigned i=0;i<MLU_GRAPH_MAX_NODES;i++) if(s->graph_nodes[i].live && s->graph_nodes[i].parent==in->graph && s->graph_nodes[i].handle==handle) { out->id=s->graph_nodes[i].id; return 0; }
      return CN_ERROR_INVALID_VALUE;
    }
    default: return CN_ERROR_INVALID_VALUE;
  }
}
static int graph_edit(mlu_backend_session *s,const mlu_api_graph_edit_request *in,mlu_api_graph_edit_response *out) {
  int g=graph_index(s,in->graph); if(g<0) return CN_ERROR_INVALID_VALUE;
  if(in->action<MLU_GRAPH_ADD_EMPTY || in->action>MLU_GRAPH_REMOVE_EDGES ||
     in->count>MLU_GRAPH_MAX_ARRAY || (in->action!=MLU_GRAPH_ADD_EMPTY && in->count%2)) return CN_ERROR_INVALID_VALUE;
  CNtaskTopoNode *handles=calloc(in->count ? in->count : 1,sizeof(*handles)); if(!handles) return CN_ERROR_SYSTEM_OUT_OF_MEMORY;
  for(uint64_t i=0;i<in->count;i++) {
    int n=graph_node_index(s,in->ids[i]);
    if(n<0 || s->graph_nodes[n].parent!=in->graph) { free(handles); return CN_ERROR_INVALID_VALUE; }
    handles[i]=s->graph_nodes[n].handle;
  }
  int st;
  if(in->action==MLU_GRAPH_ADD_EMPTY) {
    int n=graph_reserve(s,1); if(n<0) { free(handles); return MLU_REMOTE_RESOURCE_LIMIT; }
    st=cnTaskTopoAddEmptyNode(&s->graph_nodes[n].handle,s->graphs[g].handle,handles,(size_t)in->count);
    if(st) graph_forget_node(s,n);
    else { s->graph_nodes[n].parent=in->graph; out->id=s->graph_nodes[n].id; }
  } else {
    size_t count=in->count/2;
    st=in->action==MLU_GRAPH_ADD_EDGES ? cnTaskTopoAddDependencies(s->graphs[g].handle,handles,handles+count,count) :
        cnTaskTopoRemoveDependencies(s->graphs[g].handle,handles,handles+count,count);
  }
  free(handles); return st;
}
static int graph_query(mlu_backend_session *s,const mlu_api_graph_query_request *in,mlu_api_graph_query_response *out) {
  int g=graph_index(s,in->object),n=graph_node_index(s,in->object);
  int node_query=in->query==MLU_GRAPH_DEPENDENCIES || in->query==MLU_GRAPH_DEPENDENTS;
  if(in->query<MLU_GRAPH_NODES || in->query>MLU_GRAPH_DEPENDENTS ||
     (node_query ? n<0 : g<0) || in->capacity>MLU_GRAPH_MAX_ARRAY ||
     in->slots!=in->capacity*(in->query==MLU_GRAPH_EDGES ? 2u : 1u) ||
     in->slots>MLU_GRAPH_MAX_ARRAY || in->has_array>1 || (!in->has_array && in->slots)) return CN_ERROR_INVALID_VALUE;
  uint64_t parent=node_query ? s->graph_nodes[n].parent : in->object;
  /* Allocate a non-NULL sentinel for zero-capacity array queries. */
  CNtaskTopoNode *handles=in->has_array ? calloc(in->slots ? in->slots : 2,sizeof(*handles)) : NULL;
  if(in->has_array && !handles) return CN_ERROR_SYSTEM_OUT_OF_MEMORY;
  size_t count=in->capacity; int st;
  switch(in->query) {
    case MLU_GRAPH_NODES: st=cnTaskTopoGetNodes(s->graphs[g].handle,handles,&count); break;
    case MLU_GRAPH_EDGES: st=cnTaskTopoGetEdges(s->graphs[g].handle,handles,handles ? handles+in->capacity : NULL,&count); break;
    case MLU_GRAPH_ROOTS: st=cnTaskTopoGetRootNodes(s->graphs[g].handle,handles,&count); break;
    case MLU_GRAPH_DEPENDENCIES: st=cnTaskTopoNodeGetDependencies(s->graph_nodes[n].handle,handles,&count); break;
    default: st=cnTaskTopoNodeGetDependentNodes(s->graph_nodes[n].handle,handles,&count); break;
  }
  if(!st && in->has_array) for(uint64_t i=0;i<in->slots;i++) {
    out->ids[i]=0; if(!handles[i]) continue;
    unsigned j=0;
    for(;j<MLU_GRAPH_MAX_NODES;j++) if(s->graph_nodes[j].live && s->graph_nodes[j].parent==parent && s->graph_nodes[j].handle==handles[i]) { out->ids[i]=s->graph_nodes[j].id; break; }
    if(j==MLU_GRAPH_MAX_NODES) { st=CN_ERROR_OPERATING_SYSTEM; break; }
  }
  out->count=count; free(handles); return st;
}

static int graph_child(mlu_backend_session *s,const mlu_api_graph_child_request *in,mlu_api_graph_child_response *out) {
  int g=graph_index(s,in->graph),n=graph_node_index(s,in->node),child=graph_index(s,in->child);
  if(in->action==MLU_GRAPH_CHILD_GET) {
    if(n<0 || in->count) return CN_ERROR_INVALID_VALUE;
    int embedded=graph_owned_child(s,in->node);
    if(embedded<0) return CN_ERROR_INVALID_VALUE;
    out->id=s->graphs[embedded].id; return 0;
  }
  if(child<0 || in->count>MLU_GRAPH_MAX_NODES) return CN_ERROR_INVALID_VALUE;
  if(in->action==MLU_GRAPH_CHILD_ENTITY_SET) {
    if(n<0 || in->count) return CN_ERROR_INVALID_VALUE;
    mlu_graph_entity *e=graph_find_entity(s,in->entity); size_t i=0;
    if(e) for(;i<e->count;i++) if(e->source_ids[i]==in->node) break;
    if(!e || !e->handle || i==e->count) return CN_ERROR_INVALID_VALUE;
    mlu_graph_payload *p=calloc(1,sizeof(*p)); if(!p) return CN_ERROR_SYSTEM_OUT_OF_MEMORY;
    p->refs=1; p->children=calloc(MLU_GRAPH_MAX_NODES,sizeof(*p->children));
    if(!p->children) { free(p); return CN_ERROR_SYSTEM_OUT_OF_MEMORY; }
    size_t count=0; int st=graph_collect(s,child,p->children,&count,0);
    if(!st) st=cnTaskTopoEntityChildTopoNodeSetParams(e->handle,e->nodes[i],s->graphs[child].handle);
    if(!st) {
      p->child_count=count;
      for(size_t j=0;j<count;j++) graph_payload_retain(p->children[j]);
      graph_payload_release(e->overrides[i]); e->overrides[i]=p;
    } else graph_payload_release(p);
    (void)graph_reap_allocations(s); return st;
  }
  if(in->action!=MLU_GRAPH_CHILD_ADD || g<0) return CN_ERROR_INVALID_VALUE;
  CNtaskTopoNode deps[MLU_GRAPH_MAX_NODES];
  for(size_t i=0;i<in->count;i++) {
    int d=graph_node_index(s,in->ids[i]);
    if(d<0 || s->graph_nodes[d].parent!=in->graph) return CN_ERROR_INVALID_VALUE;
    deps[i]=s->graph_nodes[d].handle;
  }
  n=graph_reserve(s,1); if(n<0) return MLU_REMOTE_RESOURCE_LIMIT;
  s->graph_nodes[n].parent=in->graph;
  int embedded=graph_reserve(s,0); if(embedded<0) { graph_forget_node(s,n); return MLU_REMOTE_RESOURCE_LIMIT; }
  s->graphs[embedded].owner_node=s->graph_nodes[n].id;
  int st=cnTaskTopoAddChildTopoNode(&s->graph_nodes[n].handle,s->graphs[g].handle,deps,in->count,s->graphs[child].handle);
  if(!st) st=cnTaskTopoChildTopoNodeGetTopo(s->graph_nodes[n].handle,&s->graphs[embedded].handle);
  if(!st) st=graph_adopt_nodes(s,child,embedded,0);
  if(st) {
    int cleanup=s->graph_nodes[n].handle ? cnTaskTopoDestroyNode(s->graph_nodes[n].handle) : 0;
    /* If native rollback fails, keep identities and leases for parent cleanup. */
    if(!cleanup) graph_forget_node(s,n);
    return st;
  }
  out->id=s->graph_nodes[n].id; return 0;
}
static int graph_memset(mlu_backend_session *s,const mlu_api_graph_memset_request *in,mlu_api_graph_memset_response *out) {
  int g=graph_index(s,in->graph),n=graph_node_index(s,in->node);
  if(in->action<MLU_GRAPH_PARAM_ADD || in->action>MLU_GRAPH_PARAM_ENTITY_SET ||
     in->count>MLU_GRAPH_MAX_NODES || (in->count && !in->ids) ||
     (in->action==MLU_GRAPH_PARAM_ADD ? g<0 : n<0) ||
     (in->action!=MLU_GRAPH_PARAM_ADD && in->count)) return CN_ERROR_INVALID_VALUE;
  if(in->action!=MLU_GRAPH_PARAM_ADD) {
    CNtaskTopoNodeType kind;
    int st=cnTaskTopoNodeGetType(s->graph_nodes[n].handle,&kind);
    if(st || kind!=CN_TASK_TOPO_NODE_TYPE_MEMSET) return st ? st : CN_ERROR_INVALID_VALUE;
  }
  if(in->action==MLU_GRAPH_PARAM_GET) {
    CN_MEMSET_NODE_PARAMS p={0};
    int st=cnTaskTopoMemsetNodeGetParams(s->graph_nodes[n].handle,&p); if(st) return st;
    mlu_backend_allocation *a=s->graph_nodes[n].payload->allocation;
    out->allocation=a->id; out->offset=p.dst-a->address; out->pitch=p.pitch;
    out->value=p.value; out->element_size=p.elementSize; out->width=p.width; out->height=p.height;
    return 0;
  }
  mlu_backend_allocation *a=find_alloc(s,in->allocation); CNaddr address;
  if(!a || !in->width || !in->height ||
     (in->element_size!=1 && in->element_size!=2 && in->element_size!=4) ||
     in->width>UINT64_MAX/in->element_size) return CN_ERROR_INVALID_VALUE;
  uint64_t row=in->width*in->element_size,span=row;
  if(in->height>1) {
    if(in->pitch<row || in->height-1>(UINT64_MAX-row)/in->pitch) return CN_ERROR_INVALID_VALUE;
    span+=(in->height-1)*in->pitch;
  }
  if(range(a,in->offset,span,&address)) return CN_ERROR_INVALID_VALUE;
  mlu_graph_payload *p=calloc(1,sizeof(*p)); if(!p) return CN_ERROR_SYSTEM_OUT_OF_MEMORY;
  p->refs=1; p->allocation=a; a->graph_refs++;
  p->memset=(CN_MEMSET_NODE_PARAMS){.dst=address,.pitch=in->pitch,.value=in->value,
      .elementSize=in->element_size,.width=in->width,.height=in->height};
  int st=0;
  if(in->action==MLU_GRAPH_PARAM_ADD) {
    CNtaskTopoNode handles[MLU_GRAPH_MAX_NODES];
    for(uint64_t i=0;i<in->count;i++) {
      int d=graph_node_index(s,in->ids[i]);
      if(d<0 || s->graph_nodes[d].parent!=in->graph) { st=CN_ERROR_INVALID_VALUE; break; }
      handles[i]=s->graph_nodes[d].handle;
    }
    if(!st) {
      n=graph_reserve(s,1); if(n<0) st=MLU_REMOTE_RESOURCE_LIMIT;
      else {
        st=cnTaskTopoAddMemsetNode(&s->graph_nodes[n].handle,s->graphs[g].handle,handles,in->count,&p->memset);
        if(st) graph_forget_node(s,n);
        else { s->graph_nodes[n].parent=in->graph; out->id=s->graph_nodes[n].id; }
      }
    }
  } else if(in->action==MLU_GRAPH_PARAM_ENTITY_SET) {
    mlu_graph_entity *e=graph_find_entity(s,in->entity); size_t i=0;
    if(e) for(;i<e->count;i++) if(e->source_ids[i]==in->node) break;
    if(!e || !e->handle || i==e->count) st=CN_ERROR_INVALID_VALUE;
    else {
      st=cnTaskTopoEntityMemsetNodeSetParams(e->handle,e->nodes[i],&p->memset);
      if(!st) { graph_payload_release(e->overrides[i]); e->overrides[i]=p; }
    }
    if(st) graph_payload_release(p);
    (void)graph_reap_allocations(s); return st;
  } else st=cnTaskTopoMemsetNodeSetParams(s->graph_nodes[n].handle,&p->memset);
  if(st) graph_payload_release(p);
  else { graph_payload_release(s->graph_nodes[n].payload); s->graph_nodes[n].payload=p; }
  (void)graph_reap_allocations(s); return st;
}

static int graph_memcpy(mlu_backend_session *s,const mlu_api_graph_memcpy_request *in,mlu_api_graph_memcpy_response *out) {
  int g=graph_index(s,in->graph),n=graph_node_index(s,in->node);
  if(in->action<MLU_GRAPH_PARAM_ADD || in->action>MLU_GRAPH_PARAM_ENTITY_SET ||
     in->count>MLU_GRAPH_MAX_NODES || (in->count && !in->ids) ||
     (in->action==MLU_GRAPH_PARAM_ADD ? g<0 : n<0) ||
     (in->action!=MLU_GRAPH_PARAM_ADD && in->count)) return CN_ERROR_INVALID_VALUE;
  if(in->action!=MLU_GRAPH_PARAM_ADD) {
    CNtaskTopoNodeType kind;
    int st=cnTaskTopoNodeGetType(s->graph_nodes[n].handle,&kind);
    if(st || kind!=CN_TASK_TOPO_NODE_TYPE_MEMCPY) return st ? st : CN_ERROR_INVALID_VALUE;
  }
  if(in->action==MLU_GRAPH_PARAM_GET) {
    CNmemcpy3dParam native={0};
    int st=cnTaskTopoMemcpyNodeGetParams(s->graph_nodes[n].handle,&native); if(st) return st;
    mlu_graph_payload *payload=s->graph_nodes[n].payload;
    if(!payload || !payload->allocation || !payload->source_allocation ||
       (uintptr_t)native.dst<(uintptr_t)payload->allocation->address ||
       (uintptr_t)native.src<(uintptr_t)payload->source_allocation->address) return CN_ERROR_INVALID_VALUE;
    out->dst_allocation=payload->allocation->id;
    out->dst_offset=(uintptr_t)native.dst-(uintptr_t)payload->allocation->address;
    out->src_allocation=payload->source_allocation->id;
    out->src_offset=(uintptr_t)native.src-(uintptr_t)payload->source_allocation->address;
    out->width=native.extent.width; out->direction=native.dir;
    return 0;
  }
  mlu_backend_allocation *dst=find_alloc(s,in->dst_allocation),*src=find_alloc(s,in->src_allocation);
  CNaddr dst_address,src_address;
  if(!dst || !src || !in->width || in->width>MLU_GRAPH_MAX_COPY_BYTES ||
     range(dst,in->dst_offset,in->width,&dst_address) ||
     range(src,in->src_offset,in->width,&src_address)) return CN_ERROR_INVALID_VALUE;
  mlu_graph_payload *payload=calloc(1,sizeof(*payload)); if(!payload) return CN_ERROR_SYSTEM_OUT_OF_MEMORY;
  payload->refs=1; payload->allocation=dst; payload->source_allocation=src;
  dst->graph_refs++; src->graph_refs++;
  payload->memcpy=(CNmemcpy3dParam){0};
  payload->memcpy.dst=(void *)(uintptr_t)dst_address;
  payload->memcpy.src=(void *)(uintptr_t)src_address;
  payload->memcpy.dstPtr.ptr=payload->memcpy.dst;
  payload->memcpy.dstPtr.pitch=in->width; payload->memcpy.dstPtr.xsize=in->width; payload->memcpy.dstPtr.ysize=1;
  payload->memcpy.srcPtr.ptr=payload->memcpy.src;
  payload->memcpy.srcPtr.pitch=in->width; payload->memcpy.srcPtr.xsize=in->width; payload->memcpy.srcPtr.ysize=1;
  payload->memcpy.extent.depth=1; payload->memcpy.extent.height=1; payload->memcpy.extent.width=in->width;
  payload->memcpy.dir=in->direction;
  int st=0;
  if(in->action==MLU_GRAPH_PARAM_ADD) {
    CNtaskTopoNode handles[MLU_GRAPH_MAX_NODES];
    for(uint64_t i=0;i<in->count;i++) {
      int d=graph_node_index(s,in->ids[i]);
      if(d<0 || s->graph_nodes[d].parent!=in->graph) { st=CN_ERROR_INVALID_VALUE; break; }
      handles[i]=s->graph_nodes[d].handle;
    }
    if(!st) {
      n=graph_reserve(s,1); if(n<0) st=MLU_REMOTE_RESOURCE_LIMIT;
      else {
        st=cnTaskTopoAddMemcpyNode(&s->graph_nodes[n].handle,s->graphs[g].handle,handles,in->count,&payload->memcpy);
        if(st) graph_forget_node(s,n);
        else { s->graph_nodes[n].parent=in->graph; out->id=s->graph_nodes[n].id; }
      }
    }
  } else if(in->action==MLU_GRAPH_PARAM_ENTITY_SET) {
    mlu_graph_entity *e=graph_find_entity(s,in->entity); size_t i=0;
    if(e) for(;i<e->count;i++) if(e->source_ids[i]==in->node) break;
    if(!e || !e->handle || i==e->count) st=CN_ERROR_INVALID_VALUE;
    else {
      st=cnTaskTopoEntityMemcpyNodeSetParams(e->handle,e->nodes[i],&payload->memcpy);
      if(!st) { graph_payload_release(e->overrides[i]); e->overrides[i]=payload; }
    }
    if(st) graph_payload_release(payload);
    (void)graph_reap_allocations(s); return st;
  } else st=cnTaskTopoMemcpyNodeSetParams(s->graph_nodes[n].handle,&payload->memcpy);
  if(st) graph_payload_release(payload);
  else { graph_payload_release(s->graph_nodes[n].payload); s->graph_nodes[n].payload=payload; }
  (void)graph_reap_allocations(s); return st;
}

/* The cloned DAG retains the original instantiate-time node correspondence.
 * Native EntityUpdate deliberately does not change that identity mapping. */
static int graph_snapshot_create(mlu_backend_session *s,unsigned g,mlu_graph_entity *e) {
  size_t count=0; int st=cnTaskTopoGetNodes(s->graphs[g].handle,NULL,&count); if(st) return st;
  mlu_graph_payload *leases[MLU_GRAPH_MAX_NODES]; size_t lease_count=0;
  st=graph_collect(s,g,leases,&lease_count,0); if(st) return st;
  if(lease_count>MLU_GRAPH_MAX_NODES-s->graph_snapshot_nodes) return MLU_REMOTE_RESOURCE_LIMIT;
  e->source_ids=calloc(count ? count : 1,sizeof(*e->source_ids));
  e->nodes=calloc(count ? count : 1,sizeof(*e->nodes));
  e->payloads=calloc(lease_count ? lease_count : 1,sizeof(*e->payloads));
  e->overrides=calloc(count ? count : 1,sizeof(*e->overrides));
  if(!e->source_ids || !e->nodes || !e->payloads || !e->overrides) return CN_ERROR_SYSTEM_OUT_OF_MEMORY;
  e->count=count; e->lease_count=lease_count; e->snapshot_cost=lease_count;
  s->graph_snapshot_nodes+=(unsigned)lease_count;
  for(size_t i=0;i<lease_count;i++) { e->payloads[i]=leases[i]; graph_payload_retain(leases[i]); }
  st=cnTaskTopoClone(&e->snapshot,s->graphs[g].handle); if(st) return st;
  CNtaskTopoNode handles[MLU_GRAPH_MAX_NODES]; size_t actual=count;
  st=cnTaskTopoGetNodes(s->graphs[g].handle,handles,&actual); if(st || actual!=count) return st ? st : CN_ERROR_OPERATING_SYSTEM;
  for(size_t i=0;i<count;i++) {
    st=cnTaskTopoNodeFindInClone(&e->nodes[i],handles[i],e->snapshot); if(st) return st;
    for(unsigned j=0;j<MLU_GRAPH_MAX_NODES;j++) if(s->graph_nodes[j].live && s->graph_nodes[j].handle==handles[i] && s->graph_nodes[j].parent==s->graphs[g].id) {
      e->source_ids[i]=s->graph_nodes[j].id; break;
    }
    if(!e->source_ids[i]) return CN_ERROR_OPERATING_SYSTEM;
  }
  return 0;
}
static uint64_t graph_node_id(mlu_backend_session *s,CNtaskTopoNode handle) {
  for(unsigned i=0;i<MLU_GRAPH_MAX_NODES;i++) if(s->graph_nodes[i].live && s->graph_nodes[i].handle==handle) return s->graph_nodes[i].id;
  return 0;
}
static int graph_entity(mlu_backend_session *s,const mlu_api_graph_entity_request *in,mlu_api_graph_entity_response *out) {
  int g=graph_index(s,in->graph); CNtaskTopoNode error=NULL;
  if(in->log_size) memset(out->log,0,in->log_size);
  if(in->action==MLU_GRAPH_INSTANTIATE) {
    if(g<0) return CN_ERROR_INVALID_VALUE;
    unsigned count=0; for(mlu_graph_entity *e=s->graph_entities;e;e=e->next) count++;
    if(count==MLU_BACKEND_MAX_OBJECTS) return MLU_REMOTE_RESOURCE_LIMIT;
    mlu_graph_entity *e=calloc(1,sizeof(*e)); if(!e) return CN_ERROR_SYSTEM_OUT_OF_MEMORY;
    uint32_t generation; int st=register_object(s,MLU_OBJECT_GRAPH_ENTITY,&e->id,&generation);
    if(st) { free(e); return st; }
    e->next=s->graph_entities; s->graph_entities=e;
    st=graph_snapshot_create(s,g,e);
    if(!st) st=cnTaskTopoInstantiate(&e->handle,e->snapshot,&error,(char *)out->log,in->log_size);
    if(st) {
      if(error) for(size_t i=0;i<e->count;i++) if(e->nodes[i]==error) { out->error_node=e->source_ids[i]; break; }
      int cleanup=graph_entity_destroy(s,e); return cleanup ? cleanup : st;
    }
    out->id=e->id; return 0;
  }
  mlu_graph_entity *e=graph_find_entity(s,in->entity); if(!e) return CN_ERROR_INVALID_VALUE;
  if(in->action==MLU_GRAPH_ENTITY_DESTROY) {
    int st=graph_entity_destroy(s,e); (void)graph_reap_allocations(s); return st;
  }
  if(!e->handle) return CN_ERROR_INVALID_VALUE;
  if(in->action==MLU_GRAPH_ENTITY_INVOKE) {
    mlu_backend_queue *q=in->queue ? find_queue(s,in->queue) : NULL;
    if(in->queue && !q) return CN_ERROR_INVALID_HANDLE;
    int st=transfer_capacity(s); if(st) return st;
    mlu_graph_invocation *run=calloc(1,sizeof(*run)+(e->lease_count+e->count)*sizeof(*run->payloads));
    if(!run) return CN_ERROR_SYSTEM_OUT_OF_MEMORY;
    run->count=e->lease_count+e->count;
    memcpy(run->payloads,e->payloads,e->lease_count*sizeof(*run->payloads));
    memcpy(run->payloads+e->lease_count,e->overrides,e->count*sizeof(*run->payloads));
    for(size_t i=0;i<run->count;i++) graph_payload_retain(run->payloads[i]);
    if(!in->queue) s->default_queue_used=1;
    st=cnTaskTopoEntityInvoke(e->handle,q ? q->handle : NULL);
    if(st) { graph_invocation_release(run); return st; }
    st=record_transfer(s,in->queue,1,&out->id);
    if(!st) find_transfer(s,out->id)->graph_hold=run;
    else {
      /* Submission succeeded but no completion identity could be published.
       * Preserve every lease until a confirmed queue/context completion. */
      if(!cnQueueSync(q ? q->handle : NULL)) graph_invocation_release(run);
      else { run->next=s->graph_orphans; s->graph_orphans=run; }
    }
    return st;
  }
  if(in->action==MLU_GRAPH_ENTITY_UPDATE) {
    if(g<0) return CN_ERROR_INVALID_VALUE;
    CNtaskTopoEntityUpdateResult result=CN_TASK_TOPO_ENTITY_UPDATE_ERROR;
    mlu_graph_payload *leases[MLU_GRAPH_MAX_NODES]; size_t count=0;
    int st=graph_collect(s,g,leases,&count,0); if(st) return st;
    mlu_graph_payload **replacement=calloc(count ? count : 1,sizeof(*replacement));
    if(!replacement) return CN_ERROR_SYSTEM_OUT_OF_MEMORY;
    memcpy(replacement,leases,count*sizeof(*replacement));
    st=cnTaskTopoEntityUpdate(e->handle,s->graphs[g].handle,&error,&result);
    out->update_result=result; out->error_node=graph_node_id(s,error);
    if(!st) {
      /* No enumeration order is used as native node correspondence. */
      for(size_t i=0;i<count;i++) graph_payload_retain(replacement[i]);
      for(size_t i=0;i<e->lease_count;i++) graph_payload_release(e->payloads[i]);
      free(e->payloads); e->payloads=replacement; replacement=NULL; e->lease_count=count;
      for(size_t i=0;i<e->count;i++) { graph_payload_release(e->overrides[i]); e->overrides[i]=NULL; }
      (void)graph_reap_allocations(s);
    }
    free(replacement);
    return st;
  }
  return CN_ERROR_INVALID_VALUE;
}
