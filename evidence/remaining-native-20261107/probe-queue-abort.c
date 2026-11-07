#include <cn_api.h>
#include <stdio.h>
#define X(x) do{int s=(x);printf("%s = %d\n",#x,s);if(s)return 1;}while(0)
int main(){setbuf(stdout,NULL);X(cnInit(0));CNcontext c;X(cnCtxCreate(&c,0,0));CNqueue q;X(cnCreateQueue(&q,0));cn_uint64_t *p;X(cnMallocHost((void**)&p,8));*p=0;X(cnQueueAtomicOperation(q,(uintptr_t)p,99,0,CN_ATOMIC_OP_COMPARE,CN_ATOMIC_COMPARE_EQUAL));const void *fn;X(cnGetExportFunction("cnQueueAbort",&fn));X(((CNresult(*)(CNqueue))fn)(q));printf("sync=%d\n",cnQueueSync(q));X(cnDestroyQueue(q));X(cnFreeHost(p));X(cnCtxDestroy(c));return 0;}
