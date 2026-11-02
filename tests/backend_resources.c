/* Native backend with selected failure returns injected by the linker's wrap.
 * Success paths execute real CNDrv. No successful native call is fabricated. */
#include "mlu_backend.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail_alloc, fail_image, fail_copy, fail_sync, fail_destroy;
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

int main(int argc, char **argv) {
  assert(argc == 2 || argc == 3);
  mlu_backend_session *s = calloc(1, sizeof(*s)); assert(s);
  assert(!mlu_backend_global_init()); assert(!mlu_backend_session_init(s, 0));
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
  assert(!s->usage.bytes[MLU_RESOURCE_IMAGE]); free(image);
  puts("PASS injected native module-load failure rolls back image reservation");

  assert(!mlu_backend_memcpy(s, MLU_MEMCPY_H2D, a, 0, 0, 0, data, NULL, 16, q, &tid));
  fail_sync = 1; fail_destroy = argc == 3;
  unsigned char output[16], untouched[16];
  memset(output, 0xa5, sizeof(output)); memcpy(untouched, output, sizeof(output));
  assert(mlu_backend_memcpy(s, MLU_MEMCPY_D2H, 0, 0, a, 0, NULL, output, 16, q, &tid) == CN_ERROR_INVALID_VALUE);
  assert(!memcmp(output, untouched, sizeof(output)));
  assert(s->usage.bytes[MLU_RESOURCE_STAGING] == 16);
  puts("PASS failed D2H queue wait preserves host output and pending staging");
  assert(mlu_backend_session_cleanup(s) == CN_ERROR_INVALID_VALUE);
  assert(s->cleanup_failed == CN_ERROR_INVALID_VALUE);
  if (fail_destroy) {
    assert(s->context && s->usage.bytes[MLU_RESOURCE_STAGING] == 16);
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
