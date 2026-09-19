#ifndef HYPERSINV_LOADER_CET_VMCS_PROBE_H
#define HYPERSINV_LOADER_CET_VMCS_PROBE_H

#include <stdint.h>

int LoaderCetVmcsStateSuppressed(
    uint32_t primary_controls,
    uint32_t exit_controls,
    uint32_t entry_controls,
    uint64_t guest_cr4,
    uint64_t host_cr4,
    uint64_t root_cr4);

#endif
