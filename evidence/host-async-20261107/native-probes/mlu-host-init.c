#include <cn_api.h>
#include <cnrt.h>
#include <stdio.h>
int main(int argc,char **argv) { void *p=NULL; if(argc>1) {cnInit(0); printf("driver-no-context malloc=%d ",cnMallocHost(&p,4)); printf("p=%p free=%d\n",p,cnFreeHost(p));} else {printf("runtime-initial malloc=%d ",cnrtHostMalloc(&p,4));printf("p=%p free=%d\n",p,cnrtFreeHost(p));} }
