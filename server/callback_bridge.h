#ifndef MLUR_CALLBACK_BRIDGE_H
#define MLUR_CALLBACK_BRIDGE_H
#include <stdint.h>
typedef struct mlu_callback_service mlu_callback_service;
mlu_callback_service *mlu_callback_create(int primary_fd,uint32_t timeout_ms,uint64_t secret[2]);
void mlu_callback_retain(mlu_callback_service *s);
void mlu_callback_release(mlu_callback_service *s);
void mlu_callback_abort(mlu_callback_service *s,int poison);
int mlu_callback_finish(mlu_callback_service *s);
int mlu_callback_attach(int fd,const unsigned char *secret,uint32_t bytes,uint64_t request,uint32_t timeout_ms);
int mlu_callback_exchange(mlu_callback_service *s,unsigned kind,uint64_t run,uint64_t token,void *data,uint32_t bytes,int wait);
void mlu_callback_user_add(mlu_callback_service *s);
void mlu_callback_user_done(mlu_callback_service *s);
#endif
