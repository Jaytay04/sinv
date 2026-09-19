#ifndef HYPERSINV_VMX_CPUID_ROUTE_PLAN_H
#define HYPERSINV_VMX_CPUID_ROUTE_PLAN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VMX_CPUID_INSTRUCTION_LENGTH UINT32_C(2)

typedef enum VMX_CPUID_ROUTE_STATUS {
    VMX_CPUID_ROUTE_INVALID_INPUT = 0,
    VMX_CPUID_ROUTE_INVALID_RIP = 1,
    VMX_CPUID_ROUTE_INVALID_LENGTH = 2,
    VMX_CPUID_ROUTE_OK = 3
} VMX_CPUID_ROUTE_STATUS;

typedef struct VMX_CPUID_REGISTERS {
    uint32_t Eax;
    uint32_t Ebx;
    uint32_t Ecx;
    uint32_t Edx;
} VMX_CPUID_REGISTERS;

typedef struct VMX_CPUID_ROUTE_PLAN {
    VMX_CPUID_REGISTERS Guest;
    uint64_t AdvancedRip;
} VMX_CPUID_ROUTE_PLAN;

VMX_CPUID_ROUTE_STATUS VmxCpuidRoutePlanBuild(
    uint32_t Leaf,
    uint32_t Subleaf,
    const VMX_CPUID_REGISTERS* Host,
    uint64_t GuestRip,
    uint32_t InstructionLength,
    VMX_CPUID_ROUTE_PLAN* OutPlan);

#ifdef __cplusplus
}
#endif

#endif
