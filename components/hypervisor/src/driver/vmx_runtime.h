#pragma once

#include <ntddk.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VMX_RUNTIME_INSTRUCTION_STATUS_NOT_EXECUTED ((UCHAR)0xFF)

typedef enum _VMX_RUNTIME_PROBE_STAGE {
    VmxRuntimeProbeStageNotStarted = 0,
    VmxRuntimeProbeStageValidatePassiveLevel,
    VmxRuntimeProbeStageAcquireSerialization,
    VmxRuntimeProbeStagePinProcessorZero,
    VmxRuntimeProbeStageCheckCpuid,
    VmxRuntimeProbeStageCheckFeatureControl,
    VmxRuntimeProbeStageCheckVmxBasic,
    VmxRuntimeProbeStageCheckFixedControlRegisters,
    VmxRuntimeProbeStageAllocateVmxonRegion,
    VmxRuntimeProbeStageValidateVmxonRegion,
    VmxRuntimeProbeStageInstructionInterval,
    VmxRuntimeProbeStageVmxOn,
    VmxRuntimeProbeStageVmxOff,
    VmxRuntimeProbeStageRestoreControlRegisters,
    VmxRuntimeProbeStageComplete
} VMX_RUNTIME_PROBE_STAGE;

typedef struct _VMX_RUNTIME_PROBE_RESULT {
    ULONG Size;
    NTSTATUS Status;
    VMX_RUNTIME_PROBE_STAGE Stage;
    UCHAR VmxInstructionStatus;
    BOOLEAN VmxOffCompleted;
    BOOLEAN ControlRegistersRestored;
    UCHAR Reserved0;
    USHORT ProcessorGroup;
    UCHAR ProcessorNumber;
    UCHAR Reserved1;
    ULONG VmxRevisionId;
    ULONG VmxRegionSize;
    ULONGLONG FeatureControl;
    ULONGLONG VmxBasic;
    ULONGLONG VmxonPhysicalAddress;
    ULONGLONG OriginalCr0;
    ULONGLONG AdjustedCr0;
    ULONGLONG OriginalCr4;
    ULONGLONG AdjustedCr4;
} VMX_RUNTIME_PROBE_RESULT, *PVMX_RUNTIME_PROBE_RESULT;

_IRQL_requires_(PASSIVE_LEVEL)
_Must_inspect_result_
NTSTATUS
VmxRuntimeProbeProcessorZero(
    _Out_ PVMX_RUNTIME_PROBE_RESULT Result
    );

#ifdef __cplusplus
}
#endif
