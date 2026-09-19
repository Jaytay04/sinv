#ifndef HYPERSINV_VMX_EPT_SESSION_CONFIG_H
#define HYPERSINV_VMX_EPT_SESSION_CONFIG_H

#include <stddef.h>
#include <stdint.h>

#include "vmx_ept_identity_map.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VMX_EPT_POLICY_LEAF_2MB UINT64_C(0x200000)
#define VMX_EPT_POLICY_LEAF_1GB UINT64_C(0x40000000)

typedef enum VMX_EPT_SESSION_CONFIG_STATUS {
    VMX_EPT_SESSION_CONFIG_OK = 0,
    VMX_EPT_SESSION_CONFIG_INVALID = -1,
    VMX_EPT_SESSION_CONFIG_UNSUPPORTED = -2,
    VMX_EPT_SESSION_CONFIG_OVERFLOW = -3
} VMX_EPT_SESSION_CONFIG_STATUS;

/* WDK-free authenticated input captured by a future kernel adapter.
 * HighestPresentPhysicalAddress is inclusive. Reserved must be zero. Build and
 * validation snapshot every admitted fixed-size caller object once and use
 * only the frozen values for policy derivation and exact comparison. */
typedef struct VMX_EPT_SESSION_CONFIG_INPUT {
    uint64_t EptVpidCapabilities;
    uint64_t HighestPresentPhysicalAddress;
    uint32_t PhysicalAddressBits;
    uint32_t Reserved;
} VMX_EPT_SESSION_CONFIG_INPUT;

/* Exact D4 session configuration. The conservative initial policy maps all
 * 2 MiB leaves UC with RWX permissions and A/D disabled so later hook splits
 * have one deterministic parent boundary; this is safe across RAM,
 * holes, and MMIO until a later memory-type-aware mapper exists. */
typedef struct VMX_EPT_SESSION_CONFIG {
    VMX_EPT_CAPABILITIES Capabilities;
    VMX_EPT_IDENTITY_CONFIG Identity;
    VMX_EPT_POPULATE_CONFIG Populate;
    VMX_EPT_IDENTITY_PLAN Plan;
} VMX_EPT_SESSION_CONFIG;

VMX_EPT_SESSION_CONFIG_STATUS VmxEptSessionConfigBuild(
    const VMX_EPT_SESSION_CONFIG_INPUT* input,
    VMX_EPT_SESSION_CONFIG* config);
VMX_EPT_SESSION_CONFIG_STATUS VmxEptSessionConfigValidate(
    const VMX_EPT_SESSION_CONFIG_INPUT* input,
    const VMX_EPT_SESSION_CONFIG* config);

#if defined(__cplusplus)
#define VMX_EPT_CONFIG_ASSERT static_assert
#define VMX_EPT_CONFIG_ALIGNOF(type) alignof(type)
#else
#define VMX_EPT_CONFIG_ASSERT _Static_assert
#define VMX_EPT_CONFIG_ALIGNOF(type) _Alignof(type)
#endif

VMX_EPT_CONFIG_ASSERT(sizeof(VMX_EPT_SESSION_CONFIG_INPUT) == 24,
    "EPT session-config input size");
VMX_EPT_CONFIG_ASSERT(offsetof(VMX_EPT_SESSION_CONFIG_INPUT,
    HighestPresentPhysicalAddress) == 8, "EPT highest-address offset");
VMX_EPT_CONFIG_ASSERT(offsetof(VMX_EPT_SESSION_CONFIG_INPUT,
    PhysicalAddressBits) == 16, "EPT physical-width offset");
VMX_EPT_CONFIG_ASSERT(VMX_EPT_CONFIG_ALIGNOF(
    VMX_EPT_SESSION_CONFIG_INPUT) == 8, "EPT input alignment");
VMX_EPT_CONFIG_ASSERT(sizeof(VMX_EPT_SESSION_CONFIG) == 128,
    "EPT session-config size");
VMX_EPT_CONFIG_ASSERT(offsetof(VMX_EPT_SESSION_CONFIG, Identity) == 16,
    "EPT identity offset");
VMX_EPT_CONFIG_ASSERT(offsetof(VMX_EPT_SESSION_CONFIG, Populate) == 40,
    "EPT populate offset");
VMX_EPT_CONFIG_ASSERT(offsetof(VMX_EPT_SESSION_CONFIG, Plan) == 56,
    "EPT plan offset");
VMX_EPT_CONFIG_ASSERT(VMX_EPT_CONFIG_ALIGNOF(
    VMX_EPT_SESSION_CONFIG) == 8, "EPT config alignment");

#undef VMX_EPT_CONFIG_ALIGNOF
#undef VMX_EPT_CONFIG_ASSERT

#ifdef __cplusplus
}
#endif

#endif
