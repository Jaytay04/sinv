#ifndef HYPERSINV_VMX_PERSISTENT_DISPATCH_H
#define HYPERSINV_VMX_PERSISTENT_DISPATCH_H

/* WDK-free V3D2b VM-exit dispatcher and immutable I/O seam. */
#include <stdint.h>
#include "vmx_cpuid_route_plan.h"
#include "vmx_msr_read_route_plan.h"
#include "vmx_msr_write_route_plan.h"
#include "vmx_cr3_route_executor.h"
#include "vmx_persistent_launch_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VMX_V3D_DISPATCH_VMCS_EXIT_REASON       UINT64_C(0x4402)
#define VMX_V3D_DISPATCH_VMCS_EXIT_QUALIFICATION UINT64_C(0x6400)
#define VMX_V3D_DISPATCH_VMCS_GUEST_RIP        UINT64_C(0x681e)
#define VMX_V3D_DISPATCH_VMCS_GUEST_CR3        UINT64_C(0x6802)
#define VMX_V3D_DISPATCH_VMCS_GUEST_RSP        UINT64_C(0x681c)
#define VMX_V3D_DISPATCH_VMCS_EXIT_LENGTH      UINT64_C(0x440c)
#define VMX_V3D_DISPATCH_VMCS_GUEST_PHYSICAL_ADDRESS UINT64_C(0x2400)
#define VMX_V3D_DISPATCH_VMCS_EPT_POINTER      UINT64_C(0x201a)
#define VMX_V3D_DISPATCH_EXIT_REASON_BASIC_MASK UINT64_C(0xffff)
#define VMX_V3D_DISPATCH_EXIT_REASON_CPUID         UINT64_C(10)
#define VMX_V3D_DISPATCH_EXIT_REASON_HLT           UINT64_C(12)
#define VMX_V3D_DISPATCH_EXIT_REASON_CR_ACCESS     UINT64_C(28)
#define VMX_V3D_DISPATCH_EXIT_REASON_MSR_READ      UINT64_C(31)
#define VMX_V3D_DISPATCH_EXIT_REASON_MSR_WRITE     UINT64_C(32)
#define VMX_V3D_DISPATCH_EXIT_REASON_EPT_VIOLATION UINT64_C(48)
#define VMX_V3D_DISPATCH_EXIT_REASON_PREEMPTION_TIMER UINT64_C(52)
#define VMX_V3D_DISPATCH_CR3_NUMBER                UINT64_C(3)
#define VMX_V3D_DISPATCH_CR_ACCESS_MASK            UINT64_C(0x30)
#define VMX_V3D_DISPATCH_CR_ACCESS_SHIFT           UINT32_C(4)
#define VMX_V3D_DISPATCH_CR_MOV_FROM                UINT64_C(1)
#define VMX_V3D_DISPATCH_CR_GPR_MASK                UINT64_C(0x0f00)
#define VMX_V3D_DISPATCH_CR_GPR_SHIFT               UINT32_C(8)
#define VMX_V3D_DISPATCH_CR_RSP_INDEX               UINT32_C(4)

/* Dispatcher failures are disjoint from V3D2a protocol validation codes. */
#define VMX_V3D_DISPATCH_FAIL_INVALID_INPUT UINT32_C(0x100)
#define VMX_V3D_DISPATCH_FAIL_VMREAD        UINT32_C(0x101)
#define VMX_V3D_DISPATCH_FAIL_VMWRITE       UINT32_C(0x102)
#define VMX_V3D_DISPATCH_FAIL_EPT_ROUTE      UINT32_C(0x103)
#define VMX_V3D_DISPATCH_FAIL_DIAGNOSTIC     UINT32_C(0x104)
#define VMX_V3D_DISPATCH_FAIL_CR3_ROUTE      UINT32_C(0x105)
#define VMX_V3D_DISPATCH_FAIL_CPUID_ROUTE    UINT32_C(0x106)
#define VMX_V3D_DISPATCH_FAIL_MSR_READ_ROUTE UINT32_C(0x107)
#define VMX_V3D_DISPATCH_FAIL_MSR_WRITE_ROUTE UINT32_C(0x108)
#define VMX_V3D_DISPATCH_FAIL_HLT_ROUTE       UINT32_C(0x109)
#define VMX_V3D_DISPATCH_FAIL_PREEMPTION_TIMER UINT32_C(0x10A)

typedef enum VMX_V3D_EPT_ROUTE_RESULT {
    VMX_V3D_EPT_ROUTE_INVALID = 0,
    VMX_V3D_EPT_ROUTE_RESUME = 1,
    VMX_V3D_EPT_ROUTE_FAIL_CLOSED = 2
} VMX_V3D_EPT_ROUTE_RESULT;

typedef struct VMX_V3D_EPT_VIOLATION_EVENT {
    uint64_t Qualification;
    uint64_t GuestPhysicalAddress;
    uint64_t GuestRip;
    uint64_t EptPointer;
    uint32_t AccessMask;
    uint32_t Reserved;
} VMX_V3D_EPT_VIOLATION_EVENT;

typedef VMX_V3D_EPT_ROUTE_RESULT (*VMX_V3D_EPT_VIOLATION_FN)(
    void* Context, const VMX_V3D_EPT_VIOLATION_EVENT* Event);

typedef struct VMX_V3D_PERSISTENT_IO_RESULT {
    uint32_t Outcome;
    uint32_t ErrorValid;
    uint32_t Error;
    uint64_t Value;
} VMX_V3D_PERSISTENT_IO_RESULT;

typedef VMX_V3D_PERSISTENT_IO_RESULT (*VMX_V3D_PERSISTENT_VMREAD_FN)(
    void* Context, uint64_t Field);
typedef VMX_V3D_PERSISTENT_IO_RESULT (*VMX_V3D_PERSISTENT_VMWRITE_FN)(
    void* Context, uint64_t Field, uint64_t Value);
typedef int (*VMX_V3D_PERSISTENT_CPUID_FN)(
    void* Context,
    uint32_t Leaf,
    uint32_t Subleaf,
    VMX_CPUID_REGISTERS* OutRegisters);

/* DispatcherContext must equal Context->DispatcherContext exactly. The pointed
 * seam and callbacks must remain stable and nonpageable for the resident
 * lifetime, until VMXOFF/context destruction. */
typedef struct VMX_V3D_PERSISTENT_IO {
    void* Context;
    VMX_V3D_PERSISTENT_VMREAD_FN Vmread;
    VMX_V3D_PERSISTENT_VMWRITE_FN Vmwrite;
    VMX_V3D_PERSISTENT_CPUID_FN Cpuid;
    void* EptContext;
    VMX_V3D_EPT_VIOLATION_FN EptViolation;
    VMX_CR3_ROUTE_OWNER* Cr3Owner;
    size_t ProcessorIndex;
} VMX_V3D_PERSISTENT_IO;

/* ProtocolCode stores the V3D2a passed-check mask after protocol evaluation. */
uint32_t VmxV3dPersistentDispatch(
    VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* Context,
    VMX_V3D_GUEST_GPRS* GuestGprs,
    void* DispatcherContext);

#ifdef __cplusplus
}
#endif

#endif /* HYPERSINV_VMX_PERSISTENT_DISPATCH_H */
