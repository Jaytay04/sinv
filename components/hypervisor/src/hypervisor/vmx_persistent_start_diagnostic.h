#ifndef HYPERSINV_VMX_PERSISTENT_START_DIAGNOSTIC_H
#define HYPERSINV_VMX_PERSISTENT_START_DIAGNOSTIC_H
/* WDK-free capture of the persistent Start failure before rollback. */
#include "vmx_persistent_activate.h"
#include "vmx_persistent_coordinator.h"
#ifdef __cplusplus
extern "C" {
#endif
#define VMX_PERSISTENT_START_DIAGNOSTIC_VERSION UINT32_C(5)
#define VMX_PERSISTENT_START_DIAGNOSTIC_NO_CPU_INDEX UINT32_MAX
#define VMX_PERSISTENT_START_DIAGNOSTIC_UNKNOWN_STATE UINT32_MAX
typedef enum _VMX_PERSISTENT_START_FAILURE_KIND {
 VMX_PERSISTENT_START_FAILURE_UNKNOWN=0, VMX_PERSISTENT_START_FAILURE_PREPARE_KERNEL=1,
 VMX_PERSISTENT_START_FAILURE_ACTIVATE_KERNEL=2, VMX_PERSISTENT_START_FAILURE_VMXON=3,
 VMX_PERSISTENT_START_FAILURE_INITIAL_VMCLEAR=4, VMX_PERSISTENT_START_FAILURE_VMPTRLD=5,
 VMX_PERSISTENT_START_FAILURE_STATE=6, VMX_PERSISTENT_START_FAILURE_PLAN=7,
 VMX_PERSISTENT_START_FAILURE_LAUNCH=8, VMX_PERSISTENT_START_FAILURE_CLEANUP=9,
 VMX_PERSISTENT_START_FAILURE_AFFINITY_RESTORE=10, VMX_PERSISTENT_START_FAILURE_LIFECYCLE=11
} VMX_PERSISTENT_START_FAILURE_KIND;
typedef struct _VMX_PERSISTENT_START_LAUNCH_DIAGNOSTIC {
 uint32_t ProtocolStep,AssemblyAction,ValidationFlags,LaunchFrameValid;
 uint32_t ReadyObserved,ShutdownObserved;
 uint64_t LaunchFrameGeneration,HostStackTop,HostRsp,GuestRsp;
 uint32_t VmlaunchOutcome,VmlaunchErrorValid,VmlaunchError;
 uint32_t GuestRspOutcome,GuestRspErrorValid,GuestRspError;
 uint32_t GuestRflagsOutcome,GuestRflagsErrorValid,GuestRflagsError;
 uint32_t VmcsPlanOutcome,VmcsPlanErrorValid,VmcsPlanError,VmcsPlanWriteCount;
 uint64_t LastExitReason; uint32_t LastExitInstructionLength;
 uint64_t LastExitQualification,LastExitGuestRip;
 uint32_t ProtocolCode,ValidationCode,FailClosedCode;
 uint32_t ExitVmreadOutcome,ExitVmreadErrorValid,ExitVmreadError;
 uint32_t GuestRipVmwriteOutcome,GuestRipVmwriteErrorValid,GuestRipVmwriteError;
 uint64_t FailedVmcsField;
} VMX_PERSISTENT_START_LAUNCH_DIAGNOSTIC;
typedef struct _VMX_PERSISTENT_START_DIAGNOSTIC {
 uint32_t Version,Size,Valid,Kind,SystemIndex; uint16_t Group; uint8_t Number,CpuStage;
 int32_t LifecycleStatus; uint32_t LifecycleState;
 VMX_PERSISTENT_KERNEL_RESULT PrepareKernel,ActivateKernel;
 VMX_PERSISTENT_OPERATION_RESULT Operation;
 VMX_PERSISTENT_START_TRACE Trace;
 VMX_PERSISTENT_START_LAUNCH_DIAGNOSTIC Launch;
} VMX_PERSISTENT_START_DIAGNOSTIC;
/* Out must be zero. The first lifecycle operation index is authoritative.
 * Success copies all operation-local reports without pointers. A clean
 * lifecycle rollback may already have released launch storage; in that case
 * the launch sub-record remains zero while preserved kernel/operation results
 * and CPU identity are still published. Invalid input preserves Out. */
bool VmxPersistentStartDiagnosticCapture(const VMX_PERSISTENT_LIFECYCLE* lifecycle,
 const VMX_PERSISTENT_KERNEL_RESULT* prepare_results,
 const VMX_PERSISTENT_KERNEL_RESULT* activate_results,
 const VMX_PERSISTENT_OPERATION_RESULT* operation_results,
 const VMX_PERSISTENT_CONTEXT* contexts,size_t count,
 VMX_PERSISTENT_START_DIAGNOSTIC* out_diagnostic);
/* Publishes a sentinel diagnostic when no per-CPU operation record is
 * authoritative. The synchronous boundary trace identifies the exact failure
 * point. Out must be zero; invalid input preserves Out. */
bool VmxPersistentStartDiagnosticCaptureLifecycle(
 VMX_PERSISTENT_STATUS status,
 const VMX_PERSISTENT_START_TRACE* trace,
 VMX_PERSISTENT_START_DIAGNOSTIC* out_diagnostic);
#ifdef __cplusplus
}
#endif
#endif
