/* Host registry invariants: unavailable entropy fails closed, concurrent
 * sessions and reset/reinit never reuse IDs, stale/type/owner checks persist.
 * Actual startup entropy and cross-process restarts are tested by the server.
 */
#include "object_registry.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/random.h>

#define THREADS 4
#define IDS 512
static uint64_t identities[THREADS][IDS];
static int entropy_calls;

ssize_t __real_getrandom(void *, size_t, unsigned int);
ssize_t __wrap_getrandom(void *p, size_t n, unsigned int flags) {
  if (__sync_add_and_fetch(&entropy_calls, 1) == 1) { errno = EAGAIN; return -1; }
  return __real_getrandom(p, n, flags);
}

static void *worker(void *arg) {
  size_t index = *(size_t *)arg;
  mlu_object_registry *r = calloc(1, sizeof(*r)); assert(r);
  for (size_t i = 0; i < IDS; ++i) {
    /* Reusing even the owner number must not reuse an identity. */
    assert(mlu_object_registry_init(r, index + 1) == 0);
    uint32_t generation;
    uint64_t id = 0;
    assert(mlu_object_registry_register(r, MLU_OBJECT_ALLOCATION, &id, &generation) == 0 && id);
    identities[index][i] = id;
    assert(mlu_object_registry_contains(r, MLU_OBJECT_ALLOCATION, id));
    assert(!mlu_object_registry_contains(r, MLU_OBJECT_QUEUE, id));
    r->owner_token += 100;
    assert(!mlu_object_registry_contains(r, MLU_OBJECT_ALLOCATION, id));
    r->owner_token -= 100;
    mlu_object_registry_clear(r);
    assert(!mlu_object_registry_contains(r, MLU_OBJECT_ALLOCATION, id));
  }
  r->generation = UINT32_MAX;
  uint64_t sentinel = 77; uint32_t generation = 88;
  assert(mlu_object_registry_register(r, MLU_OBJECT_ALLOCATION, &sentinel, &generation) != 0);
  assert(sentinel == 77 && generation == 88);
  free(r); return NULL;
}

static int compare(const void *a, const void *b) {
  uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
  return (x > y) - (x < y);
}

int main(void) {
  mlu_object_registry *r = calloc(1, sizeof(*r)); assert(r);
  uint64_t id = 77; uint32_t generation = 88;
  assert(mlu_object_registry_init(r, 1) != 0 && errno == EAGAIN);
  assert(mlu_object_registry_register(r, MLU_OBJECT_ALLOCATION, &id, &generation) != 0);
  assert(id == 77 && generation == 88);
  free(r);
  pthread_t workers[THREADS]; size_t indexes[THREADS];
  for (size_t i = 0; i < THREADS; ++i) { indexes[i] = i; assert(!pthread_create(&workers[i], NULL, worker, &indexes[i])); }
  for (size_t i = 0; i < THREADS; ++i) assert(!pthread_join(workers[i], NULL));
  uint64_t sorted[THREADS * IDS];
  for (size_t i = 0; i < THREADS; ++i)
    for (size_t j = 0; j < IDS; ++j) sorted[i * IDS + j] = identities[i][j];
  qsort(sorted, THREADS * IDS, sizeof(sorted[0]), compare);
  for (size_t i = 1; i < THREADS * IDS; ++i) assert(sorted[i] != sorted[i - 1]);
  puts("PASS unavailable entropy fails closed; 2048 concurrent/reinitialized IDs unique; owner/type/stale and generation overflow checks");
  return 0;
}
