#include "test_common.h"
#include "vmx_platform_compatibility.h"

static VMX_PLATFORM_CPUID_IDENTITY HypervisorIdentity(
    uint32_t Ebx, uint32_t Ecx, uint32_t Edx)
{
    VMX_PLATFORM_CPUID_IDENTITY Identity = { 0 };
    Identity.HypervisorPresent = VMX_PLATFORM_HYPERVISOR_PRESENT;
    Identity.MaximumHypervisorLeaf = UINT32_C(0x40000010);
    Identity.VendorEbx = Ebx;
    Identity.VendorEcx = Ecx;
    Identity.VendorEdx = Edx;
    return Identity;
}

static int TestNativeAdmission(void)
{
    VMX_PLATFORM_CPUID_IDENTITY Identity = { 0 };
    VMX_PLATFORM_COMPATIBILITY_CLASS Classification =
        VmxPlatformCompatibilityClassify(&Identity);
    TEST_ASSERT_UINT_EQ(Classification,
        VMX_PLATFORM_COMPATIBILITY_NATIVE, "native classification");
    TEST_ASSERT(VmxPlatformCompatibilityAllowsVmx(Classification),
        "native admitted");
    return 0;
}

static int TestVmwareAdmission(void)
{
    VMX_PLATFORM_CPUID_IDENTITY Identity = HypervisorIdentity(
        UINT32_C(0x61774D56), UINT32_C(0x4D566572),
        UINT32_C(0x65726177));
    VMX_PLATFORM_COMPATIBILITY_CLASS Classification =
        VmxPlatformCompatibilityClassify(&Identity);
    TEST_ASSERT_UINT_EQ(Classification,
        VMX_PLATFORM_COMPATIBILITY_VMWARE_NESTED,
        "VMware nested classification");
    TEST_ASSERT(VmxPlatformCompatibilityAllowsVmx(Classification),
        "VMware nested admitted");
    return 0;
}

static int TestHyperVRejected(void)
{
    VMX_PLATFORM_CPUID_IDENTITY Identity = HypervisorIdentity(
        UINT32_C(0x7263694D), UINT32_C(0x666F736F),
        UINT32_C(0x76482074));
    VMX_PLATFORM_COMPATIBILITY_CLASS Classification =
        VmxPlatformCompatibilityClassify(&Identity);
    TEST_ASSERT_UINT_EQ(Classification,
        VMX_PLATFORM_COMPATIBILITY_MICROSOFT_HYPERV,
        "Hyper-V classification");
    TEST_ASSERT(!VmxPlatformCompatibilityAllowsVmx(Classification),
        "Hyper-V rejected");
    return 0;
}

static int TestUnknownRejected(void)
{
    VMX_PLATFORM_CPUID_IDENTITY Identity = HypervisorIdentity(
        UINT32_C(0x11111111), UINT32_C(0x22222222),
        UINT32_C(0x33333333));
    VMX_PLATFORM_COMPATIBILITY_CLASS Classification =
        VmxPlatformCompatibilityClassify(&Identity);
    TEST_ASSERT_UINT_EQ(Classification,
        VMX_PLATFORM_COMPATIBILITY_UNSUPPORTED,
        "unknown classification");
    TEST_ASSERT(!VmxPlatformCompatibilityAllowsVmx(Classification),
        "unknown rejected");
    return 0;
}

static int TestMalformedRejected(void)
{
    VMX_PLATFORM_CPUID_IDENTITY Identity = { 0 };
    TEST_ASSERT_UINT_EQ(VmxPlatformCompatibilityClassify(NULL),
        VMX_PLATFORM_COMPATIBILITY_INVALID, "null identity");
    Identity.HypervisorPresent = 2;
    TEST_ASSERT_UINT_EQ(VmxPlatformCompatibilityClassify(&Identity),
        VMX_PLATFORM_COMPATIBILITY_INVALID, "presence range");
    Identity.HypervisorPresent = 0;
    Identity.VendorEbx = 1;
    TEST_ASSERT_UINT_EQ(VmxPlatformCompatibilityClassify(&Identity),
        VMX_PLATFORM_COMPATIBILITY_INVALID, "native vendor residue");
    Identity = HypervisorIdentity(UINT32_C(0x61774D56),
        UINT32_C(0x4D566572), UINT32_C(0x65726177));
    Identity.MaximumHypervisorLeaf = UINT32_C(0x3fffffff);
    TEST_ASSERT_UINT_EQ(VmxPlatformCompatibilityClassify(&Identity),
        VMX_PLATFORM_COMPATIBILITY_INVALID, "short hypervisor leaf");
    Identity.MaximumHypervisorLeaf = UINT32_C(0x40000010);
    Identity.Reserved = 1;
    TEST_ASSERT_UINT_EQ(VmxPlatformCompatibilityClassify(&Identity),
        VMX_PLATFORM_COMPATIBILITY_INVALID, "reserved rejected");
    TEST_ASSERT(!VmxPlatformCompatibilityAllowsVmx(
        VMX_PLATFORM_COMPATIBILITY_INVALID), "invalid rejected");
    return 0;
}

