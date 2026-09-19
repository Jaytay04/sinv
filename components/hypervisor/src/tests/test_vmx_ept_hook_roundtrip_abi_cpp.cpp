#include "driver_interface.h"
static_assert(sizeof(HYPERVISOR_EPT_HOOK_ROUNDTRIP_REQUEST)==16);
static_assert(sizeof(HYPERVISOR_EPT_HOOK_ROUNDTRIP_RESPONSE)==64);
static_assert(offsetof(HYPERVISOR_EPT_HOOK_ROUNDTRIP_RESPONSE,Generation)==56);