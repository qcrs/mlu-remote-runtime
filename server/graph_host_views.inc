/* Independent native DAG revisions. Each entry is paired by FindInClone,
 * including embedded children; enumeration order never pairs unrelated graphs. */
typedef struct mlu_graph_view {
  struct mlu_graph_view *next;
  mlu_backend_session *session;
  unsigned refs;
  int borrowed;
  CNtaskTopo graph;
  size_t count;
  struct mlu_graph_view_entry {
    uint64_t source_id;
    CNtaskTopoNode node;
    CNtaskTopoNodeType type;
    mlu_graph_payload *payload;
    struct mlu_graph_view *child;
  } *entries;
} mlu_graph_view;
static int graph_owned_child(mlu_backend_session *,uint64_t);
static void graph_view_release(mlu_graph_view *v) { if(v) v->refs--; }
/* Destruction failure retains both native ownership and every backing lease. */
static int graph_views_reap(mlu_backend_session *s) {
  int first=0,changed;
  do {
    changed=0;mlu_graph_view **link=&s->graph_views;
    while(*link) {
      mlu_graph_view *v=*link;
      if(v->refs) {link=&v->next;continue;}
      int st=v->graph && !v->borrowed ? cnTaskTopoDestroy(v->graph) : 0;
      if(st) {if(!first) first=st;link=&v->next;continue;}
      for(size_t i=0;i<v->count;i++) {
        graph_payload_release(v->entries[i].payload);
        graph_view_release(v->entries[i].child);
      }
      s->graph_snapshot_nodes-=(unsigned)v->count;
      *link=v->next;free(v->entries);free(v);changed=1;
    }
  } while(changed);
  return first;
}
static int graph_view_new(mlu_backend_session *s,CNtaskTopo graph,size_t count,int borrowed,mlu_graph_view **out) {
  if(count>MLU_GRAPH_MAX_NODES-s->graph_snapshot_nodes) return MLU_REMOTE_RESOURCE_LIMIT;
  mlu_graph_view *v=calloc(1,sizeof(*v));if(!v) return CN_ERROR_SYSTEM_OUT_OF_MEMORY;
  v->entries=calloc(count ? count : 1,sizeof(*v->entries));
  if(!v->entries) {free(v);return CN_ERROR_SYSTEM_OUT_OF_MEMORY;}
  v->session=s;v->refs=1;v->count=count;v->borrowed=borrowed;
  v->next=s->graph_views;s->graph_views=v;s->graph_snapshot_nodes+=(unsigned)count;*out=v;
  if(borrowed) {v->graph=graph;return 0;}
  return graph ? cnTaskTopoClone(&v->graph,graph) : cnTaskTopoCreate(&v->graph,0);
}
static int graph_view_capture(mlu_backend_session *s,unsigned g,CNtaskTopo graph,int borrowed,mlu_graph_view **out) {
  size_t count=0;
  for(unsigned i=0;i<MLU_GRAPH_MAX_NODES;i++) count+=s->graph_nodes[i].live && s->graph_nodes[i].parent==s->graphs[g].id;
  int st=graph_view_new(s,borrowed ? graph : s->graphs[g].handle,count,borrowed,out);if(st) return st;
  mlu_graph_view *v=*out;
  size_t j=0;
  for(unsigned i=0;i<MLU_GRAPH_MAX_NODES;i++) if(s->graph_nodes[i].live && s->graph_nodes[i].parent==s->graphs[g].id) {
    struct mlu_graph_view_entry *entry=&v->entries[j++];
    entry->source_id=s->graph_nodes[i].id;entry->payload=s->graph_nodes[i].payload;graph_payload_retain(entry->payload);
    st=cnTaskTopoNodeGetType(s->graph_nodes[i].handle,&entry->type);
    if(!st) st=cnTaskTopoNodeFindInClone(&entry->node,s->graph_nodes[i].handle,v->graph);
    int child=graph_owned_child(s,s->graph_nodes[i].id);CNtaskTopo embedded=NULL;
    if(!st && child>=0) st=cnTaskTopoChildTopoNodeGetTopo(entry->node,&embedded);
    if(!st && child>=0) st=graph_view_capture(s,(unsigned)child,embedded,1,&entry->child);
    if(st) return st;
  }
  return 0;
}
static int graph_view_from_registry(mlu_backend_session *s,unsigned g,mlu_graph_view **out) {
  return graph_view_capture(s,g,NULL,0,out);
}
static int graph_view_duplicate(mlu_graph_view *src,CNtaskTopo graph,int borrowed,mlu_graph_view **out) {
  int st=graph_view_new(src->session,borrowed ? graph : src->graph,src->count,borrowed,out);if(st) return st;
  mlu_graph_view *v=*out;
  for(size_t i=0;i<v->count;i++) {
    struct mlu_graph_view_entry *entry=&v->entries[i],*old=&src->entries[i];
    entry->source_id=old->source_id;entry->type=old->type;
    entry->payload=old->payload;graph_payload_retain(entry->payload);
    st=cnTaskTopoNodeFindInClone(&entry->node,old->node,v->graph);
    CNtaskTopo embedded=NULL;
    if(!st && old->child) st=cnTaskTopoChildTopoNodeGetTopo(entry->node,&embedded);
    if(!st && old->child) st=graph_view_duplicate(old->child,embedded,1,&entry->child);
    if(st) return st;
  }
  return 0;
}
static int graph_view_copy(mlu_graph_view *src,mlu_graph_view **out) {return graph_view_duplicate(src,NULL,0,out);}
static int graph_view_has_host(mlu_graph_view *v) {
  if(v) for(size_t i=0;i<v->count;i++) if((v->entries[i].payload && (v->entries[i].payload->host_kind || v->entries[i].payload->callback_token)) || graph_view_has_host(v->entries[i].child)) return 1;
  return 0;
}
/* Accept only uniquely distinguished structural roles after EntityUpdate.
 * Hash refinement only merges roles; final type/edge checks validate the map.
 * Symmetric/ambiguous DAGs still replay, but host-affecting setters are refused. */
