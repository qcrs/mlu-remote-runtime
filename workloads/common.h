#pragma once
#include <bang.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#define CHECK(call) do { cnrtRet_t status=(call); if(status!=cnrtSuccess){fprintf(stderr,"%s failed: %d\n",#call,(int)status);exit(2);} } while(0)
static float *device_copy(const float *host, size_t count) {
  float *ptr=NULL; CHECK(cnrtMalloc((void **)&ptr,count*sizeof(float)));
  CHECK(cnrtMemcpy(ptr,(void *)host,count*sizeof(float),cnrtMemcpyHostToDev)); return ptr;
}
static int report(const char *name,const char *shape,const float *reference,const float *actual,size_t count,float tolerance) {
  double cpu=0,mlu=0;float error=0;size_t mismatch=0;
  for(size_t i=0;i<count;++i){cpu+=reference[i];mlu+=actual[i];float delta=fabsf(reference[i]-actual[i]);
    if(!isfinite(actual[i]) || delta>tolerance) ++mismatch;
    if(delta>error)error=delta;
  }
  printf("WORKLOAD=%s\nSHAPE=%s\nCPU_CHECKSUM=%.9g\nMLU_CHECKSUM=%.9g\nMAX_ABS_ERROR=%.9g\nMISMATCH=%zu\nRESULT=%s\n",name,shape,cpu,mlu,error,mismatch,mismatch?"FAIL":"PASS");
  return mismatch?1:0;
}
