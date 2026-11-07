#ifndef MLUR_REMOTE_CALLBACK_H
#define MLUR_REMOTE_CALLBACK_H
#include <arpa/inet.h>
#include <string.h>
#include "remote_protocol.h"
#define MLU_CALLBACK_MAX_IDENTITIES 4096u
#define MLU_CALLBACK_MAX_RUNS 1024u
enum { MLU_CALLBACK_CALL=1,MLU_CALLBACK_H2D=2,MLU_CALLBACK_D2H=3,MLU_CALLBACK_DONE=4,MLU_CALLBACK_DESTROY=5,MLU_CALLBACK_CLOSE=6,MLU_CALLBACK_QUEUE=7 };
static inline uint64_t cb_swap64(uint64_t x) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  return ((uint64_t)htonl((uint32_t)x)<<32)|htonl((uint32_t)(x>>32));
#else
  return x;
#endif
}
static inline void cb_put64(void *p,uint64_t x) {x=cb_swap64(x);memcpy(p,&x,8);}
static inline uint64_t cb_get64(const void *p) {uint64_t x;memcpy(&x,p,8);return cb_swap64(x);}
static inline struct mlu_remote_wire_header cb_header(uint64_t id,uint32_t bytes,uint32_t status) {
  return (struct mlu_remote_wire_header){htonl(MLU_REMOTE_MAGIC),htons(MLU_REMOTE_VERSION),htons(MLU_OP_CALLBACK_ATTACH),cb_swap64(id),htonl(bytes),htonl(status)};
}
static inline int cb_header_valid(const struct mlu_remote_wire_header *h,uint64_t id) {
  return ntohl(h->magic)==MLU_REMOTE_MAGIC && ntohs(h->version)==MLU_REMOTE_VERSION && ntohs(h->opcode)==MLU_OP_CALLBACK_ATTACH && cb_swap64(h->request_id)==id;
}
#endif
