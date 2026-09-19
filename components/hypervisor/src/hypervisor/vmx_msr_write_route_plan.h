#ifndef HYPERSINV_VMX_MSR_WRITE_ROUTE_PLAN_H
#define HYPERSINV_VMX_MSR_WRITE_ROUTE_PLAN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VMX_MSR_WRITE_INSTRUCTION_LENGTH UINT32_C(2)

typedef enum VMX_MSR_WRITE_ROUTE_STATUS {
    VMX_MSR_WRITE_ROUTE_INVALID_INPUT = 0,
    VMX_MSR_WRITE_ROUTE_INVALID_RIP = 1,
    VMX_MSR_WRITE_ROUTE_INVALID_LENGTH = 2,
    VMX_MSR_WRITE_ROUTE_UNSUPPORTED = 3,
    VMX_MSR_WRITE_ROUTE_OK = 4
} VMX_MSR_WRITE_ROUTE_STATUS;

typedef struct VMX_MSR_WRITE_ROUTE_PLAN {
    uint32_t Msr;
    uint32_t Reserved;
    uint64_t Value;
    uint64_t AdvancedRip;
} VMX_MSR_WRITE_ROUTE_PLAN;

VMX_MSR_WRITE_ROUTE_STATUS VmxMsrWriteRoutePlanBuild(
    uint32_t Msr,
    uint64_t Value,
    uint64_t GuestRip,
    uint32_t InstructionLength,
    VMX_MSR_WRITE_ROUTE_PLAN* OutPlan);

#ifdef __cplusplus
}
#endif

#endif
