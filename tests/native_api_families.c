#include <cnrt.h>
#include <cn_api.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define OK(e) do { int st=(e); if(st) { fprintf(stderr,"line %d: %s = %d\n",__LINE__,#e,st); exit(2); } } while(0)
#define ASSERT(e) do { if(!(e)) { fprintf(stderr,"line %d: %s\n",__LINE__,#e); exit(3); } } while(0)
static void pitched(int driver) {
  unsigned char src[1024],got[1024],want[1024]; void *s,*d; CNaddr cs=0,cd=0;
  for(size_t i=0;i<sizeof(src);++i) src[i]=(i*7+11)%251;
  if(driver) { OK(cnMalloc(&cs,sizeof(src))); OK(cnMalloc(&cd,sizeof(src))); s=(void *)(uintptr_t)cs; d=(void *)(uintptr_t)cd; OK(cnMemcpyHtoD(cs,src,sizeof(src))); }
  else { OK(cnrtMalloc(&s,sizeof(src))); OK(cnrtMalloc(&d,sizeof(src))); OK(cnrtMemcpy(s,src,sizeof(src),cnrtMemcpyHostToDev)); }
  for(int alias=0;alias<(driver ? 2 : 1);++alias) {
    memset(want,0xa5,sizeof(want));
    if(driver) { OK(cnMemsetD8(cd,0xa5,sizeof(want))); if(alias) OK(cnMemcpy2D(cd+3,16,cs+2,20,8,4)); else OK(cnMemcpyDtoD2D(cd+3,16,cs+2,20,8,4)); OK(cnMemcpyDtoH(got,cd,sizeof(got))); }
    else { OK(cnrtMemset(d,0xa5,sizeof(want))); OK(cnrtMemcpy2D((char *)d+3,16,(char *)s+2,20,8,4,cnrtMemcpyDevToDev)); OK(cnrtMemcpy(got,d,sizeof(got),cnrtMemcpyDevToHost)); }
    for(int y=0;y<4;++y) memcpy(want+3+y*16,src+2+y*20,8);
    ASSERT(!memcmp(got,want,sizeof(want)));
  }
  for(int alias=0;alias<(driver ? 2 : 1);++alias) {
    memset(want,0xa5,sizeof(want));
    if(driver) {
      OK(cnMemsetD8(cd,0xa5,sizeof(want))); CNmemcpy3dParam p={0};
      p.srcPtr=(CNpitchedPtr){16,s,16,6}; p.dstPtr=(CNpitchedPtr){16,d,16,6};
      p.srcPos=(CNpos){2,1,0}; p.dstPos=(CNpos){3,2,1}; p.extent=(CNextent){2,3,8};
      if(alias) OK(cnMemcpy3D(&p)); else OK(cnMemcpyDtoD3D(&p)); OK(cnMemcpyDtoH(got,cd,sizeof(got)));
    } else {
      OK(cnrtMemset(d,0xa5,sizeof(want))); cnrtMemcpy3dParam_t p={0};
      p.srcPtr=(cnrtPitchedPtr_t){16,s,16,6}; p.dstPtr=(cnrtPitchedPtr_t){16,d,16,6};
      p.srcPos=(cnrtPos_t){2,1,0}; p.dstPos=(cnrtPos_t){3,2,1}; p.extent=(cnrtExtent_t){2,3,8}; p.dir=cnrtMemcpyDevToDev;
      OK(cnrtMemcpy3D(&p)); OK(cnrtMemcpy(got,d,sizeof(got),cnrtMemcpyDevToHost));
    }
    for(int z=0;z<2;++z) for(int y=0;y<3;++y) memcpy(want+(z+1)*96+(y+2)*16+3,src+z*96+(y+1)*16+2,8);
    ASSERT(!memcmp(got,want,sizeof(want)));
  }
  if(driver) { OK(cnFree(cs)); OK(cnFree(cd)); } else { OK(cnrtFree(s)); OK(cnrtFree(d)); }
  puts("PASS pitched 2D/3D: differing pitches; offsets; xyz; depth; untouched padding; aliases");
}
int main(int argc,char **argv) {
  if(argc>1 && !strcmp(argv[1],"driver")) {
    OK(cnInit(0)); CNdev dev; OK(cnDeviceGet(&dev,0)); CNcontext context; OK(cnCtxCreate(&context,0,dev));
    int major,minor,patch; OK(cnDriverGetVersion(&major,&minor,&patch)); printf("driver version %d.%d.%d\n",major,minor,patch);
    OK(cnGetDriverVersion(&major,&minor,&patch)); printf("driver legacy version %d.%d.%d\n",major,minor,patch);
    OK(cnGetLibVersion(&major,&minor,&patch)); printf("driver lib version %d.%d.%d\n",major,minor,patch);
    const char *error_name=NULL,*error_text=NULL;
    OK(cnGetErrorName(CN_SUCCESS,&error_name)); OK(cnGetErrorString(CN_SUCCESS,&error_text));
    ASSERT(error_name && error_text);
    printf("driver error %s: %s\n",error_name,error_text);
    for(int i=0;i<=CN_DEVICE_ATTRIBUTE_MAX;++i) {
      if(i>9 && !(i>=0x101 && i<=0x10a) && !(i>=0x201 && i<=0x20c) && !(i>=0x301 && i<=0x312) && i<0x401) continue;
      int v=-1; int st=cnDeviceGetAttribute(&v,(CNdevice_attribute)i,dev); printf("driver attr %d %d %d\n",i,st,st ? -1 : v);
    }
    cn_uint64_t freebytes,total; OK(cnMemGetInfo(&freebytes,&total)); ASSERT(freebytes>0 && freebytes<=total); puts("driver memory info valid");
    int lo,hi,priority; OK(cnCtxGetQueuePriorityRange(&lo,&hi)); printf("driver priority range %d %d\n",lo,hi);
    CNqueue q,q2; OK(cnCreateQueueWithPriority(&q,0,lo)); OK(cnCreateQueueWithPriority(&q2,0,hi-100));
    OK(cnQueueGetPriority(q,&priority)); printf("driver priority low %d\n",priority); OK(cnQueueGetPriority(q2,&priority)); printf("driver priority clamped %d\n",priority);
    CNaddr d; OK(cnMalloc(&d,128)); unsigned char h[128];
    OK(cnMemsetD8(d,0x7b,128)); OK(cnMemcpyDtoH(h,d,128)); for(int i=0;i<128;++i) ASSERT(h[i]==0x7b);
    OK(cnMemsetD16(d+2,0x1234,8)); OK(cnMemsetD32(d+32,0x12345678,4)); OK(cnMemcpyDtoH(h,d,128));
    for(int i=0;i<8;++i) ASSERT(((unsigned short *)(h+2))[i]==0x1234);
    for(int i=0;i<4;++i) ASSERT(((unsigned *)(h+32))[i]==0x12345678);
    CNnotifier a,b; OK(cnCreateNotifier(&a,0)); OK(cnCreateNotifier(&b,0)); OK(cnPlaceNotifier(a,q));
    OK(cnMemsetD8Async(d,0x8a,128,q)); OK(cnMemsetD16Async(d+2,0x2345,8,q)); OK(cnMemsetD32Async(d+32,0x3456789a,4,q));
    OK(cnPlaceNotifier(b,q)); OK(cnQueueWaitNotifier(q2,b)); OK(cnQueueSync(q2)); OK(cnWaitNotifier(b));
    float elapsed=-1; OK(cnNotifierElapsedTime(&elapsed,a,b)); ASSERT(elapsed>=0); OK(cnNotifierElapsedExecTime(&elapsed,a,b)); ASSERT(elapsed>=0);
    OK(cnMemcpyDtoH(h,d,128)); ASSERT(h[0]==0x8a && h[127]==0x8a); for(int i=0;i<8;++i) ASSERT(((unsigned short *)(h+2))[i]==0x2345); for(int i=0;i<4;++i) ASSERT(((unsigned *)(h+32))[i]==0x3456789a);
    puts("driver fill widths/offsets/async; cross queue wait; timing PASS");
    for(unsigned flag=2;flag<=4;flag+=2) { CNnotifier n; OK(cnCreateNotifier(&n,flag)); OK(cnPlaceNotifier(n,q)); OK(cnWaitNotifier(n)); printf("driver flags %u timing %d\n",flag,(int)cnNotifierElapsedTime(&elapsed,n,n)); OK(cnDestroyNotifier(n)); }
    pitched(1);
    ASSERT(cnDeviceGetAttribute(NULL,CN_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR,dev)!=0);
    if(argc>2) {
      CNmodule m; CNkernel k; OK(cnModuleLoad(argv[2],&m)); OK(cnModuleGetKernel(m,"_Z10vector_addPfPKfS1_i",&k));
      for(int i=0;i<5;++i) { cn_int64_t v=-1; OK(cnKernelGetAttribute(&v,(CNkernel_attribute)i,k)); printf("kernel attribute %d %ld\n",i,(long)v); }
      OK(cnModuleUnload(m));
    }
    OK(cnDestroyNotifier(a)); OK(cnDestroyNotifier(b)); OK(cnDestroyQueue(q)); OK(cnDestroyQueue(q2)); OK(cnFree(d)); OK(cnCtxDestroy(context));
  } else {
    OK(cnrtSetDevice(0)); int major,minor,patch;
    OK(cnrtGetLibVersion(&major,&minor,&patch)); printf("runtime version %d.%d.%d\n",major,minor,patch);
    OK(cnrtDriverGetVersion(&major,&minor,&patch)); printf("runtime driver version %d.%d.%d\n",major,minor,patch);
    cnrtDeviceProp_t prop; memset(&prop,0,sizeof(prop)); OK(cnrtGetDeviceProperties(&prop,0));
    printf("runtime properties %s %d %d %d %d %d\n",prop.name,prop.totalMem,prop.major,prop.minor,prop.clusterCount,prop.McorePerCluster);
    /* A byte digest covers all struct fields including array elements. */
    uint32_t hash=2166136261u; for(size_t i=0;i<sizeof(prop);++i) hash=(hash^((unsigned char *)&prop)[i])*16777619u; printf("properties hash %u\n",hash);
    int attrs[]={0,1,2,3,4,5,6,7,8,9,0x101,0x102,0x103,0x104,0x105,0x106,0x107,0x108,0x109,
      0x201,0x202,0x203,0x204,0x205,0x206,0x207,0x208,0x209,0x20a,0x20b,
      0x301,0x302,0x303,0x304,0x305,0x306,0x307,0x309,0x312,
      0x401,0x402,0x403,0x404,0x405,0x406,0x407,0x408,0x409};
    for(unsigned j=0;j<sizeof(attrs)/sizeof(*attrs);++j) { int i=attrs[j],v=-1; int st=cnrtDeviceGetAttribute(&v,(cnrtDeviceAttr_t)i,0); printf("runtime attr %d %d %d\n",i,st,st ? -1 : v); }
    for(int i=cnrtAttrUnsupportedFlag;i<=cnrtAttrMaxNum;++i) { int v=-1; int st=cnrtDeviceGetAttribute(&v,(cnrtDeviceAttr_t)i,0); printf("runtime attr %d %d %d\n",i,st,st ? -1 : v); }
    size_t freebytes,total; OK(cnrtMemGetInfo(&freebytes,&total)); ASSERT(freebytes>0 && freebytes<=total); puts("runtime memory info valid");
    int lo,hi,priority; OK(cnrtDeviceGetQueuePriorityRange(&lo,&hi)); printf("runtime priority range %d %d\n",lo,hi);
    cnrtQueue_t q,q2; OK(cnrtQueueCreateWithPriority(&q,0,lo)); OK(cnrtQueueCreateWithPriority(&q2,0,hi-100));
    OK(cnrtQueueGetPriority(q,&priority)); printf("runtime priority low %d\n",priority); OK(cnrtQueueGetPriority(q2,&priority)); printf("runtime priority clamped %d\n",priority);
    void *d,*dst; OK(cnrtMalloc(&d,128)); OK(cnrtMalloc(&dst,128)); unsigned char h[128];
    OK(cnrtMemset(d,0x123,128)); OK(cnrtMemcpy(h,d,128,cnrtMemcpyDevToHost)); for(int i=0;i<128;++i) ASSERT(h[i]==0x23);
    cnrtNotifier_t a,b; OK(cnrtNotifierCreate(&a)); OK(cnrtNotifierCreate(&b)); OK(cnrtPlaceNotifier(a,q));
    OK(cnrtMemsetAsync((char *)d+1,0x67,126,q)); OK(cnrtPlaceNotifier(b,q)); OK(cnrtQueueWaitNotifier(b,q2,0)); OK(cnrtQueueSync(q2)); OK(cnrtWaitNotifier(b));
    float elapsed=-1; OK(cnrtNotifierElapsedTime(a,b,&elapsed)); ASSERT(elapsed>=0); OK(cnrtNotifierDuration(a,b,&elapsed)); ASSERT(elapsed>=0);
    OK(cnrtMemcpy(h,d,128,cnrtMemcpyDevToHost)); ASSERT(h[0]==0x23 && h[127]==0x23); for(int i=1;i<127;++i) ASSERT(h[i]==0x67);
    puts("runtime fill truncation/offset/async; cross queue wait; timing PASS");
    for(unsigned flag=2;flag<=4;flag+=2) { cnrtNotifier_t n; OK(cnrtNotifierCreateWithFlags(&n,flag)); OK(cnrtPlaceNotifier(n,q)); OK(cnrtWaitNotifier(n)); printf("runtime flags %u timing %d\n",flag,(int)cnrtNotifierElapsedTime(n,n,&elapsed)); OK(cnrtNotifierDestroy(n)); }
    pitched(0); OK(cnrtGetLibVersion(NULL,&minor,&patch)); OK(cnrtGetLibVersion(NULL,NULL,NULL));
    OK(cnrtNotifierDestroy(a)); OK(cnrtNotifierDestroy(b)); OK(cnrtQueueDestroy(q)); OK(cnrtQueueDestroy(q2)); OK(cnrtFree(d)); OK(cnrtFree(dst));
  }
  puts("PASS API families"); return 0;
}
