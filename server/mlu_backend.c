#include "mlu_backend.h"
#include "remote_protocol.h"
#include "operator_registry.h"
#include <cnrt.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <limits.h>
#include <unistd.h>

static int backend_ready;
static uint64_t next_session_owner;
static uint64_t read_be64(const unsigned char *p) {
  uint64_t value=0; for(unsigned i=0;i<8;++i) value=(value<<8)|p[i]; return value;
}

typedef struct {
  uint32_t id;
  const char *name;
  const char *symbol;
  uint32_t inputs;
  uint32_t outputs;
  uint32_t rank;
  uint32_t scalar_bytes;
} operator_descriptor;

#define MLU_OPERATOR_DESCRIPTOR(id, name, symbol, inputs, outputs, rank, scalar_bytes) \
  {id, name, symbol, inputs, outputs, rank, scalar_bytes},
static const operator_descriptor operator_registry[] = {
  MLU_REMOTE_OPERATOR_REGISTRY(MLU_OPERATOR_DESCRIPTOR)
};
#undef MLU_OPERATOR_DESCRIPTOR

static const operator_descriptor *find_operator_descriptor(uint32_t id) {
  for (size_t i = 0; i < sizeof(operator_registry) / sizeof(operator_registry[0]); ++i)
    if (operator_registry[i].id == id) return &operator_registry[i];
  return NULL;
}
static const operator_descriptor *find_operator_by_name(const char *name) {
  for (size_t i = 0; i < sizeof(operator_registry) / sizeof(operator_registry[0]); ++i)
    if (!strcmp(operator_registry[i].name, name)) return &operator_registry[i];
  return NULL;
}

static int verify_operator_manifest(const char *dir, const operator_descriptor *desc) {
  char path[512];
  int n = snprintf(path, sizeof(path), "%s/%s.manifest", dir, desc->name);
  if (n <= 0 || (size_t)n >= sizeof(path)) return (int)MLU_REMOTE_UNSUPPORTED;
  FILE *file = fopen(path, "r");
  if (!file) return (int)MLU_REMOTE_UNSUPPORTED;
  char name[96] = {0}, arch[96] = {0}, symbol[192] = {0}, datatype[32] = {0}, abi[96] = {0};
  unsigned inputs = 0, outputs = 0, rank = 0, scalar_bytes = 0;
  char line[256];
  while (fgets(line, sizeof(line), file)) {
    char *newline = strchr(line, '\n'); if (newline) *newline = 0;
    char *equals = strchr(line, '='); if (!equals) continue;
    *equals = 0; const char *key = line; const char *value = equals + 1;
    if (!strcmp(key, "name")) snprintf(name, sizeof(name), "%s", value);
    else if (!strcmp(key, "arch")) snprintf(arch, sizeof(arch), "%s", value);
    else if (!strcmp(key, "symbol")) snprintf(symbol, sizeof(symbol), "%s", value);
    else if (!strcmp(key, "inputs")) inputs = (unsigned)strtoul(value, NULL, 10);
    else if (!strcmp(key, "outputs")) outputs = (unsigned)strtoul(value, NULL, 10);
    else if (!strcmp(key, "rank")) rank = (unsigned)strtoul(value, NULL, 10);
    else if (!strcmp(key, "datatype")) snprintf(datatype, sizeof(datatype), "%s", value);
    else if (!strcmp(key, "scalar_bytes")) scalar_bytes = (unsigned)strtoul(value, NULL, 10);
    else if (!strcmp(key, "abi")) snprintf(abi, sizeof(abi), "%s", value);
  }
  fclose(file);
  const char *required_arch = getenv("MLU_REMOTE_MLU_ARCH");
  if (!required_arch || !*required_arch) required_arch = "mtp_372";
  if (strcmp(name, desc->name) || strcmp(symbol, desc->symbol) || strcmp(arch, required_arch) ||
      strcmp(datatype, "f32") || strcmp(abi, "mlu_remote_operator_v1") ||
      inputs != desc->inputs || outputs != desc->outputs || rank != desc->rank ||
      scalar_bytes != desc->scalar_bytes)
    return (int)MLU_REMOTE_UNSUPPORTED;
  return 0;
}

int mlu_backend_global_init(void) {
  if (backend_ready) return 0;
  if (mlu_resource_limits_init()) return (int)CN_ERROR_INVALID_VALUE;
  CNresult r = cnInit(0);
  if (r != CN_SUCCESS) return (int)r;
  backend_ready = 1;
  return 0;
}

int mlu_backend_device_count(int *count) {
  return (int)cnDeviceGetCount(count);
}

int mlu_backend_device_info(int ordinal, char *name, size_t capacity, uint64_t *memory) {
  CNdev dev = 0;
  CNresult r = cnDeviceGet(&dev, ordinal);
  if (r != CN_SUCCESS) return (int)r;
  if (name && capacity) {
    r = cnDeviceGetName(name, (unsigned int)capacity, dev);
    if (r != CN_SUCCESS) return (int)r;
    name[capacity - 1] = 0;
  }
  if (memory) {
    r = cnDeviceTotalMem(memory, dev);
    if (r != CN_SUCCESS) return (int)r;
  }
  return 0;
}

static uint32_t new_generation(mlu_backend_session *s) {
  return ++s->generation;
}
static int register_object(mlu_backend_session *s, uint16_t type,
                           uint64_t *id, uint32_t *generation) {
  return mlu_object_registry_register(&s->objects, type, id, generation)
             ? (int)CN_ERROR_SYSTEM_OUT_OF_MEMORY : 0;
}

