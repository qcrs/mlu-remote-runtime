/* Native backend with selected failure returns injected by the linker's wrap.
 * Success paths execute real CNDrv. No successful native call is fabricated. */
#include "mlu_backend.h"
#include "remote_graph.h"
#include "graph_kernel_wire.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail_alloc, fail_image, fail_copy, fail_sync, fail_destroy, fail_queue_destroy;
static int fail_host_alloc, fail_host_free, fail_d2h, fail_notifier;
static int fail_graph_destroy, fail_graph_find, fail_graph_node_destroy;
static int fail_graph_copy_add, fail_graph_copy_set, fail_graph_copy_entity_set;
static int fail_graph_kernel_add, fail_graph_kernel_set, fail_graph_kernel_entity_set, fail_module_unload;
static int fail_graph_clone,fail_graph_instantiate,fail_graph_invoke,fail_graph_entity_destroy;
extern CNresult __real_cnTaskTopoClone(CNtaskTopo *,CNtaskTopo);
CNresult __wrap_cnTaskTopoClone(CNtaskTopo *out,CNtaskTopo graph) {
  if(fail_graph_clone) {--fail_graph_clone;return CN_ERROR_INVALID_VALUE;}
  return __real_cnTaskTopoClone(out,graph);
}
extern CNresult __real_cnTaskTopoInstantiate(CNtaskTopoEntity *,CNtaskTopo,CNtaskTopoNode *,char *,size_t);
CNresult __wrap_cnTaskTopoInstantiate(CNtaskTopoEntity *out,CNtaskTopo graph,CNtaskTopoNode *node,char *log,size_t bytes) {
  if(fail_graph_instantiate) {--fail_graph_instantiate;return CN_ERROR_INVALID_VALUE;}
  return __real_cnTaskTopoInstantiate(out,graph,node,log,bytes);
}
extern CNresult __real_cnTaskTopoEntityInvoke(CNtaskTopoEntity,CNqueue);
CNresult __wrap_cnTaskTopoEntityInvoke(CNtaskTopoEntity entity,CNqueue q) {
  if(fail_graph_invoke) {--fail_graph_invoke;return CN_ERROR_INVALID_VALUE;}
  return __real_cnTaskTopoEntityInvoke(entity,q);
}
extern CNresult __real_cnTaskTopoEntityDestroy(CNtaskTopoEntity);
CNresult __wrap_cnTaskTopoEntityDestroy(CNtaskTopoEntity entity) {
  if(fail_graph_entity_destroy) {--fail_graph_entity_destroy;return CN_ERROR_INVALID_VALUE;}
  return __real_cnTaskTopoEntityDestroy(entity);
}
extern CNresult __real_cnTaskTopoAddKernelNode(CNtaskTopoNode *,CNtaskTopo,const CNtaskTopoNode *,size_t,const CN_KERNEL_NODE_PARAMS *);
CNresult __wrap_cnTaskTopoAddKernelNode(CNtaskTopoNode *n,CNtaskTopo g,const CNtaskTopoNode *deps,size_t count,const CN_KERNEL_NODE_PARAMS *p) {
  if(fail_graph_kernel_add) { --fail_graph_kernel_add; return CN_ERROR_INVALID_VALUE; }
  return __real_cnTaskTopoAddKernelNode(n,g,deps,count,p);
}
extern CNresult __real_cnTaskTopoKernelNodeSetParams(CNtaskTopoNode,const CN_KERNEL_NODE_PARAMS *);
CNresult __wrap_cnTaskTopoKernelNodeSetParams(CNtaskTopoNode n,const CN_KERNEL_NODE_PARAMS *p) {
  if(fail_graph_kernel_set) { --fail_graph_kernel_set; return CN_ERROR_INVALID_VALUE; }
  return __real_cnTaskTopoKernelNodeSetParams(n,p);
}
extern CNresult __real_cnTaskTopoEntityKernelNodeSetParams(CNtaskTopoEntity,CNtaskTopoNode,const CN_KERNEL_NODE_PARAMS *);
CNresult __wrap_cnTaskTopoEntityKernelNodeSetParams(CNtaskTopoEntity e,CNtaskTopoNode n,const CN_KERNEL_NODE_PARAMS *p) {
  if(fail_graph_kernel_entity_set) { --fail_graph_kernel_entity_set; return CN_ERROR_INVALID_VALUE; }
  return __real_cnTaskTopoEntityKernelNodeSetParams(e,n,p);
}
extern CNresult __real_cnModuleUnload(CNmodule);
CNresult __wrap_cnModuleUnload(CNmodule m) {
  if(fail_module_unload) { --fail_module_unload; return CN_ERROR_INVALID_VALUE; }
  return __real_cnModuleUnload(m);
}
extern CNresult __real_cnTaskTopoAddMemcpyNode(CNtaskTopoNode *,CNtaskTopo,const CNtaskTopoNode *,size_t,const CNmemcpy3dParam *);
CNresult __wrap_cnTaskTopoAddMemcpyNode(CNtaskTopoNode *n,CNtaskTopo g,const CNtaskTopoNode *deps,size_t count,const CNmemcpy3dParam *p) {
  if(fail_graph_copy_add) { --fail_graph_copy_add; return CN_ERROR_INVALID_VALUE; }
  return __real_cnTaskTopoAddMemcpyNode(n,g,deps,count,p);
}
extern CNresult __real_cnTaskTopoMemcpyNodeSetParams(CNtaskTopoNode,const CNmemcpy3dParam *);
CNresult __wrap_cnTaskTopoMemcpyNodeSetParams(CNtaskTopoNode n,const CNmemcpy3dParam *p) {
  if(fail_graph_copy_set) { --fail_graph_copy_set; return CN_ERROR_INVALID_VALUE; }
  return __real_cnTaskTopoMemcpyNodeSetParams(n,p);
}
extern CNresult __real_cnTaskTopoEntityMemcpyNodeSetParams(CNtaskTopoEntity,CNtaskTopoNode,const CNmemcpy3dParam *);
CNresult __wrap_cnTaskTopoEntityMemcpyNodeSetParams(CNtaskTopoEntity e,CNtaskTopoNode n,const CNmemcpy3dParam *p) {
  if(fail_graph_copy_entity_set) { --fail_graph_copy_entity_set; return CN_ERROR_INVALID_VALUE; }
  return __real_cnTaskTopoEntityMemcpyNodeSetParams(e,n,p);
}
extern CNresult __real_cnTaskTopoDestroy(CNtaskTopo);
CNresult __wrap_cnTaskTopoDestroy(CNtaskTopo graph) {
  if(fail_graph_destroy) { --fail_graph_destroy; return CN_ERROR_INVALID_VALUE; }
  return __real_cnTaskTopoDestroy(graph);
}
extern CNresult __real_cnTaskTopoNodeFindInClone(CNtaskTopoNode *,CNtaskTopoNode,CNtaskTopo);
CNresult __wrap_cnTaskTopoNodeFindInClone(CNtaskTopoNode *out,CNtaskTopoNode node,CNtaskTopo graph) {
  if(fail_graph_find) { --fail_graph_find; return CN_ERROR_INVALID_VALUE; }
  return __real_cnTaskTopoNodeFindInClone(out,node,graph);
}
extern CNresult __real_cnTaskTopoDestroyNode(CNtaskTopoNode);
CNresult __wrap_cnTaskTopoDestroyNode(CNtaskTopoNode node) {
  if(fail_graph_node_destroy) { --fail_graph_node_destroy; return CN_ERROR_INVALID_VALUE; }
  return __real_cnTaskTopoDestroyNode(node);
}
extern CNresult __real_cnMallocHost(void **, cn_uint64_t);
CNresult __wrap_cnMallocHost(void **p, cn_uint64_t n) {
  if(fail_host_alloc) { fail_host_alloc=0; return CN_ERROR_SYSTEM_OUT_OF_MEMORY; }
  return __real_cnMallocHost(p,n);
}
extern CNresult __real_cnFreeHost(void *);
CNresult __wrap_cnFreeHost(void *p) {
  if(fail_host_free) { if(fail_host_free>0) --fail_host_free; return CN_ERROR_INVALID_VALUE; }
  return __real_cnFreeHost(p);
}
extern CNresult __real_cnMemcpyDtoHAsync(void *, CNaddr, cn_uint64_t, CNqueue);
CNresult __wrap_cnMemcpyDtoHAsync(void *p, CNaddr a, cn_uint64_t n, CNqueue q) {
  if(fail_d2h) { fail_d2h=0; return CN_ERROR_INVALID_VALUE; }
  return __real_cnMemcpyDtoHAsync(p,a,n,q);
}
extern CNresult __real_cnCreateNotifier(CNnotifier *, unsigned int);
CNresult __wrap_cnCreateNotifier(CNnotifier *p, unsigned int f) {
  if(fail_notifier) { fail_notifier=0; return CN_ERROR_INVALID_VALUE; }
  return __real_cnCreateNotifier(p,f);
}
extern CNresult __real_cnDestroyQueue(CNqueue);
CNresult __wrap_cnDestroyQueue(CNqueue q) {
  if (fail_queue_destroy) { fail_queue_destroy = 0; return CN_ERROR_INVALID_VALUE; }
  return __real_cnDestroyQueue(q);
}
extern CNresult __real_cnMalloc(CNaddr *, cn_uint64_t);
CNresult __wrap_cnMalloc(CNaddr *a, cn_uint64_t n) {
  if (fail_alloc) { fail_alloc = 0; return CN_MEMORY_ERROR_OUT_OF_MEMORY; }
  return __real_cnMalloc(a, n);
}
extern CNresult __real_cnModuleLoadFatBinary(const void *, CNmodule *);
CNresult __wrap_cnModuleLoadFatBinary(const void *p, CNmodule *m) {
  if (fail_image) { fail_image = 0; return CN_ERROR_INVALID_VALUE; }
  return __real_cnModuleLoadFatBinary(p, m);
}
extern CNresult __real_cnMemcpyHtoDAsync(CNaddr, const void *, cn_uint64_t, CNqueue);
CNresult __wrap_cnMemcpyHtoDAsync(CNaddr a, const void *p, cn_uint64_t n, CNqueue q) {
  if (fail_copy) { fail_copy = 0; return CN_ERROR_INVALID_VALUE; }
  return __real_cnMemcpyHtoDAsync(a, p, n, q);
}
extern CNresult __real_cnQueueSync(CNqueue);
CNresult __wrap_cnQueueSync(CNqueue q) {
  if (fail_sync) return CN_ERROR_INVALID_VALUE;
  return __real_cnQueueSync(q);
}
extern CNresult __real_cnCtxDestroy(CNcontext);
CNresult __wrap_cnCtxDestroy(CNcontext c) {
  if (fail_destroy) return CN_ERROR_INVALID_VALUE;
  return __real_cnCtxDestroy(c);
}

