#ifndef HYPERSINV_VMX_VPID_OWNER_H
#define HYPERSINV_VMX_VPID_OWNER_H

#include <stddef.h>
#include <stdint.h>
#include "vmx_processor_topology.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VMX_VPID_OWNER_MAGIC UINT32_C(0x44495056)
#define VMX_VPID_OWNER_VERSION UINT32_C(1)
#define VMX_VPID_MAX UINT16_MAX

typedef enum VMX_VPID_OWNER_STATUS {
    VMX_VPID_OWNER_OK = 0,
    VMX_VPID_OWNER_INVALID = -1,
    VMX_VPID_OWNER_ALREADY_OWNED = -2,
    VMX_VPID_OWNER_RETIRE_PENDING = -3,
    VMX_VPID_OWNER_NOT_OWNED = -4,
    VMX_VPID_OWNER_BUSY = -5
} VMX_VPID_OWNER_STATUS;

typedef enum VMX_VPID_RECORD_STATE {
    VMX_VPID_RECORD_FREE = 0,
    VMX_VPID_RECORD_OWNED = 1,
    VMX_VPID_RECORD_RETIRE_PENDING = 2
} VMX_VPID_RECORD_STATE;

typedef struct VMX_VPID_RECORD {
    VMX_PROCESSOR_IDENTITY Identity;
    uint16_t Vpid;
    uint16_t State;
    uint32_t Reserved;
} VMX_VPID_RECORD;

typedef struct VMX_VPID_OWNER {
    uint32_t Magic;
    uint32_t Version;
    VMX_VPID_RECORD* Records;
    VMX_VPID_RECORD* RecordsContract;
    size_t Count;
    size_t CountContract;
    size_t RecordsBytes;
    size_t RecordsBytesContract;
} VMX_VPID_OWNER;

VMX_VPID_OWNER_STATUS VmxVpidOwnerInitialize(
    const VMX_PROCESSOR_IDENTITY* Identities,
    size_t Count,
    VMX_VPID_RECORD* Records,
    VMX_VPID_OWNER* Owner);

VMX_VPID_OWNER_STATUS VmxVpidOwnerAcquire(
    VMX_VPID_OWNER* Owner,
    const VMX_PROCESSOR_IDENTITY* Identity,
    uint16_t* OutVpid);

/* Retires ownership without making the identifier reusable. */
VMX_VPID_OWNER_STATUS VmxVpidOwnerRetire(
    VMX_VPID_OWNER* Owner,
    const VMX_PROCESSOR_IDENTITY* Identity,
    uint16_t Vpid);

/* Call only after the required INVVPID completed successfully. */
VMX_VPID_OWNER_STATUS VmxVpidOwnerAcknowledgeInvalidation(
    VMX_VPID_OWNER* Owner,
    const VMX_PROCESSOR_IDENTITY* Identity,
    uint16_t Vpid);

VMX_VPID_OWNER_STATUS VmxVpidOwnerValidate(const VMX_VPID_OWNER* Owner);

/* Succeeds only when every identifier is free, then zeros owner and storage. */
VMX_VPID_OWNER_STATUS VmxVpidOwnerDestroy(VMX_VPID_OWNER* Owner);

#ifdef __cplusplus
}
#endif

#endif