static mlu_backend_allocation *find_alloc(mlu_backend_session *s, uint64_t id) {
  for (size_t i = 0; i < MLU_BACKEND_MAX_OBJECTS; ++i)
    if (s->allocations[i].live && !s->allocations[i].retired && s->allocations[i].owner == s->owner_token &&
        s->allocations[i].id == id && mlu_object_registry_contains(&s->objects, MLU_OBJECT_ALLOCATION, id)) return &s->allocations[i];
  return NULL;
}
static mlu_backend_queue *find_queue(mlu_backend_session *s, uint64_t id) {
  for (size_t i = 0; i < MLU_BACKEND_MAX_OBJECTS; ++i)
    if (s->queues[i].live && !s->queues[i].retired && s->queues[i].owner == s->owner_token &&
        s->queues[i].id == id && mlu_object_registry_contains(&s->objects, MLU_OBJECT_QUEUE, id)) return &s->queues[i];
  return NULL;
}
static mlu_backend_notifier *find_notifier(mlu_backend_session *s, uint64_t id) {
  for (size_t i = 0; i < MLU_BACKEND_MAX_OBJECTS; ++i)
    if (s->notifiers[i].live && s->notifiers[i].owner == s->owner_token &&
        s->notifiers[i].id == id && mlu_object_registry_contains(&s->objects, MLU_OBJECT_NOTIFIER, id)) return &s->notifiers[i];
  return NULL;
}
static mlu_backend_module *find_module_by_kernel(mlu_backend_session *s, uint64_t id) {
  for (size_t i = 0; i < MLU_BACKEND_MAX_OBJECTS; ++i)
    if (s->modules[i].live && s->modules[i].owner == s->owner_token &&
        s->modules[i].kernel_id == id && mlu_object_registry_contains(&s->objects, MLU_OBJECT_KERNEL, id)) return &s->modules[i];
  return NULL;
}
static mlu_backend_module *find_module_by_operator(mlu_backend_session *s, uint32_t id) {
  for (size_t i = 0; i < MLU_BACKEND_MAX_OBJECTS; ++i)
    if (s->modules[i].live && s->modules[i].owner == s->owner_token && !s->modules[i].retired && s->modules[i].operator_id == id) return &s->modules[i];
  return NULL;
}
static mlu_backend_module *find_module_by_id(mlu_backend_session *s, uint64_t id) {
  for (size_t i = 0; i < MLU_BACKEND_MAX_OBJECTS; ++i)
    if (s->modules[i].live && s->modules[i].owner == s->owner_token &&
        s->modules[i].module_id == id && mlu_object_registry_contains(&s->objects, MLU_OBJECT_MODULE, id)) return &s->modules[i];
  return NULL;
}
static mlu_backend_kernel *find_generic_kernel(mlu_backend_session *s, uint64_t id) {
  for (size_t i = 0; i < MLU_BACKEND_MAX_OBJECTS; ++i)
    if (s->kernels[i].live && s->kernels[i].owner == s->owner_token &&
        s->kernels[i].id == id &&
        mlu_object_registry_contains(&s->objects, MLU_OBJECT_KERNEL, id)) return &s->kernels[i];
  return NULL;
}
static void invalidate_module_kernels(mlu_backend_session *s, uint64_t module_id) {
  for (size_t i = 0; i < MLU_BACKEND_MAX_OBJECTS; ++i) {
    mlu_backend_kernel *k = &s->kernels[i];
    if (!k->live || k->module_id != module_id) continue;
    mlu_object_registry_release(&s->objects, MLU_OBJECT_KERNEL, k->id);
    memset(k, 0, sizeof(*k));
  }
}
static void graph_transfer_release(mlu_backend_session *s,mlu_backend_transfer *t);
static int graph_reap_allocations(mlu_backend_session *s);
static void reap_transfers(mlu_backend_session *s) {
  for (size_t i = 0; i < MLU_BACKEND_MAX_OBJECTS; ++i) {
    mlu_backend_transfer *t = &s->transfers[i];
    if (!t->live || !t->release_requested) continue;
    if (t->notifier_live && cnQueryNotifier(t->notifier) != CN_SUCCESS) continue;
    graph_transfer_release(s,t);
    if (t->notifier_live && cnDestroyNotifier(t->notifier) != CN_SUCCESS) continue;
    t->notifier_live = 0;
    if (t->host) {
      if (cnFreeHost(t->host) != CN_SUCCESS) continue;
      t->host = NULL;
      mlu_resource_release(&s->usage, MLU_RESOURCE_STAGING, t->bytes);
    }
    mlu_object_registry_release(&s->objects, MLU_OBJECT_TRANSFER, t->id);
    t->live = t->notifier_live = 0;
  }
}
static int transfer_capacity(mlu_backend_session *s) {
  reap_transfers(s);
  unsigned count = 0;
  for (size_t i = 0; i < MLU_BACKEND_MAX_OBJECTS; ++i) count += !!s->transfers[i].live;
  if (count < mlu_resource_max_transfers()) return 0;
  fprintf(stderr, "[server] RESOURCE_LIMIT kind=TRANSFER owner=%llu used=%u\n",
          (unsigned long long)s->owner_token, count);
  return (int)MLU_REMOTE_RESOURCE_LIMIT;
}
static int record_transfer(mlu_backend_session *s, uint64_t queue_id, int asynchronous, uint64_t *transfer_id) {
  if (!transfer_id) return (int)CN_ERROR_INVALID_VALUE;
  reap_transfers(s);
  size_t slot = MLU_BACKEND_MAX_OBJECTS;
  for (size_t i = 0; i < MLU_BACKEND_MAX_OBJECTS; ++i)
    if (!s->transfers[i].live) { slot = i; break; }
  if (slot == MLU_BACKEND_MAX_OBJECTS) return (int)CN_ERROR_SYSTEM_OUT_OF_MEMORY;
  CNnotifier notifier = NULL;
  if (asynchronous) {
    mlu_backend_queue *q = queue_id ? find_queue(s, queue_id) : NULL;
    if (queue_id && !q) return (int)CN_ERROR_INVALID_HANDLE;
    CNresult r = cnCreateNotifier(&notifier, 0);
    if (r != CN_SUCCESS) return (int)r;
    r = cnPlaceNotifier(notifier, q ? q->handle : NULL);
    if (r != CN_SUCCESS) { cnDestroyNotifier(notifier); return (int)r; }
  }
  uint64_t registered_id = 0; uint32_t generation = 0;
  int registry_status = register_object(s, MLU_OBJECT_TRANSFER, &registered_id, &generation);
  if (registry_status != 0) { if (notifier) cnDestroyNotifier(notifier); return registry_status; }
  uint64_t dependency = 0, latest_sequence = 0;
  uint64_t sequence = ++s->next_operation_sequence;
  for (size_t i = 0; i < MLU_BACKEND_MAX_OBJECTS; ++i) {
    if (s->transfers[i].live && s->transfers[i].queue_id == queue_id &&
        s->transfers[i].sequence > 0 && s->transfers[i].sequence < sequence &&
        s->transfers[i].sequence > latest_sequence) {
      dependency = s->transfers[i].id;
      latest_sequence = s->transfers[i].sequence;
    }
  }
  s->transfers[slot] = (mlu_backend_transfer){
      .id = registered_id, .queue_id = queue_id, .notifier = notifier,
      .sequence = sequence, .dependency = dependency,
      .owner = s->owner_token, .generation = generation,
      .state = MLU_BACKEND_OPERATION_SUBMITTED,
      .notifier_live = notifier != NULL, .live = 1};
  *transfer_id = s->transfers[slot].id;
  return 0;
}
static mlu_backend_transfer *find_transfer(mlu_backend_session *s, uint64_t id) {
  for (size_t i = 0; i < MLU_BACKEND_MAX_OBJECTS; ++i)
    if (s->transfers[i].live && s->transfers[i].owner == s->owner_token &&
        s->transfers[i].id == id && mlu_object_registry_contains(&s->objects, MLU_OBJECT_TRANSFER, id)) return &s->transfers[i];
  return NULL;
}
static int cleanup_result(int *first, const char *op, uint64_t id, CNresult result) {
  if (result != CN_SUCCESS) {
    if (!*first) *first = (int)result;
    fprintf(stderr, "[server] CLEANUP_ERROR op=%s id=%llu status=%d\n", op,
            (unsigned long long)id, (int)result);
  }
  return (int)result;
}
static void destroy_transfer_notifiers(mlu_backend_session *s, int *first) {
  for (size_t i = 0; i < MLU_BACKEND_MAX_OBJECTS; ++i) {
    if (s->transfers[i].live && s->transfers[i].notifier_live) {
      cleanup_result(first, "cnDestroyNotifier(transfer)", s->transfers[i].id,
                     cnDestroyNotifier(s->transfers[i].notifier));
      s->transfers[i].notifier_live = 0;
    }
    if (s->transfers[i].live)
      mlu_object_registry_release(&s->objects, MLU_OBJECT_TRANSFER, s->transfers[i].id);
    s->transfers[i].live = 0;
  }
}
static void release_staging(mlu_backend_session *s, uint64_t queue_id) {
  for (size_t i = 0; i < MLU_BACKEND_MAX_OBJECTS; ++i)
    if (s->staging[i].live && (!queue_id || s->staging[i].queue_id == queue_id)) {
      if (s->staging[i].pinned) { if (cnFreeHost(s->staging[i].host)) continue; }
      else free(s->staging[i].host);
      s->staging[i].live = 0;
      mlu_resource_release(&s->usage, MLU_RESOURCE_STAGING, s->staging[i].bytes);
    }
}
static void release_staging_default(mlu_backend_session *s) {
  for (size_t i = 0; i < MLU_BACKEND_MAX_OBJECTS; ++i)
    if (s->staging[i].live && s->staging[i].queue_id == 0) {
      if (s->staging[i].pinned) { if (cnFreeHost(s->staging[i].host)) continue; }
      else free(s->staging[i].host);
      s->staging[i].live = 0;
      mlu_resource_release(&s->usage, MLU_RESOURCE_STAGING, s->staging[i].bytes);
    }
}

/* D2H buffers survive queue completion and retirement until fetch/release.
 * After a failed wait they remain owned until native context destruction. */
static void release_transfer_buffers(mlu_backend_session *s) {
  for (size_t i=0; i<MLU_BACKEND_MAX_OBJECTS; ++i) {
    mlu_backend_transfer *t=&s->transfers[i];
    if (t->host && cnFreeHost(t->host)==CN_SUCCESS) {
      t->host=NULL;
      mlu_resource_release(&s->usage,MLU_RESOURCE_STAGING,t->bytes);
    }
  }
}

/* Destroy must not wait for queued work. Keep native ownership after retiring
 * the public ID, and collect only queues confirmed complete. Transfer/notifier
 * objects have independent completion handles and survive this collection.
 * Failed native destruction retains the handle for retry or session teardown. */
static int reap_retired_queues(mlu_backend_session *s) {
  int first = 0;
  for (size_t i = 0; i < MLU_BACKEND_MAX_OBJECTS; ++i) {
    mlu_backend_queue *q = &s->queues[i];
    if (!q->live || !q->retired) continue;
    CNresult r = cnQueryQueue(q->handle);
    if (r == CN_ERROR_NOT_READY) continue;
    if (cleanup_result(&first, "cnQueryQueue(retired)", q->id, r)) continue;
    release_staging(s, q->id);
    if (!cleanup_result(&first, "cnDestroyQueue(retired)", q->id,
                        cnDestroyQueue(q->handle))) memset(q, 0, sizeof(*q));
  }
  return first;
}

int mlu_backend_session_init(mlu_backend_session *s, int ordinal) {
  return mlu_backend_session_init_flags(s, ordinal, CN_CTX_SCHED_SYNC_WAIT);
}
int mlu_backend_session_init_flags(mlu_backend_session *s, int ordinal, unsigned flags) {
  memset(s, 0, sizeof(*s));
  s->device_ordinal = ordinal;
  s->owner_token = __sync_add_and_fetch(&next_session_owner, 1);
  if (mlu_object_registry_init(&s->objects, s->owner_token))
    return (int)CN_ERROR_OPERATING_SYSTEM;
  int r = mlu_backend_global_init();
  if (r) return r;
  CNresult cr = cnDeviceGet(&s->device, ordinal);
  if (cr != CN_SUCCESS) return (int)cr;
  cr = cnCtxCreate(&s->context, flags, s->device);
  if (cr != CN_SUCCESS) return (int)cr;
  cr = cnCtxSetCurrent(s->context);
  if (cr != CN_SUCCESS) {
    cnCtxDestroy(s->context);
    s->context = NULL;
    return (int)cr;
  }
  return 0;
}

static int session_has_resources(const mlu_backend_session *s) {
  for (size_t i = 0; i < MLU_BACKEND_MAX_OBJECTS; ++i)
    if (s->allocations[i].live || s->queues[i].live || s->notifiers[i].live ||
        s->modules[i].live || s->staging[i].live || s->transfers[i].live)
      return 1;
  return 0;
}

int mlu_backend_context_sync(mlu_backend_session *s) {
  CNresult r=cnCtxSync();
  if (r==CN_SUCCESS) {
    release_staging(s,0); reap_transfers(s);
    int st=reap_retired_queues(s), graph_st=graph_reap_allocations(s);
    return st ? st : graph_st;
  }
  return (int)r;
}

int mlu_backend_session_select_device(mlu_backend_session *s, int ordinal) {
  if (!s || ordinal < 0) return (int)CN_ERROR_INVALID_DEVICE;
  int count = 0;
  int st = mlu_backend_device_count(&count);
  if (st != 0) return st;
  if (ordinal >= count) return (int)CN_ERROR_INVALID_DEVICE;
  if (ordinal == s->device_ordinal) return 0;
  if (session_has_resources(s)) return (int)CN_OPS_ERROR_NOT_PERMITTED;
  st = mlu_backend_session_cleanup(s);
  if (st) return st;
  return mlu_backend_session_init(s, ordinal);
}

