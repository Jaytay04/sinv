#ifndef HYPERSINV_VMX_VPID_CAPABILITY_H
#define HYPERSINV_VMX_VPID_CAPABILITY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum VMX_VPID_CAPABILITY_STATUS {
    VMX_VPID_CAPABILITY_OK = 0,
    VMX_VPID_CAPABILITY_INVALID = -1,
    VMX_VPID_CAPABILITY_UNSUPPORTED = -2
} VMX_VPID_CAPABILITY_STATUS;

#define VMX_VPID_CAP_INVVPID \
    (UINT32_C(1) << 0)
#define VMX_VPID_CAP_INDIVIDUAL_ADDRESS \
    (UINT32_C(1) << 1)
#define VMX_VPID_CAP_SINGLE_CONTEXT \
    (UINT32_C(1) << 2)
#define VMX_VPID_CAP_ALL_CONTEXTS \
    (UINT32_C(1) << 3)
#define VMX_VPID_CAP_SINGLE_CONTEXT_RETAINING_GLOBALS \
    (UINT32_C(1) << 4)

#define VMX_VPID_CAP_RUNTIME_REQUIRED \
    (VMX_VPID_CAP_INVVPID | VMX_VPID_CAP_SINGLE_CONTEXT | \
     VMX_VPID_CAP_ALL_CONTEXTS)

typedef struct VMX_VPID_CAPABILITIES {
    uint64_t Raw;
    uint32_t Flags;
    uint32_t Reserved;
} VMX_VPID_CAPABILITIES;

/* Decode IA32_VMX_EPT_VPID_CAP without authorizing VPID activation. */
VMX_VPID_CAPABILITY_STATUS VmxVpidDecodeCapabilities(
    uint64_t Raw,
    VMX_VPID_CAPABILITIES* OutCapabilities);

/* Validate exact raw/decoded consistency and the Runtime V4 minimum set. */
VMX_VPID_CAPABILITY_STATUS VmxVpidValidateCapabilities(
    const VMX_VPID_CAPABILITIES* Capabilities);

/* VPID activation also requires the secondary VM-execution control's
 * allowed-1 bit. IA32_VMX_EPT_VPID_CAP alone authorizes INVVPID forms, not
 * the existence of the VMCS VPID field. */
VMX_VPID_CAPABILITY_STATUS VmxVpidValidateSecondaryControls(
    uint64_t RawSecondaryControls);

#ifdef __cplusplus
}
#endif

#endif
