#include <cnrt.h>
#include <stdio.h>

int main(void) { cnrtRet_t r=cnrtSetDevice(0); void *p=NULL; if(!r) r=cnrtMalloc(&p,4096); printf("set/malloc=%d ptr=%p\n",r,p); if(p) { cnrtRet_t f=cnrtFree(p); printf("free=%d\n",f); if(!r) r=f; } return r != cnrtSuccess; }
