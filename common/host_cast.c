#include <cnrt.h>
#include <fenv.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Both public enum spaces use this host-only engine. No device, RPC or vendor
 * library is needed. Pair masks reflect CNRT 6.5.2 on AArch64 (not merely the
 * list of enum constants). See docs/HOST_CAST.md for native numerical quirks. */
enum kind { D, F, H, B, U64, U32, U16, U8, I64, I32, I16, I8, BOOL, NK };
static const unsigned char widths[NK]={8,4,2,2,8,4,2,1,8,4,2,1,1};
struct cnrtQuantizedParam {
  unsigned magic;
  int pos, offset, channels;
  float scale;
  size_t inner, total;
  int *poses;
  float *scales, *offsets;
};
#define QUANT_MAGIC 0x514e5451u
#define BIT(k) (1u<<(k))
#define FP (BIT(F)|BIT(H)|BIT(B))
#define INTS (BIT(U64)|BIT(U32)|BIT(U16)|BIT(U8)|BIT(I64)|BIT(I32)|BIT(I16)|BIT(I8))
static const unsigned short plain[2][NK]={
  {[F]=BIT(F)|BIT(H)|BIT(I8)|BIT(I16)|BIT(I32)|BIT(I64)|BIT(U8)|BIT(BOOL),
   [H]=BIT(F)|BIT(I8)|BIT(I16)|BIT(I32)|BIT(U8),
   [I8]=BIT(F)|BIT(H),[I16]=BIT(F)|BIT(H),[I32]=BIT(F)|BIT(H),
   [I64]=BIT(F)|BIT(H),[U8]=BIT(F)|BIT(H),[BOOL]=BIT(F)},
  {[D]=BIT(H)|BIT(B),[F]=FP|INTS|BIT(BOOL),
   [H]=BIT(F)|BIT(U32)|BIT(U16)|BIT(U8)|BIT(I64)|BIT(I32)|BIT(I16)|BIT(I8),
   [B]=BIT(F)|BIT(U64)|BIT(U32)|BIT(U16)|BIT(I64)|BIT(I32),
   [U64]=FP,[U32]=FP,[U16]=FP,[U8]=FP,[I64]=FP,[I32]=FP,[I16]=FP,[I8]=FP,[BOOL]=BIT(F)}
};
static const unsigned short quant[2][NK]={
  {[F]=BIT(F)|BIT(H)|BIT(I8)|BIT(I16)|BIT(I32)|BIT(BOOL),[H]=BIT(I8)|BIT(I16),
   [I8]=BIT(F)|BIT(H),[I16]=BIT(F)|BIT(H),[I32]=BIT(F)},
  {[D]=BIT(B),[F]=BIT(F)|BIT(H)|BIT(I8)|BIT(I16)|BIT(I32)|BIT(BOOL),
   [H]=BIT(I8)|BIT(I16),[I8]=FP,[I16]=BIT(F)|BIT(H),
   [U8]=BIT(F),[I64]=BIT(F),[BOOL]=BIT(F)}
};