static void graph_failures(void) {
  mlu_backend_session *s=calloc(1,sizeof(*s)); assert(s);
  assert(!mlu_backend_session_init(s,0)); uint64_t allocation=0;
  assert(!mlu_backend_alloc(s,64,&allocation));
  mlu_api_graph_action_request action={.action=MLU_GRAPH_CREATE};
  mlu_api_graph_action_response graph={0},parent={0},unused={0};
  assert(!mlu_backend_api_graph_action(s,&action,&graph));
  assert(!mlu_backend_api_graph_action(s,&action,&parent));
  mlu_api_graph_memset_request fill={.action=MLU_GRAPH_PARAM_ADD,.graph=graph.id,
    .allocation=allocation,.element_size=1,.width=64,.height=1,.value=7};
  mlu_api_graph_memset_response filled={0};
  assert(!mlu_backend_api_graph_memset(s,&fill,&filled));
  mlu_api_graph_memcpy_request copy={.action=MLU_GRAPH_PARAM_ADD,.graph=graph.id,
    .dst_allocation=allocation,.dst_offset=32,.src_allocation=allocation,.width=32};
  mlu_api_graph_memcpy_response copied={0};
  assert(!mlu_backend_api_graph_memcpy(s,&copy,&copied));
  /* A real clone/add succeeds before its identity adoption is made to fail.
   * Failed native rollback must retain the hidden native objects and leases. */
  action.action=MLU_GRAPH_CLONE; action.graph=graph.id;
  fail_graph_find=1; fail_graph_destroy=1;
  assert(mlu_backend_api_graph_action(s,&action,&unused)==CN_ERROR_INVALID_VALUE);
  mlu_api_graph_child_request child={.action=MLU_GRAPH_CHILD_ADD,.graph=parent.id,.child=graph.id};
  mlu_api_graph_child_response added={0};
  fail_graph_find=1; fail_graph_node_destroy=1;
  assert(mlu_backend_api_graph_child(s,&child,&added)==CN_ERROR_INVALID_VALUE);
  assert(!mlu_backend_free(s,allocation));
  assert(s->usage.bytes[MLU_RESOURCE_DEVICE]==64);
  fail_graph_destroy=1;
  assert(mlu_backend_session_cleanup(s)==CN_ERROR_INVALID_VALUE);
  assert(s->context && s->usage.bytes[MLU_RESOURCE_DEVICE]==64);
  assert(!mlu_backend_session_cleanup(s));
  assert(!s->context && !s->usage.bytes[MLU_RESOURCE_DEVICE]);
  free(s);
  puts("PASS failed graph/child adoption rollback retains ownership; cleanup quarantine retries and releases allocation leases");
}

