#ifndef HYPERSINV_VMX_CR3_TARGET_PROVIDER_H
#define HYPERSINV_VMX_CR3_TARGET_PROVIDER_H

/* WDK-only capture of a referenced process kernel CR3 target. */
#include "vmx_cr3_target_selection.h"

#ifdef __cplusplus
extern "C" {
#endif

/* PASSIVE-only. Process must remain referenced by the caller for the entire
 * capture. Generation is caller-owned, nonzero identity. No process pointer is
 * retained and the output remains zero on failure. */
VMX_CR3_TARGET_STATUS VmxPersistentProcessCr3TargetCapture(
    void* Process,
    uint64_t Generation,
    VMX_CR3_TARGET_SET* OutTargetSet);

typedef void (*VMX_PROCESS_ATTACHED_VISITOR)(void* Context);

/* PASSIVE-only synchronous attach seam. Process remains caller-referenced;
 * Visitor runs exactly once in that address-space context. */
VMX_CR3_TARGET_STATUS VmxPersistentProcessVisitAttached(
    void* Process,
    void* Context,
    VMX_PROCESS_ATTACHED_VISITOR Visitor);

/* PASSIVE-only. Captures exactly one normalized System-process kernel root,
 * physical-address width, PCIDE state, and a nonzero freshness generation.
 * PsInitialSystemProcess is system-lifetime storage; no caller or process
 * pointer is retained. OutTargetSet must be zero and remains unchanged on
 * failure. This function captures metadata only and performs no allocation,
 * VMX instruction, VMCS access, route binding, or lifecycle transition. */
VMX_CR3_TARGET_STATUS VmxPersistentSystemCr3TargetCapture(
    VMX_CR3_TARGET_SET* OutTargetSet);

#ifdef __cplusplus
}
#endif

#endif
