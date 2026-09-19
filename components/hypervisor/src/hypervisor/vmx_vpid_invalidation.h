#ifndef HYPERSINV_VMX_VPID_INVALIDATION_H
#define HYPERSINV_VMX_VPID_INVALIDATION_H

#include "vmx_vpid_capability.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VMX_VPID_INVALIDATION_SINGLE_CONTEXT UINT32_C(1)
#define VMX_VPID_INVALIDATION_ALL_CONTEXTS UINT32_C(2)

typedef enum VMX_VPID_INVALIDATION_STATUS {
    VMX_VPID_INVALIDATION_OK = 0,
    VMX_VPID_INVALIDATION_INVALID = -1,
    VMX_VPID_INVALIDATION_UNSUPPORTED = -2
} VMX_VPID_INVALIDATION_STATUS;

typedef struct VMX_VPID_INVALIDATION_REQUEST {
    uint32_t Type;
    uint16_t Vpid;
    uint16_t Reserved16;
    uint64_t LinearAddress;
} VMX_VPID_INVALIDATION_REQUEST;

typedef struct VMX_VPID_INVALIDATION_DESCRIPTOR {
    uint64_t VpidAndReserved;
    uint64_t LinearAddress;
} VMX_VPID_INVALIDATION_DESCRIPTOR;

VMX_VPID_INVALIDATION_STATUS VmxVpidBuildInvalidationDescriptor(
    const VMX_VPID_CAPABILITIES* Capabilities,
    const VMX_VPID_INVALIDATION_REQUEST* Request,
    VMX_VPID_INVALIDATION_DESCRIPTOR* Descriptor);

VMX_VPID_INVALIDATION_STATUS VmxVpidValidateInvalidationDescriptor(
    const VMX_VPID_CAPABILITIES* Capabilities,
    const VMX_VPID_INVALIDATION_REQUEST* Request,
    const VMX_VPID_INVALIDATION_DESCRIPTOR* Descriptor);

#ifdef __cplusplus
}
#endif

#endif
