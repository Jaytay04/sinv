#ifndef HYPERSINV_VMX_CR3_STATUS_ABI_H
#define HYPERSINV_VMX_CR3_STATUS_ABI_H
/* Fixed-width, pointer-free public CR3 routing status ABI. CR3 roots are redacted. */
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define HYPERVISOR_CR3_STATUS_VERSION UINT32_C(1)
#define HYPERVISOR_CR3_STATUS_HEADER_SIZE UINT32_C(64)
#define HYPERVISOR_CR3_STATUS_RECORD_SIZE UINT32_C(32)
#define HYPERVISOR_CR3_STATUS_NO_INDEX UINT32_MAX
#define HYPERVISOR_CR3_STATUS_MAX_TARGETS UINT32_C(2)
#define HYPERVISOR_CR3_STATUS_MAX_PCID UINT32_C(0xfff)
#define HYPERVISOR_CR3_STATUS_MIN_PHYSICAL_BITS UINT32_C(36)
#define HYPERVISOR_CR3_STATUS_MAX_PHYSICAL_BITS UINT32_C(52)
#define HYPERVISOR_CR3_STATUS_FLAG_AUTHENTICATED (UINT32_C(1)<<0)
#define HYPERVISOR_CR3_STATUS_FLAG_PCIDE_ENABLED (UINT32_C(1)<<1)
#define HYPERVISOR_CR3_STATUS_FLAG_ANY_RESIDENT (UINT32_C(1)<<2)
#define HYPERVISOR_CR3_STATUS_FLAG_ANY_TRANSITION (UINT32_C(1)<<3)
typedef struct HYPERVISOR_CR3_STATUS_HEADER {
 uint32_t Version,HeaderSize,RecordSize,TotalSize;
 uint64_t OwnerGeneration,TargetGeneration;
 uint32_t ContextCount,TargetCount,PhysicalAddressBits,Flags;
 uint32_t ResidentCount,Reserved32;
 uint64_t TotalTransitions;
} HYPERVISOR_CR3_STATUS_HEADER;
typedef struct HYPERVISOR_CR3_STATUS_RECORD {
 uint32_t SystemIndex;
 uint16_t Group;
 uint8_t Number,Resident;
 uint32_t MatchedIndex,Pcid;
 uint64_t TransitionSequence,Reserved;
} HYPERVISOR_CR3_STATUS_RECORD;
#ifdef __cplusplus
}
#define HYPERSINV_CR3_ABI_ASSERT static_assert
#define HYPERSINV_CR3_ABI_ALIGNOF alignof
#else
#define HYPERSINV_CR3_ABI_ASSERT _Static_assert
#define HYPERSINV_CR3_ABI_ALIGNOF _Alignof
#endif
HYPERSINV_CR3_ABI_ASSERT(sizeof(HYPERVISOR_CR3_STATUS_HEADER)==64,"CR3 header ABI");
HYPERSINV_CR3_ABI_ASSERT(sizeof(HYPERVISOR_CR3_STATUS_RECORD)==32,"CR3 record ABI");
HYPERSINV_CR3_ABI_ASSERT(HYPERSINV_CR3_ABI_ALIGNOF(HYPERVISOR_CR3_STATUS_HEADER)==8,"CR3 header alignment");
HYPERSINV_CR3_ABI_ASSERT(offsetof(HYPERVISOR_CR3_STATUS_HEADER,TotalTransitions)==56,"CR3 header offsets");
HYPERSINV_CR3_ABI_ASSERT(offsetof(HYPERVISOR_CR3_STATUS_RECORD,TransitionSequence)==16,"CR3 record offsets");
#undef HYPERSINV_CR3_ABI_ASSERT
#undef HYPERSINV_CR3_ABI_ALIGNOF
#endif