static int TestNativeConsensus(void)
{
    VMX_PLATFORM_COMPATIBILITY_CONSENSUS Consensus = { 0 };
    uint32_t Index;

    TEST_ASSERT(VmxPlatformCompatibilityConsensusBegin(4, &Consensus),
        "native consensus begin");
    for (Index = 0; Index < 4; ++Index) {
        TEST_ASSERT(VmxPlatformCompatibilityConsensusObserve(&Consensus,
            Index, VMX_PLATFORM_COMPATIBILITY_NATIVE),
            "native consensus observation");
    }
    TEST_ASSERT(VmxPlatformCompatibilityConsensusComplete(&Consensus),
        "native consensus complete");
    TEST_ASSERT_UINT_EQ(Consensus.Status,
        VMX_PLATFORM_COMPATIBILITY_CONSENSUS_ADMITTED,
        "native consensus status");
    TEST_ASSERT_UINT_EQ(Consensus.ObservedProcessors, 4,
        "native consensus count");
    TEST_ASSERT_UINT_EQ(Consensus.ConsensusClassification,
        VMX_PLATFORM_COMPATIBILITY_NATIVE,
        "native consensus classification");
    return 0;
}

static int TestVmwareConsensus(void)
{
    VMX_PLATFORM_COMPATIBILITY_CONSENSUS Consensus = { 0 };
    uint32_t Index;

    TEST_ASSERT(VmxPlatformCompatibilityConsensusBegin(4, &Consensus),
        "VMware consensus begin");
    for (Index = 0; Index < 4; ++Index) {
        TEST_ASSERT(VmxPlatformCompatibilityConsensusObserve(&Consensus,
            Index, VMX_PLATFORM_COMPATIBILITY_VMWARE_NESTED),
            "VMware consensus observation");
    }
    TEST_ASSERT(VmxPlatformCompatibilityConsensusComplete(&Consensus),
        "VMware consensus complete");
    TEST_ASSERT_UINT_EQ(Consensus.ConsensusClassification,
        VMX_PLATFORM_COMPATIBILITY_VMWARE_NESTED,
        "VMware consensus classification");
    return 0;
}

static int TestMixedConsensusRejected(void)
{
    VMX_PLATFORM_COMPATIBILITY_CONSENSUS Consensus = { 0 };

    TEST_ASSERT(VmxPlatformCompatibilityConsensusBegin(2, &Consensus),
        "mixed consensus begin");
    TEST_ASSERT(VmxPlatformCompatibilityConsensusObserve(&Consensus, 0,
        VMX_PLATFORM_COMPATIBILITY_NATIVE), "mixed first observation");
    TEST_ASSERT(!VmxPlatformCompatibilityConsensusObserve(&Consensus, 1,
        VMX_PLATFORM_COMPATIBILITY_VMWARE_NESTED),
        "mixed classification rejected");
    TEST_ASSERT_UINT_EQ(Consensus.Status,
        VMX_PLATFORM_COMPATIBILITY_CONSENSUS_DENIED,
        "mixed consensus denied status");
    TEST_ASSERT_UINT_EQ(Consensus.RejectedSystemIndex, 1,
        "mixed rejection index");
    TEST_ASSERT_UINT_EQ(Consensus.RejectedClassification,
        VMX_PLATFORM_COMPATIBILITY_VMWARE_NESTED,
        "mixed rejected classification");
    TEST_ASSERT(!VmxPlatformCompatibilityConsensusComplete(&Consensus),
        "denied consensus cannot complete");
    return 0;
}

