#ifndef HYPERSINV_VMX_CR3_PUBLIC_STATUS_H
#define HYPERSINV_VMX_CR3_PUBLIC_STATUS_H
#include <stddef.h>
#include <stdint.h>
#include "vmx_cr3_status_abi.h"
#include "vmx_cr3_target_selection.h"
#include "vmx_cr3_residency.h"
#include "vmx_processor_topology.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct VMX_CR3_PUBLIC_STATUS_VIEW {
 uint32_t Version,HeaderSize,RecordSize,Reserved;
 uint64_t OwnerGeneration;
 const VMX_CR3_TARGET_SET* Target;
 const VMX_PROCESSOR_IDENTITY* Identities;
 size_t IdentityCount;
 const VMX_CR3_RESIDENCY_STATE* States;
 size_t StateCount;
} VMX_CR3_PUBLIC_STATUS_VIEW;
typedef enum VMX_CR3_PUBLIC_STATUS_RESULT {
 VMX_CR3_PUBLIC_STATUS_OK=0,VMX_CR3_PUBLIC_STATUS_INVALID=-1,
 VMX_CR3_PUBLIC_STATUS_BUFFER_TOO_SMALL=-2,VMX_CR3_PUBLIC_STATUS_OVERFLOW=-3
} VMX_CR3_PUBLIC_STATUS_RESULT;
/* Encodes exact 64+32*N bytes and never publishes CR3 roots. Before source/output
 * separateness is established, failures preserve output and RequiredSize. After
 * separateness, failures clear both. No partial wire image is published. */
VMX_CR3_PUBLIC_STATUS_RESULT VmxCr3PublicStatusEncode(
 const VMX_CR3_PUBLIC_STATUS_VIEW* View,void* Output,size_t OutputSize,
 size_t* RequiredSize);
#ifdef __cplusplus
}
#endif
#endif