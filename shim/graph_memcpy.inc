/* Shared ABI adaptation for generated typed memcpy-node wrappers. */
#include "remote_graph.h"
#ifdef MLU_GRAPH_RUNTIME
#define GRAPH_OWNER void *owner
#define GRAPH_PARAMS cnrtMemcpy3dParam_t
#define GRAPH_LOOKUP(p,o) lookup((const void *)(uintptr_t)(p),o)
#define GRAPH_ALLOCATION_OWNER(a) 1
#define GRAPH_DIR(p) ((uint32_t)(p)->dir)
#define GRAPH_SET_DIR(p,d) ((p)->dir=(cnrtMemTransDir_t)(d))
#define GRAPH_ADDRESS(a,o) ((void *)((char *)(a)->base+(o)))
#else
#define GRAPH_OWNER context *owner
#define GRAPH_PARAMS CNmemcpy3dParam
#define GRAPH_LOOKUP(p,o) lookup(owner,(CNaddr)(uintptr_t)(p),o)
#define GRAPH_ALLOCATION_OWNER(a) ((a)->owner==owner)
#define GRAPH_DIR(p) ((uint32_t)(p)->dir)
#define GRAPH_SET_DIR(p,d) ((p)->dir=(uint32_t)(d))
#define GRAPH_ADDRESS(a,o) ((void *)(uintptr_t)((CNaddr)(uintptr_t)(a)->base+(o)))
#endif
static int graph_memcpy_encode(GRAPH_OWNER,const GRAPH_PARAMS *p,mlu_api_graph_memcpy_request *out) {
  uint64_t dst_offset=0,src_offset=0;
  allocation *dst=GRAPH_LOOKUP(p->dst,&dst_offset);
  allocation *src=GRAPH_LOOKUP(p->src,&src_offset);
  if(!dst || !src || !GRAPH_ALLOCATION_OWNER(dst) || !GRAPH_ALLOCATION_OWNER(src) ||
     !p->extent.width || p->extent.width>MLU_GRAPH_MAX_COPY_BYTES ||
     p->extent.height!=1 || p->extent.depth!=1 ||
     p->dstPtr.pitch!=p->extent.width || p->srcPtr.pitch!=p->extent.width ||
     p->dstPtr.ysize!=1 || p->srcPtr.ysize!=1) return -1;
  out->dst_allocation=dst->id; out->dst_offset=dst_offset;
  out->src_allocation=src->id; out->src_offset=src_offset;
  out->width=p->extent.width; out->direction=GRAPH_DIR(p);
  return 0;
}
static void graph_memcpy_decode(GRAPH_OWNER,const mlu_api_graph_memcpy_response *p,GRAPH_PARAMS *out) {
  memset(out,0,sizeof(*out));
  allocation *dst=NULL,*src=NULL;
  /* Graph payloads may keep retired allocation tombstones alive for getters. */
  for(allocation *a=allocations;a;a=a->next) if(GRAPH_ALLOCATION_OWNER(a)) {
    if(a->id==p->dst_allocation) dst=a;
    if(a->id==p->src_allocation) src=a;
  }
  if(dst) out->dst=GRAPH_ADDRESS(dst,p->dst_offset);
  if(src) out->src=GRAPH_ADDRESS(src,p->src_offset);
  out->dstPtr.ptr=out->dst; out->dstPtr.pitch=p->width; out->dstPtr.xsize=p->width; out->dstPtr.ysize=1;
  out->srcPtr.ptr=out->src; out->srcPtr.pitch=p->width; out->srcPtr.xsize=p->width; out->srcPtr.ysize=1;
  out->extent.depth=1; out->extent.height=1; out->extent.width=p->width;
  GRAPH_SET_DIR(out,p->direction);
}
#undef GRAPH_OWNER
#undef GRAPH_PARAMS
#undef GRAPH_LOOKUP
#undef GRAPH_ALLOCATION_OWNER
#undef GRAPH_DIR
#undef GRAPH_SET_DIR
#undef GRAPH_ADDRESS
