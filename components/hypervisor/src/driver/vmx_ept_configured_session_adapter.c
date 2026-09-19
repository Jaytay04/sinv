#include "vmx_ept_configured_session_adapter.h"

static int ZeroOwner(const VMX_EPT_SESSION_OWNER* owner)
{
    const unsigned char* bytes = (const unsigned char*)owner;
    size_t index;
    if (owner == NULL)
        return 0;
    for (index = 0; index < sizeof(*owner); ++index)
        if (bytes[index] != 0)
            return 0;
    return 1;
}

static VMX_EPT_KERNEL_CONFIGURED_SESSION_STATUS MapConfig(
    VMX_EPT_KERNEL_CONFIG_STATUS status)
{
    switch (status) {
    case VMX_EPT_KERNEL_CONFIG_OK:
        return VMX_EPT_KERNEL_CONFIGURED_SESSION_OK;
    case VMX_EPT_KERNEL_CONFIG_INVALID:
        return VMX_EPT_KERNEL_CONFIGURED_SESSION_INVALID;
    case VMX_EPT_KERNEL_CONFIG_WRONG_IRQL:
        return VMX_EPT_KERNEL_CONFIGURED_SESSION_WRONG_IRQL;
    case VMX_EPT_KERNEL_CONFIG_CAPTURE_FAILED:
        return VMX_EPT_KERNEL_CONFIGURED_SESSION_CAPTURE_FAILED;
    case VMX_EPT_KERNEL_CONFIG_RANGE_INVALID:
        return VMX_EPT_KERNEL_CONFIGURED_SESSION_RANGE_INVALID;
    case VMX_EPT_KERNEL_CONFIG_UNSUPPORTED:
        return VMX_EPT_KERNEL_CONFIGURED_SESSION_UNSUPPORTED;
    case VMX_EPT_KERNEL_CONFIG_OVERFLOW:
        return VMX_EPT_KERNEL_CONFIGURED_SESSION_OVERFLOW;
    case VMX_EPT_KERNEL_CONFIG_INVARIANT:
        return VMX_EPT_KERNEL_CONFIGURED_SESSION_INVARIANT;
    default:
        return VMX_EPT_KERNEL_CONFIGURED_SESSION_INVALID;
    }
}

static VMX_EPT_KERNEL_CONFIGURED_SESSION_STATUS MapSession(
    VMX_EPT_KERNEL_SESSION_STATUS status)
{
    switch (status) {
    case VMX_EPT_KERNEL_SESSION_OK:
        return VMX_EPT_KERNEL_CONFIGURED_SESSION_OK;
    case VMX_EPT_KERNEL_SESSION_INVALID:
        return VMX_EPT_KERNEL_CONFIGURED_SESSION_INVALID;
    case VMX_EPT_KERNEL_SESSION_WRONG_IRQL:
        return VMX_EPT_KERNEL_CONFIGURED_SESSION_WRONG_IRQL;
    case VMX_EPT_KERNEL_SESSION_TOPOLOGY_FAILED:
        return VMX_EPT_KERNEL_CONFIGURED_SESSION_TOPOLOGY_FAILED;
    case VMX_EPT_KERNEL_SESSION_UNSUPPORTED:
        return VMX_EPT_KERNEL_CONFIGURED_SESSION_UNSUPPORTED;
    case VMX_EPT_KERNEL_SESSION_ALLOCATION_FAILED:
        return VMX_EPT_KERNEL_CONFIGURED_SESSION_ALLOCATION_FAILED;
    case VMX_EPT_KERNEL_SESSION_REGION_INVALID:
        return VMX_EPT_KERNEL_CONFIGURED_SESSION_REGION_INVALID;
    case VMX_EPT_KERNEL_SESSION_BUSY:
        return VMX_EPT_KERNEL_CONFIGURED_SESSION_BUSY;
    case VMX_EPT_KERNEL_SESSION_DIRTY:
        return VMX_EPT_KERNEL_CONFIGURED_SESSION_DIRTY;
    default:
        return VMX_EPT_KERNEL_CONFIGURED_SESSION_INVALID;
    }
}

VMX_EPT_KERNEL_CONFIGURED_SESSION_STATUS
VmxEptKernelConfiguredSessionInitialize(VMX_EPT_SESSION_OWNER* owner)
{
    VMX_EPT_SESSION_CONFIG config = { 0 };
    VMX_EPT_KERNEL_CONFIG_STATUS config_status;
    VMX_EPT_KERNEL_SESSION_STATUS session_status;
    if (owner == NULL)
        return VMX_EPT_KERNEL_CONFIGURED_SESSION_INVALID;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return VMX_EPT_KERNEL_CONFIGURED_SESSION_WRONG_IRQL;
    if (!ZeroOwner(owner))
        return VMX_EPT_KERNEL_CONFIGURED_SESSION_INVALID;
    config_status = VmxEptKernelSessionConfigBuild(&config);
    if (config_status != VMX_EPT_KERNEL_CONFIG_OK) {
        RtlSecureZeroMemory(&config, sizeof(config));
        return MapConfig(config_status);
    }
    session_status = VmxEptKernelSessionInitialize(&config.Capabilities,
        &config.Identity, &config.Populate, owner);
    RtlSecureZeroMemory(&config, sizeof(config));
    return MapSession(session_status);
}

VMX_EPT_KERNEL_CONFIGURED_SESSION_STATUS
VmxEptKernelConfiguredSessionDestroy(VMX_EPT_SESSION_OWNER* owner)
{
    return MapSession(VmxEptKernelSessionDestroy(owner));
}