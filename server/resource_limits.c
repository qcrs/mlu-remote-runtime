#include "resource_limits.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <limits.h>

static pthread_mutex_t resource_lock = PTHREAD_MUTEX_INITIALIZER;
static uint64_t per_session[MLU_RESOURCE_KINDS], total_limit[MLU_RESOURCE_KINDS];
static uint64_t total_used[MLU_RESOURCE_KINDS];
static uint32_t connections, transfers;
static const char *names[] = {"DEVICE", "STAGING", "IMAGE"};

static int setting(const char *name, uint64_t fallback, uint64_t max, uint64_t *out) {
  const char *p = getenv(name);
  if (!p) { *out = fallback; return 0; }
  uint64_t n = 0;
  if (!*p) goto bad;
  for (; *p; ++p) {
    if (*p < '0' || *p > '9' || n > (max - (unsigned)(*p - '0')) / 10) goto bad;
    n = n * 10 + (unsigned)(*p - '0');
  }
  if (!n) goto bad;
  *out = n; return 0;
bad:
  fprintf(stderr, "[server] invalid %s: expected decimal integer 1..%" PRIu64 "\n", name, max);
  return -1;
}

int mlu_resource_limits_init(void) {
  const uint64_t mib = 1024ull * 1024;
  const uint64_t defaults[] = {1024 * mib, 64 * mib, 64 * mib};
  const uint64_t totals[] = {8192 * mib, 256 * mib, 256 * mib};
  for (unsigned i = 0; i < MLU_RESOURCE_KINDS; ++i) {
    char name[96];
    snprintf(name, sizeof(name), "MLU_REMOTE_SESSION_%s_BYTES", names[i]);
    if (setting(name, defaults[i], UINT64_MAX, &per_session[i])) return -1;
    snprintf(name, sizeof(name), "MLU_REMOTE_TOTAL_%s_BYTES", names[i]);
    if (setting(name, totals[i], UINT64_MAX, &total_limit[i])) return -1;
    fprintf(stderr, "[server] limit %s session=%" PRIu64 " total=%" PRIu64 " bytes\n",
            names[i], per_session[i], total_limit[i]);
  }
  uint64_t value;
  if (setting("MLU_REMOTE_MAX_CONNECTIONS", 32, 1024, &value)) return -1;
  connections = (uint32_t)value;
  if (setting("MLU_REMOTE_SESSION_MAX_TRANSFERS", 1024, 1024, &value)) return -1;
  transfers = (uint32_t)value;
  return 0;
}

int mlu_resource_reserve(mlu_resource_usage *usage, enum mlu_resource_kind kind, uint64_t bytes) {
  pthread_mutex_lock(&resource_lock);
  int denied = bytes > per_session[kind] - usage->bytes[kind] ||
               bytes > total_limit[kind] - total_used[kind];
  if (!denied) { usage->bytes[kind] += bytes; total_used[kind] += bytes; }
  else fprintf(stderr, "[server] RESOURCE_LIMIT kind=%s request=%" PRIu64
                      " session_used=%" PRIu64 " total_used=%" PRIu64 "\n",
               names[kind], bytes, usage->bytes[kind], total_used[kind]);
  pthread_mutex_unlock(&resource_lock);
  return denied ? -1 : 0;
}
void mlu_resource_release(mlu_resource_usage *usage, enum mlu_resource_kind kind, uint64_t bytes) {
  pthread_mutex_lock(&resource_lock);
  /* A double release is a programming error; never silently wrap accounting. */
  if (bytes > usage->bytes[kind] || bytes > total_used[kind]) abort();
  usage->bytes[kind] -= bytes; total_used[kind] -= bytes;
  pthread_mutex_unlock(&resource_lock);
}
uint32_t mlu_resource_max_connections(void) { return connections; }
void mlu_resource_transfer(mlu_resource_usage *from,mlu_resource_usage *to,enum mlu_resource_kind kind,uint64_t bytes) {
  pthread_mutex_lock(&resource_lock);
  if(bytes>from->bytes[kind] || bytes>UINT64_MAX-to->bytes[kind])abort();
  from->bytes[kind]-=bytes;to->bytes[kind]+=bytes;
  pthread_mutex_unlock(&resource_lock);
}
uint32_t mlu_resource_max_transfers(void) { return transfers; }
void mlu_resource_report(void) {
  pthread_mutex_lock(&resource_lock);
  fprintf(stderr, "[server] RESOURCE_TOTAL device=%" PRIu64 " staging=%" PRIu64 " image=%" PRIu64 "\n",
          total_used[0], total_used[1], total_used[2]);
  pthread_mutex_unlock(&resource_lock);
}
