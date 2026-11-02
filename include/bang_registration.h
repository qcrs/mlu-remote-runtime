#ifndef MLU_BANG_REGISTRATION_H
#define MLU_BANG_REGISTRATION_H
/* Private ABI transcribed from CNCC 4.5.1 bang.h preprocessing. Include cnrt.h
 * first. This header pins the measured signatures, not a portable SDK promise. */
cnrtRet_t __bangPushCallConfiguration(cnrtDim3_t, cnrtFunctionType_t, cnrtQueue_t);
cnrtRet_t __bangPopCallConfiguration(cnrtDim3_t *, cnrtFunctionType_t *, cnrtQueue_t *);
void **__bangRegisterFatBinary(void *);
void __bangUnregisterFatBinary(void **);
void __bangRegisterFunction(void **, const char *, char *, const char *, int,
                            void *, void *, cnrtDim3_t *, cnrtDim3_t *, int *);
void __bangRegisterVar(void **, char *, char *, const char *, int, int, int, int);
#endif
