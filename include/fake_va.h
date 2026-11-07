#ifndef MLU_FAKE_VA_H
#define MLU_FAKE_VA_H
#include <stddef.h>
#include <stdint.h>

enum { MLU_ENDPOINT_INVALID=-1, MLU_ENDPOINT_HOST=0, MLU_ENDPOINT_DEVICE=1 };
/* Call under the owning registry lock for every reserved mapping, including
 * dead allocations. PROT_NONE reservations and their guard pages survive free
 * so stale pointers cannot fall through into a host memcpy. */
static inline int mlu_fake_va_endpoint(uintptr_t address, uint64_t bytes,
                                      uintptr_t base, size_t size, size_t mapped,
                                      int live, int owned) {
  if(address<base || address-base>=mapped) return MLU_ENDPOINT_HOST;
  uint64_t offset=address-base;
  if(!live || !owned || offset>=size || bytes>size-offset) return MLU_ENDPOINT_INVALID;
  return MLU_ENDPOINT_DEVICE;
}
#endif