cnrtRet_t cnrtCreateQuantizedParam(cnrtQuantizedParam_t *out,int pos,float scale,int offset) {
  if(!out) return cnrtErrorArgsInvalid;
  struct cnrtQuantizedParam *p=calloc(1,sizeof(*p));
  if(!p) return cnrtErrorSysNoMem;
  p->magic=QUANT_MAGIC; p->pos=pos; p->scale=scale; p->offset=offset;
  *out=p; return cnrtSuccess;
}
cnrtRet_t cnrtCreateQuantizedParamByChannel(cnrtQuantizedParam_t *out,int *poses,float *scales,float *offsets,int ndim,int *dims,int axis) {
  if(!out||!poses||!scales||!offsets||!dims||ndim<=0||axis<0||axis>=ndim) return cnrtErrorArgsInvalid;
  size_t total=1,inner=1;
  for(int i=0;i<ndim;++i) {
    if(dims[i]<=0 || total>SIZE_MAX/(unsigned)dims[i]) return cnrtErrorArgsInvalid;
    total*=dims[i]; if(i>axis) inner*=dims[i];
  }
  struct cnrtQuantizedParam *p=calloc(1,sizeof(*p));
  if(!p) return cnrtErrorSysNoMem;
  size_t bytes=(size_t)dims[axis]*sizeof(int);
  p->poses=malloc(bytes);p->scales=malloc(bytes);p->offsets=malloc(bytes);
  if(!p->poses||!p->scales||!p->offsets) {
    free(p->poses);free(p->scales);free(p->offsets);free(p);return cnrtErrorSysNoMem;
  }
  memcpy(p->poses,poses,bytes);memcpy(p->scales,scales,bytes);memcpy(p->offsets,offsets,bytes);
  p->channels=dims[axis];p->inner=inner;p->total=total;p->magic=QUANT_MAGIC;
  *out=p;return cnrtSuccess;
}
cnrtRet_t cnrtDestroyQuantizedParam(cnrtQuantizedParam_t p) {
  if(!p) return cnrtSuccess;
  if(p->magic!=QUANT_MAGIC) return cnrtErrorArgsInvalid;
  p->magic=0;free(p->poses);free(p->scales);free(p->offsets);free(p);return cnrtSuccess;
}
static uint32_t bits(float x) { uint32_t u;memcpy(&u,&x,4);return u; }
static float from_bits(uint32_t u) { float x;memcpy(&x,&u,4);return x; }
static int32_t offset_integer(float x) {
  if(isnan(x)) return 0;
  if(x >= 0x1p31f) return INT32_MAX;
  if(x < -0x1p31f) return INT32_MIN;
  return (int32_t)x;
}
static float half_float(uint16_t h) {
  unsigned sign=(h&0x8000u)<<16,exp=(h>>10)&31,m=h&1023;
  if(!exp) {
    if(!m) return from_bits(sign);
    int e=-14;while(!(m&1024)) { m<<=1;--e; }
    return from_bits(sign|((unsigned)(e+127)<<23)|((m&1023)<<13));
  }
  return from_bits(sign|(exp==31?0x7f800000u:(exp+112)<<23)|(m<<13));
}
static int increment(uint32_t mant,int shift,unsigned truncated,int sign,int mode) {
  if(!mant) return 0;
  if(mode==cnrtRounding_rz) return 0;
  if(mode==cnrtRounding_rd) return sign;
  if(mode==cnrtRounding_ru) return !sign;
  if(mode==cnrtRounding_ro) return 1;
  if(shift>31) return 0;
  uint32_t mid=1u<<(shift-1);
  return mant>mid || (mant==mid && (truncated&1));
}
static uint16_t float_half(float x,int mode) {
  uint32_t u=bits(x),m=u&0x7fffffu;unsigned sign=(u>>16)&0x8000u;
  int e=(int)((u>>23)&255)-112;
  if(((u>>23)&255)==255) return sign|0x7c00u|(m?0x200u:0);
  if(e>=31) {
    int finite=mode==cnrtRounding_rz || (mode==cnrtRounding_rd&&!sign) || (mode==cnrtRounding_ru&&sign);
    return sign|(finite?0x7bffu:0x7c00u);
  }
  int shift=13;
  if(e<=0) { m|=(((u>>23)&255)!=0)?0x800000u:0;shift=14-e;e=0; }
  unsigned out=shift>31?0:m>>shift;
  uint32_t rem=shift>31?m:m&((1u<<shift)-1);
  out+=increment(rem,shift,out,!!sign,mode);
  return sign|(((unsigned)e<<10)+out);
}
static uint16_t float_bfloat(float x,int mode) {
  uint32_t u=bits(x);uint16_t hi=u>>16;unsigned lo=u&65535;
  if((u&0x7f800000u)==0x7f800000u) return hi|(lo?64:0);
  if(!lo) return hi;
  /* Native BF16 directed modes adjust the truncated bit representation. */
  if(mode==cnrtRounding_rd || (mode==cnrtRounding_ro&&(u>>31))) return hi-1;
  if(mode==cnrtRounding_ru || mode==cnrtRounding_ro) return hi+1;
  if(mode==cnrtRounding_rz) return hi;
  return hi+(lo>32768 || (lo==32768 && (mode==cnrtRounding_rm || (hi&1))));
}
#define LOAD(T) do { T v;memcpy(&v,(const char *)src+i*sizeof(v),sizeof(v));return (float)v; } while(0)
static float read_float(const void *src,int k,size_t i) {
  switch(k) {
    case D:LOAD(double);case F:LOAD(float);
    case H:{uint16_t h;memcpy(&h,(const char *)src+2*i,2);return half_float(h);}
    case B:{uint16_t h;memcpy(&h,(const char *)src+2*i,2);return from_bits((uint32_t)h<<16);}
    case U64:LOAD(uint64_t);case U32:LOAD(uint32_t);case U16:LOAD(uint16_t);case U8:LOAD(uint8_t);
    case I64:LOAD(int64_t);case I32:LOAD(int32_t);case I16:LOAD(int16_t);case I8:LOAD(int8_t);
    default:LOAD(uint8_t);
  }
}
#undef LOAD
static double rounded(double x,int mode) {
  switch(mode) {
    case cnrtRounding_rn:return 2*round(x/2); /* CNRT 6.5.2 integer RN */
    case cnrtRounding_rz:return trunc(x);
    case cnrtRounding_rd:return floor(x);
    case cnrtRounding_ru:return ceil(x);
    case cnrtRounding_ro:return x<0?floor(x):ceil(x);
    default:return round(x);
  }
}
#define STORE(T,value) do { T v=(value);memcpy((char *)dst+i*sizeof(v),&v,sizeof(v)); } while(0)
static void write_int(void *dst,int k,size_t i,double x,int wrap) {
  unsigned n=widths[k]*8;int uns=k>=U64&&k<=U8;
  if(isnan(x)) x=0;
  if(wrap) {
    /* Half->small integer kernels narrow after signed 32-bit conversion. */
    if(x>INT32_MAX)x=INT32_MAX;if(x<INT32_MIN)x=INT32_MIN;
  } else if(uns) {
    if(x<0)x=0;
    if(x>=ldexp(1,n)) { uint64_t u=UINT64_MAX>>(64-n);memcpy((char *)dst+i*widths[k],&u,widths[k]);return; }
  } else {
    if(x>=ldexp(1,n-1)) { int64_t s=INT64_MAX>>(64-n);memcpy((char *)dst+i*widths[k],&s,widths[k]);return; }
    if(x< -ldexp(1,n-1)) x=-ldexp(1,n-1);
  }
  uint64_t u=uns&&!wrap?(uint64_t)x:(uint64_t)(int64_t)x;
  memcpy((char *)dst+i*widths[k],&u,widths[k]);
}
static cnrtRet_t convert(const void *src,int s,void *dst,int d,int count,cnrtQuantizedParam_t p,int mode,int v2) {
  if(v2&&count==0) return cnrtSuccess;
  if(!src||!dst||count<0||s<0||d<0||mode<0||mode>=cnrtRounding_max) return cnrtErrorArgsInvalid;
  if(p&&p->magic!=QUANT_MAGIC) return cnrtErrorArgsInvalid;
  if(p&&p->channels) {
    if(s!=F||(d!=I8&&d!=I16)||(size_t)count!=p->total) return cnrtErrorArgsInvalid;
  } else if(!((p?quant:plain)[v2][s]&BIT(d))) return cnrtErrorArgsInvalid;
  /* These native scalar-quantized dispatches return success without writing. */
  if(p&&v2&&((s==D&&d==B)||(s==I8&&d==B))) return cnrtSuccess;
  if(s==F&&d==F) { memmove(dst,src,(size_t)count*4);return cnrtSuccess; }
  for(int i=0;i<count;++i) {
    float x=read_float(src,s,i);int outmode=mode;
    if(p) {
      int c=p->channels?(int)(((size_t)i/p->inner)%p->channels):0;
      int pos=p->channels?p->poses[c]:p->pos;
      float scale=p->channels?p->scales[c]:p->scale;
      float offset=p->channels?p->offsets[c]:(float)p->offset;
      uint32_t offset_bits=(uint32_t)(p->channels?offset_integer(offset):p->offset);
      if((s==F||s==H)&&(d==I8||d==I16)) {
        x=(float)((double)fmaf(x,scale,-from_bits(offset_bits))/pow(2,pos));
        if(!v2) outmode=cnrtRounding_rm;
      } else if(s==F&&d==H) {
        x=(float)((double)(x*scale)/pow(2,pos));
        if(!v2) { write_int(dst,I16,i,rounded(x,cnrtRounding_rm),1);continue; }
      } else if((s==I8||s==I16)&&(d==F||d==H)) {
        float off=v2?offset:from_bits(offset_bits);
        x=((float)((double)x*pow(2,pos))+off)/scale;
      } else if(s==F&&d==I32) outmode=cnrtRounding_rz;
    }
    if(d==F) { STORE(float,x); }
    else if(d==H) {
      uint16_t h=float_half(x,s>=U64?cnrtRounding_rn:outmode);
      if(!v2) {
        if(s==I16&&!p) h=float_half(x,cnrtRounding_rz);
        else {
          /* Legacy converter rounds ties away, clamps pre-existing overflow,
           * and maps every nonzero underflow (including -0) to min subnormal. */
          if((bits(x)&0x7fffffffu)>=0x47800000u) h=(signbit(x)?0x8000:0)|0x7bff;
          else if((x!=0&&fabsf(x)<0x1p-24f)||bits(x)==0x80000000u) h=(signbit(x)?0x8000:0)|1;
          else {
            unsigned exp=(bits(x)>>23)&255;
            if(exp>=113 && (bits(x)&8191)==4096) h=float_half(x,cnrtRounding_ro);
          }
        }
      }
      STORE(uint16_t,h);
    }
    else if(d==B) { STORE(uint16_t,float_bfloat(x,s>=U64?cnrtRounding_rn:outmode)); }
    else if(d==BOOL) { STORE(uint8_t,x!=0); }
    else {
      /* The legacy float->int64 kernel clamps at +/-2^32. */
      if(!v2&&s==F&&d==I64) x=fmaxf(-0x1p32f,fminf(0x1p32f,x));
      if(!v2&&d==U8) outmode=cnrtRounding_rz;
      if(v2&&outmode==cnrtRounding_rz) fesetround(FE_TOWARDZERO);
      int wrap=v2&&s==H&&!p&&(d==I8||d==I16||d==U8||(d==U16&&outmode==cnrtRounding_rn));
      if(v2&&s==H&&d==U8&&x<0) x=0;
      write_int(dst,d,i,rounded(x,outmode),wrap);
    }
  }
  return cnrtSuccess;
}
#undef STORE
static int kind_v2(cnrtDataType_V2_t t) {
  switch(t) {
    case cnrtDouble:return D;case cnrtFloat:return F;case cnrtHalf:return H;case cnrtBfloat:return B;
    case cnrtUlonglong:return U64;case cnrtUint:return U32;case cnrtUshort:return U16;case cnrtUchar:return U8;
    case cnrtLonglong:return I64;case cnrtInt:return I32;case cnrtShort:return I16;case cnrtChar:return I8;
    case cnrtBoolean:return BOOL;default:return -1;
  }
}
static int kind_v1(cnrtDataType_t t) {
  switch(t) {
    case CNRT_FLOAT16:return H;case CNRT_FLOAT32:return F;case CNRT_FLOAT64:return D;
    case CNRT_INT8:return I8;case CNRT_INT16:return I16;case CNRT_INT32:return I32;case CNRT_INT64:return I64;
    case CNRT_UINT8:return U8;case CNRT_UINT16:return U16;case CNRT_UINT32:return U32;case CNRT_BOOL:return BOOL;
    default:return -1;
  }
}
cnrtRet_t cnrtCastDataType(void *src,cnrtDataType_t s,void *dst,cnrtDataType_t d,int n,cnrtQuantizedParam_t p) {
  return convert(src,kind_v1(s),dst,kind_v1(d),n,p,cnrtRounding_rm,0);
}
cnrtRet_t cnrtCastDataType_V2(const void *src,cnrtDataType_V2_t s,void *dst,cnrtDataType_V2_t d,int n,cnrtQuantizedParam_t p,cnrtRoundingMode_t mode) {
  return convert(src,kind_v2(s),dst,kind_v2(d),n,p,mode,1);
}
