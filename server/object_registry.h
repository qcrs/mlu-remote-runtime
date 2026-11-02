#ifndef MLU_REMOTE_OBJECT_REGISTRY_H
#define MLU_REMOTE_OBJECT_REGISTRY_H

#include <stdint.h>
#include <stddef.h>

#define MLU_OBJECT_REGISTRY_MAX 8192

enum mlu_object_type {
  MLU_OBJECT_ALLOCATION = 1,
  MLU_OBJECT_QUEUE = 2,
  MLU_OBJECT_NOTIFIER = 3,
  MLU_OBJECT_MODULE = 4,
  MLU_OBJECT_KERNEL = 5,
  MLU_OBJECT_TRANSFER = 6,
};

typedef struct {
  uint64_t id;
  uint64_t owner_token;
  uint32_t generation;
  uint16_t type;
  int live;
} mlu_object_entry;

typedef struct {
  uint64_t owner_token;
  uint64_t next_id;
  uint32_t generation;
  mlu_object_entry entries[MLU_OBJECT_REGISTRY_MAX];
} mlu_object_registry;

void mlu_object_registry_init(mlu_object_registry *registry,
                              uint64_t owner_token, uint64_t id_seed);
int mlu_object_registry_register(mlu_object_registry *registry,
                                 uint16_t type, uint64_t *id,
                                 uint32_t *generation);
int mlu_object_registry_contains(const mlu_object_registry *registry,
                                 uint16_t type, uint64_t id);
void mlu_object_registry_release(mlu_object_registry *registry,
                                 uint16_t type, uint64_t id);
void mlu_object_registry_clear(mlu_object_registry *registry);

#endif
