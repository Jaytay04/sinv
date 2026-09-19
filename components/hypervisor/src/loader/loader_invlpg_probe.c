#include "loader_invlpg_probe.h"
#include "vmx.h"

int LoaderInvlpgControlsTransparent(uint32_t primary_controls)
{
    return (primary_controls & CPU_BASED_INVLPG_EXITING) == 0;
}
