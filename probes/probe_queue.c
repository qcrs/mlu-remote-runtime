#include <cnrt.h>
#include <stdio.h>

int main(void) { cnrtQueue_t q=NULL; cnrtNotifier_t n=NULL; cnrtRet_t r=cnrtSetDevice(0); if(!r)r=cnrtQueueCreate(&q); if(!r)r=cnrtNotifierCreate(&n); if(!r)r=cnrtPlaceNotifier(n,q); if(!r)r=cnrtQueueSync(q); printf("queue/notifier=%d query=%d\n",r,n?cnrtQueryNotifier(n):-1); if(n)cnrtNotifierDestroy(n);if(q)cnrtQueueDestroy(q);return r != cnrtSuccess; }