int mlu_backend_session_reset(mlu_backend_session *s) {
  if (!s) return (int)CN_ERROR_INVALID_VALUE;
  int ordinal = s->device_ordinal;
  int st = mlu_backend_session_cleanup(s);
  if (st) return st;
  return mlu_backend_session_init(s, ordinal);
}

static int graph_cleanup(mlu_backend_session *s);
static void graph_orphans_release(mlu_backend_session *s);
int mlu_backend_session_cleanup(mlu_backend_session *s) {
  if (!s) return 0;
  int first = 0, synchronized = 1;
  if (s->context && cleanup_result(&first, "cnCtxSetCurrent", 0, cnCtxSetCurrent(s->context)))
    synchronized = 0;
  for (size_t i = 0; i < MLU_BACKEND_MAX_OBJECTS; ++i) {
    if (s->queues[i].live && cleanup_result(&first, "cnQueueSync", s->queues[i].id,
                                           cnQueueSync(s->queues[i].handle))) synchronized = 0;
  }
  if (s->default_queue_used && cleanup_result(&first,"cnQueueSync(NULL)",0,cnQueueSync(NULL))) synchronized=0;
  /* On a synchronization error, retain host memory until context destruction
   * confirms no native operation can access it. Never free in-flight staging. */
  if (synchronized) {
    release_staging(s, 0); release_transfer_buffers(s);
    for(unsigned i=0;i<MLU_BACKEND_MAX_OBJECTS;i++) graph_transfer_release(s,&s->transfers[i]);
    graph_orphans_release(s);
  }
  destroy_transfer_notifiers(s, &first);
  int graph_status=graph_cleanup(s);
  if(graph_status) {
    /* Graphs are host-side native objects; context destruction alone does not
     * prove their release. Keep ownership instead of discarding failed DAGs. */
    s->cleanup_failed=first ? first : graph_status;
    fprintf(stderr,"[server] CLEANUP_QUARANTINED owner=%llu Graph release failed\n",(unsigned long long)s->owner_token);
    return s->cleanup_failed;
  }
  for (size_t i = 0; i < MLU_BACKEND_MAX_OBJECTS; ++i)
    if (s->modules[i].live && !s->modules[i].graph_refs && !cleanup_result(&first, "cnModuleUnload", s->modules[i].module_id,
                                             cnModuleUnload(s->modules[i].module))) {
      free(s->modules[i].image); s->modules[i].image = NULL;
      mlu_resource_release(&s->usage, MLU_RESOURCE_IMAGE, s->modules[i].image_size);
      s->modules[i].live = 0;
    }
  for (size_t i = 0; i < MLU_BACKEND_MAX_OBJECTS; ++i)
    if (s->notifiers[i].live) cleanup_result(&first, "cnDestroyNotifier", s->notifiers[i].id, cnDestroyNotifier(s->notifiers[i].handle));
  for (size_t i = 0; i < MLU_BACKEND_MAX_OBJECTS; ++i)
    if (s->queues[i].live) cleanup_result(&first, "cnDestroyQueue", s->queues[i].id, cnDestroyQueue(s->queues[i].handle));
  for (size_t i = 0; i < MLU_BACKEND_MAX_OBJECTS; ++i)
    if (s->allocations[i].live && !s->allocations[i].graph_refs &&
        !cleanup_result(&first, "cnFree", s->allocations[i].id, cnFree(s->allocations[i].address))) {
      mlu_resource_release(&s->usage, MLU_RESOURCE_DEVICE, s->allocations[i].bytes);
      s->allocations[i].live = 0;
    }
  int destroyed = !s->context || !cleanup_result(&first, "cnCtxDestroy", 0, cnCtxDestroy(s->context));
  if (!destroyed) {
    /* Quarantine unconfirmed native charges and retained host memory until
     * process exit. Do not reopen/reset this session after failed cleanup. */
    fprintf(stderr, "[server] CLEANUP_QUARANTINED owner=%llu\n", (unsigned long long)s->owner_token);
    s->cleanup_failed = first;
    return first;
  }
  s->context = NULL;
  for(unsigned i=0;i<MLU_BACKEND_MAX_OBJECTS;i++) graph_transfer_release(s,&s->transfers[i]);
  graph_orphans_release(s);
  release_staging(s, 0);
  release_transfer_buffers(s);
  /* cnFreeHost failure still owns pinned native pages, even after context
   * destruction. Keep the allocation and quota instead of losing ownership. */
  for (size_t i=0; i<MLU_BACKEND_MAX_OBJECTS; ++i)
    if (s->transfers[i].host || s->staging[i].live) {
      s->cleanup_failed=first ? first : (int)CN_ERROR_OPERATING_SYSTEM;
      fprintf(stderr,"[server] CLEANUP_QUARANTINED owner=%llu pinned Host release failed\n",(unsigned long long)s->owner_token);
      return s->cleanup_failed;
    }
  for (size_t i = 0; i < MLU_BACKEND_MAX_OBJECTS; ++i)
    if (s->modules[i].live) free(s->modules[i].image);
  for (unsigned kind = 0; kind < MLU_RESOURCE_KINDS; ++kind)
    mlu_resource_release(&s->usage, (enum mlu_resource_kind)kind, s->usage.bytes[kind]);
  mlu_object_registry_clear(&s->objects);
  memset(s, 0, sizeof(*s));
  s->cleanup_failed = first;
  return first;
}

static int backend_alloc_kind(mlu_backend_session *s, uint64_t bytes, uint32_t kind, int node, uint64_t *id) {
  if (!id || kind>7) return (int)CN_ERROR_INVALID_VALUE;
  for (size_t i = 0; i < MLU_BACKEND_MAX_OBJECTS; ++i) if (!s->allocations[i].live) {
    if (mlu_resource_reserve(&s->usage, MLU_RESOURCE_DEVICE, bytes)) return (int)MLU_REMOTE_RESOURCE_LIMIT;
    CNaddr p = 0;
    CNresult r;
    switch(kind) {
      case 0: r=cnMalloc(&p,bytes); break;
      case 1: r=cnMallocNode(&p,bytes,node); break;
      case 2: r=cnZmalloc(&p,bytes); break;
      case 3: r=cnZmallocNode(&p,bytes,node); break;
      case 4: r=cnMallocConstant(&p,bytes); break;
      case 5: r=cnMallocNodeConstant(&p,bytes,node); break;
      case 6: r=cnMallocSecurity(&p,bytes); break;
      default: r=cnMallocFrameBuffer(&p,bytes,node); break;
    }
    if (r != CN_SUCCESS) { mlu_resource_release(&s->usage, MLU_RESOURCE_DEVICE, bytes); return (int)r; }
    if (!bytes) { *id=0; return 0; } /* Native validates node but leaves output untouched. */
    uint64_t object_id = 0; uint32_t generation = 0;
    int registry_status = register_object(s, MLU_OBJECT_ALLOCATION, &object_id, &generation);
    if (registry_status != 0) {
      if (cnFree(p) == CN_SUCCESS) mlu_resource_release(&s->usage, MLU_RESOURCE_DEVICE, bytes);
      return registry_status;
    }
    s->allocations[i] = (mlu_backend_allocation){.id = object_id, .address = p, .bytes = bytes,
                                                .owner = s->owner_token, .generation = generation, .live = 1};
    *id = s->allocations[i].id;
    return 0;
  }
  return (int)CN_ERROR_SYSTEM_OUT_OF_MEMORY;
}

int mlu_backend_alloc(mlu_backend_session *s, uint64_t bytes, uint64_t *id) {
  if (!bytes) return CN_ERROR_INVALID_VALUE;
  return backend_alloc_kind(s,bytes,0,0,id);
}

/* Graph payloads and executable snapshots own native storage independently of
 * the public allocation ID. Retirement revokes the ID before reclaiming bytes.
 * Failed native release remains charged and owned for retry during cleanup. */
static int graph_release_allocation(mlu_backend_session *s, mlu_backend_allocation *a) {
  if(!a || !a->live || !a->retired || a->graph_refs) return 0;
  int st=cnFree(a->address);
  if(!st) {
    mlu_resource_release(&s->usage,MLU_RESOURCE_DEVICE,a->bytes);
    memset(a,0,sizeof(*a));
  }
  return st;
}
static int graph_release_module(mlu_backend_session *s,mlu_backend_module *m) {
  if(!m->live || !m->retired || m->graph_refs) return 0;
  int st=cnModuleUnload(m->module);
  if(!st) {
    free(m->image);
    mlu_resource_release(&s->usage,MLU_RESOURCE_IMAGE,m->image_size);
    memset(m,0,sizeof(*m));
  }
  return st;
}
/* Reap both kinds of backing resource after the last immutable graph lease. */
static int graph_reap_allocations(mlu_backend_session *s) {
  int first=0;
  for(unsigned i=0;i<MLU_BACKEND_MAX_OBJECTS;i++) {
    int st=graph_release_allocation(s,&s->allocations[i]);
    if(st && !first) first=st;
    st=graph_release_module(s,&s->modules[i]);
    if(st && !first) first=st;
  }
  return first;
}
int mlu_backend_free(mlu_backend_session *s, uint64_t id) {
  mlu_backend_allocation *a = find_alloc(s, id);
  if (!a) return (int)CN_ERROR_INVALID_HANDLE;
  if(a->graph_refs) {
    a->retired=1;
    mlu_object_registry_release(&s->objects,MLU_OBJECT_ALLOCATION,id);
    return 0;
  }
  for (size_t i = 0; i < MLU_BACKEND_MAX_OBJECTS; ++i)
    if (s->queues[i].live) { CNresult qr = cnQueueSync(s->queues[i].handle); if (qr != CN_SUCCESS) return (int)qr; }
  if (s->default_queue_used) { CNresult qr=cnQueueSync(NULL); if(qr) return (int)qr; }
  release_staging(s, 0);
  int retired_status = reap_retired_queues(s);
  if (retired_status) return retired_status;
  CNresult r = cnFree(a->address);
  if (r == CN_SUCCESS) {
    mlu_resource_release(&s->usage, MLU_RESOURCE_DEVICE, a->bytes);
    a->live = 0; mlu_object_registry_release(&s->objects, MLU_OBJECT_ALLOCATION, id);
  }
  return (int)r;
}

