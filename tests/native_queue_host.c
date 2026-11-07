#define _DEFAULT_SOURCE
#include <cn_api.h>
#include <cnrt.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{int st=(x);if(st){fprintf(stderr,"FAIL %s = %d line=%d\n",#x,st,__LINE__);exit(1);}}while(0)
static CNqueue q;static CNcontext ctx;static cn_uint64_t *atomic_ptr;
static int order,observed,queue_callback_count;static unsigned char *host;
static void hostfn(void *data){if(host && *host!=42)abort();if(order!=*(int*)data)abort();order++;}
static void callback(CNqueue queue,CNresult st,void *data){if(st || queue!=q || order!=1)abort();queue_callback_count++;order++;}
static void *waiter(void *data){CHECK(cnCtxSetCurrent(ctx));__atomic_store_n(&observed,1,__ATOMIC_RELEASE);CHECK(cnQueueSync(q));__atomic_store_n(&observed,2,__ATOMIC_RELEASE);return NULL;}
int main(int argc,char **argv){setbuf(stdout,NULL);int zero=0;
 if(argc>1 && !strcmp(argv[1],"runtime")){CHECK(cnrtSetDevice(0));cnrtQueue_t rt;CHECK(cnrtQueueCreate(&rt));CHECK(cnrtHostMalloc((void**)&host,8));void *d;CHECK(cnrtMalloc(&d,8));CHECK(cnrtMemset(d,42,8));CHECK(cnrtMemcpyAsync(host,d,8,rt,cnrtMemcpyDevToHost));CHECK(cnrtInvokeHostFunc(rt,hostfn,&zero));CHECK(cnrtQueueSync(rt));if(order!=1)abort();CHECK(cnrtQueueDestroy(rt));CHECK(cnrtFree(d));CHECK(cnrtFreeHost(host));puts("PASS runtime queue Host + deferred D2H");return 0;}
 CHECK(cnInit(0));CHECK(cnCtxCreate(&ctx,0,0));CHECK(cnCreateQueue(&q,0));CHECK(cnMallocHost((void**)&host,8));CNaddr d;CHECK(cnMalloc(&d,8));CHECK(cnMemsetD8(d,42,8));CHECK(cnMemcpyDtoHAsync(host,d,8,q));CHECK(cnInvokeHostFunc(q,hostfn,&zero));CHECK(cnQueueAddCallback(q,callback,NULL,0));CHECK(cnQueueSync(q));if(order!=2 || queue_callback_count!=1)abort();
 CHECK(cnMallocHost((void**)&atomic_ptr,8));*atomic_ptr=7;cn_uint64_t value=0;CHECK(cnAtomicReadOps((uintptr_t)atomic_ptr,&value));if(value!=7)abort();CHECK(cnAtomicOperation((uintptr_t)atomic_ptr,19,0,CN_ATOMIC_OP_REQUEST,CN_ATOMIC_REQUEST_SET));CHECK(cnQueueAtomicOperation(q,(uintptr_t)atomic_ptr,3,0,CN_ATOMIC_OP_REQUEST,CN_ATOMIC_REQUEST_ADD));CHECK(cnQueueSync(q));CHECK(cnAtomicReadOps((uintptr_t)atomic_ptr,&value));if(value!=22 || *atomic_ptr!=22)abort();
 cn_uint64_t *cells;CHECK(cnMallocHost((void**)&cells,40*8));
 for(unsigned i=0;i<40;i++){cells[i]=i;CHECK(cnAtomicOperation((uintptr_t)&cells[i],1,0,CN_ATOMIC_OP_REQUEST,CN_ATOMIC_REQUEST_ADD));}
 CHECK(cnQueueSync(q));for(unsigned i=0;i<40;i++)if(cells[i]!=i+1)abort();CHECK(cnFreeHost(cells));
 CHECK(cnQueueAtomicOperation(q,(uintptr_t)atomic_ptr,99,0,CN_ATOMIC_OP_COMPARE,CN_ATOMIC_COMPARE_EQUAL));
 if(argc>1 && !strcmp(argv[1],"abrupt")){puts("READY unsatisfied compare then EOF");_exit(0);}
 if(argc>1 && !strcmp(argv[1],"blocked-sync")){puts("READY blocked sync");CHECK(cnQueueSync(q));abort();}
 pthread_t thread;pthread_create(&thread,NULL,waiter,NULL);while(!__atomic_load_n(&observed,__ATOMIC_ACQUIRE))usleep(1000);usleep(100000);CHECK(cnAtomicReadOps((uintptr_t)atomic_ptr,&value));if(value!=22 || observed!=1)abort();CHECK(cnAtomicOperation((uintptr_t)atomic_ptr,99,0,CN_ATOMIC_OP_REQUEST,CN_ATOMIC_REQUEST_SET));pthread_join(thread,NULL);if(observed!=2)abort();CHECK(cnFreeHost(atomic_ptr));CHECK(cnFree(d));CHECK(cnFreeHost(host));CHECK(cnDestroyQueue(q));CHECK(cnCtxDestroy(ctx));puts("PASS driver queue Host + callback + deferred D2H + atomics + concurrent compare wake");return 0;}
