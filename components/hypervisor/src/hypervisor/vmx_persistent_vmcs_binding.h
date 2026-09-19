#ifndef HYPERSINV_VMX_PERSISTENT_VMCS_BINDING_H
#define HYPERSINV_VMX_PERSISTENT_VMCS_BINDING_H

#include <stddef.h>
#include <stdint.h>

#include "vmx_processor_topology.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VMX_PERSISTENT_VMCS_BINDING_MAX_COUNT UINT32_C(4096)

/* Pointer-free copied evidence for one Persistent V3-owned VMCS page. */
typedef struct _VMX_PERSISTENT_VMCS_BINDING {
    VMX_PROCESSOR_IDENTITY Identity;
    uint64_t VmcsPhysicalAddress;
} VMX_PERSISTENT_VMCS_BINDING;

#if defined(__cplusplus)
static_assert(sizeof(VMX_PERSISTENT_VMCS_BINDING) == 16,
    "persistent VMCS binding ABI");
#else
_Static_assert(sizeof(VMX_PERSISTENT_VMCS_BINDING) == 16,
    "persistent VMCS binding ABI");
#endif

#ifdef __cplusplus
}
#endif

#endif