#include <cnrt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define OK(e) do{int s=(e);if(s){fprintf(stderr,"%s=%d\n",#e,s);return 2;}}while(0)
#define CHECK(e) do{if(!(e)){fprintf(stderr,"CHECK %s\n",#e);return 3;}}while(0)
int main(){float f[4]={-2.2f,0,1.5f,255.7f};int i[4]={0};unsigned char u[4]={0};float back[4]={0};
 OK(cnrtCastDataType(f,CNRT_FLOAT32,i,CNRT_INT32,4,NULL));CHECK(i[0]==-2&&i[1]==0&&i[2]==2&&i[3]==256);
 OK(cnrtCastDataType(f,CNRT_FLOAT32,u,CNRT_UINT8,4,NULL));CHECK(u[0]==0&&u[1]==0&&u[2]==1&&u[3]==255);
 OK(cnrtCastDataType(i,CNRT_INT32,back,CNRT_FLOAT32,4,NULL));CHECK(back[0]==-2&&back[2]==2&&back[3]==256);
 cnrtQuantizedParam_t param=NULL; OK(cnrtCreateQuantizedParam(&param,2,0.5f,3)); OK(cnrtDestroyQuantizedParam(param));
 int poses[2]={1,2},dims[2]={2,4};float scales[2]={0.5f,0.25f},offsets[2]={3,4}; OK(cnrtCreateQuantizedParamByChannel(&param,poses,scales,offsets,2,dims,0)); OK(cnrtDestroyQuantizedParam(param));
 printf("PASS cnrtCastDataType f32/i32/u8: %d %d %d %d / %u %u %u %u\\n",i[0],i[1],i[2],i[3],u[0],u[1],u[2],u[3]);return 0;}
