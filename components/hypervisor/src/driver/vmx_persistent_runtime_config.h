#ifndef HYPERSINV_VMX_PERSISTENT_RUNTIME_CONFIG_H
#define HYPERSINV_VMX_PERSISTENT_RUNTIME_CONFIG_H
/* WDK-only trusted Persistent V3 runtime-configuration builder. */
#include <ntddk.h>
#include "vmx_persistent_runtime_lifecycle.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct _VMX_PERSISTENT_RUNTIME_CONFIG_SOURCE {
    uint64_t (*ReadCr4)(void);
    bool (*CheckCpuidVmx)(void);
    bool (*ReadMsr)(uint32_t msr, uint64_t* value);
    bool (*GenerateRandom)(uint8_t* buffer, size_t size);
} VMX_PERSISTENT_RUNTIME_CONFIG_SOURCE;
/* Structural validation authenticates the exact trusted platform, allocator,
 * VMX_BASIC, launch ABI, cookie, dispatcher, and canonical RIP mapping. */
bool VmxPersistentRuntimeConfigValidate(
    const VMX_PERSISTENT_RUNTIME_CONFIG* config);
/* Out must be zero and remains unchanged on failure. Both builders are
 * PASSIVE-only. INVALID means bad arguments/seam/output, NOT_READY means CET
 * active, VMX unsupported, FEATURE_CONTROL unsafe, MSR failure, or malformed
 * VMX_BASIC, or fresh nonzero-cookie generation; INVARIANT means a trusted
 * internal dependency is unavailable.
 * BuildWithSource exists only for bounded capability verification. */
VMX_PERSISTENT_STATUS VmxPersistentRuntimeConfigBuildWithSource(
    const VMX_PERSISTENT_RUNTIME_CONFIG_SOURCE* source,
    VMX_PERSISTENT_RUNTIME_CONFIG* out_config);
/* Authoritative LOAD uses the exact topology-consensus VMX_BASIC value and
 * does not resample a caller CPU. Per-processor preentry checks remain active
 * immediately before mutation. */
VMX_PERSISTENT_STATUS VmxPersistentRuntimeConfigBuildFromApprovedVmxBasic(
    uint64_t vmx_basic,
    VMX_PERSISTENT_RUNTIME_CONFIG* out_config);
VMX_PERSISTENT_STATUS VmxPersistentRuntimeConfigBuild(
    VMX_PERSISTENT_RUNTIME_CONFIG* out_config);
/* This builder gates only the current caller. It is NOT sufficient for LOAD:
 * every target CPU must independently reject CR4.CET before persistent entry. */
#ifdef __cplusplus
}
#endif
#endif
