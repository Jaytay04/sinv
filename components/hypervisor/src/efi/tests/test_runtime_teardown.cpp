#include <stdint.h>
#include <stdio.h>

#include "runtime_teardown.h"

namespace {

int Expect(uint32_t condition, const char *message)
{
    if (condition == 0) {
        fprintf(stderr, "FAIL: %s\n", message);
        return 1;
    }
    return 0;
}

void FillGuestState(HV_EFI_TEARDOWN_GUEST_STATE *state, uint64_t seed)
{
    uint64_t *words = reinterpret_cast<uint64_t *>(state);
    for (uint32_t index = 0;
         index < HV_EFI_TEARDOWN_GUEST_STATE_BYTES / sizeof(uint64_t);
         ++index) {
        words[index] = seed + index;
    }
}

uint32_t GuestStatesEqual(
    const HV_EFI_TEARDOWN_GUEST_STATE *left,
    const HV_EFI_TEARDOWN_GUEST_STATE *right)
{
    const uint8_t *leftBytes = reinterpret_cast<const uint8_t *>(left);
    const uint8_t *rightBytes = reinterpret_cast<const uint8_t *>(right);
    uint8_t difference = 0;
    for (size_t index = 0; index < sizeof(*left); ++index) {
        difference = static_cast<uint8_t>(
            difference | (leftBytes[index] ^ rightBytes[index]));
    }
    return difference == 0 ? 1u : 0u;
}

void ConfigureCpus(
    HV_EFI_TEARDOWN_CPU_STATE *cpus, uint32_t cpuCount)
{
    for (uint32_t index = 0; index < cpuCount; ++index) {
        cpus[index].ProcessorIndex = 100 + index;
        cpus[index].GroupId = index % 2;
        cpus[index].Enabled = 1;
        cpus[index].VmxonPhysicalAddress =
            UINT64_C(0x100000) + static_cast<uint64_t>(index) * UINT64_C(0x2000);
        cpus[index].VmcsPhysicalAddress =
            UINT64_C(0x200000) + static_cast<uint64_t>(index) * UINT64_C(0x2000);
    }
}

int PrepareCapture(
    HV_EFI_TEARDOWN *teardown,
    HV_EFI_TEARDOWN_CPU_STATE *cpus,
    uint32_t cpuCount,
    uint64_t generation)
{
    int failures = 0;
    for (uint32_t index = 0; index < cpuCount; ++index) {
        HV_EFI_TEARDOWN_GUEST_STATE state = {};
        FillGuestState(&state, UINT64_C(0x1000) + index * UINT64_C(0x100));
        failures += Expect(
            HvEfiTeardownCaptureGuestState(
                teardown, cpus[index].ProcessorIndex, generation, &state,
                HV_EFI_TEARDOWN_REQUIRED_GUEST_STATE_MASK) ==
                HV_EFI_TEARDOWN_OK,
            "guest-state capture completes with every required field");
    }
    return failures;
}

int CompleteNativeTeardown(
    HV_EFI_TEARDOWN *teardown,
    HV_EFI_TEARDOWN_CPU_STATE *cpus,
    uint32_t cpuCount,
    uint64_t generation)
{
    int failures = 0;
    for (uint32_t index = 0; index < cpuCount; ++index) {
        failures += Expect(
            HvEfiTeardownCompleteVmclear(
                teardown, cpus[index].ProcessorIndex, generation,
                HV_EFI_TEARDOWN_NATIVE_SUCCESS) == HV_EFI_TEARDOWN_OK,
            "VMCLEAR completes after the capture barrier");
    }
    failures += Expect(
        HvEfiTeardownGetPhase(teardown) == HV_EFI_TEARDOWN_VMXOFF,
        "all VMCLEAR results advance to the VMXOFF barrier");
    for (uint32_t index = 0; index < cpuCount; ++index) {
        failures += Expect(
            HvEfiTeardownCompleteVmxoff(
                teardown, cpus[index].ProcessorIndex, generation,
                HV_EFI_TEARDOWN_NATIVE_SUCCESS) == HV_EFI_TEARDOWN_OK,
            "VMXOFF completes only after VMCLEAR");
    }
    failures += Expect(
        HvEfiTeardownGetPhase(teardown) == HV_EFI_TEARDOWN_RESTORING,
        "all VMXOFF results open restoration readiness");
    for (uint32_t index = 0; index < cpuCount; ++index) {
        HV_EFI_TEARDOWN_GUEST_STATE restored = {};
        failures += Expect(
            HvEfiTeardownGetRestorationState(
                teardown, cpus[index].ProcessorIndex, generation, &restored) ==
                HV_EFI_TEARDOWN_OK &&
            GuestStatesEqual(&restored, &cpus[index].GuestState) != 0,
            "restoration getter returns the captured state");
        failures += Expect(
            HvEfiTeardownCompleteRestoration(
                teardown, cpus[index].ProcessorIndex, generation, &restored,
                HV_EFI_TEARDOWN_NATIVE_SUCCESS) == HV_EFI_TEARDOWN_OK,
            "native restoration requires successful exact read-back");
    }
    failures += Expect(
        HvEfiTeardownGetPhase(teardown) == HV_EFI_TEARDOWN_ACKNOWLEDGING,
        "restored processors wait for explicit acknowledgements");
    return failures;
}

int TestTwentyCpuOrderedTeardown()
{
    HV_EFI_TEARDOWN_CPU_STATE cpus[20] = {};
    HV_EFI_TEARDOWN teardown = {};
    ConfigureCpus(cpus, 20);
    int failures = 0;
    failures += Expect(
        HvEfiTeardownInitialize(&teardown, cpus, 20) == HV_EFI_TEARDOWN_OK,
        "twenty-CPU teardown initializes");
    failures += Expect(
        HvEfiTeardownBegin(&teardown, 1) == HV_EFI_TEARDOWN_OK &&
        HvEfiTeardownGetPhase(&teardown) == HV_EFI_TEARDOWN_CAPTURING,
        "generation one establishes the capture phase");
    failures += PrepareCapture(&teardown, cpus, 20, 1);
    failures += Expect(
        HvEfiTeardownGetPhase(&teardown) == HV_EFI_TEARDOWN_VMCLEAR,
        "twenty captured CPUs establish the VMCLEAR barrier");
    failures += Expect(
        HvEfiTeardownCompleteVmxoff(
            &teardown, cpus[0].ProcessorIndex, 1,
            HV_EFI_TEARDOWN_NATIVE_SUCCESS) ==
            HV_EFI_TEARDOWN_VMXOFF_REQUIRED &&
        HvEfiTeardownGetPhase(&teardown) == HV_EFI_TEARDOWN_QUARANTINED,
        "VMXOFF cannot precede VMCLEAR");
    /* The previous ordering violation quarantines the model; use a fresh
     * fixture for the complete path so the test also proves quarantine is
     * terminal. */
    ConfigureCpus(cpus, 20);
    failures += Expect(
        HvEfiTeardownInitialize(&teardown, cpus, 20) == HV_EFI_TEARDOWN_OK &&
        HvEfiTeardownBegin(&teardown, 1) == HV_EFI_TEARDOWN_OK,
        "fresh twenty-CPU fixture starts after ordering failure");
    failures += PrepareCapture(&teardown, cpus, 20, 1);
    failures += CompleteNativeTeardown(&teardown, cpus, 20, 1);
    for (uint32_t index = 0; index < 20; ++index) {
        failures += Expect(
            HvEfiTeardownAcknowledgeRestored(
                &teardown, cpus[index].ProcessorIndex, 1) ==
                HV_EFI_TEARDOWN_OK,
            "acknowledgement follows successful restoration");
    }
    failures += Expect(
        HvEfiTeardownGetPhase(&teardown) == HV_EFI_TEARDOWN_COMPLETE &&
        HvEfiTeardownAllEnabledAcknowledged(&teardown) != 0 &&
        HvEfiTeardownCanReleaseResources(&teardown) != 0 &&
        teardown.AcknowledgedCount == 20,
        "twenty restored and acknowledged CPUs permit release");
    failures += Expect(
        HvEfiTeardownAcknowledgeRestored(
            &teardown, cpus[0].ProcessorIndex, 1) ==
            HV_EFI_TEARDOWN_DUPLICATE_ACK,
        "duplicate acknowledgement is idempotently rejected");
    return failures;
}

int TestCaptureAndRestorationBoundaries()
{
    HV_EFI_TEARDOWN_CPU_STATE cpu = {};
    HV_EFI_TEARDOWN teardown = {};
    ConfigureCpus(&cpu, 1);
    HV_EFI_TEARDOWN_GUEST_STATE state = {};
    FillGuestState(&state, UINT64_C(0x5000));
    int failures = 0;
    failures += Expect(
        HvEfiTeardownInitialize(&teardown, &cpu, 1) == HV_EFI_TEARDOWN_OK &&
        HvEfiTeardownBegin(&teardown, 1) == HV_EFI_TEARDOWN_OK,
        "single-CPU boundary fixture initializes");
    failures += Expect(
        HvEfiTeardownCaptureGuestState(
            &teardown, cpu.ProcessorIndex, 1, &state,
            HV_EFI_TEARDOWN_REQUIRED_GUEST_STATE_MASK ^
                HV_EFI_TEARDOWN_GUEST_FIELD_CR3) ==
            HV_EFI_TEARDOWN_CAPTURE_INCOMPLETE &&
        cpu.CapturedMask == 0,
        "missing guest-state field blocks capture without advancing");
    failures += Expect(
        HvEfiTeardownCaptureGuestState(
            &teardown, cpu.ProcessorIndex, 1, &state,
            HV_EFI_TEARDOWN_REQUIRED_GUEST_STATE_MASK) ==
            HV_EFI_TEARDOWN_OK,
        "complete guest-state capture is accepted");
    failures += Expect(
        HvEfiTeardownCompleteVmclear(
            &teardown, cpu.ProcessorIndex, 1,
            HV_EFI_TEARDOWN_NATIVE_SUCCESS) == HV_EFI_TEARDOWN_OK &&
        HvEfiTeardownCompleteVmxoff(
            &teardown, cpu.ProcessorIndex, 1,
            HV_EFI_TEARDOWN_NATIVE_SUCCESS) == HV_EFI_TEARDOWN_OK,
        "single-CPU native clear/off sequence completes");
    failures += Expect(
        HvEfiTeardownAcknowledgeRestored(
            &teardown, cpu.ProcessorIndex, 1) ==
            HV_EFI_TEARDOWN_RESTORATION_NOT_READY &&
        HvEfiTeardownGetPhase(&teardown) == HV_EFI_TEARDOWN_QUARANTINED,
        "early acknowledgement fails closed and quarantines");

    ConfigureCpus(&cpu, 1);
    failures += Expect(
        HvEfiTeardownInitialize(&teardown, &cpu, 1) == HV_EFI_TEARDOWN_OK &&
        HvEfiTeardownBegin(&teardown, 1) == HV_EFI_TEARDOWN_OK,
        "restoration mismatch fixture initializes");
    failures += PrepareCapture(&teardown, &cpu, 1, 1);
    failures += Expect(
        HvEfiTeardownCompleteVmclear(
            &teardown, cpu.ProcessorIndex, 1,
            HV_EFI_TEARDOWN_NATIVE_SUCCESS) == HV_EFI_TEARDOWN_OK &&
        HvEfiTeardownCompleteVmxoff(
            &teardown, cpu.ProcessorIndex, 1,
            HV_EFI_TEARDOWN_NATIVE_SUCCESS) == HV_EFI_TEARDOWN_OK,
        "mismatch fixture reaches restoration");
    HV_EFI_TEARDOWN_GUEST_STATE mismatch = cpu.GuestState;
    ++mismatch.Rip;
    failures += Expect(
        HvEfiTeardownCompleteRestoration(
            &teardown, cpu.ProcessorIndex, 1, &mismatch,
            HV_EFI_TEARDOWN_NATIVE_SUCCESS) ==
            HV_EFI_TEARDOWN_RESTORATION_FAILED &&
        HvEfiTeardownGetPhase(&teardown) == HV_EFI_TEARDOWN_QUARANTINED &&
        HvEfiTeardownCanReleaseResources(&teardown) == 0,
        "state read-back mismatch quarantines restoration");
    return failures;
}

int TestPartialNativeFailure()
{
    HV_EFI_TEARDOWN_CPU_STATE cpus[2] = {};
    HV_EFI_TEARDOWN teardown = {};
    ConfigureCpus(cpus, 2);
    int failures = 0;
    failures += Expect(
        HvEfiTeardownInitialize(&teardown, cpus, 2) == HV_EFI_TEARDOWN_OK &&
        HvEfiTeardownBegin(&teardown, 7) == HV_EFI_TEARDOWN_OK,
        "partial-failure fixture initializes");
    failures += PrepareCapture(&teardown, cpus, 2, 7);
    failures += Expect(
        HvEfiTeardownCompleteVmclear(
            &teardown, cpus[0].ProcessorIndex, 7,
            HV_EFI_TEARDOWN_NATIVE_SUCCESS) == HV_EFI_TEARDOWN_OK &&
        HvEfiTeardownCompleteVmclear(
            &teardown, cpus[1].ProcessorIndex, 7,
            UINT32_C(0xdead)) == HV_EFI_TEARDOWN_VMCLEAR_FAILED &&
        HvEfiTeardownGetPhase(&teardown) == HV_EFI_TEARDOWN_QUARANTINED &&
        cpus[1].Phase == HV_EFI_TEARDOWN_CPU_FAILED &&
        cpus[0].Phase == HV_EFI_TEARDOWN_CPU_QUARANTINED &&
        HvEfiTeardownCanReleaseResources(&teardown) == 0,
        "partial VMCLEAR failure quarantines every unfinished processor");
    const uint32_t phase = teardown.Phase;
    failures += Expect(
        HvEfiTeardownCompleteVmclear(
            &teardown, cpus[0].ProcessorIndex, 7,
            HV_EFI_TEARDOWN_NATIVE_SUCCESS) ==
            HV_EFI_TEARDOWN_QUARANTINE_ACTIVE &&
        teardown.Phase == phase,
        "quarantine is terminal after partial VMCLEAR failure");

    ConfigureCpus(cpus, 2);
    failures += Expect(
        HvEfiTeardownInitialize(&teardown, cpus, 2) == HV_EFI_TEARDOWN_OK &&
        HvEfiTeardownBegin(&teardown, 8) == HV_EFI_TEARDOWN_OK,
        "partial-VMXOFF fixture initializes");
    failures += PrepareCapture(&teardown, cpus, 2, 8);
    failures += Expect(
        HvEfiTeardownCompleteVmclear(
            &teardown, cpus[0].ProcessorIndex, 8,
            HV_EFI_TEARDOWN_NATIVE_SUCCESS) == HV_EFI_TEARDOWN_OK &&
        HvEfiTeardownCompleteVmclear(
            &teardown, cpus[1].ProcessorIndex, 8,
            HV_EFI_TEARDOWN_NATIVE_SUCCESS) == HV_EFI_TEARDOWN_OK &&
        HvEfiTeardownCompleteVmxoff(
            &teardown, cpus[0].ProcessorIndex, 8,
            HV_EFI_TEARDOWN_NATIVE_SUCCESS) == HV_EFI_TEARDOWN_OK &&
        HvEfiTeardownCompleteVmxoff(
            &teardown, cpus[1].ProcessorIndex, 8,
            HV_EFI_TEARDOWN_NATIVE_FAILURE) == HV_EFI_TEARDOWN_VMXOFF_FAILED &&
        HvEfiTeardownGetPhase(&teardown) == HV_EFI_TEARDOWN_QUARANTINED,
        "partial VMXOFF failure quarantines after prior clear");
    return failures;
}

int TestValidationAndGeneration()
{
    HV_EFI_TEARDOWN_CPU_STATE badCpu = {};
    HV_EFI_TEARDOWN teardown = {};
    badCpu.ProcessorIndex = 1;
    badCpu.Enabled = 1;
    badCpu.VmxonPhysicalAddress = UINT64_C(0x1001);
    badCpu.VmcsPhysicalAddress = UINT64_C(0x2000);
    int failures = 0;
    failures += Expect(
        HvEfiTeardownInitialize(&teardown, &badCpu, 1) ==
            HV_EFI_TEARDOWN_INVALID_ARGUMENT,
        "unaligned VMX resource is rejected before model initialization");

    HV_EFI_TEARDOWN_CPU_STATE duplicate[2] = {};
    ConfigureCpus(duplicate, 2);
    duplicate[1].ProcessorIndex = duplicate[0].ProcessorIndex;
    failures += Expect(
        HvEfiTeardownInitialize(&teardown, duplicate, 2) ==
            HV_EFI_TEARDOWN_INVALID_ARGUMENT,
        "duplicate processor identity is rejected");

    HV_EFI_TEARDOWN_CPU_STATE cpu = {};
    ConfigureCpus(&cpu, 1);
    failures += Expect(
        HvEfiTeardownInitialize(&teardown, &cpu, 1) == HV_EFI_TEARDOWN_OK &&
        HvEfiTeardownBegin(&teardown, 0) ==
            HV_EFI_TEARDOWN_GENERATION_INVALID &&
        HvEfiTeardownGetPhase(&teardown) == HV_EFI_TEARDOWN_READY,
        "zero generation is rejected without entering teardown");
    failures += Expect(
        HvEfiTeardownBegin(&teardown, 1) == HV_EFI_TEARDOWN_OK &&
        HvEfiTeardownBegin(&teardown, 2) == HV_EFI_TEARDOWN_INVALID_STATE,
        "a second generation cannot overlap the active teardown");
    return failures;
}

} // namespace

int main()
{
    int failures = 0;
    failures += TestTwentyCpuOrderedTeardown();
    failures += TestCaptureAndRestorationBoundaries();
    failures += TestPartialNativeFailure();
    failures += TestValidationAndGeneration();
    if (failures != 0) {
        return 1;
    }
    puts("PASS: EFI runtime teardown contracts");
    return 0;
}