static mlu_backend_allocation *allocation_record(mlu_backend_session *s,uint64_t id) {
  for(size_t i=0;i<MLU_BACKEND_MAX_OBJECTS;i++) if(s->allocations[i].live && s->allocations[i].id==id) return &s->allocations[i];
  assert(0); return NULL;
}
static void graph_copy_failures(void) {
  mlu_backend_session *s=calloc(1,sizeof(*s)); assert(s);
  assert(!mlu_backend_session_init(s,0));
  uint64_t ids[6]; mlu_backend_allocation *a[6];
  for(unsigned i=0;i<4;i++) { assert(!mlu_backend_alloc(s,128,&ids[i])); a[i]=allocation_record(s,ids[i]); }
  mlu_api_graph_action_request action={.action=MLU_GRAPH_CREATE};
  mlu_api_graph_action_response graph={0},unused={0};
  assert(!mlu_backend_api_graph_action(s,&action,&graph));
  mlu_api_graph_memcpy_request copy={.action=MLU_GRAPH_PARAM_ADD,.graph=graph.id,
    .dst_allocation=ids[0],.src_allocation=ids[1],.width=32};
  mlu_api_graph_memcpy_response out={0};
  fail_graph_copy_add=1;
  assert(mlu_backend_api_graph_memcpy(s,&copy,&out)==CN_ERROR_INVALID_VALUE);
  assert(!a[0]->graph_refs && !a[1]->graph_refs);
  assert(!mlu_backend_api_graph_memcpy(s,&copy,&out)); uint64_t node=out.id;
  assert(a[0]->graph_refs==1 && a[1]->graph_refs==1);
  copy.action=MLU_GRAPH_PARAM_SET; copy.node=node; copy.dst_allocation=ids[2]; copy.src_allocation=ids[3];
  fail_graph_copy_set=1;
  assert(mlu_backend_api_graph_memcpy(s,&copy,&out)==CN_ERROR_INVALID_VALUE);
  assert(a[0]->graph_refs==1 && a[1]->graph_refs==1 && !a[2]->graph_refs && !a[3]->graph_refs);
  assert(!mlu_backend_free(s,ids[0])); assert(!mlu_backend_free(s,ids[1]));
  assert(s->usage.bytes[MLU_RESOURCE_DEVICE]==512);
  assert(!mlu_backend_api_graph_memcpy(s,&copy,&out));
  assert(!a[0]->live && !a[1]->live && a[2]->graph_refs==1 && a[3]->graph_refs==1);
  assert(s->usage.bytes[MLU_RESOURCE_DEVICE]==256);
  mlu_api_graph_entity_request entity={.action=MLU_GRAPH_INSTANTIATE,.graph=graph.id};
  mlu_api_graph_entity_response executable={0};
  assert(!mlu_backend_api_graph_entity(s,&entity,&executable));
  for(unsigned i=4;i<6;i++) { assert(!mlu_backend_alloc(s,128,&ids[i])); a[i]=allocation_record(s,ids[i]); }
  copy.action=MLU_GRAPH_PARAM_ENTITY_SET; copy.entity=executable.id;
  copy.dst_allocation=ids[4]; copy.src_allocation=ids[5];
  fail_graph_copy_entity_set=1;
  assert(mlu_backend_api_graph_memcpy(s,&copy,&out)==CN_ERROR_INVALID_VALUE);
  assert(!a[4]->graph_refs && !a[5]->graph_refs && a[2]->graph_refs==1 && a[3]->graph_refs==1);
  assert(!mlu_backend_api_graph_memcpy(s,&copy,&out));
  assert(a[4]->graph_refs==1 && a[5]->graph_refs==1);
  for(unsigned i=2;i<6;i++) assert(!mlu_backend_free(s,ids[i]));
  action.action=MLU_GRAPH_DESTROY; action.graph=graph.id;
  assert(!mlu_backend_api_graph_action(s,&action,&unused));
  assert(s->usage.bytes[MLU_RESOURCE_DEVICE]==512);
  entity.action=MLU_GRAPH_ENTITY_DESTROY; entity.entity=executable.id;
  assert(!mlu_backend_api_graph_entity(s,&entity,&executable));
  assert(!s->usage.bytes[MLU_RESOURCE_DEVICE]);
  /* One allocation can occupy both endpoints; success and native failure
   * each acquire/release exactly two references, including after retirement. */
  assert(!mlu_backend_alloc(s,128,&ids[0])); a[0]=allocation_record(s,ids[0]);
  action.action=MLU_GRAPH_CREATE; assert(!mlu_backend_api_graph_action(s,&action,&graph));
  copy=(mlu_api_graph_memcpy_request){.action=MLU_GRAPH_PARAM_ADD,.graph=graph.id,
    .dst_allocation=ids[0],.dst_offset=64,.src_allocation=ids[0],.width=32};
  assert(!mlu_backend_api_graph_memcpy(s,&copy,&out)); assert(a[0]->graph_refs==2);
  fail_graph_copy_add=1;
  assert(mlu_backend_api_graph_memcpy(s,&copy,&out)==CN_ERROR_INVALID_VALUE);
  assert(a[0]->graph_refs==2);
  assert(!mlu_backend_free(s,ids[0])); assert(s->usage.bytes[MLU_RESOURCE_DEVICE]==128);
  action.action=MLU_GRAPH_DESTROY; action.graph=graph.id;
  assert(!mlu_backend_api_graph_action(s,&action,&unused));
  assert(!s->usage.bytes[MLU_RESOURCE_DEVICE]);
  assert(!mlu_backend_session_cleanup(s)); free(s);
  puts("PASS memcpy native Add/Set/EntitySet failure rolls back both endpoint leases; replacement reaps retired endpoints; same-allocation refs balance");
}

