#ifndef HYPERSINV_LOADER_TSC_PROBE_H
#define HYPERSINV_LOADER_TSC_PROBE_H

#include <stdint.h>

typedef enum LOADER_TSC_PROBE_RESULT {
    LOADER_TSC_PROBE_PASS = 0,
    LOADER_TSC_PROBE_ZERO_BOUND = 1,
    LOADER_TSC_PROBE_REVERSED_WINDOW = 2,
    LOADER_TSC_PROBE_GUEST_BEFORE_WINDOW = 3,
    LOADER_TSC_PROBE_GUEST_AFTER_WINDOW = 4
} LOADER_TSC_PROBE_RESULT;

LOADER_TSC_PROBE_RESULT LoaderTscProbeClassify(
    uint64_t root_before,
    uint64_t guest_tsc,
    uint64_t root_after);

int LoaderTscControlsTransparent(
    uint32_t primary_controls,
    uint32_t secondary_controls,
    uint64_t tsc_offset);

#endif
