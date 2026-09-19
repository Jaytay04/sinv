#include "loader_tsc_probe.h"
#include "vmx.h"

LOADER_TSC_PROBE_RESULT LoaderTscProbeClassify(
    uint64_t root_before,
    uint64_t guest_tsc,
    uint64_t root_after)
{
    if (root_before == 0 || guest_tsc == 0 || root_after == 0)
        return LOADER_TSC_PROBE_ZERO_BOUND;
    if (root_after < root_before)
        return LOADER_TSC_PROBE_REVERSED_WINDOW;
    if (guest_tsc < root_before)
        return LOADER_TSC_PROBE_GUEST_BEFORE_WINDOW;
    if (guest_tsc > root_after)
        return LOADER_TSC_PROBE_GUEST_AFTER_WINDOW;
    return LOADER_TSC_PROBE_PASS;
}

int LoaderTscControlsTransparent(
    uint32_t primary_controls,
    uint32_t secondary_controls,
    uint64_t tsc_offset)
{
    return (primary_controls &
            (CPU_BASED_USE_TSC_OFFSETTING | CPU_BASED_RDTSC_EXITING)) == 0 &&
        (secondary_controls & SECONDARY_USE_TSC_SCALING) == 0 &&
        tsc_offset == 0;
}
