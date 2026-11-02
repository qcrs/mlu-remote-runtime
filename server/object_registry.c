#include "object_registry.h"

#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <sys/random.h>

static pthread_mutex_t identity_lock = PTHREAD_MUTEX_INITIALIZER;
static uint64_t identity_nonce, identity_sequence;
static int identity_ready;

int mlu_object_registry_init(mlu_object_registry *registry, uint64_t owner_token) {
  if (!registry || !owner_token) return -1;
  memset(registry, 0, sizeof(*registry));
  pthread_mutex_lock(&identity_lock);
  if (!identity_ready) {
    size_t remaining = sizeof(identity_nonce);
    unsigned char *p = (unsigned char *)&identity_nonce;
    while (remaining) {
      ssize_t n = getrandom(p, remaining, GRND_NONBLOCK);
      if (n < 0 && errno == EINTR) continue;
      if (n <= 0) { pthread_mutex_unlock(&identity_lock); return -1; }
      p += n; remaining -= (size_t)n;
    }
    identity_ready = 1;
  }
  pthread_mutex_unlock(&identity_lock);
  registry->owner_token = owner_token;
  return 0;
}

int mlu_object_registry_register(mlu_object_registry *registry,
                                 uint16_t type, uint64_t *id,
                                 uint32_t *generation) {
  if (!registry || !registry->owner_token || !id || !generation || type == 0 ||
      registry->generation == UINT32_MAX) return -1;
  for (size_t i = 0; i < MLU_OBJECT_REGISTRY_MAX; ++i) {
    if (registry->entries[i].live) continue;
    pthread_mutex_lock(&identity_lock);
    uint64_t identity = 0;
    while (!identity && identity_ready && identity_sequence != UINT64_MAX)
      identity = (++identity_sequence) ^ identity_nonce;
    pthread_mutex_unlock(&identity_lock);
    if (!identity) return -1;
    registry->entries[i].id = identity;
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
