#include "object_registry.h"

#include <string.h>

void mlu_object_registry_init(mlu_object_registry *registry,
                              uint64_t owner_token, uint64_t id_seed) {
  memset(registry, 0, sizeof(*registry));
  registry->owner_token = owner_token;
  registry->next_id = id_seed;
}

int mlu_object_registry_register(mlu_object_registry *registry,
                                 uint16_t type, uint64_t *id,
                                 uint32_t *generation) {
  if (!registry || !id || !generation || type == 0) return -1;
  for (size_t i = 0; i < MLU_OBJECT_REGISTRY_MAX; ++i) {
    if (registry->entries[i].live) continue;
    registry->entries[i].id = ++registry->next_id;
    registry->entries[i].owner_token = registry->owner_token;
    registry->entries[i].generation = ++registry->generation;
    registry->entries[i].type = type;
    registry->entries[i].live = 1;
    *id = registry->entries[i].id;
    *generation = registry->entries[i].generation;
    return 0;
  }
  return -1;
}

int mlu_object_registry_contains(const mlu_object_registry *registry,
                                 uint16_t type, uint64_t id) {
  if (!registry || !id || !type) return 0;
  for (size_t i = 0; i < MLU_OBJECT_REGISTRY_MAX; ++i) {
    const mlu_object_entry *entry = &registry->entries[i];
    if (entry->live && entry->owner_token == registry->owner_token &&
        entry->type == type && entry->id == id)
      return 1;
  }
  return 0;
}

void mlu_object_registry_release(mlu_object_registry *registry,
                                 uint16_t type, uint64_t id) {
  if (!registry || !id || !type) return;
  for (size_t i = 0; i < MLU_OBJECT_REGISTRY_MAX; ++i) {
    mlu_object_entry *entry = &registry->entries[i];
    if (entry->live && entry->owner_token == registry->owner_token &&
        entry->type == type && entry->id == id) {
      entry->live = 0;
      return;
    }
  }
}

void mlu_object_registry_clear(mlu_object_registry *registry) {
  if (!registry) return;
  for (size_t i = 0; i < MLU_OBJECT_REGISTRY_MAX; ++i)
    registry->entries[i].live = 0;
}
