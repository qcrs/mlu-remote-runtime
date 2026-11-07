/* Shared ABI adaptation for generated typed memset-node wrappers. */
#include "remote_graph.h"
#ifdef MLU_GRAPH_RUNTIME
#define GRAPH_OWNER void *owner
#define GRAPH_PARAMS cnrtMemsetParams_t
#define GRAPH_LOOKUP(p,o) lookup((const void *)(uintptr_t)(p),o)
#define GRAPH_ALLOCATION_OWNER(a) 1
#else
#define GRAPH_OWNER context *owner
#define GRAPH_PARAMS CN_MEMSET_NODE_PARAMS
#define GRAPH_LOOKUP(p,o) lookup(owner,(CNaddr)(uintptr_t)(p),o)
#define GRAPH_ALLOCATION_OWNER(a) ((a)->owner==owner)
#endif
static int graph_memset_encode(GRAPH_OWNER,const GRAPH_PARAMS *p,mlu_api_graph_memset_request *out) {
  uint64_t offset=0;
  allocation *a=GRAPH_LOOKUP(p->dst,&offset);
  if(!a) return -1;
  out->allocation=a->id; out->offset=offset; out->pitch=p->pitch;
  out->value=p->value; out->element_size=p->elementSize; out->width=p->width; out->height=p->height;
  return 0;
}
static void graph_memset_decode(GRAPH_OWNER,const mlu_api_graph_memset_response *p,GRAPH_PARAMS *out) {
  memset(out,0,sizeof(*out));
  /* A retained graph may still describe a retired allocation. Its client VA
   * tombstone remains reserved, so GetParams can preserve pointer identity. */
  for(allocation *a=allocations;a;a=a->next) if(GRAPH_ALLOCATION_OWNER(a) && a->id==p->allocation) {
#ifdef MLU_GRAPH_RUNTIME
    out->dst=(char *)a->base+p->offset;
#else
    out->dst=(CNaddr)(uintptr_t)a->base+p->offset;
#endif
    break;
  }
  out->pitch=p->pitch; out->value=p->value; out->elementSize=p->element_size;
  out->width=p->width; out->height=p->height;
}
#undef GRAPH_OWNER
#undef GRAPH_PARAMS
#undef GRAPH_LOOKUP
#undef GRAPH_ALLOCATION_OWNER