static int range(mlu_backend_allocation *a, uint64_t off, uint64_t bytes, CNaddr *out) {
  if (!a || off > a->bytes || bytes > a->bytes - off) return (int)CN_ERROR_INVALID_VALUE;
  *out = a->address + off;
  return 0;
}

static int backend_d2h_submit(mlu_backend_session *s,uint64_t allocation,uint64_t offset,
                               uint64_t bytes,uint64_t queue_id,uint64_t *id) {
  CNaddr src;
  if (!bytes || bytes>MLU_REMOTE_MAX_PAYLOAD-128 ||
      range(find_alloc(s,allocation),offset,bytes,&src)) return CN_ERROR_INVALID_VALUE;
  mlu_backend_queue *q=queue_id ? find_queue(s,queue_id) : NULL;
  if (queue_id && !q) return CN_ERROR_INVALID_HANDLE;
  int st=transfer_capacity(s); if(st) return st;
  size_t slot=0;
  while(slot<MLU_BACKEND_MAX_OBJECTS && s->staging[slot].live) ++slot;
  if(slot==MLU_BACKEND_MAX_OBJECTS) return MLU_REMOTE_RESOURCE_LIMIT;
  if(mlu_resource_reserve(&s->usage,MLU_RESOURCE_STAGING,bytes)) return MLU_REMOTE_RESOURCE_LIMIT;
  void *host=NULL;
  st=cnMallocHost(&host,bytes);
  if(st) { mlu_resource_release(&s->usage,MLU_RESOURCE_STAGING,bytes); return st; }
  /* Publish ownership before native submission. Failure to record completion
   * or to synchronize must leave a reachable buffer for teardown. */
  s->staging[slot]=(mlu_backend_staging){.queue_id=queue_id,.host=host,.bytes=bytes,
    .owner=s->owner_token,.generation=new_generation(s),.pinned=1,.live=1};
  st=cnMemcpyDtoHAsync(host,src,bytes,q ? q->handle : NULL);
  if(!queue_id) s->default_queue_used=1;
  if(!st) st=record_transfer(s,queue_id,1,id);
  if(st) {
    /* A submission error does not authorize freeing a possibly referenced
     * page. The staging registry retains it if this wait also fails. */
    if(cnQueueSync(q ? q->handle : NULL)==CN_SUCCESS) {
      if(queue_id) release_staging(s,queue_id); else release_staging_default(s);
    }
    return st;
  }
  mlu_backend_transfer *t=find_transfer(s,*id);
  t->host=host; t->bytes=bytes;
  s->staging[slot].live=0;
  return 0;
}
static int backend_transfer_fetch(mlu_backend_session *s,uint64_t id,void *out,uint64_t bytes) {
  mlu_backend_transfer *t=find_transfer(s,id);
  if(!t || t->release_requested || !t->host || t->bytes!=bytes) return CN_ERROR_INVALID_HANDLE;
  int st=mlu_backend_transfer_query(s,id); if(st) return st;
  memcpy(out,t->host,(size_t)bytes);
  return 0;
}

int mlu_backend_memcpy_ex(mlu_backend_session *s, int direction,
                          uint64_t dst_id, uint64_t dst_offset,
                          uint64_t src_id, uint64_t src_offset,
                          const void *host_src, void *host_dst, uint64_t bytes,
                          uint64_t queue_id, uint32_t asynchronous,
                          uint64_t *transfer_id) {
  if (!bytes) return (int)CN_ERROR_INVALID_VALUE;
  if (asynchronous > 1) return (int)CN_ERROR_INVALID_VALUE;
  mlu_backend_queue *q = queue_id ? find_queue(s, queue_id) : NULL;
  if (queue_id && !q) return (int)CN_ERROR_INVALID_HANDLE;
  int queued = asynchronous || queue_id != 0;
  if (transfer_id) { int st = transfer_capacity(s); if (st) return st; }
  CNaddr dst = 0, src = 0;
  CNresult r;
  if (direction == MLU_MEMCPY_H2D) {
    if (range(find_alloc(s, dst_id), dst_offset, bytes, &dst)) return (int)CN_ERROR_INVALID_VALUE;
    if (!host_src) return (int)CN_ERROR_INVALID_ADDRESS;
    if (queued) {
      /* Only a completed queue makes every staging buffer on it reclaimable.
       * Poll before adding work so a long async-copy stream does not leak. */
      CNresult ready = q ? cnQueryQueue(q->handle) : cnQueryQueue(NULL);
      if (ready == CN_SUCCESS) {
        if (q) release_staging(s, queue_id);
        else release_staging_default(s);
      }
      size_t staging_slot = MLU_BACKEND_MAX_OBJECTS;
      for (size_t i = 0; i < MLU_BACKEND_MAX_OBJECTS; ++i)
        if (!s->staging[i].live) { staging_slot = i; break; }
      if (staging_slot == MLU_BACKEND_MAX_OBJECTS) return (int)CN_ERROR_SYSTEM_OUT_OF_MEMORY;
      if (mlu_resource_reserve(&s->usage, MLU_RESOURCE_STAGING, bytes)) return (int)MLU_REMOTE_RESOURCE_LIMIT;
      void *staging = malloc((size_t)bytes);
      if (!staging) { mlu_resource_release(&s->usage, MLU_RESOURCE_STAGING, bytes); return (int)CN_ERROR_SYSTEM_OUT_OF_MEMORY; }
      memcpy(staging, host_src, (size_t)bytes);
      r = cnMemcpyHtoDAsync(dst, staging, bytes, q ? q->handle : NULL);
      if (r == CN_SUCCESS) {
        s->staging[staging_slot] = (mlu_backend_staging){.queue_id = queue_id, .host = staging, .bytes = bytes,
                                                         .owner = s->owner_token, .generation = new_generation(s), .live = 1};
        staging = NULL;
      }
      if (staging) { free(staging); mlu_resource_release(&s->usage, MLU_RESOURCE_STAGING, bytes); }
    } else r = cnMemcpyHtoD(dst, host_src, bytes);
  } else if (direction == MLU_MEMCPY_D2H) {
    if (range(find_alloc(s, src_id), src_offset, bytes, &src)) return (int)CN_ERROR_INVALID_VALUE;
    if (!host_dst) return (int)CN_ERROR_INVALID_ADDRESS;
    /* This protocol returns D2H bytes in the response. Wait before using a
     * synchronous native copy: a failed queue wait must never leave an async
     * writer referencing the response buffer that the dispatcher will free. */
    r = q ? cnQueueSync(q->handle) : (queued ? cnQueueSync(NULL) : CN_SUCCESS);
    if (r == CN_SUCCESS && q) release_staging(s, queue_id);
    if (r == CN_SUCCESS && queued && !q) release_staging_default(s);
    if (r == CN_SUCCESS) r = cnMemcpyDtoH(host_dst, src, bytes);
  } else if (direction == MLU_MEMCPY_D2D) {
    if (range(find_alloc(s, dst_id), dst_offset, bytes, &dst)) return (int)CN_ERROR_INVALID_VALUE;
    if (range(find_alloc(s, src_id), src_offset, bytes, &src)) return (int)CN_ERROR_INVALID_VALUE;
    r = queued ? cnMemcpyDtoDAsync(dst, src, bytes, q ? q->handle : NULL) : cnMemcpyDtoD(dst, src, bytes);
  } else return (int)CN_ERROR_INVALID_VALUE;
  if (transfer_id && r == CN_SUCCESS) {
    int tr = record_transfer(s, queue_id, queued, transfer_id);
    if (tr != 0) {
      if (q && cnQueueSync(q->handle) == CN_SUCCESS) release_staging(s, queue_id);
      if (!q && queued && cnQueueSync(NULL) == CN_SUCCESS) release_staging_default(s);
      return tr;
    }
  }
  return (int)r;
}

int mlu_backend_memcpy(mlu_backend_session *s, int direction,
                       uint64_t dst_id, uint64_t dst_offset,
                       uint64_t src_id, uint64_t src_offset,
                       const void *host_src, void *host_dst, uint64_t bytes,
                       uint64_t queue_id, uint64_t *transfer_id) {
  return mlu_backend_memcpy_ex(s, direction, dst_id, dst_offset, src_id, src_offset,
                               host_src, host_dst, bytes, queue_id, 0, transfer_id);
}

