#include "vmx_persistent_unload_policy.h"

static bool PlatformValid(const VMX_PERSISTENT_UNLOAD_PLATFORM* p)
{
    return p && p->Context && p->QueryPhase && p->Stop && p->Detach &&
        p->Delay && p->FailStop;
}

int32_t VmxPersistentUnloadPolicyRun(
    const VMX_PERSISTENT_UNLOAD_PLATFORM* p,
    uint32_t max_attempts)
{
    VMX_PERSISTENT_UNLOAD_PHASE phase = VMX_PERSISTENT_UNLOAD_PHASE_INVALID;
    int32_t status = VMX_PERSISTENT_UNLOAD_POLICY_INVALID;
    uint32_t attempt, completed = 0;

    if (!PlatformValid(p) || max_attempts == 0 ||
        max_attempts > VMX_PERSISTENT_UNLOAD_MAX_ATTEMPTS)
        return VMX_PERSISTENT_UNLOAD_POLICY_INVALID;

    for (attempt = 1; attempt <= max_attempts; ++attempt) {
        completed = attempt;
        int32_t query = p->QueryPhase(p->Context, &phase);
        if (query == 0 && phase == VMX_PERSISTENT_UNLOAD_PHASE_DETACHED)
            return 0;
        status = query;
        if (query == 0 && phase == VMX_PERSISTENT_UNLOAD_PHASE_ATTACHED) {
            int32_t stop = p->Stop(p->Context);
            int32_t detach = p->Detach(p->Context);
            status = stop != 0 ? stop : detach;
        } else if (query == 0 &&
            phase == VMX_PERSISTENT_UNLOAD_PHASE_TEARDOWN_PENDING) {
            status = p->Detach(p->Context);
        } else if (query == 0) {
            status = VMX_PERSISTENT_UNLOAD_POLICY_INVALID;
        }
        query = p->QueryPhase(p->Context, &phase);
        if (query == 0 && phase == VMX_PERSISTENT_UNLOAD_PHASE_DETACHED)
            return 0;
        if (status == 0)
            status = query != 0 ? query : VMX_PERSISTENT_UNLOAD_POLICY_INVALID;
        if (attempt < max_attempts && !p->Delay(p->Context, attempt))
            break;
    }
    p->FailStop(p->Context, status, phase, completed);
    return status != 0 ? status : VMX_PERSISTENT_UNLOAD_POLICY_INVALID;
}
