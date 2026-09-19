#include "vmx_platform_readiness.h"

#include "vmx.h"
#include "vmx_controls.h"
#include "vmx_persistent_preentry.h"

static bool ReadinessConsensusCollectingValid(
    const VMX_PLATFORM_READINESS_CONSENSUS* Consensus)
{
    return Consensus != NULL &&
        Consensus->Version == VMX_PLATFORM_READINESS_CONSENSUS_VERSION &&
        Consensus->ExpectedProcessors != 0 &&
        Consensus->ObservedProcessors <= Consensus->ExpectedProcessors &&
        Consensus->RejectedSystemIndex == VMX_PLATFORM_READINESS_NO_PROCESSOR &&
        Consensus->RejectionReason == VMX_PLATFORM_READINESS_REASON_NONE &&
        Consensus->PreemptionTimerRate <= 31u &&
        Consensus->PreemptionTimerAvailable <= 1u &&
        (Consensus->PreemptionTimerAvailable != 0u ||
            Consensus->PreemptionTimerRate == 0u) &&
        Consensus->Status == VMX_PLATFORM_READINESS_CONSENSUS_COLLECTING;
}

bool VmxPlatformReadinessSampleValidate(
    const VMX_PLATFORM_READINESS_SAMPLE* Sample,
    VMX_PLATFORM_READINESS_REASON* OutReason)
{
    VMX_BASIC_CAPABILITIES Capabilities = { 0 };
    VMX_PLATFORM_READINESS_REASON Reason = VMX_PLATFORM_READINESS_REASON_NONE;

    if (Sample == NULL || OutReason == NULL)
        return false;
    if (Sample->Reserved != 0)
        Reason = VMX_PLATFORM_READINESS_REASON_RESERVED;
    else if (Sample->CpuidVmx != 1)
        Reason = VMX_PLATFORM_READINESS_REASON_CPUID_VMX;
    else if ((Sample->Cr4 & CR4_CET) != 0)
        Reason = VMX_PLATFORM_READINESS_REASON_CET_ACTIVE;
    else if (!VmxPersistentPreentryValidate(
                 Sample->Cr4, Sample->FeatureControl))
        Reason = VMX_PLATFORM_READINESS_REASON_FEATURE_CONTROL;
    else if (!VmxParseBasicCapabilities(Sample->VmxBasic, &Capabilities))
        Reason = VMX_PLATFORM_READINESS_REASON_VMX_BASIC;

    *OutReason = Reason;
    return Reason == VMX_PLATFORM_READINESS_REASON_NONE;
}

bool VmxPlatformReadinessConsensusBegin(
    uint32_t ExpectedProcessors,
    VMX_PLATFORM_READINESS_CONSENSUS* OutConsensus)
{
    VMX_PLATFORM_READINESS_CONSENSUS Staged = { 0 };

    if (ExpectedProcessors == 0 || OutConsensus == NULL)
        return false;
    Staged.Version = VMX_PLATFORM_READINESS_CONSENSUS_VERSION;
    Staged.ExpectedProcessors = ExpectedProcessors;
    Staged.RejectedSystemIndex = VMX_PLATFORM_READINESS_NO_PROCESSOR;
    Staged.Status = VMX_PLATFORM_READINESS_CONSENSUS_COLLECTING;
    *OutConsensus = Staged;
    return true;
}

bool VmxPlatformReadinessConsensusObserve(
    VMX_PLATFORM_READINESS_CONSENSUS* Consensus,
    uint32_t SystemIndex,
    const VMX_PLATFORM_READINESS_SAMPLE* Sample)
{
    VMX_PLATFORM_READINESS_CONSENSUS Staged;
    VMX_PLATFORM_READINESS_REASON Reason;
    uint32_t PinMustBe1 = 0, PinAllowed1 = 0;
    uint32_t TimerAvailable;

    if (!ReadinessConsensusCollectingValid(Consensus) ||
        Consensus->ObservedProcessors >= Consensus->ExpectedProcessors ||
        SystemIndex != Consensus->ObservedProcessors || Sample == NULL)
        return false;

    Staged = *Consensus;
    Staged.ObservedProcessors++;
    if (!VmxPlatformReadinessSampleValidate(Sample, &Reason)) {
        Staged.RejectedSystemIndex = SystemIndex;
        Staged.RejectionReason = (uint32_t)Reason;
        Staged.Status = VMX_PLATFORM_READINESS_CONSENSUS_DENIED;
        *Consensus = Staged;
        return false;
    }
    VmxSplitControlMsr(Sample->PinControls, &PinMustBe1, &PinAllowed1);
    (void)PinMustBe1;
    TimerAvailable =
        (PinAllowed1 & PIN_BASED_ACTIVATE_PREEMPTION_TIMER) != 0 ? 1u : 0u;
    if (Staged.ObservedProcessors == 1) {
        Staged.VmxBasic = Sample->VmxBasic;
        Staged.PreemptionTimerAvailable = TimerAvailable;
        Staged.PreemptionTimerRate = TimerAvailable != 0u
            ? (uint32_t)(Sample->VmxMisc & VMX_PREEMPTION_TIMER_RATE_MASK)
            : 0u;
    } else if (Staged.VmxBasic != Sample->VmxBasic) {
        Staged.RejectedSystemIndex = SystemIndex;
        Staged.RejectionReason =
            VMX_PLATFORM_READINESS_REASON_VMX_BASIC_MISMATCH;
        Staged.Status = VMX_PLATFORM_READINESS_CONSENSUS_DENIED;
        *Consensus = Staged;
        return false;
    } else if (Staged.PreemptionTimerAvailable != TimerAvailable) {
        Staged.RejectedSystemIndex = SystemIndex;
        Staged.RejectionReason =
            VMX_PLATFORM_READINESS_REASON_TIMER_CAPABILITY_MISMATCH;
        Staged.Status = VMX_PLATFORM_READINESS_CONSENSUS_DENIED;
        *Consensus = Staged;
        return false;
    } else if (TimerAvailable != 0u && Staged.PreemptionTimerRate !=
            (uint32_t)(Sample->VmxMisc & VMX_PREEMPTION_TIMER_RATE_MASK)) {
        Staged.RejectedSystemIndex = SystemIndex;
        Staged.RejectionReason =
            VMX_PLATFORM_READINESS_REASON_TIMER_RATE_MISMATCH;
        Staged.Status = VMX_PLATFORM_READINESS_CONSENSUS_DENIED;
        *Consensus = Staged;
        return false;
    }
    *Consensus = Staged;
    return true;
}

bool VmxPlatformReadinessConsensusComplete(
    VMX_PLATFORM_READINESS_CONSENSUS* Consensus)
{
    VMX_PLATFORM_READINESS_CONSENSUS Staged;

    if (!ReadinessConsensusCollectingValid(Consensus) ||
        Consensus->ObservedProcessors != Consensus->ExpectedProcessors ||
        Consensus->VmxBasic == 0)
        return false;
    Staged = *Consensus;
    Staged.Status = VMX_PLATFORM_READINESS_CONSENSUS_ADMITTED;
    *Consensus = Staged;
    return true;
}
