/*
 * vmx_init.c — VMX initialization logic (pure functions)
 *
 * This file contains ONLY functions that can compile without WDK headers.
 * Kernel-dependent functions (VMXON, VMLAUNCH, ExAllocatePool) are stubbed
 * here and implemented in the driver layer (core_mgmt.c).
 *
 * All functions in this file are pure C11, no kernel-mode includes required.
 */

#include "vmx.h"

/*===========================================================================
 * VMCS Revision ID Extraction
 *
 * IA32_VMX_BASIC bits 30:0 contain the VMCS revision identifier.
 * The upper bits contain VMCS size, memory type, and other capabilities.
 *===========================================================================*/

uint32_t VmxExtractRevisionId(uint64_t vmx_basic_msr) {
    return (uint32_t)(vmx_basic_msr & VMX_BASIC_REVISION_MASK);
}

/*===========================================================================
 * VMX Region Alignment Check
 *
 * VMXON and VMCS regions MUST be 4KB-aligned. Returns true if the
 * pointer is aligned to a 4096-byte boundary.
 *===========================================================================*/

bool VmxIsRegionAligned(const void* region) {
    return ((uintptr_t)region & VMX_REGION_ALIGNMENT_MASK) == 0;
}

/*===========================================================================
 * VMCS Field Encoding Decoders
 *
 * Intel SDM Vol 3, Appendix B defines the VMCS field encoding format:
 *   bits 0:0   — access type (0=full, 1=high)
 *   bits 9:1   — index
 *   bits 11:10 — type (0=control, 1=VM-exit info, 2=guest, 3=host)
 *   bits 14:13 — width (0=16-bit, 1=64-bit, 2=32-bit, 3=natural)
 *   bit  15    — must be 0 for valid encodings
 *===========================================================================*/

uint32_t VmcsFieldEncoding_Width(uint32_t encoding) {
    /* bits 14:13 */
    return (encoding >> 13) & 0x3;
}

uint32_t VmcsFieldEncoding_Type(uint32_t encoding) {
    /* bits 11:10 */
    return (encoding >> 10) & 0x3;
}

uint32_t VmcsFieldEncoding_Index(uint32_t encoding) {
    /* bits 10:1 (bits 9:1 for the index, bit 10 overlaps with type but
     * the index field is bits 9:1 from the hardware perspective.
     * Actually from the SDM: the index field is bits 9:1.
     * The full field encoding has bits 10 as the low bit of the type field.
     * Index = (encoding >> 1) & 0x1FF */
    return (encoding >> 1) & 0x1FF;
}

bool VmcsFieldEncoding_IsValid(uint32_t encoding) {
    /* Bit 15 must be 0 for valid encodings */
    return (encoding & (1u << 15)) == 0;
}

/*===========================================================================
 * VMX Detection — checks CPUID leaf 1 ECX bit 5
 *
 * This is the "full" version that calls __cpuid. It is NOT callable from
 * user-mode tests (will #GP fault). Use CpuCheckVmxFromEcx for unit testing.
 *===========================================================================*/

#if defined(_MSC_VER) && !defined(_KERNEL_MODE)
#include <intrin.h>
#endif

bool VmxDetectSupport(void) {
#if defined(_MSC_VER)
    int cpu_info[4];
    __cpuid(cpu_info, 1);
    return CpuCheckVmxFromEcx((uint32_t)cpu_info[2]); /* ECX = cpu_info[2] */
#else
    return false;
#endif
}

/*===========================================================================
 * VMCS Revision Read — calls __readmsr for IA32_VMX_BASIC
 *
 * Not callable from user-mode tests (privileged instruction).
 * Use VmxExtractRevisionId with mocked values for unit testing.
 *===========================================================================*/

uint32_t VmxReadVmcsRevision(void) {
#if defined(_MSC_VER) && !defined(_KERNEL_MODE)
    /* In user mode, __readmsr is privileged. Return 0 — tested via mock. */
    return 0;
#else
    uint64_t vmx_basic = __readmsr(IA32_VMX_BASIC);
    return VmxExtractRevisionId(vmx_basic);
#endif
}

/*===========================================================================
 * Stubs for Kernel-Dependent Functions
 *
 * These are declared in vmx.h but require kernel APIs (ExAllocatePoolWithTag)
 * and privileged VMX instructions (VMXON, VMLAUNCH). Real implementations
 * live in src/driver/core_mgmt.c where WDK is available.
 *
 * The stubs return error codes and are overridden by the driver at link time
 * or replaced by calling the driver's implementation directly.
 *===========================================================================*/

int VmxAllocateVmxonRegion(void** region_out, uint64_t* pa_out) {
    /* Stub: allocation requires ExAllocatePoolWithTag (kernel mode) */
    (void)region_out;
    (void)pa_out;
    return VMX_ERROR_VMXON;
}

int VmxAllocateVmcsRegion(void** region_out, uint64_t* pa_out) {
    /* Stub: allocation requires ExAllocatePoolWithTag (kernel mode) */
    (void)region_out;
    (void)pa_out;
    return VMX_ERROR_VMXON;
}

void VmxFreeRegion(void* region) {
    /* Stub: freeing requires ExFreePool (kernel mode) */
    (void)region;
}

int VmxEnterRoot(const void* vmxon_region_pa, const void* vmcs_region_pa) {
    /* Stub: VMXON/VMCLEAR/VMPTRLD are privileged instructions */
    (void)vmxon_region_pa;
    (void)vmcs_region_pa;
    return VMX_ERROR_VMXON;
}

int VmxConfigureHostState(void) {
    return VMX_ERROR_VMXON;
}

int VmxConfigureGuestState(void) {
    return VMX_ERROR_VMXON;
}

int VmxConfigureControls(void) {
    return VMX_ERROR_VMXON;
}

int VmxLaunch(void) {
    return VMX_ERROR_VMLAUNCH;
}

int VmxLeaveRoot(void) {
    return VMX_ERROR_VMXON;
}

int VmxReadVmcsField(uint32_t encoding, uint64_t* value) {
    (void)encoding;
    (void)value;
    return VMX_ERROR_VMXON;
}

int VmxWriteVmcsField(uint32_t encoding, uint64_t value) {
    (void)encoding;
    (void)value;
    return VMX_ERROR_VMXON;
}

uint32_t VmxGetCoreCount(void) {
    /* Stub: requires KeQueryActiveProcessorCount (kernel mode) */
    return 0;
}