static int mlu_backend_queue_create_options(mlu_backend_session *s, uint32_t flags, int priority, uint64_t *id) {
  int st = reap_retired_queues(s);
  if (st) return st;
  for (size_t i = 0; i < MLU_BACKEND_MAX_OBJECTS; ++i) if (!s->queues[i].live) {
    CNqueue q = NULL; CNresult r = cnCreateQueueWithPriority(&q, flags, priority); if (r != CN_SUCCESS) return (int)r;
    uint64_t object_id = 0; uint32_t generation = 0;
    int registry_status = register_object(s, MLU_OBJECT_QUEUE, &object_id, &generation);
    if (registry_status != 0) { cnDestroyQueue(q); return registry_status; }
    s->queues[i] = (mlu_backend_queue){.id = object_id, .handle = q, .owner = s->owner_token,
                                       .generation = generation, .live = 1}; *id = s->queues[i].id; return 0;
  }
  return (int)CN_ERROR_SYSTEM_OUT_OF_MEMORY;
}
int mlu_backend_queue_create(mlu_backend_session *s, uint64_t *id) {
  return mlu_backend_queue_create_options(s,0,0,id);
}
int mlu_backend_queue_destroy(mlu_backend_session *s, uint64_t id) {
  mlu_backend_queue *q = find_queue(s, id); if (!q) return (int)CN_ERROR_INVALID_HANDLE;
  q->retired = 1;
  mlu_object_registry_release(&s->objects, MLU_OBJECT_QUEUE, id);
  return 0;
}
int mlu_backend_queue_sync(mlu_backend_session *s, uint64_t id) {
  /* Driver NULL queue is passed through to CNDrv. It must not become a
   * context-wide barrier or release staging owned by explicit queues. */
  if (!id) { CNresult r=cnQueueSync(NULL); if(!r) { release_staging_default(s); reap_transfers(s); return reap_retired_queues(s); } return (int)r; }
  mlu_backend_queue *q = find_queue(s, id); if (!q) return (int)CN_ERROR_INVALID_HANDLE;
  CNresult r = cnQueueSync(q->handle); if (r == CN_SUCCESS) { release_staging(s, id); reap_transfers(s); return reap_retired_queues(s); } return (int)r;
}
int mlu_backend_queue_query(mlu_backend_session *s, uint64_t id) {
  mlu_backend_queue *q = id ? find_queue(s, id) : NULL;
  if (id && !q) return (int)CN_ERROR_INVALID_HANDLE;
  CNresult r = cnQueryQueue(q ? q->handle : NULL);
  if (r == CN_SUCCESS) {
    if (q) release_staging(s, id); else release_staging_default(s);
    reap_transfers(s);
    return reap_retired_queues(s);
  }
  return (int)r;
}
static int mlu_backend_notifier_create_options(mlu_backend_session *s, uint32_t flags, uint64_t *id) {
  for (size_t i = 0; i < MLU_BACKEND_MAX_OBJECTS; ++i) if (!s->notifiers[i].live) {
    CNnotifier n = NULL; CNresult r = cnCreateNotifier(&n, flags); if (r != CN_SUCCESS) return (int)r;
    uint64_t object_id = 0; uint32_t generation = 0;
    int registry_status = register_object(s, MLU_OBJECT_NOTIFIER, &object_id, &generation);
    if (registry_status != 0) { cnDestroyNotifier(n); return registry_status; }
    s->notifiers[i] = (mlu_backend_notifier){.id = object_id, .handle = n, .owner = s->owner_token,
                                             .generation = generation, .live = 1}; *id = s->notifiers[i].id; return 0;
  }
  return (int)CN_ERROR_SYSTEM_OUT_OF_MEMORY;
}
int mlu_backend_notifier_create(mlu_backend_session *s, uint64_t *id) {
  return mlu_backend_notifier_create_options(s,0,id);
}
int mlu_backend_notifier_destroy(mlu_backend_session *s, uint64_t id) {
  mlu_backend_notifier *n = find_notifier(s, id); if (!n) return (int)CN_ERROR_INVALID_HANDLE;
  CNresult r = cnDestroyNotifier(n->handle); if (r == CN_SUCCESS) { n->live = 0; mlu_object_registry_release(&s->objects, MLU_OBJECT_NOTIFIER, id); } return (int)r;
}
int mlu_backend_notifier_record(mlu_backend_session *s, uint64_t nid, uint64_t qid) {
  mlu_backend_notifier *n = find_notifier(s, nid); mlu_backend_queue *q = find_queue(s, qid);
  if (!n || (qid && !q)) return (int)CN_ERROR_INVALID_HANDLE;
  return (int)cnPlaceNotifier(n->handle, q ? q->handle : NULL);
}
int mlu_backend_notifier_query(mlu_backend_session *s, uint64_t id) {
  mlu_backend_notifier *n = find_notifier(s, id); if (!n) return (int)CN_ERROR_INVALID_HANDLE;
  CNresult r = cnQueryNotifier(n->handle);
  return r == CN_SUCCESS ? reap_retired_queues(s) : (int)r;
}
int mlu_backend_notifier_sync(mlu_backend_session *s, uint64_t id) {
  mlu_backend_notifier *n = find_notifier(s, id); if (!n) return (int)CN_ERROR_INVALID_HANDLE;
  CNresult r = cnWaitNotifier(n->handle);
  return r == CN_SUCCESS ? reap_retired_queues(s) : (int)r;
}
int mlu_backend_transfer_query(mlu_backend_session *s, uint64_t id) {
  mlu_backend_transfer *t = find_transfer(s, id);
  if (!t) return (int)CN_ERROR_INVALID_HANDLE;
  if (!t->notifier_live) { t->state = MLU_BACKEND_OPERATION_COMPLETE; return 0; }
  int st = (int)cnQueryNotifier(t->notifier);
  if (st == CN_SUCCESS) { t->state = MLU_BACKEND_OPERATION_COMPLETE; return reap_retired_queues(s); }
  return st;
}
int mlu_backend_transfer_sync(mlu_backend_session *s, uint64_t id) {
  mlu_backend_transfer *t = find_transfer(s, id);
  if (!t) return (int)CN_ERROR_INVALID_HANDLE;
  if (!t->notifier_live) { t->state = MLU_BACKEND_OPERATION_COMPLETE; return 0; }
  int st = (int)cnWaitNotifier(t->notifier);
  if (st == CN_SUCCESS) { t->state = MLU_BACKEND_OPERATION_COMPLETE; return reap_retired_queues(s); }
  else t->state = MLU_BACKEND_OPERATION_FAILED;
  return st;
}
int mlu_backend_transfer_release(mlu_backend_session *s, uint64_t id) {
  mlu_backend_transfer *t = find_transfer(s, id);
  if (!t || t->release_requested) return (int)CN_ERROR_INVALID_HANDLE;
  t->release_requested = 1;
  reap_transfers(s);
  return 0;
}

static int load_operator_module(mlu_backend_session *s, const operator_descriptor *desc,
                                uint64_t *module_id, uint64_t *kernel_id) {
  if (!desc || !module_id || !kernel_id) return (int)CN_ERROR_INVALID_VALUE;
  mlu_backend_module *existing = find_module_by_operator(s, desc->id);
  if (existing) { *module_id = existing->module_id; *kernel_id = existing->kernel_id; return 0; }
  const char *dir = getenv("MLU_REMOTE_MODULE_DIR"); if (!dir) dir = "modules";
  int manifest_status = verify_operator_manifest(dir, desc);
  if (manifest_status != 0) return manifest_status;
  char path[512]; int n = snprintf(path, sizeof(path), "%s/%s.fatbin", dir, desc->name);
  if (n <= 0 || (size_t)n >= sizeof(path)) return (int)CN_ERROR_INVALID_VALUE;
  for (size_t i = 0; i < MLU_BACKEND_MAX_OBJECTS; ++i) if (!s->modules[i].live) {
    CNmodule m = NULL; CNresult r = cnModuleLoad(path, &m); if (r != CN_SUCCESS) return (int)r;
    CNkernel k = NULL; r = cnModuleGetKernel(m, desc->name, &k);
    /* cncc treats a .mlu translation unit as C++; the registry stores the
     * verified symbol and tries the source spelling first for toolchain
     * versions that preserve it. */
    if (r != CN_SUCCESS) r = cnModuleGetKernel(m, desc->symbol, &k);
    if (r != CN_SUCCESS) { cnModuleUnload(m); return (int)r; }
    uint64_t module_object = 0, kernel_object = 0; uint32_t generation = 0, kernel_generation = 0;
    int registry_status = register_object(s, MLU_OBJECT_MODULE, &module_object, &generation);
    if (registry_status == 0) registry_status = register_object(s, MLU_OBJECT_KERNEL, &kernel_object, &kernel_generation);
    if (registry_status != 0) {
      if (module_object) mlu_object_registry_release(&s->objects, MLU_OBJECT_MODULE, module_object);
      cnModuleUnload(m); return registry_status;
    }
    s->modules[i] = (mlu_backend_module){.module_id = module_object, .kernel_id = kernel_object,
                                         .operator_id = desc->id, .module = m, .kernel = k,
                                         .owner = s->owner_token, .generation = generation, .live = 1};
    *module_id = s->modules[i].module_id; *kernel_id = s->modules[i].kernel_id; return 0;
  }
  return (int)CN_ERROR_SYSTEM_OUT_OF_MEMORY;
}

int mlu_backend_module_load(mlu_backend_session *s, const char *name, uint64_t *module_id, uint64_t *kernel_id) {
  if (!name || !*name || strlen(name) > 64 || strchr(name, '/') || strchr(name, '.'))
    return (int)CN_ERROR_INVALID_VALUE;
  const operator_descriptor *desc = find_operator_by_name(name);
  if (!desc) return (int)CN_ERROR_INVALID_VALUE;
  return load_operator_module(s, desc, module_id, kernel_id);
}

int mlu_backend_module_get_kernel(mlu_backend_session *s, uint64_t module_id,
                                  const char *name, uint64_t *kernel_id) {
  if (!s || !name || !*name || strlen(name) >= 256 || !kernel_id)
    return (int)CN_ERROR_INVALID_VALUE;
  mlu_backend_module *m = find_module_by_id(s, module_id);
  if (!m) return (int)CN_ERROR_INVALID_HANDLE;
  if (!m->generic_image) return (int)MLU_REMOTE_UNSUPPORTED;
  mlu_backend_kernel *slot = NULL;
  for (size_t i = 0; i < MLU_BACKEND_MAX_OBJECTS; ++i) {
    mlu_backend_kernel *k = &s->kernels[i];
    if (!k->live) { if (!slot) slot = k; continue; }
    if (k->module_id == module_id && !strcmp(k->name, name)) {
      *kernel_id = k->id; return 0;
    }
  }
  if (!slot) return (int)CN_ERROR_SYSTEM_OUT_OF_MEMORY;
  CNkernel handle = NULL;
  CNresult r = cnModuleGetKernel(m->module, name, &handle);
  if (r != CN_SUCCESS) return (int)r;
  bang_param_layout params;
  if (bang_image_params(m->image, m->image_size, name, &params))
    return (int)MLU_REMOTE_UNSUPPORTED;
  uint64_t id = 0; uint32_t generation = 0;
  int st = register_object(s, MLU_OBJECT_KERNEL, &id, &generation);
  if (st) return st;
  *slot = (mlu_backend_kernel){.id = id, .module_id = module_id, .handle = handle,
                               .params = params, .owner = s->owner_token,
                               .generation = generation, .live = 1};
  snprintf(slot->name, sizeof(slot->name), "%s", name);
  *kernel_id = id; return 0;
}

