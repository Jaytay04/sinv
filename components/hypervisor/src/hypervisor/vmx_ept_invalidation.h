#ifndef HYPERSINV_VMX_EPT_INVALIDATION_H
#define HYPERSINV_VMX_EPT_INVALIDATION_H

#include "vmx_ept_model.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VMX_EPT_INVALIDATION_SINGLE_CONTEXT UINT32_C(1)
#define VMX_EPT_INVALIDATION_ALL_CONTEXTS   UINT32_C(2)

typedef struct VMX_EPT_INVALIDATION_REQUEST {
    uint32_t Type;
    uint32_t Reserved32;
    VMX_EPT_POINTER_CONFIG Pointer;
    uint64_t Reserved;
} VMX_EPT_INVALIDATION_REQUEST;

typedef struct VMX_EPT_INVALIDATION_DESCRIPTOR {
    uint64_t EptPointer;
    uint64_t Reserved;
} VMX_EPT_INVALIDATION_DESCRIPTOR;

/*
 * Build or validate the architected 128-bit INVEPT descriptor only.
 * This module never executes INVEPT or any other VMX instruction.
 * Single-context requests carry one exact validated EPTP; all-context requests
 * require a completely zero pointer configuration and produce a zero descriptor.
 */
VMX_EPT_STATUS VmxEptBuildInvalidationDescriptor(
    const VMX_EPT_CAPABILITIES* capabilities,
    const VMX_EPT_INVALIDATION_REQUEST* request,
    VMX_EPT_INVALIDATION_DESCRIPTOR* descriptor);

VMX_EPT_STATUS VmxEptValidateInvalidationDescriptor(
    const VMX_EPT_CAPABILITIES* capabilities,
    const VMX_EPT_INVALIDATION_REQUEST* request,
    const VMX_EPT_INVALIDATION_DESCRIPTOR* descriptor);

#ifdef __cplusplus
}
#endif
#endif
