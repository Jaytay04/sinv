#ifndef HYPERSINV_VMX_PERSISTENT_ROOT_PROOF_H
#define HYPERSINV_VMX_PERSISTENT_ROOT_PROOF_H

#include <stdint.h>
#include "vmx_processor_topology.h"

#define VMX_PERSISTENT_ROOT_PROOF_VERSION UINT32_C(1)

typedef struct _VMX_PERSISTENT_ROOT_PROOF {
    uint32_t Version;
    VMX_PROCESSOR_IDENTITY Identity;
    uint64_t VmxonPhysicalAddress;
    uint64_t VmcsPhysicalAddress;
    uint64_t CurrentVmcsPhysicalAddress;
    uint32_t ContextVersion;
    uint32_t ContextOwnedResources;
    uint32_t ContextPublished;
    uint32_t VmxActive;
    uint32_t VmxCurrent;
    uint32_t RecordStage;
    uint32_t RecordOwnedMask;
    uint32_t RecordActive;
    uint32_t CleanupRequired;
    uint32_t CurrentVmcsValid;
    uint32_t OperationsAuthenticated;
    uint32_t Reserved[2];
} VMX_PERSISTENT_ROOT_PROOF;

#endif