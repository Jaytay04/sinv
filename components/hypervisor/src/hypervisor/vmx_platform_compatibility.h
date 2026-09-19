#ifndef HYPERSINV_VMX_PLATFORM_COMPATIBILITY_H
#define HYPERSINV_VMX_PLATFORM_COMPATIBILITY_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VMX_PLATFORM_HYPERVISOR_PRESENT UINT32_C(1)
#define VMX_PLATFORM_HYPERVISOR_LEAF_MIN UINT32_C(0x40000000)
#define VMX_PLATFORM_COMPATIBILITY_CONSENSUS_VERSION UINT32_C(1)
#define VMX_PLATFORM_COMPATIBILITY_NO_PROCESSOR UINT32_MAX

typedef struct VMX_PLATFORM_CPUID_IDENTITY {
    uint32_t HypervisorPresent;
    uint32_t MaximumHypervisorLeaf;
    uint32_t VendorEbx;
    uint32_t VendorEcx;
    uint32_t VendorEdx;
    uint32_t Reserved;
} VMX_PLATFORM_CPUID_IDENTITY;

typedef enum VMX_PLATFORM_COMPATIBILITY_CLASS {
    VMX_PLATFORM_COMPATIBILITY_INVALID = 0,
    VMX_PLATFORM_COMPATIBILITY_NATIVE = 1,
    VMX_PLATFORM_COMPATIBILITY_VMWARE_NESTED = 2,
    VMX_PLATFORM_COMPATIBILITY_MICROSOFT_HYPERV = 3,
    VMX_PLATFORM_COMPATIBILITY_UNSUPPORTED = 4
} VMX_PLATFORM_COMPATIBILITY_CLASS;

typedef enum VMX_PLATFORM_COMPATIBILITY_CONSENSUS_STATUS {
    VMX_PLATFORM_COMPATIBILITY_CONSENSUS_INVALID = 0,
    VMX_PLATFORM_COMPATIBILITY_CONSENSUS_COLLECTING = 1,
    VMX_PLATFORM_COMPATIBILITY_CONSENSUS_ADMITTED = 2,
    VMX_PLATFORM_COMPATIBILITY_CONSENSUS_DENIED = 3
} VMX_PLATFORM_COMPATIBILITY_CONSENSUS_STATUS;

typedef struct VMX_PLATFORM_COMPATIBILITY_CONSENSUS {
    uint32_t Version;
    uint32_t ExpectedProcessors;
    uint32_t ObservedProcessors;
    uint32_t ConsensusClassification;
    uint32_t RejectedSystemIndex;
    uint32_t RejectedClassification;
    uint32_t Status;
    uint32_t Reserved;
} VMX_PLATFORM_COMPATIBILITY_CONSENSUS;

VMX_PLATFORM_COMPATIBILITY_CLASS VmxPlatformCompatibilityClassify(
    const VMX_PLATFORM_CPUID_IDENTITY* Identity);

bool VmxPlatformCompatibilityAllowsVmx(
    VMX_PLATFORM_COMPATIBILITY_CLASS Classification);

bool VmxPlatformCompatibilityConsensusBegin(
    uint32_t ExpectedProcessors,
    VMX_PLATFORM_COMPATIBILITY_CONSENSUS* OutConsensus);

bool VmxPlatformCompatibilityConsensusObserve(
    VMX_PLATFORM_COMPATIBILITY_CONSENSUS* Consensus,
    uint32_t SystemIndex,
    VMX_PLATFORM_COMPATIBILITY_CLASS Classification);

bool VmxPlatformCompatibilityConsensusComplete(
    VMX_PLATFORM_COMPATIBILITY_CONSENSUS* Consensus);

#ifdef __cplusplus
}
#endif

#endif /* HYPERSINV_VMX_PLATFORM_COMPATIBILITY_H */
