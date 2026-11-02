#include "mlu_backend.h"
#include "remote_protocol.h"
#include "operator_registry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <limits.h>
#include <unistd.h>

static int backend_ready;
static uint64_t next_session_owner;

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
    if (s->allocations[i].live && s->allocations[i].owner == s->owner_token &&
        s->allocations[i].id == id && mlu_object_registry_contains(&s->objects, MLU_OBJECT_ALLOCATION, id)) return &s->allocations[i];
  return NULL;
}
static mlu_backend_queue *find_queue(mlu_backend_session *s, uint64_t id) {
  for (size_t i = 0; i < MLU_BACKEND_MAX_OBJECTS; ++i)
    if (s->queues[i].live && s->queues[i].owner == s->owner_token &&
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
    if (s->modules[i].live && s->modules[i].owner == s->owner_token && s->modules[i].operator_id == id) return &s->modules[i];
  return NULL;
}
static mlu_backend_module *find_module_by_id(mlu_backend_session *s, uint64_t id) {
  for (size_t i = 0; i < MLU_BACKEND_MAX_OBJECTS; ++i)
    if (s->modules[i].live && s->modules[i].owner == s->owner_token &&
        s->modules[i].module_id == id && mlu_object_registry_contains(&s->objects, MLU_OBJECT_MODULE, id)) return &s->modules[i];
  return NULL;
}
static int record_transfer(mlu_backend_session *s, uint64_t queue_id, uint64_t *transfer_id) {
  if (!transfer_id) return (int)CN_ERROR_INVALID_VALUE;
  size_t slot = MLU_BACKEND_MAX_OBJECTS;
  for (size_t i = 0; i < MLU_BACKEND_MAX_OBJECTS; ++i)
    if (!s->transfers[i].live) { slot = i; break; }
  if (slot == MLU_BACKEND_MAX_OBJECTS) return (int)CN_ERROR_SYSTEM_OUT_OF_MEMORY;
  CNnotifier notifier = NULL;
  if (queue_id) {
    mlu_backend_queue *q = find_queue(s, queue_id);
    if (!q) return (int)CN_ERROR_INVALID_HANDLE;
    CNresult r = cnCreateNotifier(&notifier, 0);
    if (r != CN_SUCCESS) return (int)r;
    r = cnPlaceNotifier(notifier, q->handle);
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
      .notifier_live = queue_id ? 1 : 0, .live = 1};
  *transfer_id = s->transfers[slot].id;
  return 0;
}
static mlu_backend_transfer *find_transfer(mlu_backend_session *s, uint64_t id) {
  for (size_t i = 0; i < MLU_BACKEND_MAX_OBJECTS; ++i)
    if (s->transfers[i].live && s->transfers[i].owner == s->owner_token &&
        s->transfers[i].id == id && mlu_object_registry_contains(&s->objects, MLU_OBJECT_TRANSFER, id)) return &s->transfers[i];
  return NULL;
}
static void destroy_transfer_notifiers(mlu_backend_session *s) {
  for (size_t i = 0; i < MLU_BACKEND_MAX_OBJECTS; ++i) {
    if (s->transfers[i].live && s->transfers[i].notifier_live) {
      cnDestroyNotifier(s->transfers[i].notifier);
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
      free(s->staging[i].host); s->staging[i].live = 0;
    }
}

int mlu_backend_session_init(mlu_backend_session *s, int ordinal) {
  memset(s, 0, sizeof(*s));
  s->device_ordinal = ordinal;
  s->owner_token = __sync_add_and_fetch(&next_session_owner, 1);
  int r = mlu_backend_global_init();
  if (r) return r;
  CNresult cr = cnDeviceGet(&s->device, ordinal);
  if (cr != CN_SUCCESS) return (int)cr;
  cr = cnCtxCreate(&s->context, CN_CTX_SCHED_SYNC_WAIT, s->device);
  if (cr != CN_SUCCESS) return (int)cr;
  cr = cnCtxSetCurrent(s->context);
  if (cr != CN_SUCCESS) {
    cnCtxDestroy(s->context);
    s->context = NULL;
    return (int)cr;
  }
  /* Include a process-local session generation in every opaque ID. This keeps
   * an ID from a disconnected session from aliasing a new session's slot. */
  s->next_id = (s->owner_token << 32) | 1000u;
  mlu_object_registry_init(&s->objects, s->owner_token, s->next_id);
  return 0;
}

static int session_has_resources(const mlu_backend_session *s) {
  for (size_t i = 0; i < MLU_BACKEND_MAX_OBJECTS; ++i)
    if (s->allocations[i].live || s->queues[i].live || s->notifiers[i].live ||
        s->modules[i].live || s->staging[i].live || s->transfers[i].live)
      return 1;
  return 0;
}

int mlu_backend_session_select_device(mlu_backend_session *s, int ordinal) {
  if (!s || ordinal < 0) return (int)CN_ERROR_INVALID_DEVICE;
  int count = 0;
  int st = mlu_backend_device_count(&count);
  if (st != 0) return st;
  if (ordinal >= count) return (int)CN_ERROR_INVALID_DEVICE;
  if (ordinal == s->device_ordinal) return 0;
  if (session_has_resources(s)) return (int)CN_OPS_ERROR_NOT_PERMITTED;
  mlu_backend_session_cleanup(s);
  return mlu_backend_session_init(s, ordinal);
}

int mlu_backend_session_reset(mlu_backend_session *s) {
  if (!s) return (int)CN_ERROR_INVALID_VALUE;
  int ordinal = s->device_ordinal;
  mlu_backend_session_cleanup(s);
  return mlu_backend_session_init(s, ordinal);
}

void mlu_backend_session_cleanup(mlu_backend_session *s) {
  if (!s) return;
  if (s->context) cnCtxSetCurrent(s->context);
  for (size_t i = 0; i < MLU_BACKEND_MAX_OBJECTS; ++i) {
    if (s->queues[i].live) cnQueueSync(s->queues[i].handle);
  }
  release_staging(s, 0);
  destroy_transfer_notifiers(s);
  for (size_t i = 0; i < MLU_BACKEND_MAX_OBJECTS; ++i)
    if (s->modules[i].live) { cnModuleUnload(s->modules[i].module); s->modules[i].live = 0; }
  for (size_t i = 0; i < MLU_BACKEND_MAX_OBJECTS; ++i)
    if (s->notifiers[i].live) { cnDestroyNotifier(s->notifiers[i].handle); s->notifiers[i].live = 0; }
  for (size_t i = 0; i < MLU_BACKEND_MAX_OBJECTS; ++i)
    if (s->queues[i].live) { cnDestroyQueue(s->queues[i].handle); s->queues[i].live = 0; }
  for (size_t i = 0; i < MLU_BACKEND_MAX_OBJECTS; ++i)
    if (s->allocations[i].live) { cnFree(s->allocations[i].address); s->allocations[i].live = 0; }
  mlu_object_registry_clear(&s->objects);
  if (s->context) cnCtxDestroy(s->context);
  memset(s, 0, sizeof(*s));
}

int mlu_backend_alloc(mlu_backend_session *s, uint64_t bytes, uint64_t *id) {
  if (!bytes || !id) return (int)CN_ERROR_INVALID_VALUE;
  for (size_t i = 0; i < MLU_BACKEND_MAX_OBJECTS; ++i) if (!s->allocations[i].live) {
    CNaddr p = 0;
    CNresult r = cnMalloc(&p, bytes);
    if (r != CN_SUCCESS) return (int)r;
    uint64_t object_id = 0; uint32_t generation = 0;
    int registry_status = register_object(s, MLU_OBJECT_ALLOCATION, &object_id, &generation);
    if (registry_status != 0) { cnFree(p); return registry_status; }
    s->allocations[i] = (mlu_backend_allocation){.id = object_id, .address = p, .bytes = bytes,
                                                .owner = s->owner_token, .generation = generation, .live = 1};
    *id = s->allocations[i].id;
    return 0;
  }
  return (int)CN_ERROR_SYSTEM_OUT_OF_MEMORY;
}

int mlu_backend_free(mlu_backend_session *s, uint64_t id) {
  mlu_backend_allocation *a = find_alloc(s, id);
  if (!a) return (int)CN_ERROR_INVALID_HANDLE;
  for (size_t i = 0; i < MLU_BACKEND_MAX_OBJECTS; ++i)
    if (s->queues[i].live) { CNresult qr = cnQueueSync(s->queues[i].handle); if (qr != CN_SUCCESS) return (int)qr; }
  release_staging(s, 0);
  CNresult r = cnFree(a->address);
  if (r == CN_SUCCESS) { a->live = 0; mlu_object_registry_release(&s->objects, MLU_OBJECT_ALLOCATION, id); }
  return (int)r;
}

static int range(mlu_backend_allocation *a, uint64_t off, uint64_t bytes, CNaddr *out) {
  if (!a || off > a->bytes || bytes > a->bytes - off) return (int)CN_ERROR_INVALID_VALUE;
  *out = a->address + off;
  return 0;
}

int mlu_backend_memcpy(mlu_backend_session *s, int direction,
                       uint64_t dst_id, uint64_t dst_offset,
                       uint64_t src_id, uint64_t src_offset,
                       const void *host_src, void *host_dst, uint64_t bytes,
                       uint64_t queue_id, uint64_t *transfer_id) {
  if (!bytes) return (int)CN_ERROR_INVALID_VALUE;
  mlu_backend_queue *q = queue_id ? find_queue(s, queue_id) : NULL;
  if (queue_id && !q) return (int)CN_ERROR_INVALID_HANDLE;
  CNaddr dst = 0, src = 0;
  CNresult r;
  if (direction == MLU_MEMCPY_H2D) {
    if (range(find_alloc(s, dst_id), dst_offset, bytes, &dst)) return (int)CN_ERROR_INVALID_VALUE;
    if (!host_src) return (int)CN_ERROR_INVALID_ADDRESS;
    if (q) {
      size_t staging_slot = MLU_BACKEND_MAX_OBJECTS;
      for (size_t i = 0; i < MLU_BACKEND_MAX_OBJECTS; ++i)
        if (!s->staging[i].live) { staging_slot = i; break; }
      if (staging_slot == MLU_BACKEND_MAX_OBJECTS) return (int)CN_ERROR_SYSTEM_OUT_OF_MEMORY;
      void *staging = malloc((size_t)bytes);
      if (!staging) return (int)CN_ERROR_SYSTEM_OUT_OF_MEMORY;
      memcpy(staging, host_src, (size_t)bytes);
      r = cnMemcpyHtoDAsync(dst, staging, bytes, q->handle);
      if (r == CN_SUCCESS) {
        s->staging[staging_slot] = (mlu_backend_staging){.queue_id = queue_id, .host = staging,
                                                         .owner = s->owner_token, .generation = new_generation(s), .live = 1};
        staging = NULL;
      }
      if (staging) free(staging);
    } else r = cnMemcpyHtoD(dst, host_src, bytes);
  } else if (direction == MLU_MEMCPY_D2H) {
    if (range(find_alloc(s, src_id), src_offset, bytes, &src)) return (int)CN_ERROR_INVALID_VALUE;
    if (!host_dst) return (int)CN_ERROR_INVALID_ADDRESS;
    r = q ? cnMemcpyDtoHAsync(host_dst, src, bytes, q->handle) : cnMemcpyDtoH(host_dst, src, bytes);
    if (r == CN_SUCCESS && q) r = cnQueueSync(q->handle);
  } else if (direction == MLU_MEMCPY_D2D) {
    if (range(find_alloc(s, dst_id), dst_offset, bytes, &dst)) return (int)CN_ERROR_INVALID_VALUE;
    if (range(find_alloc(s, src_id), src_offset, bytes, &src)) return (int)CN_ERROR_INVALID_VALUE;
    r = q ? cnMemcpyDtoDAsync(dst, src, bytes, q->handle) : cnMemcpyDtoD(dst, src, bytes);
  } else return (int)CN_ERROR_INVALID_VALUE;
  if (transfer_id && r == CN_SUCCESS) {
    int tr = record_transfer(s, queue_id, transfer_id);
    if (tr != 0) {
      if (q) { cnQueueSync(q->handle); release_staging(s, queue_id); }
      return tr;
    }
  }
  return (int)r;
}

int mlu_backend_queue_create(mlu_backend_session *s, uint64_t *id) {
  for (size_t i = 0; i < MLU_BACKEND_MAX_OBJECTS; ++i) if (!s->queues[i].live) {
    CNqueue q = NULL; CNresult r = cnCreateQueue(&q, 0); if (r != CN_SUCCESS) return (int)r;
    uint64_t object_id = 0; uint32_t generation = 0;
    int registry_status = register_object(s, MLU_OBJECT_QUEUE, &object_id, &generation);
    if (registry_status != 0) { cnDestroyQueue(q); return registry_status; }
    s->queues[i] = (mlu_backend_queue){.id = object_id, .handle = q, .owner = s->owner_token,
                                       .generation = generation, .live = 1}; *id = s->queues[i].id; return 0;
  }
  return (int)CN_ERROR_SYSTEM_OUT_OF_MEMORY;
}
int mlu_backend_queue_destroy(mlu_backend_session *s, uint64_t id) {
  mlu_backend_queue *q = find_queue(s, id); if (!q) return (int)CN_ERROR_INVALID_HANDLE;
  CNresult r = cnQueueSync(q->handle); if (r == CN_SUCCESS) release_staging(s, id);
  if (r == CN_SUCCESS) r = cnDestroyQueue(q->handle); if (r == CN_SUCCESS) { q->live = 0; mlu_object_registry_release(&s->objects, MLU_OBJECT_QUEUE, id); } return (int)r;
}
int mlu_backend_queue_sync(mlu_backend_session *s, uint64_t id) {
  mlu_backend_queue *q = find_queue(s, id); if (!q) return (int)CN_ERROR_INVALID_HANDLE;
  CNresult r = cnQueueSync(q->handle); if (r == CN_SUCCESS) release_staging(s, id); return (int)r;
}
int mlu_backend_queue_query(mlu_backend_session *s, uint64_t id) {
  mlu_backend_queue *q = find_queue(s, id); if (!q) return (int)CN_ERROR_INVALID_HANDLE;
  return (int)cnQueryQueue(q->handle);
}
int mlu_backend_notifier_create(mlu_backend_session *s, uint64_t *id) {
  for (size_t i = 0; i < MLU_BACKEND_MAX_OBJECTS; ++i) if (!s->notifiers[i].live) {
    CNnotifier n = NULL; CNresult r = cnCreateNotifier(&n, 0); if (r != CN_SUCCESS) return (int)r;
    uint64_t object_id = 0; uint32_t generation = 0;
    int registry_status = register_object(s, MLU_OBJECT_NOTIFIER, &object_id, &generation);
    if (registry_status != 0) { cnDestroyNotifier(n); return registry_status; }
    s->notifiers[i] = (mlu_backend_notifier){.id = object_id, .handle = n, .owner = s->owner_token,
                                             .generation = generation, .live = 1}; *id = s->notifiers[i].id; return 0;
  }
  return (int)CN_ERROR_SYSTEM_OUT_OF_MEMORY;
}
int mlu_backend_notifier_destroy(mlu_backend_session *s, uint64_t id) {
  mlu_backend_notifier *n = find_notifier(s, id); if (!n) return (int)CN_ERROR_INVALID_HANDLE;
  CNresult r = cnDestroyNotifier(n->handle); if (r == CN_SUCCESS) { n->live = 0; mlu_object_registry_release(&s->objects, MLU_OBJECT_NOTIFIER, id); } return (int)r;
}
int mlu_backend_notifier_record(mlu_backend_session *s, uint64_t nid, uint64_t qid) {
  mlu_backend_notifier *n = find_notifier(s, nid); mlu_backend_queue *q = find_queue(s, qid);
  if (!n || !q) return (int)CN_ERROR_INVALID_HANDLE; return (int)cnPlaceNotifier(n->handle, q->handle);
}
int mlu_backend_notifier_query(mlu_backend_session *s, uint64_t id) {
  mlu_backend_notifier *n = find_notifier(s, id); if (!n) return (int)CN_ERROR_INVALID_HANDLE;
  return (int)cnQueryNotifier(n->handle);
}
int mlu_backend_notifier_sync(mlu_backend_session *s, uint64_t id) {
  mlu_backend_notifier *n = find_notifier(s, id); if (!n) return (int)CN_ERROR_INVALID_HANDLE;
  return (int)cnWaitNotifier(n->handle);
}
int mlu_backend_transfer_query(mlu_backend_session *s, uint64_t id) {
  mlu_backend_transfer *t = find_transfer(s, id);
  if (!t) return (int)CN_ERROR_INVALID_HANDLE;
  if (!t->notifier_live) { t->state = MLU_BACKEND_OPERATION_COMPLETE; return 0; }
  int st = (int)cnQueryNotifier(t->notifier);
  if (st == CN_SUCCESS) t->state = MLU_BACKEND_OPERATION_COMPLETE;
  return st;
}
int mlu_backend_transfer_sync(mlu_backend_session *s, uint64_t id) {
  mlu_backend_transfer *t = find_transfer(s, id);
  if (!t) return (int)CN_ERROR_INVALID_HANDLE;
  if (!t->notifier_live) { t->state = MLU_BACKEND_OPERATION_COMPLETE; return 0; }
  int st = (int)cnWaitNotifier(t->notifier);
  if (st == CN_SUCCESS) t->state = MLU_BACKEND_OPERATION_COMPLETE;
  else t->state = MLU_BACKEND_OPERATION_FAILED;
  return st;
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

int mlu_backend_operator_create(mlu_backend_session *s, uint32_t operator_id,
                                uint64_t *module_id, uint64_t *kernel_id) {
  return load_operator_module(s, find_operator_descriptor(operator_id), module_id, kernel_id);
}

int mlu_backend_operator_destroy(mlu_backend_session *s, uint32_t operator_id) {
  mlu_backend_module *m = find_module_by_operator(s, operator_id);
  if (!m) return (int)CN_ERROR_INVALID_HANDLE;
  for (size_t i = 0; i < MLU_BACKEND_MAX_OBJECTS; ++i)
    if (s->queues[i].live) {
      CNresult r = cnQueueSync(s->queues[i].handle);
      if (r != CN_SUCCESS) return (int)r;
    }
  release_staging(s, 0);
  CNresult r = cnModuleUnload(m->module);
  if (r == CN_SUCCESS) {
    m->live = 0;
    mlu_object_registry_release(&s->objects, MLU_OBJECT_MODULE, m->module_id);
    mlu_object_registry_release(&s->objects, MLU_OBJECT_KERNEL, m->kernel_id);
  }
  return (int)r;
}

int mlu_backend_module_unload(mlu_backend_session *s, uint64_t module_id) {
  mlu_backend_module *m = find_module_by_id(s, module_id);
  if (!m) return (int)CN_ERROR_INVALID_HANDLE;
  for (size_t i = 0; i < MLU_BACKEND_MAX_OBJECTS; ++i)
    if (s->queues[i].live) {
      CNresult r = cnQueueSync(s->queues[i].handle);
      if (r != CN_SUCCESS) return (int)r;
    }
  release_staging(s, 0);
  CNresult r = cnModuleUnload(m->module);
  if (r == CN_SUCCESS) {
    m->live = 0;
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
  int tr = record_transfer(s, request->queue_id, transfer_id);
  if (tr != 0) {
    cnQueueSync(q->handle);
    release_staging(s, request->queue_id);
    return tr;
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
  const char *s = NULL; if (cnGetErrorName((CNresult)code, &s) == CN_SUCCESS && s) return s;
  switch ((unsigned)code) { case MLU_REMOTE_UNSUPPORTED: return "REMOTE_UNSUPPORTED"; default: return "REMOTE_PROTOCOL_ERROR"; }
}
const char *mlu_backend_error_string(int code) {
  const char *s = NULL; if (cnGetErrorString((CNresult)code, &s) == CN_SUCCESS && s) return s;
  switch ((unsigned)code) { case MLU_REMOTE_UNSUPPORTED: return "backend capability is unavailable"; default: return "remote protocol error"; }
}
