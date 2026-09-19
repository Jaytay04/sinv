#include "vmx_exit_telemetry.h"
#define VMX_EXIT_REASON_BASIC_MASK UINT64_C(0xffff)
#define VMX_EXIT_REASON_CPUID UINT64_C(10)
#define VMX_EXIT_REASON_VMCALL UINT64_C(18)
#define VMX_EXIT_REASON_CR_ACCESS UINT64_C(28)
#define VMX_EXIT_REASON_MSR_READ UINT64_C(31)
#define VMX_EXIT_REASON_MSR_WRITE UINT64_C(32)
#define VMX_EXIT_REASON_EPT_VIOLATION UINT64_C(48)

#if defined(_MSC_VER)
#include <intrin.h>
#define TELEMETRY_BARRIER() _ReadWriteBarrier()
#elif defined(__GNUC__) || defined(__clang__)
#define TELEMETRY_BARRIER() __asm__ __volatile__("" ::: "memory")
#else
#define TELEMETRY_BARRIER() ((void)0)
#endif

static int Zero(const void* pointer, size_t bytes)
{
    const unsigned char* value = (const unsigned char*)pointer;
    size_t index;
    for (index = 0; index < bytes; ++index) {
        if (value[index] != 0) return 0;
    }
    return 1;
}

static void Begin(VMX_EXIT_TELEMETRY* telemetry)
{
    volatile VMX_EXIT_TELEMETRY* live =
        (volatile VMX_EXIT_TELEMETRY*)telemetry;
    ++live->Sequence;
    TELEMETRY_BARRIER();
}

static void End(VMX_EXIT_TELEMETRY* telemetry)
{
    volatile VMX_EXIT_TELEMETRY* live =
        (volatile VMX_EXIT_TELEMETRY*)telemetry;
    TELEMETRY_BARRIER();
    ++live->Sequence;
}

void VmxExitTelemetryRecordReason(
    VMX_EXIT_TELEMETRY* telemetry,
    uint64_t exit_reason)
{
    uint64_t basic;
    volatile VMX_EXIT_TELEMETRY* live;
    if (telemetry == NULL) return;
    live = (volatile VMX_EXIT_TELEMETRY*)telemetry;
    basic = exit_reason & VMX_EXIT_REASON_BASIC_MASK;
    Begin(telemetry);
    ++live->TotalExits;
    switch (basic) {
    case VMX_EXIT_REASON_CPUID:
        ++live->CpuidExits;
        break;
    case VMX_EXIT_REASON_CR_ACCESS:
        ++live->CrAccessExits;
        break;
    case VMX_EXIT_REASON_MSR_READ:
        ++live->MsrReadExits;
        break;
    case VMX_EXIT_REASON_MSR_WRITE:
        ++live->MsrWriteExits;
        break;
    case VMX_EXIT_REASON_EPT_VIOLATION:
        ++live->EptViolationExits;
        break;
    case VMX_EXIT_REASON_VMCALL:
        ++live->VmcallExits;
        break;
    default:
        ++live->OtherExits;
        break;
    }
    End(telemetry);
}

void VmxExitTelemetryRecordFailClosed(VMX_EXIT_TELEMETRY* telemetry)
{
    volatile VMX_EXIT_TELEMETRY* live;
    if (telemetry == NULL) return;
    live = (volatile VMX_EXIT_TELEMETRY*)telemetry;
    Begin(telemetry);
    ++live->FailClosedEvents;
    End(telemetry);
}

int VmxExitTelemetrySnapshot(
    const VMX_EXIT_TELEMETRY* telemetry,
    VMX_EXIT_TELEMETRY_SNAPSHOT* out_snapshot)
{
    const volatile VMX_EXIT_TELEMETRY* live;
    VMX_EXIT_TELEMETRY_SNAPSHOT staged;
    uint64_t first;
    uint64_t last;
    uint64_t classified;
    unsigned int attempt;

    if (telemetry == NULL || out_snapshot == NULL ||
        !Zero(out_snapshot, sizeof(*out_snapshot))) return 0;
    live = (const volatile VMX_EXIT_TELEMETRY*)telemetry;

    for (attempt = 0; attempt < 64; ++attempt) {
        first = live->Sequence;
        if ((first & UINT64_C(1)) != 0) continue;
        TELEMETRY_BARRIER();
        staged.Sequence = first;
        staged.TotalExits = live->TotalExits;
        staged.CpuidExits = live->CpuidExits;
        staged.CrAccessExits = live->CrAccessExits;
        staged.MsrReadExits = live->MsrReadExits;
        staged.MsrWriteExits = live->MsrWriteExits;
        staged.EptViolationExits = live->EptViolationExits;
        staged.VmcallExits = live->VmcallExits;
        staged.OtherExits = live->OtherExits;
        staged.FailClosedEvents = live->FailClosedEvents;
        TELEMETRY_BARRIER();
        last = live->Sequence;
        if (first != last || (last & UINT64_C(1)) != 0) continue;
        staged.Sequence = last;
        classified = staged.CpuidExits + staged.CrAccessExits +
            staged.MsrReadExits + staged.MsrWriteExits +
            staged.EptViolationExits + staged.VmcallExits +
            staged.OtherExits;
        if (classified != staged.TotalExits) return 0;
        *out_snapshot = staged;
        return 1;
    }
    return 0;
}
