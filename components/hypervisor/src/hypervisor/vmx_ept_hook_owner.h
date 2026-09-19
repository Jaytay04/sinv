#ifndef HYPERSINV_VMX_EPT_HOOK_OWNER_H
#define HYPERSINV_VMX_EPT_HOOK_OWNER_H
#include "vmx_ept_hook_location.h"
#include "vmx_ept_session_operations.h"
#ifdef __cplusplus
extern "C" {
#endif
#define VMX_EPT_HOOK_OWNER_MAGIC UINT32_C(0x4B485045)
#define VMX_EPT_HOOK_OWNER_VERSION UINT32_C(1)
#define VMX_EPT_HOOK_OWNER_READY UINT32_C(1)
typedef enum VMX_EPT_HOOK_OWNER_STATUS { VMX_EPT_HOOK_OWNER_OK=0,VMX_EPT_HOOK_OWNER_INVALID=-1,VMX_EPT_HOOK_OWNER_UNSUPPORTED=-2,VMX_EPT_HOOK_OWNER_ALLOCATION_FAILED=-3,VMX_EPT_HOOK_OWNER_BUSY=-4,VMX_EPT_HOOK_OWNER_NOT_FOUND=-5,VMX_EPT_HOOK_OWNER_DIRTY=-6,VMX_EPT_HOOK_OWNER_INVARIANT=-7 } VMX_EPT_HOOK_OWNER_STATUS;
typedef struct VMX_EPT_HOOK_SLOT { VMX_EPT_HOOK_RECORD Record; VMX_EPT_HOOK_LOCATION Location; void* PageTable; uint64_t PageTablePhysicalAddress; uint64_t Reserved[2]; } VMX_EPT_HOOK_SLOT;
/* Externally serialized allocation-owned hook graph. Prepare writes one
 * split parent only after reserving a session mutation; Commit requires exact
 * invalidation coverage. BeginUnhook/CompleteUnhook restore the original parent
 * and free the split page only after a second covered mutation. Initialize
 * admits exactly the authenticated allocator embedded in the session or a
 * pairwise-disjoint external allocator, while rejecting every other allocator
 * overlap with the session-owned graph and storage. The output remains disjoint
 * from both admitted inputs. It then freezes the allocator for allocation,
 * unwind, and retained ownership. Destroy rejects every active or pending slot. */
typedef struct VMX_EPT_HOOK_OWNER { uint32_t Magic,Version,Stage,Capacity,ActiveCount,Reserved32; VMX_PERSISTENT_ALLOCATOR Allocator,AllocatorContract; VMX_EPT_SESSION_OWNER* Session; VMX_EPT_SESSION_OWNER* SessionContract; VMX_EPT_HOOK_SLOT* Slots; uint64_t Reserved[2]; } VMX_EPT_HOOK_OWNER;
VMX_EPT_HOOK_OWNER_STATUS VmxEptHookOwnerInitialize(VMX_EPT_SESSION_OWNER*,uint32_t,const VMX_PERSISTENT_ALLOCATOR*,VMX_EPT_HOOK_OWNER*);
VMX_EPT_HOOK_OWNER_STATUS VmxEptHookOwnerValidate(const VMX_EPT_HOOK_OWNER*);
VMX_EPT_HOOK_OWNER_STATUS VmxEptHookOwnerQuery(const VMX_EPT_HOOK_OWNER*,uint64_t,uint32_t*,uint32_t*);
VMX_EPT_HOOK_OWNER_STATUS VmxEptHookOwnerQueryFirst(const VMX_EPT_HOOK_OWNER*,uint64_t*,uint32_t*,uint32_t*);
VMX_EPT_HOOK_OWNER_STATUS VmxEptHookOwnerValidateExternalSpan(const VMX_EPT_HOOK_OWNER*,const void*,size_t);
VMX_EPT_HOOK_OWNER_STATUS VmxEptHookOwnerPrepare(VMX_EPT_HOOK_OWNER*,uint64_t,uint8_t,uint32_t*);
VMX_EPT_HOOK_OWNER_STATUS VmxEptHookOwnerCommit(VMX_EPT_HOOK_OWNER*,uint32_t);
VMX_EPT_HOOK_OWNER_STATUS VmxEptHookOwnerBeginUnhook(VMX_EPT_HOOK_OWNER*,uint64_t,uint32_t*);
VMX_EPT_HOOK_OWNER_STATUS VmxEptHookOwnerCompleteUnhook(VMX_EPT_HOOK_OWNER*,uint32_t);
VMX_EPT_HOOK_OWNER_STATUS VmxEptHookOwnerDestroy(VMX_EPT_HOOK_OWNER*);
#ifdef __cplusplus
}
#endif
#endif
