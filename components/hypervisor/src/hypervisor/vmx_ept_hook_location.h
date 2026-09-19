#ifndef HYPERSINV_VMX_EPT_HOOK_LOCATION_H
#define HYPERSINV_VMX_EPT_HOOK_LOCATION_H
#include <stdbool.h>
#include <stdint.h>
#include "vmx_ept_domain.h"
#include "vmx_ept_hook_transaction.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum VMX_EPT_HOOK_LOCATION_STATUS { VMX_EPT_HOOK_LOCATION_OK=0, VMX_EPT_HOOK_LOCATION_INVALID=-1, VMX_EPT_HOOK_LOCATION_UNSUPPORTED=-2, VMX_EPT_HOOK_LOCATION_NOT_MAPPED=-3 } VMX_EPT_HOOK_LOCATION_STATUS;
typedef struct VMX_EPT_HOOK_LOCATION { uint64_t GuestPhysicalPage,ObservedParentEntry,MutationGeneration; uint32_t ParentPageOrdinal,ParentEntryIndex; uint64_t Reserved; } VMX_EPT_HOOK_LOCATION;
/* Resolves only an authenticated 2 MiB identity leaf. No internal pointer escapes
 * and the disjoint output remains unchanged on failure. */
VMX_EPT_HOOK_LOCATION_STATUS VmxEptHookLocate2Mb(const VMX_EPT_DOMAIN*,uint64_t,VMX_EPT_HOOK_LOCATION*);
bool VmxEptHookLocationValid(const VMX_EPT_DOMAIN*,const VMX_EPT_HOOK_LOCATION*);
#ifdef __cplusplus
}
#endif
#endif
