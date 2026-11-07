#include <cn_api.h>
#include <cnrt.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#define OBS(call) do { int st=(call); printf("%s = %d\n",#call,st); fflush(stdout); } while(0)
static void state(const char *tag) {
  unsigned f=99;int s=99;int r=cnSharedContextGetState(0,&f,&s);
  printf("state %s status=%d flags=%u active=%d\n",tag,r,f,s);
}
int main(int argc,char **argv) {if(argc!=2)return 2;setbuf(stdout,NULL);OBS(cnInit(0));
  if(!strcmp(argv[1],"shared-life")) {
    CNcontext a=0,b=0; CNaddr p=0; CNdev d=99;
    OBS(cnSharedContextAcquire(&a,0));OBS(cnCtxSetCurrent(a));OBS(cnMalloc(&p,4096));OBS(cnSharedContextRelease(0));
    OBS(cnCtxGetCurrent(&b));printf("inactive current same=%d\n",a==b);OBS(cnCtxGetDevice(&d));printf("device=%llu\n",(unsigned long long)d);OBS(cnMemsetD8(p,1,32));OBS(cnMalloc(&p,4096));state("after-malloc");OBS(cnCtxDestroy(a));state("after-destroy");OBS(cnSharedContextAcquire(&b,0));printf("destroy-acquire same=%d\n",a==b);OBS(cnCtxSetCurrent(b));OBS(cnMalloc(&p,4096));OBS(cnCtxDestroy(b));state("active-destroy");OBS(cnSharedContextReset(0));OBS(cnMemsetD8(p,2,32));OBS(cnCtxGetCurrent(&a));printf("after-reset current same=%d\n",a==b);OBS(cnSharedContextRelease(0));return 0;
  }
  if(!strcmp(argv[1],"shared")) {
    CNcontext a=NULL,b=NULL,c=NULL;state("initial");OBS(cnSharedContextSetFlags(0,1));state("flags");
    OBS(cnSharedContextAcquire(&a,0));OBS(cnCtxGetCurrent(&c));printf("acquire changes current=%d\n",c==a);state("one");
    OBS(cnSharedContextAcquire(&b,0));printf("same=%d\n",a==b);OBS(cnSharedContextSetFlags(0,2));
    OBS(cnSharedContextRelease(0));state("release-one");OBS(cnSharedContextRelease(0));state("release-two");
    OBS(cnSharedContextRelease(0));OBS(cnSharedContextAcquire(&a,0));OBS(cnCtxSetCurrent(a));
    OBS(cnSharedContextReset(0));state("reset");OBS(cnCtxGetCurrent(&c));printf("current null=%d\n",c==NULL);
    OBS(cnSharedContextAcquire(&b,0));printf("reacquire same=%d\n",a==b);OBS(cnSharedContextRelease(0));return 0;
  }
return 2;}