static uint64_t graph_role_hash(uint64_t x) {
  x^=x>>30;x*=UINT64_C(0xbf58476d1ce4e5b9);x^=x>>27;x*=UINT64_C(0x94d049bb133111eb);return x^(x>>31);
}
static int graph_view_edges(mlu_graph_view *v,unsigned char *edges) {
  size_t count=0;int st=cnTaskTopoGetEdges(v->graph,NULL,NULL,&count);if(st) return st;
  if(count>MLU_GRAPH_MAX_ARRAY/2) return MLU_REMOTE_RESOURCE_LIMIT;
  CNtaskTopoNode *pairs=calloc(count ? 2*count : 1,sizeof(*pairs));if(!pairs) return CN_ERROR_SYSTEM_OUT_OF_MEMORY;
  size_t actual=count;if(count) st=cnTaskTopoGetEdges(v->graph,pairs,pairs+count,&actual);
  if(!st && actual!=count) st=CN_ERROR_OPERATING_SYSTEM;
  for(size_t i=0;!st && i<count;i++) {
    size_t a=0,b=0;while(a<v->count && v->entries[a].node!=pairs[i]) a++;
    while(b<v->count && v->entries[b].node!=pairs[count+i]) b++;
    if(a==v->count || b==v->count) st=CN_ERROR_OPERATING_SYSTEM;
    else edges[a*v->count+b]=1;
  }
  free(pairs);return st;
}
static void graph_view_correspondence(mlu_graph_entity *e,mlu_graph_view *v,int *slots) {
  for(size_t i=0;i<e->count;i++) slots[i]=-1;
  mlu_graph_view *old=e->revision;size_t n=v->count;
  if(!old || n!=old->count || n>512) return; /* bounded optional setter routing */
  unsigned char *a=calloc(n ? n*n : 1,1),*b=calloc(n ? n*n : 1,1);
  uint64_t *colors=calloc(n ? 4*n : 1,sizeof(*colors));int *map=malloc((n ? n : 1)*sizeof(*map));
  if(!a || !b || !colors || !map || graph_view_edges(old,a) || graph_view_edges(v,b)) goto done;
  for(size_t i=0;i<n;i++) {colors[i]=old->entries[i].type+1;colors[n+i]=v->entries[i].type+1;}
  for(size_t round=0;round<n;round++) {
    for(unsigned side=0;side<2;side++) for(size_t i=0;i<n;i++) {
      unsigned char *edges=side ? b : a;uint64_t *c=colors+side*n;
      uint64_t in=0,out=0;
      for(size_t j=0;j<n;j++) {if(edges[j*n+i]) in+=graph_role_hash(c[j]);if(edges[i*n+j]) out+=graph_role_hash(c[j]);}
      colors[2*n+side*n+i]=graph_role_hash(c[i])^graph_role_hash(in+17)^graph_role_hash(out+37);
    }
    memcpy(colors,colors+2*n,2*n*sizeof(*colors));
  }
  for(size_t i=0;i<n;i++) {
    map[i]=-1;for(size_t j=0;j<n;j++) if(colors[i]==colors[n+j]) {if(map[i]>=0) goto done;map[i]=(int)j;}
    if(map[i]<0 || old->entries[i].type!=v->entries[map[i]].type) goto done;
    for(size_t j=0;j<i;j++) if(map[i]==map[j]) goto done;
  }
  for(size_t i=0;i<n;i++) for(size_t j=0;j<n;j++) if(a[i*n+j]!=b[map[i]*n+map[j]]) goto done;
  for(size_t i=0;i<e->count;i++) if(e->host_slots[i]>=0) slots[i]=map[e->host_slots[i]];
done:
  free(a);free(b);free(colors);free(map);
}
