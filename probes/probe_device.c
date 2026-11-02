#include <cnrt.h>
#include <stdio.h>

int main(void) { unsigned int count = 0; cnrtRet_t r = cnrtGetDeviceCount(&count); printf("cnrt=%d count=%u\n", r, count); for (unsigned int i=0;i<count;i++){cnrtDeviceProp_t p; r=cnrtGetDeviceProperties(&p,(int)i); printf("device=%u ret=%d name=%s\n",i,r,p.name);} return r != cnrtSuccess; }
