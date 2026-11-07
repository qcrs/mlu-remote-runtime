#include <cn_api.h>
#include <stdio.h>
int main(void) {
  void *p=NULL; int t=99; CNcontext c;
  printf("init=%d\n",cnInit(0));
  printf("malloc=%d\n",cnMallocHost(&p,16));
  int s=cnGetMemAttribute(&t,CN_MEM_ATTRIBUTE_TYPE,(CNaddr)p);
  printf("no-context host type status=%d value=%d\n",s,t);
  printf("create=%d\n",cnCtxCreate(&c,0,0));
  int page=0; t=99; s=cnGetMemAttribute(&t,CN_MEM_ATTRIBUTE_TYPE,(CNaddr)&page);
  printf("pageable type status=%d value=%d\n",s,t);
  cnFreeHost(p); cnCtxDestroy(c); return 0;
}
