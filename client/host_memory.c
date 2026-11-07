#define _GNU_SOURCE
#include "host_memory.h"
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

struct mlu_host_allocation {
  struct mlu_host_allocation *next;
  void *base;
  size_t bytes, pages, mapped, references;
  int live;
};
static pthread_mutex_t host_lock = PTHREAD_MUTEX_INITIALIZER;
static mlu_host_allocation *hosts;
static void retire(mlu_host_allocation *a) {
  if (a->live || a->references || !a->pages) return;
  munlock(a->base, a->pages);
  mprotect(a->base, a->pages, PROT_NONE);
  madvise(a->base, a->pages, MADV_DONTNEED);
  a->pages = 0; /* Keep a VA tombstone, as for remote device allocations. */
}
int mlu_host_alloc(void **out, size_t bytes) {
  if (!out) return 100006;
  if (!bytes) { *out = NULL; return 0; }
  size_t page = (size_t)sysconf(_SC_PAGESIZE);
  if (bytes > SIZE_MAX - 2 * page) return 100006;
  mlu_host_allocation *a = calloc(1, sizeof(*a));
  if (!a) return 100010;
  a->pages = (bytes + page - 1) / page * page;
  a->mapped = a->pages + page;
  a->base = mmap(NULL, a->mapped, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (a->base == MAP_FAILED) { free(a); return 100010; }
  if (mprotect(a->base, a->pages, PROT_READ | PROT_WRITE) || mlock(a->base, a->pages)) {
    munmap(a->base, a->mapped); free(a); return 100010;
  }
  a->bytes = bytes; a->live = 1;
  pthread_mutex_lock(&host_lock);
  a->next = hosts; hosts = a; *out = a->base;
  pthread_mutex_unlock(&host_lock);
  return 0;
}
int mlu_host_free(void *ptr) {
  if (!ptr) return 0;
  pthread_mutex_lock(&host_lock);
  int st = 100006;
  for (mlu_host_allocation *a = hosts; a; a = a->next) if (a->base == ptr && a->live) {
    a->live = 0; retire(a); st = 0; break;
  }
  pthread_mutex_unlock(&host_lock);
  return st;
}
static int acquire(const void *ptr, size_t bytes, mlu_host_allocation **lease) {
  if (lease) *lease = NULL;
  int kind = 0;
  pthread_mutex_lock(&host_lock);
  uintptr_t value = (uintptr_t)ptr;
  for (mlu_host_allocation *a = hosts; a; a = a->next) {
    if (value < (uintptr_t)a->base || value - (uintptr_t)a->base >= a->mapped) continue;
    size_t off = value - (uintptr_t)a->base;
    kind = a->live && off < a->bytes && bytes <= a->bytes - off ? 1 : -1;
    if (kind == 1 && lease) { a->references++; *lease = a; }
    break;
  }
  pthread_mutex_unlock(&host_lock);
  return kind;
}
int mlu_host_range(const void *ptr, size_t bytes) { return acquire(ptr, bytes, NULL); }
int mlu_host_acquire(void *ptr, size_t bytes, mlu_host_allocation **lease) { return acquire(ptr, bytes, lease); }
void mlu_host_release(mlu_host_allocation *lease) {
  if (!lease) return;
  pthread_mutex_lock(&host_lock);
  if (lease->references) lease->references--;
  retire(lease);
  pthread_mutex_unlock(&host_lock);
}
