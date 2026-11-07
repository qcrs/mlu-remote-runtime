/* Native DAG is authoritative. This adapter owns every native graph/node and
 * translates identities and bounded arrays for both public libraries. */
#include "remote_graph.h"
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
static void graph_forget_node(mlu_backend_session *s,unsigned i) {
  mlu_object_registry_release(&s->objects,MLU_OBJECT_GRAPH_NODE,s->graph_nodes[i].id);
  memset(&s->graph_nodes[i],0,sizeof(s->graph_nodes[i]));
}
static int graph_destroy(mlu_backend_session *s,unsigned i) {
  CNresult st=cnTaskTopoDestroy(s->graphs[i].handle); if(st) return (int)st;
  uint64_t id=s->graphs[i].id;
  for(unsigned j=0;j<MLU_GRAPH_MAX_NODES;j++) if(s->graph_nodes[j].live && s->graph_nodes[j].parent==id) graph_forget_node(s,j);
  mlu_object_registry_release(&s->objects,MLU_OBJECT_GRAPH,id);
  memset(&s->graphs[i],0,sizeof(s->graphs[i])); return 0;
}
static int graph_cleanup(mlu_backend_session *s) {
  int first=0; unsigned graphs=0,nodes=0;
  for(unsigned i=0;i<MLU_GRAPH_MAX_NODES;i++) nodes+=s->graph_nodes[i].live!=0;
  for(unsigned i=0;i<MLU_BACKEND_MAX_OBJECTS;i++) if(s->graphs[i].live) {
    graphs++; int st=graph_destroy(s,i); if(st && !first) first=st;
  }
  if(graphs) fprintf(stderr,"[server] GRAPH_CLEANUP owner=%llu graphs=%u nodes=%u status=%d\n",(unsigned long long)s->owner_token,graphs,nodes,first);
  return first;
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
static int graph_action(mlu_backend_session *s,const mlu_api_graph_action_request *in,mlu_api_graph_action_response *out) {
  int g=graph_index(s,in->graph),n=graph_node_index(s,in->node);
  if(in->action==MLU_GRAPH_CREATE || in->action==MLU_GRAPH_CLONE) {
    if(in->action==MLU_GRAPH_CLONE && g<0) return CN_ERROR_INVALID_VALUE;
    int dst=graph_reserve(s,0); if(dst<0) return MLU_REMOTE_RESOURCE_LIMIT;
    int st;
    if(in->action==MLU_GRAPH_CREATE) st=cnTaskTopoCreate(&s->graphs[dst].handle,in->flags);
    else {
      size_t count=0; st=cnTaskTopoGetNodes(s->graphs[g].handle,NULL,&count);
      if(st || count>MLU_GRAPH_MAX_NODES) { graph_unreserve(s,dst); return st ? st : (int)MLU_REMOTE_RESOURCE_LIMIT; }
      int slots[MLU_GRAPH_MAX_NODES]; size_t reserved=0;
      for(;reserved<count;reserved++) { slots[reserved]=graph_reserve(s,1); if(slots[reserved]<0) break; }
      if(reserved!=count) {
        while(reserved) graph_forget_node(s,slots[--reserved]);
        graph_unreserve(s,dst); return MLU_REMOTE_RESOURCE_LIMIT;
      }
      st=cnTaskTopoClone(&s->graphs[dst].handle,s->graphs[g].handle);
      CNtaskTopoNode handles[MLU_GRAPH_MAX_NODES]; size_t actual=count;
      if(!st) st=cnTaskTopoGetNodes(s->graphs[dst].handle,handles,&actual);
      if(!st && actual!=count) st=CN_ERROR_OPERATING_SYSTEM;
      for(size_t i=0;i<count;i++) {
        if(st) graph_forget_node(s,slots[i]);
        else { s->graph_nodes[slots[i]].parent=s->graphs[dst].id; s->graph_nodes[slots[i]].handle=handles[i]; }
      }
      if(st && s->graphs[dst].handle) { int cleanup=graph_destroy(s,dst); if(cleanup) return cleanup; }
    }
    if(st) { graph_unreserve(s,dst); return st; }
    out->id=s->graphs[dst].id; return 0;
  }
  switch(in->action) {
    case MLU_GRAPH_DESTROY: return g<0 ? CN_ERROR_INVALID_VALUE : graph_destroy(s,g);
    case MLU_GRAPH_DESTROY_NODE: {
      if(n<0) return CN_ERROR_INVALID_VALUE;
      int st=cnTaskTopoDestroyNode(s->graph_nodes[n].handle); if(!st) graph_forget_node(s,n); return st;
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