int mlu_backend_module_load_binary(mlu_backend_session *s, const void *image,
                                   uint64_t image_size, uint64_t *module_id) {
  if (!s || !image || !image_size || image_size>MLU_REMOTE_MAX_PAYLOAD || !module_id)
    return (int)CN_ERROR_INVALID_VALUE;
  if (mlu_resource_reserve(&s->usage,MLU_RESOURCE_IMAGE,image_size)) return (int)MLU_REMOTE_RESOURCE_LIMIT;
  if (bang_image_validate(image,image_size)) {
    mlu_resource_release(&s->usage,MLU_RESOURCE_IMAGE,image_size);
    return (int)MLU_REMOTE_UNSUPPORTED;
  }
  for (size_t i=0;i<MLU_BACKEND_MAX_OBJECTS;++i) if (!s->modules[i].live) {
    void *owned=malloc(image_size);
    if (!owned) { mlu_resource_release(&s->usage,MLU_RESOURCE_IMAGE,image_size); return CN_ERROR_SYSTEM_OUT_OF_MEMORY; }
    memcpy(owned,image,image_size);
    CNmodule native=NULL; CNresult r=cnModuleLoadFatBinary(owned,&native);
    if (r) { free(owned); mlu_resource_release(&s->usage,MLU_RESOURCE_IMAGE,image_size); return (int)r; }
    uint64_t mid=0; uint32_t generation=0;
    int st=register_object(s,MLU_OBJECT_MODULE,&mid,&generation);
    s->modules[i]=(mlu_backend_module){.module_id=mid,.module=native,.generic_image=1,
      .image=owned,.image_size=image_size,.owner=s->owner_token,.generation=generation,.live=1};
    if (st) {
      if (cnModuleUnload(native)==CN_SUCCESS) {
        free(owned); mlu_resource_release(&s->usage,MLU_RESOURCE_IMAGE,image_size);
        memset(&s->modules[i],0,sizeof(s->modules[i]));
      }
      return st;
    }
    *module_id=mid; return 0;
  }
  mlu_resource_release(&s->usage,MLU_RESOURCE_IMAGE,image_size);
  return CN_ERROR_SYSTEM_OUT_OF_MEMORY;
}

int mlu_backend_module_query_fatbinary_memory(mlu_backend_session *s,
                                              const void *image, uint64_t image_size,
                                              uint64_t *bytes) {
  if (!s || !image || !image_size || image_size > MLU_REMOTE_MAX_PAYLOAD || !bytes)
    return (int)CN_ERROR_INVALID_VALUE;
  size_t usage = 0;
  CNresult current = s->context ? cnCtxSetCurrent(s->context) : CN_ERROR_NOT_INITIALIZED;
  if (current != CN_SUCCESS) return (int)current;
  CNresult r = cnModuleQueryFatBinaryMemoryUsage(image, &usage);
  if (r == CN_SUCCESS) *bytes = (uint64_t)usage;
  return (int)r;
}

int mlu_backend_module_load_image(mlu_backend_session *s, const void *image,
                                  uint64_t image_size, const char *kernel_name,
                                  uint64_t *module_id, uint64_t *kernel_id) {
  if (!s || !image || !image_size || image_size > MLU_REMOTE_MAX_PAYLOAD ||
      !kernel_name || !*kernel_name || strlen(kernel_name) >= 256 ||
      !module_id || !kernel_id)
    return (int)CN_ERROR_INVALID_VALUE;
  if (mlu_resource_reserve(&s->usage, MLU_RESOURCE_IMAGE, image_size)) return (int)MLU_REMOTE_RESOURCE_LIMIT;
  bang_param_layout params;
  size_t exact = 0;
  if (bang_image_span(image, image_size, &exact) || exact != image_size ||
      bang_image_params(image, image_size, kernel_name, &params))
    { mlu_resource_release(&s->usage, MLU_RESOURCE_IMAGE, image_size); return (int)MLU_REMOTE_UNSUPPORTED; }
  for (size_t i = 0; i < MLU_BACKEND_MAX_OBJECTS; ++i) if (!s->modules[i].live) {
    void *owned_image = malloc(image_size);
    if (!owned_image) { mlu_resource_release(&s->usage, MLU_RESOURCE_IMAGE, image_size); return CN_ERROR_SYSTEM_OUT_OF_MEMORY; }
    memcpy(owned_image, image, image_size);
    CNmodule m = NULL; CNresult r = cnModuleLoadFatBinary(owned_image, &m);
    if (r != CN_SUCCESS) { free(owned_image); mlu_resource_release(&s->usage, MLU_RESOURCE_IMAGE, image_size); return (int)r; }
    uint64_t mid = 0, kid = 0; uint32_t mg = 0;
    memset(&s->modules[i], 0, sizeof(s->modules[i]));
    s->modules[i].module = m;
    s->modules[i].generic_image = 1; s->modules[i].owner = s->owner_token;
    s->modules[i].image = owned_image; s->modules[i].image_size = image_size;
    s->modules[i].live = 1;
    int st = register_object(s, MLU_OBJECT_MODULE, &mid, &mg);
    s->modules[i].module_id = mid; s->modules[i].generation = mg;
    if (!st) st = mlu_backend_module_get_kernel(s, mid, kernel_name, &kid);
    if (st) {
      if (cnModuleUnload(m) == CN_SUCCESS) {
        free(owned_image); mlu_resource_release(&s->usage, MLU_RESOURCE_IMAGE, image_size);
        memset(&s->modules[i], 0, sizeof(s->modules[i]));
      }
      mlu_object_registry_release(&s->objects, MLU_OBJECT_MODULE, mid);
      return st;
    }
    *module_id = mid; *kernel_id = kid; return 0;
  }
  mlu_resource_release(&s->usage, MLU_RESOURCE_IMAGE, image_size);
  return (int)CN_ERROR_SYSTEM_OUT_OF_MEMORY;
}

int mlu_backend_operator_create(mlu_backend_session *s, uint32_t operator_id,
                                uint64_t *module_id, uint64_t *kernel_id) {
  return load_operator_module(s, find_operator_descriptor(operator_id), module_id, kernel_id);
}

int mlu_backend_operator_destroy(mlu_backend_session *s, uint32_t operator_id) {
  mlu_backend_module *m = find_module_by_operator(s, operator_id);
  if (!m) return (int)CN_ERROR_INVALID_HANDLE;
  return mlu_backend_module_unload(s, m->module_id);
}

int mlu_backend_module_unload(mlu_backend_session *s, uint64_t module_id) {
  mlu_backend_module *m = find_module_by_id(s, module_id);
  if (!m) return (int)CN_ERROR_INVALID_HANDLE;
  for (size_t i = 0; i < MLU_BACKEND_MAX_OBJECTS; ++i)
    if (s->queues[i].live) {
      CNresult r = cnQueueSync(s->queues[i].handle);
      if (r != CN_SUCCESS) return (int)r;
    }
  if (s->default_queue_used) { CNresult r=cnQueueSync(NULL); if(r) return (int)r; }
  release_staging(s, 0);
  int retired_status = reap_retired_queues(s);
  if (retired_status) return retired_status;
  if(m->graph_refs) {
    m->retired=1;
    invalidate_module_kernels(s,module_id);
    mlu_object_registry_release(&s->objects,MLU_OBJECT_MODULE,m->module_id);
    mlu_object_registry_release(&s->objects,MLU_OBJECT_KERNEL,m->kernel_id);
    return 0;
  }
  CNresult r = cnModuleUnload(m->module);
  if (r == CN_SUCCESS) {
    invalidate_module_kernels(s, module_id);
    m->live = 0;
    free(m->image); m->image = NULL;
    mlu_resource_release(&s->usage, MLU_RESOURCE_IMAGE, m->image_size);
    mlu_object_registry_release(&s->objects, MLU_OBJECT_MODULE, m->module_id);
    mlu_object_registry_release(&s->objects, MLU_OBJECT_KERNEL, m->kernel_id);
  }
  return (int)r;
}

static int tensor_span(const mlu_backend_tensor *t, uint64_t *bytes) {
  if (!t || !bytes || t->rank == 0 || t->rank > MLU_REMOTE_MAX_RANK ||
      t->datatype != MLU_REMOTE_DTYPE_F32 ||
      (t->layout != MLU_REMOTE_LAYOUT_ANY && t->layout != MLU_REMOTE_LAYOUT_ROW_MAJOR))
    return (int)CN_ERROR_INVALID_VALUE;
  uint64_t elements = 1;
  for (uint32_t i = 0; i < t->rank; ++i) {
    if (!t->shape[i] || !t->stride[i]) return (int)CN_ERROR_INVALID_VALUE;
    if (t->shape[i] > (UINT64_MAX / elements)) return (int)CN_ERROR_INVALID_VALUE;
    if ((t->shape[i] - 1) > (UINT64_MAX - elements) / t->stride[i]) return (int)CN_ERROR_INVALID_VALUE;
    elements += (t->shape[i] - 1) * t->stride[i];
  }
  if (elements > UINT64_MAX / sizeof(float)) return (int)CN_ERROR_INVALID_VALUE;
  *bytes = elements * sizeof(float);
  return 0;
}
static int tensor_address(mlu_backend_session *s, const mlu_backend_tensor *t, CNaddr *address) {
  uint64_t bytes = 0; if (tensor_span(t, &bytes)) return (int)CN_ERROR_INVALID_VALUE;
  return range(find_alloc(s, t->allocation_id), t->offset, bytes, address);
}
static int check_tensor_group(const mlu_backend_tensor *tensors, uint32_t count,
                              uint32_t rank, const uint64_t *shape) {
  if (!tensors || count == 0) return (int)CN_ERROR_INVALID_VALUE;
  for (uint32_t j = 0; j < count; ++j) {
    const mlu_backend_tensor *t = &tensors[j];
    if (t->rank != rank) return (int)CN_ERROR_INVALID_VALUE;
    for (uint32_t i = 0; i < rank; ++i) if (t->shape[i] != shape[i]) return (int)CN_ERROR_INVALID_VALUE;
  }
  return 0;
}