static void graph_host_failures(void) {
  mlu_backend_session *s=calloc(1,sizeof(*s));assert(s);assert(!mlu_backend_session_init(s,0));
  uint64_t allocation;assert(!mlu_backend_alloc(s,64,&allocation));
  mlu_api_graph_action_request action={.action=MLU_GRAPH_CREATE};mlu_api_graph_action_response graph={0},unused={0};
  assert(!mlu_backend_api_graph_action(s,&action,&graph));
  mlu_api_graph_memcpy_request copy={.action=MLU_GRAPH_PARAM_ADD,.graph=graph.id,
    .dst_allocation=allocation,.src_offset=0x10000,.width=64,.direction=2};
  mlu_api_graph_memcpy_response copied={0};fail_graph_copy_add=1;
  assert(mlu_backend_api_graph_memcpy(s,&copy,&copied)==CN_ERROR_INVALID_VALUE);assert(!s->usage.bytes[MLU_RESOURCE_STAGING]);
  assert(!mlu_backend_api_graph_memcpy(s,&copy,&copied));
  mlu_api_graph_entity_request entity={.action=MLU_GRAPH_INSTANTIATE,.graph=graph.id};mlu_api_graph_entity_response executable={0};
  assert(!mlu_backend_api_graph_entity(s,&entity,&executable));
  unsigned snapshot=s->graph_snapshot_nodes;assert(snapshot && s->usage.bytes[MLU_RESOURCE_STAGING]==64);
  unsigned char descriptor[24]={0},data[88]={0};mlu_api_graph_host_request in={.action=1,.entity=executable.id,.capacity=24};
  mlu_api_graph_host_response out={.data=descriptor};assert(!mlu_backend_api_graph_host(s,&in,&out));
  memcpy(data,descriptor,24);memset(data+24,19,64);in.action=2;in.capacity=0;in.input_bytes=sizeof(data);in.data=data;
  int *faults[]={&fail_graph_clone,&fail_host_alloc,&fail_graph_copy_set,&fail_graph_instantiate,&fail_graph_invoke,&fail_notifier};
  for(size_t i=0;i<sizeof(faults)/sizeof(*faults);i++) {
    *faults[i]=1;assert(mlu_backend_api_graph_host(s,&in,&out));
    assert(!*faults[i] && s->graph_snapshot_nodes==snapshot && s->usage.bytes[MLU_RESOURCE_STAGING]==64 && !s->graph_orphans);
  }
  /* A failed completion-record creation plus failed wait cannot free an
   * invocation that may already be accessing its pinned source. */
  fail_notifier=1;fail_sync=1;assert(mlu_backend_api_graph_host(s,&in,&out));
  assert(s->graph_orphans && s->usage.bytes[MLU_RESOURCE_STAGING]==128);
  fail_sync=0;assert(!mlu_backend_context_sync(s));assert(!s->graph_orphans && s->usage.bytes[MLU_RESOURCE_STAGING]==64);
  for(unsigned failure=0;failure<3;failure++) {
    assert(!mlu_backend_api_graph_host(s,&in,&out));uint64_t transfer=out.id;
    assert(!mlu_backend_transfer_sync(s,transfer));
    if(failure==0) fail_graph_entity_destroy=1;
    else if(failure==1) fail_host_free=1;
    else fail_graph_destroy=1;
    assert(!mlu_backend_transfer_release(s,transfer));
    if(failure<2) assert(s->usage.bytes[MLU_RESOURCE_STAGING]==128);
    else assert(s->graph_snapshot_nodes>snapshot);
    assert(!mlu_backend_context_sync(s));
    assert(s->graph_snapshot_nodes==snapshot && s->usage.bytes[MLU_RESOURCE_STAGING]==64);
  }
  action.action=MLU_GRAPH_DESTROY;action.graph=graph.id;assert(!mlu_backend_api_graph_action(s,&action,&unused));
  entity.action=MLU_GRAPH_ENTITY_DESTROY;entity.entity=executable.id;assert(!mlu_backend_api_graph_entity(s,&entity,&executable));
  assert(!s->graph_views && !s->graph_snapshot_nodes && !s->usage.bytes[MLU_RESOURCE_STAGING]);
  assert(!mlu_backend_session_cleanup(s));free(s);
  puts("PASS Host replay clone/allocate/set/instantiate/invoke/notifier rollback; failed wait retains orphan; native entity/Host/view release retries preserve ownership and quota");
}

