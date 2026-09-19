#include "loader_msr_read_probe.h"

LOADER_MSR_READ_PROBE_RESULT LoaderMsrReadProbeClassify(
    uint64_t FeatureControl,
    uint64_t VmxBasic,
    uint64_t UserCet,
    uint64_t NativeEferBefore,
    uint64_t NativeEferAfter)
{
    if (FeatureControl != UINT64_C(1))
        return LOADER_MSR_READ_PROBE_FEATURE_CONTROL_VISIBLE;
    if (VmxBasic != 0)
        return LOADER_MSR_READ_PROBE_VMX_BASIC_VISIBLE;
    if (UserCet != 0)
        return LOADER_MSR_READ_PROBE_USER_CET_VISIBLE;
    if ((NativeEferBefore & (UINT64_C(1) << 8 | UINT64_C(1) << 10)) !=
        (UINT64_C(1) << 8 | UINT64_C(1) << 10))
        return LOADER_MSR_READ_PROBE_NATIVE_EFER_INVALID;
    if (NativeEferAfter != NativeEferBefore)
        return LOADER_MSR_READ_PROBE_NATIVE_EFER_CHANGED;
    return LOADER_MSR_READ_PROBE_PASS;
}