int mlu_backend_operator_launch(mlu_backend_session *s,
                                const mlu_backend_operator_request *request,
                                uint64_t *transfer_id) {
  if (!request || !transfer_id) return (int)CN_ERROR_INVALID_VALUE;
  const operator_descriptor *desc = find_operator_descriptor(request->operator_id);
  mlu_backend_module *m = find_module_by_kernel(s, request->kernel_id);
  mlu_backend_queue *q = find_queue(s, request->queue_id);
  if (!desc || !m || m->operator_id != request->operator_id || !q)
    return (int)CN_ERROR_INVALID_HANDLE;
  if (request->input_count != desc->inputs || request->output_count != desc->outputs ||
      request->scalar_bytes != desc->scalar_bytes || !request->inputs || !request->outputs ||
      !request->grid_x || !request->block_x || !request->block_y || !request->block_z) return (int)CN_ERROR_INVALID_VALUE;
  uint64_t shape[MLU_REMOTE_MAX_RANK] = {0};
  for (uint32_t i = 0; i < desc->rank; ++i) shape[i] = request->inputs[0].shape[i];
  if (check_tensor_group(request->inputs, request->input_count, desc->rank, shape) ||
      check_tensor_group(request->outputs, request->output_count, desc->rank, shape))
    return (int)CN_ERROR_INVALID_VALUE;
  CNaddr out_addr = 0, in0_addr = 0, in1_addr = 0;
  if (tensor_address(s, &request->outputs[0], &out_addr) ||
      tensor_address(s, &request->inputs[0], &in0_addr)) return (int)CN_ERROR_INVALID_VALUE;
  if (request->input_count > 1 && tensor_address(s, &request->inputs[1], &in1_addr))
    return (int)CN_ERROR_INVALID_VALUE;
  for (uint32_t i = 0; i < desc->rank; ++i)
    if (shape[i] > INT_MAX) return (int)CN_ERROR_INVALID_VALUE;
  uint32_t gx = request->grid_x, gy = request->grid_y ? request->grid_y : 1, gz = request->grid_z ? request->grid_z : 1;
  (void)request->block_x; (void)request->block_y; (void)request->block_z;
  CNresult r;
  int capacity = transfer_capacity(s); if (capacity) return capacity;
  if (request->operator_id == MLU_REMOTE_OPERATOR_VECTOR_ADD) {
    int n = (int)shape[0]; void *params[] = {&out_addr, &in0_addr, &in1_addr, &n};
    r = cnInvokeKernel(m->kernel, gx, gy, gz, CN_KERNEL_CLASS_BLOCK, 0, q->handle, params, NULL);
  } else if (request->operator_id == MLU_REMOTE_OPERATOR_SCALAR_ADD) {
    if (request->scalars == NULL || request->scalar_bytes != 4) return (int)CN_ERROR_INVALID_VALUE;
    uint32_t bits; memcpy(&bits, request->scalars, 4); bits = ntohl(bits); float scalar; memcpy(&scalar, &bits, 4);
    int n = (int)shape[0]; void *params[] = {&out_addr, &in0_addr, &scalar, &n};
    r = cnInvokeKernel(m->kernel, gx, gy, gz, CN_KERNEL_CLASS_BLOCK, 0, q->handle, params, NULL);
  } else if (request->operator_id == MLU_REMOTE_OPERATOR_SCALAR_MUL) {
    if (request->scalars == NULL || request->scalar_bytes != 4) return (int)CN_ERROR_INVALID_VALUE;
    uint32_t bits; memcpy(&bits, request->scalars, 4); bits = ntohl(bits); float scalar; memcpy(&scalar, &bits, 4);
    int n = (int)shape[0]; void *params[] = {&out_addr, &in0_addr, &scalar, &n};
    r = cnInvokeKernel(m->kernel, gx, gy, gz, CN_KERNEL_CLASS_BLOCK, 0, q->handle, params, NULL);
  } else if (request->operator_id == MLU_REMOTE_OPERATOR_RELU) {
    int n = (int)shape[0]; void *params[] = {&out_addr, &in0_addr, &n};
    r = cnInvokeKernel(m->kernel, gx, gy, gz, CN_KERNEL_CLASS_BLOCK, 0, q->handle, params, NULL);
  } else if (request->operator_id == MLU_REMOTE_OPERATOR_TENSOR_ADD_STRIDED) {
    const mlu_backend_tensor *o = &request->outputs[0], *a = &request->inputs[0], *b = &request->inputs[1];
    int rows = (int)shape[0], cols = (int)shape[1];
    int dst_rs = (int)o->stride[0], dst_cs = (int)o->stride[1];
    int a_rs = (int)a->stride[0], a_cs = (int)a->stride[1];
    int b_rs = (int)b->stride[0], b_cs = (int)b->stride[1];
    void *params[] = {&out_addr, &in0_addr, &in1_addr, &rows, &cols, &dst_rs, &dst_cs, &a_rs, &a_cs, &b_rs, &b_cs};
    r = cnInvokeKernel(m->kernel, gx, gy, gz, CN_KERNEL_CLASS_BLOCK, 0, q->handle, params, NULL);
  } else if (request->operator_id == MLU_REMOTE_OPERATOR_TENSOR_MUL_STRIDED) {
    const mlu_backend_tensor *o = &request->outputs[0], *a = &request->inputs[0], *b = &request->inputs[1];
    int rows = (int)shape[0], cols = (int)shape[1];
    int dst_rs = (int)o->stride[0], dst_cs = (int)o->stride[1];
    int a_rs = (int)a->stride[0], a_cs = (int)a->stride[1];
    int b_rs = (int)b->stride[0], b_cs = (int)b->stride[1];
    void *params[] = {&out_addr, &in0_addr, &in1_addr, &rows, &cols, &dst_rs, &dst_cs, &a_rs, &a_cs, &b_rs, &b_cs};
    r = cnInvokeKernel(m->kernel, gx, gy, gz, CN_KERNEL_CLASS_BLOCK, 0, q->handle, params, NULL);
  } else return (int)MLU_REMOTE_UNSUPPORTED;
  if (r != CN_SUCCESS) return (int)r;
  int tr = record_transfer(s, request->queue_id, 1, transfer_id);
  if (tr != 0) {
    if (cnQueueSync(q->handle) == CN_SUCCESS) release_staging(s, request->queue_id);
    return tr;
  }
  return 0;
}