static void graph_kernel_failures(const void *image,size_t bytes) {
  mlu_backend_session *s=calloc(1,sizeof(*s)); assert(s);
  assert(!mlu_backend_session_init(s,0)); uint64_t mid=0,kid=0,ids[3];
  assert(!mlu_backend_module_load_image(s,image,bytes,"_Z11module_readPii",&mid,&kid));
  mlu_backend_module *m=NULL;
  for(unsigned i=0;i<MLU_BACKEND_MAX_OBJECTS;i++) if(s->modules[i].module_id==mid) m=&s->modules[i];
  assert(m); mlu_backend_allocation *a[3];
  for(unsigned i=0;i<3;i++) {assert(!mlu_backend_alloc(s,64,&ids[i]));a[i]=allocation_record(s,ids[i]);}
  unsigned char wire[76]={0};gk_put32(wire,1);gk_put32(wire+4,8);gk_put64(wire+8,ids[0]);
  gk_put32(wire+40,2);gk_put32(wire+44,4);
  mlu_api_graph_action_request action={.action=MLU_GRAPH_CREATE};
  mlu_api_graph_action_response graph={0},clone={0},unused={0};
  assert(!mlu_backend_api_graph_action(s,&action,&graph));
  mlu_api_graph_kernel_request in={.action=MLU_GRAPH_PARAM_ADD,.graph=graph.id,.module=mid,.kernel=kid,
    .x=1,.y=1,.z=1,.klass=CN_KERNEL_CLASS_BLOCK,.arg_count=2,.input_bytes=sizeof(wire),.data=wire};
  mlu_api_graph_kernel_response out={0};
  fail_graph_kernel_add=1;
  assert(mlu_backend_api_graph_kernel(s,&in,&out)==CN_ERROR_INVALID_VALUE);
  assert(!m->graph_refs && !a[0]->graph_refs && !s->usage.bytes[MLU_RESOURCE_STAGING]);
  assert(!mlu_backend_api_graph_kernel(s,&in,&out));uint64_t node=out.id;
  uint64_t charge=s->usage.bytes[MLU_RESOURCE_STAGING];assert(charge && m->graph_refs==1 && a[0]->graph_refs==1);
  in.action=MLU_GRAPH_PARAM_SET;in.node=node;gk_put64(wire+8,ids[1]);fail_graph_kernel_set=1;
  assert(mlu_backend_api_graph_kernel(s,&in,&out)==CN_ERROR_INVALID_VALUE);
  assert(m->graph_refs==1 && a[0]->graph_refs==1 && !a[1]->graph_refs && s->usage.bytes[MLU_RESOURCE_STAGING]==charge);
  assert(!mlu_backend_free(s,ids[0])); assert(!mlu_backend_api_graph_kernel(s,&in,&out));
  assert(!a[0]->live && a[1]->graph_refs==1 && m->graph_refs==1 && s->usage.bytes[MLU_RESOURCE_STAGING]==charge);
  action.action=MLU_GRAPH_CLONE;action.graph=graph.id;
  assert(!mlu_backend_api_graph_action(s,&action,&clone));
  mlu_api_graph_entity_request entity={.action=MLU_GRAPH_INSTANTIATE,.graph=graph.id};
  mlu_api_graph_entity_response executable={0};assert(!mlu_backend_api_graph_entity(s,&entity,&executable));
  assert(m->graph_refs==1 && a[1]->graph_refs==1); /* Shared immutable record. */
  in.action=MLU_GRAPH_PARAM_ENTITY_SET;in.entity=executable.id;gk_put64(wire+8,ids[2]);fail_graph_kernel_entity_set=1;
  assert(mlu_backend_api_graph_kernel(s,&in,&out)==CN_ERROR_INVALID_VALUE);
  assert(m->graph_refs==1 && !a[2]->graph_refs && s->usage.bytes[MLU_RESOURCE_STAGING]==charge);
  assert(!mlu_backend_api_graph_kernel(s,&in,&out));
  assert(m->graph_refs==2 && a[2]->graph_refs==1 && s->usage.bytes[MLU_RESOURCE_STAGING]==2*charge);
  assert(!mlu_backend_free(s,ids[1]));assert(!mlu_backend_free(s,ids[2]));
  assert(!mlu_backend_module_unload(s,mid));assert(m->retired && m->live && m->graph_refs==2);
  assert(mlu_backend_module_unload(s,mid)==CN_ERROR_INVALID_HANDLE);
  action.action=MLU_GRAPH_DESTROY;action.graph=graph.id;assert(!mlu_backend_api_graph_action(s,&action,&unused));
  action.graph=clone.id;assert(!mlu_backend_api_graph_action(s,&action,&unused));
  assert(s->usage.bytes[MLU_RESOURCE_STAGING]==2*charge && s->usage.bytes[MLU_RESOURCE_IMAGE]==bytes);
  fail_module_unload=1;entity.action=MLU_GRAPH_ENTITY_DESTROY;entity.entity=executable.id;
  assert(!mlu_backend_api_graph_entity(s,&entity,&executable));
  assert(!m->graph_refs && m->live && !s->usage.bytes[MLU_RESOURCE_STAGING] && !s->usage.bytes[MLU_RESOURCE_DEVICE]);
  assert(s->usage.bytes[MLU_RESOURCE_IMAGE]==bytes); /* Failed deferred unload keeps backing charged. */
  assert(!mlu_backend_context_sync(s));assert(!m->live && !s->usage.bytes[MLU_RESOURCE_IMAGE]);
  assert(!mlu_backend_session_cleanup(s));free(s);
  puts("PASS kernel Add/Set/EntitySet rollback balances staging/module/allocation leases; clone/entity share immutable records; failed deferred unload retains image until retry");
}