static int TestDisallowedConsensusRejected(void)
{
    VMX_PLATFORM_COMPATIBILITY_CONSENSUS Consensus = { 0 };

    TEST_ASSERT(VmxPlatformCompatibilityConsensusBegin(1, &Consensus),
        "disallowed consensus begin");
    TEST_ASSERT(!VmxPlatformCompatibilityConsensusObserve(&Consensus, 0,
        VMX_PLATFORM_COMPATIBILITY_MICROSOFT_HYPERV),
        "Hyper-V consensus rejected");
    TEST_ASSERT_UINT_EQ(Consensus.RejectedSystemIndex, 0,
        "Hyper-V rejection index");
    TEST_ASSERT_UINT_EQ(Consensus.RejectedClassification,
        VMX_PLATFORM_COMPATIBILITY_MICROSOFT_HYPERV,
        "Hyper-V rejected classification");
    return 0;
}

static int TestConsensusFramingRejected(void)
{
    VMX_PLATFORM_COMPATIBILITY_CONSENSUS Consensus = { 0 };
    VMX_PLATFORM_COMPATIBILITY_CONSENSUS Before;

    TEST_ASSERT(!VmxPlatformCompatibilityConsensusBegin(0, &Consensus),
        "zero expected count rejected");
    TEST_ASSERT(VmxPlatformCompatibilityConsensusBegin(2, &Consensus),
        "framing consensus begin");
    Before = Consensus;
    TEST_ASSERT(!VmxPlatformCompatibilityConsensusObserve(&Consensus, 1,
        VMX_PLATFORM_COMPATIBILITY_NATIVE), "skipped index rejected");
    TEST_ASSERT_UINT_EQ(Consensus.ObservedProcessors,
        Before.ObservedProcessors, "skipped index preserves count");
    TEST_ASSERT(VmxPlatformCompatibilityConsensusObserve(&Consensus, 0,
        VMX_PLATFORM_COMPATIBILITY_NATIVE), "first ordered observation");
    TEST_ASSERT(!VmxPlatformCompatibilityConsensusComplete(&Consensus),
        "incomplete consensus rejected");
    TEST_ASSERT(!VmxPlatformCompatibilityConsensusObserve(&Consensus, 0,
        VMX_PLATFORM_COMPATIBILITY_NATIVE), "duplicate index rejected");
    TEST_ASSERT(VmxPlatformCompatibilityConsensusObserve(&Consensus, 1,
        VMX_PLATFORM_COMPATIBILITY_NATIVE), "second ordered observation");
    TEST_ASSERT(!VmxPlatformCompatibilityConsensusObserve(&Consensus, 2,
        VMX_PLATFORM_COMPATIBILITY_NATIVE), "extra observation rejected");
    TEST_ASSERT(VmxPlatformCompatibilityConsensusComplete(&Consensus),
        "complete framing admitted");
    TEST_ASSERT(!VmxPlatformCompatibilityConsensusObserve(&Consensus, 2,
        VMX_PLATFORM_COMPATIBILITY_NATIVE),
        "finalized consensus rejects observation");

    TEST_ASSERT(VmxPlatformCompatibilityConsensusBegin(1, &Consensus),
        "reserved consensus begin");
    Consensus.Reserved = 1;
    TEST_ASSERT(!VmxPlatformCompatibilityConsensusObserve(&Consensus, 0,
        VMX_PLATFORM_COMPATIBILITY_NATIVE), "reserved state rejected");
    return 0;
}

TEST_MAIN_BEGIN()
    TEST_RUN(TestNativeAdmission);
    TEST_RUN(TestVmwareAdmission);
    TEST_RUN(TestHyperVRejected);
    TEST_RUN(TestUnknownRejected);
    TEST_RUN(TestMalformedRejected);
    TEST_RUN(TestNativeConsensus);
    TEST_RUN(TestVmwareConsensus);
    TEST_RUN(TestMixedConsensusRejected);
    TEST_RUN(TestDisallowedConsensusRejected);
    TEST_RUN(TestConsensusFramingRejected);
TEST_MAIN_END()
