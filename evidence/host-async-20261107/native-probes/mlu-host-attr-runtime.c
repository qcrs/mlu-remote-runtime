#include <cnrt.h>
#include <stdio.h>
int main(void) {void *p=NULL; cnrtPointerAttributes_t a; printf("malloc=%d\n",cnrtHostMalloc(&p,16)); int s=cnrtPointerGetAttributes(&a,p); printf("attr=%d type=%d host=%d\n",s,a.type,a.hostPointer==p);cnrtFreeHost(p);}