int mlu_backend_kernel_launch_generic(mlu_backend_session *s,
                                       uint64_t module_id, uint64_t kernel_id,
                                       uint64_t queue_id, uint32_t dim_x,
                                       uint32_t dim_y, uint32_t dim_z,
                                       uint32_t kernel_class,
                                       const mlu_backend_kernel_arg *args,
                                       uint32_t arg_count, uint64_t *transfer_id) {
  if (!s || !transfer_id || arg_count > 256 || (arg_count && !args) ||
      !dim_x || !dim_y || !dim_z)
    return (int)CN_ERROR_INVALID_VALUE;
  mlu_backend_module *m = find_module_by_id(s, module_id);
  mlu_backend_kernel *k = find_generic_kernel(s, kernel_id);
  mlu_backend_queue *q = queue_id ? find_queue(s, queue_id) : NULL;
  if (!m || !k || (queue_id && !q) || k->module_id != module_id) return (int)CN_ERROR_INVALID_HANDLE;
  if (!m->generic_image || arg_count != k->params.count) return CN_ERROR_INVALID_VALUE;
  void **params = arg_count ? calloc(arg_count, sizeof(*params)) : NULL;
  CNaddr *device_values = arg_count ? calloc(arg_count, sizeof(*device_values)) : NULL;
  if (arg_count && (!params || !device_values)) {
    free(params); free(device_values); return CN_ERROR_SYSTEM_OUT_OF_MEMORY;
  }
  size_t scalar_bytes = 0;
  for (uint32_t i = 0; i < arg_count; ++i) {
    if ((args[i].kind < 1 || args[i].kind > 3) || !args[i].size || args[i].size > 4096 ||
        args[i].size != k->params.sizes[i])
      { free(params); free(device_values); return (int)CN_ERROR_INVALID_VALUE; }
    if (args[i].kind == 1 && args[i].size != sizeof(CNaddr))
      { free(params); free(device_values); return (int)CN_ERROR_INVALID_VALUE; }
    if (args[i].kind != 1) {
      if (!args[i].scalar || scalar_bytes > SIZE_MAX - args[i].size) { free(params); free(device_values); return (int)CN_ERROR_INVALID_VALUE; }
      scalar_bytes += (args[i].size + 15u) & ~15u;
    }
  }
  unsigned char *scalars = scalar_bytes ? calloc(1, scalar_bytes) : NULL;
  if (scalar_bytes && !scalars) { free(params); free(device_values); return (int)CN_ERROR_SYSTEM_OUT_OF_MEMORY; }
  size_t scalar_cursor = 0;
  for (uint32_t i = 0; i < arg_count; ++i) {
    if (args[i].kind == 1) {
      /* A kernel pointer is valid when it points inside an owned allocation;
       * the allocation size is intentionally not guessed from user bytes. */
      if (!args[i].allocation_id && !args[i].offset) { params[i] = &device_values[i]; continue; }
      mlu_backend_allocation *a = find_alloc(s, args[i].allocation_id);
      if (!a || args[i].offset > a->bytes) { free(scalars); free(params); free(device_values); return (int)CN_ERROR_INVALID_VALUE; }
      device_values[i] = a->address + args[i].offset; params[i] = &device_values[i];
    } else {
      memcpy(scalars + scalar_cursor, args[i].scalar, args[i].size);
      if(args[i].kind==3) {
        uint32_t previous=0;
        if(!args[i].relocation_count || args[i].relocation_count>BANG_MAX_RELOCS || !args[i].relocations) {
          free(scalars);free(params);free(device_values);return CN_ERROR_INVALID_VALUE;
        }
        for(uint32_t j=0;j<args[i].relocation_count;++j) {
          const unsigned char *r=args[i].relocations+j*24;
          uint32_t off=0,reserved=0; uint64_t aid=0,offset=0,value=0;
          memcpy(&off,r,4);off=ntohl(off);memcpy(&reserved,r+4,4);
          aid=read_be64(r+8);offset=read_be64(r+16);
          if(off>args[i].size || args[i].size-off<8 || off%8 || reserved || (j && off<previous+8)) {
            free(scalars);free(params);free(device_values);return CN_ERROR_INVALID_VALUE;
          }
          memcpy(&value,scalars+scalar_cursor+off,8);
          mlu_backend_allocation *a=aid ? find_alloc(s,aid) : NULL;
          if(value || (aid ? !a || offset>a->bytes : offset!=0)) {
            free(scalars);free(params);free(device_values);return CN_ERROR_INVALID_VALUE;
          }
          value=a ? a->address+offset : 0;
          memcpy(scalars+scalar_cursor+off,&value,8); previous=off;
        }
      }
      params[i] = scalars + scalar_cursor; scalar_cursor += (args[i].size + 15u) & ~15u;
    }
  }
  KernelClass klass = (KernelClass)kernel_class;
  if (klass != CN_KERNEL_CLASS_BLOCK && klass != CN_KERNEL_CLASS_UNION &&
      klass != CN_KERNEL_CLASS_UNION2 && klass != CN_KERNEL_CLASS_UNION4 &&
      klass != CN_KERNEL_CLASS_UNION8 && klass != CN_KERNEL_CLASS_UNION16)
    { free(scalars); free(params); free(device_values); return (int)CN_ERROR_INVALID_VALUE; }
  int capacity = transfer_capacity(s);
  if (capacity) { free(scalars); free(params); free(device_values); return capacity; }
  if (!queue_id) s->default_queue_used=1;
  CNresult r = cnInvokeKernel(k->handle, dim_x, dim_y, dim_z, klass, 0,
                              q ? q->handle : NULL, params, NULL);
  free(scalars); free(params); free(device_values);
  if (r != CN_SUCCESS) return (int)r;
  int st = record_transfer(s, queue_id, 1, transfer_id);
  if (st != 0) {
    if (cnQueueSync(q ? q->handle : NULL) == CN_SUCCESS && queue_id) release_staging(s, queue_id);
    return st;
  }
  return 0;
}

int mlu_backend_kernel_launch_vector_add(mlu_backend_session *s, uint64_t kernel_id,
                                         uint64_t queue_id, uint64_t dst_id,
                                         uint64_t a_id, uint64_t b_id,
                                         uint32_t elements, uint32_t grid_x) {
  mlu_backend_tensor in[2] = {{a_id, 0, 1, MLU_REMOTE_LAYOUT_ROW_MAJOR, MLU_REMOTE_DTYPE_F32, {elements}, {1}},
                              {b_id, 0, 1, MLU_REMOTE_LAYOUT_ROW_MAJOR, MLU_REMOTE_DTYPE_F32, {elements}, {1}}};
  mlu_backend_tensor out = {dst_id, 0, 1, MLU_REMOTE_LAYOUT_ROW_MAJOR, MLU_REMOTE_DTYPE_F32, {elements}, {1}};
  mlu_backend_operator_request req = {MLU_REMOTE_OPERATOR_VECTOR_ADD, kernel_id, queue_id, 2, 1,
                                      grid_x, 1, 1, 1, 1, 1, in, &out, NULL, 0};
  uint64_t tid = 0; return mlu_backend_operator_launch(s, &req, &tid);
}

const char *mlu_backend_error_name(int code) {
  if ((unsigned)code == MLU_REMOTE_RESOURCE_LIMIT) return "REMOTE_RESOURCE_LIMIT";
  const char *s = NULL; if (cnGetErrorName((CNresult)code, &s) == CN_SUCCESS && s) return s;
  switch ((unsigned)code) { case MLU_REMOTE_UNSUPPORTED: return "REMOTE_UNSUPPORTED"; default: return "REMOTE_PROTOCOL_ERROR"; }
}
const char *mlu_backend_error_string(int code) {
  if ((unsigned)code == MLU_REMOTE_RESOURCE_LIMIT) return "server resource quota exceeded; request not submitted";
  const char *s = NULL; if (cnGetErrorString((CNresult)code, &s) == CN_SUCCESS && s) return s;
  switch ((unsigned)code) { case MLU_REMOTE_UNSUPPORTED: return "backend capability is unavailable"; default: return "remote protocol error"; }
}

/* Ordinary linear fills share the owned allocation range and native queue
 * mapping. Count is in elements, not bytes, for D16/D32. */
static int mlu_backend_memset(mlu_backend_session *s,uint64_t id,uint64_t off,
                              uint32_t value,uint32_t width,uint64_t count,
                              uint64_t qid,uint32_t async) {
  if((width!=1 && width!=2 && width!=4) || async>1 || count>UINT64_MAX/width || off%width)
    return (int)CN_ERROR_INVALID_VALUE;
  CNaddr dst; int st=range(find_alloc(s,id),off,count*width,&dst); if(st) return st;
  mlu_backend_queue *q=qid ? find_queue(s,qid) : NULL;
  if(qid && !q) return (int)MLU_REMOTE_BAD_OBJECT;
  if(async && !qid) s->default_queue_used=1;
  if(width==1) return (int)(async ? cnMemsetD8Async(dst,(unsigned char)value,count,q ? q->handle : NULL) : cnMemsetD8(dst,(unsigned char)value,count));
  if(width==2) return (int)(async ? cnMemsetD16Async(dst,(unsigned short)value,count,q ? q->handle : NULL) : cnMemsetD16(dst,(unsigned short)value,count));
  return (int)(async ? cnMemsetD32Async(dst,value,count,q ? q->handle : NULL) : cnMemsetD32(dst,value,count));
}
static int mlu_backend_copy2d(mlu_backend_session *s,const mlu_api_copy2d_request *in) {
  if(!in->height || !in->width || in->width>in->dpitch || in->width>in->spitch ||
      (in->height-1)>(UINT64_MAX-in->width)/in->dpitch ||
      (in->height-1)>(UINT64_MAX-in->width)/in->spitch) return (int)CN_ERROR_INVALID_VALUE;
  CNaddr dst,src;
  if(range(find_alloc(s,in->dst),in->dst_offset,(in->height-1)*in->dpitch+in->width,&dst) ||
     range(find_alloc(s,in->src),in->src_offset,(in->height-1)*in->spitch+in->width,&src)) return (int)CN_ERROR_INVALID_VALUE;
  return (int)cnMemcpyDtoD2D(dst,in->dpitch,src,in->spitch,in->width,in->height);
}
/* Compute the full allocation span before entering native CE code. */
static int copy3d_span(uint64_t pitch,uint64_t ysize,uint64_t x,uint64_t y,uint64_t z,
                         uint64_t w,uint64_t h,uint64_t d,uint64_t *span) {
  if(!pitch || !ysize || !w || !h || !d || x>pitch || w>pitch-x || y>ysize || h>ysize-y ||
      z>UINT64_MAX-(d-1) || pitch>UINT64_MAX/ysize) return -1;
  uint64_t slice=pitch*ysize, lastz=z+d-1;
  if(lastz>UINT64_MAX/slice) return -1;
  uint64_t base=lastz*slice, row=(y+h-1)*pitch;
  if(row>UINT64_MAX-base || x+w>UINT64_MAX-base-row) return -1;
  *span=base+row+x+w; return 0;
}
static int mlu_backend_copy3d(mlu_backend_session *s,const mlu_api_copy3d_request *in) {
  uint64_t ds,ss; CNaddr dst,src;
  if(copy3d_span(in->dpitch,in->dysize,in->dx,in->dy,in->dz,in->width,in->height,in->depth,&ds) ||
     copy3d_span(in->spitch,in->sysize,in->sx,in->sy,in->sz,in->width,in->height,in->depth,&ss) ||
     range(find_alloc(s,in->dst),in->dst_offset,ds,&dst) || range(find_alloc(s,in->src),in->src_offset,ss,&src))
    return (int)CN_ERROR_INVALID_VALUE;
  CNmemcpy3dParam p={0};
  p.dst=(void *)(uintptr_t)dst; p.src=(void *)(uintptr_t)src;
  p.dstPtr=(CNpitchedPtr){in->dpitch,(void *)(uintptr_t)dst,in->width,in->dysize};
  p.srcPtr=(CNpitchedPtr){in->spitch,(void *)(uintptr_t)src,in->width,in->sysize};
  p.dstPos=(CNpos){in->dx,in->dy,in->dz}; p.srcPos=(CNpos){in->sx,in->sy,in->sz};
  p.extent=(CNextent){in->depth,in->height,in->width};
  return (int)cnMemcpyDtoD3D(&p);
}
#include "graph_backend.inc"
#include "api_queries.inc"
#include "typed_backend.inc"
