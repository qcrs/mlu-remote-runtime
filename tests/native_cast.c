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
  float wide[3]={-2.2f,1.5f,1000000000.0f}; int64_t si64[3]={0}; uint64_t ui64[3]={0}; OK(cnrtCastDataType_V2(wide,cnrtFloat,si64,cnrtLonglong,3,NULL,cnrtRounding_rn)); OK(cnrtCastDataType_V2(wide,cnrtFloat,ui64,cnrtUlonglong,3,NULL,cnrtRounding_rn)); CHECK(si64[0]==-2&&si64[1]==2&&si64[2]==1000000000LL&&ui64[0]==0&&ui64[1]==2&&ui64[2]==1000000000ULL);
  uint16_t half[2]={0}, bf[2]={0}; float hv[2]={-2.2f,255.7f}; OK(cnrtCastDataType_V2(hv,cnrtFloat,half,cnrtHalf,2,NULL,cnrtRounding_rn)); CHECK(half[0]==0xc066&&half[1]==0x5bfe); OK(cnrtCastDataType_V2(hv,cnrtFloat,bf,cnrtBfloat,2,NULL,cnrtRounding_rn)); CHECK(bf[0]==0xc00d&&bf[1]==0x4380); float hback[2]; OK(cnrtCastDataType_V2(half,cnrtHalf,hback,cnrtFloat,2,NULL,cnrtRounding_rn)); CHECK(hback[0]<-2.19f&&hback[0]>-2.20f);
  int v2i[3]={0}; unsigned char v2u[3]={0}; OK(cnrtCastDataType_V2(f,cnrtFloat,v2i,cnrtInt,3,NULL,cnrtRounding_rn)); printf("v2=%d,%d,%d u=%u,%u,%u\n",v2i[0],v2i[1],v2i[2],v2u[0],v2u[1],v2u[2]); CHECK(v2i[0]==-2&&v2i[1]==0&&v2i[2]==2); OK(cnrtCastDataType_V2(f,cnrtFloat,v2u,cnrtUchar,3,NULL,cnrtRounding_rz)); CHECK(v2u[0]==0&&v2u[1]==0&&v2u[2]==1); int ro[2]={0},rm[2]={0}; float halves[2]={-2.1f,1.1f}; OK(cnrtCastDataType_V2(halves,cnrtFloat,ro,cnrtInt,2,NULL,cnrtRounding_ro)); OK(cnrtCastDataType_V2(halves,cnrtFloat,rm,cnrtInt,2,NULL,cnrtRounding_rm)); CHECK(ro[0]==-3&&ro[1]==2&&rm[0]==-2&&rm[1]==1);
  printf("PASS cnrtCastDataType f32/i32/u8/int8/V2 subset\n"); return 0;
}
