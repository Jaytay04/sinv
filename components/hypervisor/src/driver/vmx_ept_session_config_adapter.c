#include "vmx_ept_session_config_adapter.h"

#include <intrin.h>

#include "vmx.h"
#include "vmx_kernel_adapters.h"

static bool Zero(const void* memory, size_t size)
{
    const unsigned char* bytes = (const unsigned char*)memory;
    size_t index;
    if (memory == NULL)
        return false;
    for (index = 0; index < size; ++index)
        if (bytes[index] != 0)
            return false;
    return true;
}

static VMX_EPT_KERNEL_CONFIG_STATUS MapConfig(
    VMX_EPT_SESSION_CONFIG_STATUS status)
{
    switch (status) {
    case VMX_EPT_SESSION_CONFIG_OK:
        return VMX_EPT_KERNEL_CONFIG_OK;
    case VMX_EPT_SESSION_CONFIG_UNSUPPORTED:
        return VMX_EPT_KERNEL_CONFIG_UNSUPPORTED;
    case VMX_EPT_SESSION_CONFIG_OVERFLOW:
        return VMX_EPT_KERNEL_CONFIG_OVERFLOW;
    default:
        return VMX_EPT_KERNEL_CONFIG_INVALID;
    }
}

static bool CaptureHighest(
    const PHYSICAL_MEMORY_RANGE* ranges, uint64_t* highest)
{
    uint64_t previous_end = 0;
    uint32_t index;
    bool any = false;
    if (ranges == NULL || highest == NULL)
        return false;
    for (index = 0; index < VMX_EPT_KERNEL_RANGE_LIMIT; ++index) {
        LONGLONG signed_base = ranges[index].BaseAddress.QuadPart;
        LONGLONG signed_bytes = ranges[index].NumberOfBytes.QuadPart;
        uint64_t base, bytes, end;
        if (signed_base == 0 && signed_bytes == 0) {
            if (!any)
                return false;
            *highest = previous_end - 1;
            return true;
        }
        if (signed_base < 0 || signed_bytes <= 0)
            return false;
        base = (uint64_t)signed_base;
        bytes = (uint64_t)signed_bytes;
        if ((base & (PAGE_SIZE - 1)) != 0 ||
            (bytes & (PAGE_SIZE - 1)) != 0 ||
            base > UINT64_MAX - bytes)
            return false;
        end = base + bytes;
        if (any && base < previous_end)
            return false;
        previous_end = end;
        any = true;
    }
    return false;
}

static bool NativePhysicalAddressBits(uint32_t* bits)
{
    int registers[4];
    if (bits == NULL)
        return false;
    __cpuid(registers, (int)UINT32_C(0x80000000));
    if ((uint32_t)registers[0] < UINT32_C(0x80000008))
        return false;
    __cpuid(registers, (int)UINT32_C(0x80000008));
    *bits = (uint32_t)registers[0] & UINT32_C(0xFF);
    return true;
}

static PPHYSICAL_MEMORY_RANGE NativeQueryRanges(void)
{
    return MmGetPhysicalMemoryRangesEx2(MM_SYSTEM_PARTITION_OBJECT, 0);
}

static void NativeFreeRanges(PPHYSICAL_MEMORY_RANGE ranges)
{
    if (ranges != NULL)
        ExFreePool(ranges);
}

VMX_EPT_KERNEL_CONFIG_STATUS VmxEptKernelSessionConfigBuildWithSource(
    const VMX_EPT_KERNEL_CONFIG_SOURCE* source,
    VMX_EPT_SESSION_CONFIG* out_config)
{
    VMX_EPT_SESSION_CONFIG_INPUT input = { 0 };
    VMX_EPT_SESSION_CONFIG staged;
    VMX_EPT_SESSION_CONFIG_STATUS status;
    PPHYSICAL_MEMORY_RANGE ranges;
    uint64_t highest;
    bool range_valid;
    if (source == NULL || source->CheckCpuidVmx == NULL ||
        source->ReadMsr == NULL ||
        source->QueryPhysicalAddressBits == NULL ||
        source->QueryRanges == NULL || source->FreeRanges == NULL ||
        out_config == NULL || !Zero(out_config, sizeof(*out_config)))
        return VMX_EPT_KERNEL_CONFIG_INVALID;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return VMX_EPT_KERNEL_CONFIG_WRONG_IRQL;
    if (!source->CheckCpuidVmx())
        return VMX_EPT_KERNEL_CONFIG_UNSUPPORTED;
    if (!source->ReadMsr(
            IA32_VMX_EPT_VPID_CAP, &input.EptVpidCapabilities) ||
        !source->QueryPhysicalAddressBits(&input.PhysicalAddressBits))
        return VMX_EPT_KERNEL_CONFIG_CAPTURE_FAILED;
    ranges = source->QueryRanges();
    if (ranges == NULL)
        return VMX_EPT_KERNEL_CONFIG_CAPTURE_FAILED;
    highest = 0;
    range_valid = CaptureHighest(ranges, &highest);
    source->FreeRanges(ranges);
    if (!range_valid)
        return VMX_EPT_KERNEL_CONFIG_RANGE_INVALID;
    input.HighestPresentPhysicalAddress = highest;
    status = VmxEptSessionConfigBuild(&input, &staged);
    if (status != VMX_EPT_SESSION_CONFIG_OK)
        return MapConfig(status);
    if (VmxEptSessionConfigValidate(&input, &staged) !=
        VMX_EPT_SESSION_CONFIG_OK)
        return VMX_EPT_KERNEL_CONFIG_INVARIANT;
    RtlCopyMemory(out_config, &staged, sizeof(staged));
    return VMX_EPT_KERNEL_CONFIG_OK;
}

VMX_EPT_KERNEL_CONFIG_STATUS VmxEptKernelSessionConfigBuild(
    VMX_EPT_SESSION_CONFIG* out_config)
{
    const VMX_EPT_KERNEL_CONFIG_SOURCE source = {
        KernelPreflightPlatform.CheckCpuidVmx,
        KernelPreflightPlatform.ReadMsr,
        NativePhysicalAddressBits,
        NativeQueryRanges,
        NativeFreeRanges
    };
    return VmxEptKernelSessionConfigBuildWithSource(&source, out_config);
}