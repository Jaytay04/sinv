#ifndef HYPERSINV_VMX_EXIT_TELEMETRY_H
#define HYPERSINV_VMX_EXIT_TELEMETRY_H

/* WDK-free, per-processor VM-exit telemetry retained in launch storage. */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VMX_EXIT_TELEMETRY_VERSION UINT32_C(1)

typedef struct VMX_EXIT_TELEMETRY {
    uint64_t Sequence;
    uint64_t TotalExits;
    uint64_t CpuidExits;
    uint64_t CrAccessExits;
    uint64_t MsrReadExits;
    uint64_t MsrWriteExits;
    uint64_t EptViolationExits;
    uint64_t VmcallExits;
    uint64_t OtherExits;
    uint64_t FailClosedEvents;
} VMX_EXIT_TELEMETRY;

typedef struct VMX_EXIT_TELEMETRY_SNAPSHOT {
    uint64_t Sequence;
    uint64_t TotalExits;
    uint64_t CpuidExits;
    uint64_t CrAccessExits;
    uint64_t MsrReadExits;
    uint64_t MsrWriteExits;
    uint64_t EptViolationExits;
    uint64_t VmcallExits;
    uint64_t OtherExits;
    uint64_t FailClosedEvents;
} VMX_EXIT_TELEMETRY_SNAPSHOT;

/* The launch context has one writer: its resident logical processor. */
void VmxExitTelemetryRecordReason(
    VMX_EXIT_TELEMETRY* telemetry,
    uint64_t exit_reason);
void VmxExitTelemetryRecordFailClosed(VMX_EXIT_TELEMETRY* telemetry);

/* A coherent even-sequence snapshot is required. Out must be zero. */
int VmxExitTelemetrySnapshot(
    const VMX_EXIT_TELEMETRY* telemetry,
    VMX_EXIT_TELEMETRY_SNAPSHOT* out_snapshot);

#ifdef __cplusplus
}
#endif

#endif
