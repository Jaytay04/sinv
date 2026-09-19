#include "loader_cet_vmcs_probe.h"
#include "vmx.h"

int LoaderCetVmcsStateSuppressed(
    uint32_t primary_controls,
    uint32_t exit_controls,
    uint32_t entry_controls,
    uint64_t guest_cr4,
    uint64_t host_cr4,
    uint64_t root_cr4)
{
    return (primary_controls & CPU_BASED_HLT_EXITING) == 0 &&
        (exit_controls & VM_EXIT_LOAD_CET_STATE) == 0 &&
        (entry_controls & VM_ENTRY_LOAD_CET_STATE) == 0 &&
        (guest_cr4 & CR4_CET) == 0 &&
        (host_cr4 & CR4_CET) == 0 &&
        (root_cr4 & CR4_CET) == 0;
}
