#ifndef HYPERSINV_VMX_EPT_HOOK_TRANSACTION_H
#define HYPERSINV_VMX_EPT_HOOK_TRANSACTION_H
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define VMX_EPT_HOOK_PAGE_BYTES UINT64_C(4096)
#define VMX_EPT_HOOK_LARGE_PAGE_BYTES UINT64_C(0x200000)
#define VMX_EPT_HOOK_PTE_COUNT UINT32_C(512)
#define VMX_EPT_HOOK_PERMISSION_MASK UINT8_C(7)
typedef enum VMX_EPT_HOOK_STATUS { VMX_EPT_HOOK_OK=0, VMX_EPT_HOOK_INVALID=-1, VMX_EPT_HOOK_UNSUPPORTED=-2, VMX_EPT_HOOK_BUSY=-3, VMX_EPT_HOOK_INVARIANT=-4 } VMX_EPT_HOOK_STATUS;
typedef enum VMX_EPT_HOOK_STAGE { VMX_EPT_HOOK_STAGE_FREE=0, VMX_EPT_HOOK_STAGE_PREPARED=1, VMX_EPT_HOOK_STAGE_COMMITTED=2, VMX_EPT_HOOK_STAGE_ROLLBACK_PENDING=3 } VMX_EPT_HOOK_STAGE;
typedef struct VMX_EPT_HOOK_PLAN { uint64_t GuestPhysicalPage,PageTablePhysicalAddress,OriginalParentEntry,ReplacementParentEntry,OriginalTargetEntry,HookedTargetEntry; uint32_t TargetIndex; uint8_t TrapPermissions,Reserved8[3]; uint32_t Reserved32; } VMX_EPT_HOOK_PLAN;
typedef struct VMX_EPT_HOOK_RECORD { uint32_t Stage,Reserved; uint64_t Generation; VMX_EPT_HOOK_PLAN Plan; } VMX_EPT_HOOK_RECORD;
/* Pure allocation-free 2 MiB-to-4 KiB planning; outputs remain unchanged on failure. */
VMX_EPT_HOOK_STATUS VmxEptHookPlan2Mb(uint64_t,uint8_t,uint64_t,uint64_t,uint64_t[VMX_EPT_HOOK_PTE_COUNT],VMX_EPT_HOOK_PLAN*);
bool VmxEptHookPlanValid(const VMX_EPT_HOOK_PLAN*);
bool VmxEptHookRecordValid(const VMX_EPT_HOOK_RECORD*);
VMX_EPT_HOOK_STATUS VmxEptHookPrepare(const VMX_EPT_HOOK_PLAN*,uint64_t,VMX_EPT_HOOK_RECORD*);
VMX_EPT_HOOK_STATUS VmxEptHookCommit(VMX_EPT_HOOK_RECORD*,uint64_t,uint64_t);
VMX_EPT_HOOK_STATUS VmxEptHookBeginRollback(VMX_EPT_HOOK_RECORD*,uint64_t);
VMX_EPT_HOOK_STATUS VmxEptHookCompleteRollback(VMX_EPT_HOOK_RECORD*,uint64_t,uint64_t);
VMX_EPT_HOOK_STATUS VmxEptHookAbortPrepare(VMX_EPT_HOOK_RECORD*,uint64_t);
#ifdef __cplusplus
}
#endif
#endif
