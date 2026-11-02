/* Test-only LD_PRELOAD failure injection. Never linked into production. */
#include <cn_api.h>
CNresult cnCtxDestroy(CNcontext context) {
  (void)context;
  return CN_ERROR_INVALID_VALUE;
}
