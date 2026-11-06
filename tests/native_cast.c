#include <cnrt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define OK(e) do{int s=(e);if(s){fprintf(stderr,"%s=%d\n",#e,s);return 2;}}while(0)
#define CHECK(e) do{if(!(e)){fprintf(stderr,"CHECK %s\n",#e);return 3;}}while(0)
int main(){
  float f[4]={-2.2f,0,1.5f,255.7f}; int i[4]={0}; unsigned char u[4]={0}; float back[4]={0};
  OK(cnrtCastDataType(f,CNRT_FLOAT32,i,CNRT_INT32,4,NULL)); CHECK(i[0]==-2&&i[1]==0&&i[2]==2&&i[3]==256);
  OK(cnrtCastDataType(f,CNRT_FLOAT32,u,CNRT_UINT8,4,NULL)); CHECK(u[0]==0&&u[1]==0&&u[2]==1&&u[3]==255);
  OK(cnrtCastDataType(i,CNRT_INT32,back,CNRT_FLOAT32,4,NULL)); CHECK(back[0]==-2&&back[2]==2&&back[3]==256);
  cnrtQuantizedParam_t param=NULL; OK(cnrtCreateQuantizedParam(&param,2,0.5f,3)); OK(cnrtDestroyQuantizedParam(param));
  int poses[2]={1,2},dims[2]={2,4}; float scales[2]={0.5f,0.25f},offsets[2]={3,4};
  OK(cnrtCreateQuantizedParamByChannel(&param,poses,scales,offsets,2,dims,0)); OK(cnrtDestroyQuantizedParam(param));
  OK(cnrtCreateQuantizedParam(&param,0,2.0f,3)); signed char q[4]={0};
  OK(cnrtCastDataType(f,CNRT_FLOAT32,q,CNRT_INT8,4,param)); CHECK(q[0]==-4&&q[1]==0&&q[2]==3&&q[3]==127);
  float deq[4]={0}; OK(cnrtCastDataType(q,CNRT_INT8,deq,CNRT_FLOAT32,4,param)); CHECK(deq[0]==-2.0f&&deq[2]==1.5f); OK(cnrtDestroyQuantizedParam(param));
  int v2i[3]={0}; unsigned char v2u[3]={0}; OK(cnrtCastDataType_V2(f,cnrtFloat,v2i,cnrtInt,3,NULL,cnrtRounding_rn)); printf("v2=%d,%d,%d u=%u,%u,%u\n",v2i[0],v2i[1],v2i[2],v2u[0],v2u[1],v2u[2]); CHECK(v2i[0]==-2&&v2i[1]==0&&v2i[2]==2); OK(cnrtCastDataType_V2(f,cnrtFloat,v2u,cnrtUchar,3,NULL,cnrtRounding_rz)); CHECK(v2u[0]==0&&v2u[1]==0&&v2u[2]==1);
  printf("PASS cnrtCastDataType f32/i32/u8/int8/V2 subset\n"); return 0;
}
