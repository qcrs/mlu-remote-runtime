#include <stdio.h>
#include <cn_api.h>
int main(void) {
  CNcontext c; CNaddr a,b; CNtaskTopo g; CNtaskTopoNode n; CNtaskTopoEntity e;
  cnInit(0); cnCtxCreate(&c,0,0); cnMalloc(&a,128); cnMalloc(&b,128);
  for(unsigned d=0;d<7;d++) {
    cnTaskTopoCreate(&g,0);
    CNmemcpy3dParam p={0}; p.dst=(void *)b; p.src=(void *)a;
    p.dstPtr.pitch=p.srcPtr.pitch=64; p.dstPtr.ysize=p.srcPtr.ysize=1;
    p.extent.width=64; p.extent.height=p.extent.depth=1; p.dir=d;
    int add=cnTaskTopoAddMemcpyNode(&n,g,0,0,&p),set=-1,entity=-1;
    if(!add) {
      set=cnTaskTopoMemcpyNodeSetParams(n,&p);
      if(!cnTaskTopoInstantiate(&e,g,0,0,0)) { entity=cnTaskTopoEntityMemcpyNodeSetParams(e,n,&p); cnTaskTopoEntityDestroy(e); }
    }
    printf("dir=%u add=%d set=%d entity_set=%d\n",d,add,set,entity);
    cnTaskTopoDestroy(g);
  }
  cnFree(a); cnFree(b); cnCtxDestroy(c);
}
