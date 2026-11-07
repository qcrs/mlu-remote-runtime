#include <cnrt.h>
#include <cn_api.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sdk_error_codes.inc"
#define CHECK(e) do { if(!(e)) { fprintf(stderr,"line %d: %s\n",__LINE__,#e); return 2; } } while(0)
int main(void) {
  /* Deliberately query before cnInit/cnrtInit. The runner points the shim to an
   * unreachable endpoint, and repeats with vendor SDK and devices hidden. */
  const char *saved_runtime=cnrtGetErrorStr(cnrtErrorArgsInvalid),*saved_driver=NULL;
  CHECK(cnGetErrorString(CN_ERROR_INVALID_VALUE,&saved_driver)==CN_SUCCESS);
  char runtime_copy[256],driver_copy[256];
  snprintf(runtime_copy,sizeof(runtime_copy),"%s",saved_runtime);
  snprintf(driver_copy,sizeof(driver_copy),"%s",saved_driver);
  for(size_t i=0;i<sizeof(sdk_error_codes)/sizeof(*sdk_error_codes);++i) {
    int code=sdk_error_codes[i]; const char *name=NULL,*description=NULL;
    int ns=cnGetErrorName((CNresult)code,&name),ds=cnGetErrorString((CNresult)code,&description);
    CHECK(!ns && !ds && name && description);
    printf("%d|%s|%s|%d|%s|%d|%s\n",code,cnrtGetErrorName((cnrtRet_t)code),
           cnrtGetErrorStr((cnrtRet_t)code),ns,name,ds,description);
  }
  CHECK(!strcmp(runtime_copy,saved_runtime) && !strcmp(driver_copy,saved_driver));
  puts("PASS error catalogue: enum union/unknown ranges/pre-init/immutable lifetime");
  return 0;
}
