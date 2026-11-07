#ifndef MLU_RESOURCE_LIMITS_H
#define MLU_RESOURCE_LIMITS_H

#include <stdint.h>

enum mlu_resource_kind { MLU_RESOURCE_DEVICE, MLU_RESOURCE_STAGING,
                         MLU_RESOURCE_IMAGE, MLU_RESOURCE_KINDS };
typedef struct { uint64_t bytes[MLU_RESOURCE_KINDS]; } mlu_resource_usage;

/* Configure once, before starting workers. Charges include reservations while
 * a native allocation is in progress. Session use is confined to its worker. */
int mlu_resource_limits_init(void);
int mlu_resource_reserve(mlu_resource_usage *usage, enum mlu_resource_kind kind,
                         uint64_t bytes);
void mlu_resource_release(mlu_resource_usage *usage, enum mlu_resource_kind kind,
                          uint64_t bytes);
uint32_t mlu_resource_max_connections(void);
uint32_t mlu_resource_max_transfers(void);
void mlu_resource_report(void);
void mlu_resource_transfer(mlu_resource_usage *from,mlu_resource_usage *to,
                           enum mlu_resource_kind kind,uint64_t bytes);

#endif