int main(int argc, char **argv) {
  assert(argc == 2 || argc == 3);
  mlu_backend_session *s = calloc(1, sizeof(*s)); assert(s);
  assert(!mlu_backend_global_init()); graph_failures(); graph_copy_failures(); graph_host_failures(); assert(!mlu_backend_session_init(s, 0));
  uint64_t a = 0, q = 0, tid = 0, mid = 0, kid = 0;
  fail_alloc = 1;
  assert(mlu_backend_alloc(s, 64, &a) == CN_MEMORY_ERROR_OUT_OF_MEMORY);
  assert(!a && !s->usage.bytes[MLU_RESOURCE_DEVICE]);
  assert(!mlu_backend_alloc(s, 64, &a));
  assert(s->usage.bytes[MLU_RESOURCE_DEVICE] == 64);
  puts("PASS injected native allocation failure rolls back reservation");
  assert(!mlu_backend_queue_create(s, &q));
  unsigned char data[16] = {0};
  fail_copy = 1;
  assert(mlu_backend_memcpy(s, MLU_MEMCPY_H2D, a, 0, 0, 0, data, NULL, 16, q, &tid) == CN_ERROR_INVALID_VALUE);
  assert(!tid && !s->usage.bytes[MLU_RESOURCE_STAGING]);
  assert(!mlu_backend_memcpy(s, MLU_MEMCPY_H2D, a, 0, 0, 0, data, NULL, 16, q, &tid));
  assert(s->usage.bytes[MLU_RESOURCE_STAGING] == 16);
  assert(!mlu_backend_queue_sync(s, q));
  assert(!s->usage.bytes[MLU_RESOURCE_STAGING]);
  assert(!mlu_backend_transfer_release(s, tid));
  puts("PASS injected async-copy failure rolls back; real completion releases staging");

  FILE *f = fopen(argv[1], "rb"); assert(f);
  assert(!fseek(f, 0, SEEK_END)); long size = ftell(f); assert(size > 0); rewind(f);
  void *image = malloc((size_t)size); assert(image);
  assert(fread(image, 1, (size_t)size, f) == (size_t)size); fclose(f);
  fail_image = 1;
  assert(mlu_backend_module_load_image(s, image, size, "_Z12module_writei", &mid, &kid) == CN_ERROR_INVALID_VALUE);
  assert(!mid && !kid && !s->usage.bytes[MLU_RESOURCE_IMAGE]);
  assert(!mlu_backend_module_load_image(s, image, size, "_Z12module_writei", &mid, &kid));
  assert(s->usage.bytes[MLU_RESOURCE_IMAGE] == (uint64_t)size);
  assert(!mlu_backend_module_unload(s, mid));
  assert(!s->usage.bytes[MLU_RESOURCE_IMAGE]); graph_kernel_failures(image,(size_t)size); free(image);
  assert(!cnCtxSetCurrent(s->context));
  puts("PASS injected native module-load failure rolls back image reservation");

  assert(!mlu_backend_memcpy(s, MLU_MEMCPY_H2D, a, 0, 0, 0, data, NULL, 16, q, &tid));
  fail_sync = 1;
  assert(!mlu_backend_queue_destroy(s, q));
  assert(s->usage.bytes[MLU_RESOURCE_STAGING] == 16);
  assert(mlu_backend_queue_sync(s, q) == CN_ERROR_INVALID_HANDLE);
  fail_sync = 0; fail_queue_destroy = 1;
  assert(mlu_backend_context_sync(s) == CN_ERROR_INVALID_VALUE);
  assert(!s->usage.bytes[MLU_RESOURCE_STAGING]);
  unsigned retained = 0;
  for (size_t i = 0; i < MLU_BACKEND_MAX_OBJECTS; ++i) retained += !!s->queues[i].live;
  assert(retained == 1);
  assert(!mlu_backend_transfer_sync(s, tid));
  assert(!mlu_backend_transfer_release(s, tid));
  for (size_t i = 0; i < MLU_BACKEND_MAX_OBJECTS; ++i) assert(!s->queues[i].live);
  assert(!mlu_backend_queue_create(s, &q));
  puts("PASS queue retirement retains staging; failed deferred destroy is observable and retry retains native ownership");

  mlu_api_d2h_submit_request submit={.allocation=a,.offset=0,.bytes=16,.queue=q};
  mlu_api_d2h_submit_response transfer={0};
  assert(!mlu_resource_reserve(&s->usage,MLU_RESOURCE_STAGING,64*1024*1024-8));
  assert(mlu_backend_api_d2h_submit(s,&submit,&transfer)==(int)MLU_REMOTE_RESOURCE_LIMIT);
  mlu_resource_release(&s->usage,MLU_RESOURCE_STAGING,64*1024*1024-8);
  fail_host_alloc=1;
  assert(mlu_backend_api_d2h_submit(s,&submit,&transfer)==CN_ERROR_SYSTEM_OUT_OF_MEMORY);
  assert(!s->usage.bytes[MLU_RESOURCE_STAGING]);
  fail_d2h=1;
  assert(mlu_backend_api_d2h_submit(s,&submit,&transfer)==CN_ERROR_INVALID_VALUE);
  assert(!s->usage.bytes[MLU_RESOURCE_STAGING]);
  fail_notifier=1; fail_sync=1;
  assert(mlu_backend_api_d2h_submit(s,&submit,&transfer)==CN_ERROR_INVALID_VALUE);
  assert(s->usage.bytes[MLU_RESOURCE_STAGING]==16);
  fail_sync=0; assert(!mlu_backend_context_sync(s));
  assert(!s->usage.bytes[MLU_RESOURCE_STAGING]);
  assert(!mlu_backend_api_d2h_submit(s,&submit,&transfer));
  assert(!mlu_backend_transfer_sync(s,transfer.transfer));
  unsigned char fetched[16];
  mlu_api_transfer_fetch_request fetch={.transfer=transfer.transfer,.bytes=16};
  mlu_api_transfer_fetch_response fetched_out={.data=fetched};
  assert(!mlu_backend_api_transfer_fetch(s,&fetch,&fetched_out)); assert(!memcmp(fetched,data,16));
  fail_host_free=1; assert(!mlu_backend_transfer_release(s,transfer.transfer));
  assert(s->usage.bytes[MLU_RESOURCE_STAGING]==16);
  assert(!mlu_backend_context_sync(s)); assert(!s->usage.bytes[MLU_RESOURCE_STAGING]);
  puts("PASS pinned D2H allocation/submission/notifier failures, retained staging, fetch and deferred Host free retry");
  assert(!mlu_backend_api_d2h_submit(s,&submit,&transfer));
  assert(!mlu_backend_memcpy(s, MLU_MEMCPY_H2D, a, 0, 0, 0, data, NULL, 16, q, &tid));
  fail_sync = 1; fail_destroy = argc == 3;
  unsigned char output[16], untouched[16];
  memset(output, 0xa5, sizeof(output)); memcpy(untouched, output, sizeof(output));
  assert(mlu_backend_memcpy(s, MLU_MEMCPY_D2H, 0, 0, a, 0, NULL, output, 16, q, &tid) == CN_ERROR_INVALID_VALUE);
  assert(!memcmp(output, untouched, sizeof(output)));
  assert(s->usage.bytes[MLU_RESOURCE_STAGING] == 32);
  puts("PASS failed D2H queue wait preserves host output and pending staging");
  assert(mlu_backend_session_cleanup(s) == CN_ERROR_INVALID_VALUE);
  assert(s->cleanup_failed == CN_ERROR_INVALID_VALUE);
  if (fail_destroy) {
    assert(s->context && s->usage.bytes[MLU_RESOURCE_STAGING] == 32);
    puts("PASS failed synchronization and context destruction quarantine staging and its quota");
    /* Process exit is the containment boundary for this deliberately failed
     * context. Do not double-destroy successfully released children. */
  } else {
    assert(!s->context);
    for (unsigned i = 0; i < MLU_RESOURCE_KINDS; ++i) assert(!s->usage.bytes[i]);
    puts("PASS cleanup error observable; successful native context destruction returns retained host quota");
  }
  mlu_resource_report(); free(s);
  return 0;
}
