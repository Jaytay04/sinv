#ifndef HYPERSINV_VMX_PERSISTENT_UNLOAD_POLICY_H
#define HYPERSINV_VMX_PERSISTENT_UNLOAD_POLICY_H

#include <stdbool.h>
#include <stdint.h>

#define VMX_PERSISTENT_UNLOAD_MAX_ATTEMPTS UINT32_C(4)
#define VMX_PERSISTENT_UNLOAD_POLICY_INVALID INT32_C(-1)

typedef enum VMX_PERSISTENT_UNLOAD_PHASE {
    VMX_PERSISTENT_UNLOAD_PHASE_INVALID = 0,
    VMX_PERSISTENT_UNLOAD_PHASE_DETACHED = 1,
    VMX_PERSISTENT_UNLOAD_PHASE_ATTACHED = 2,
    VMX_PERSISTENT_UNLOAD_PHASE_TEARDOWN_PENDING = 3
} VMX_PERSISTENT_UNLOAD_PHASE;

typedef struct VMX_PERSISTENT_UNLOAD_PLATFORM {
    void* Context;
    int32_t (*QueryPhase)(void* context, VMX_PERSISTENT_UNLOAD_PHASE* phase);
    int32_t (*Stop)(void* context);
    int32_t (*Detach)(void* context);
    bool (*Delay)(void* context, uint32_t completed_attempt);
    void (*FailStop)(void* context, int32_t status,
        VMX_PERSISTENT_UNLOAD_PHASE phase, uint32_t attempts);
} VMX_PERSISTENT_UNLOAD_PLATFORM;

/* max_attempts is 1..VMX_PERSISTENT_UNLOAD_MAX_ATTEMPTS. Returns only after
 * authenticated DETACHED or after invoking FailStop. */
int32_t VmxPersistentUnloadPolicyRun(
    const VMX_PERSISTENT_UNLOAD_PLATFORM* platform,
    uint32_t max_attempts);

#endif
