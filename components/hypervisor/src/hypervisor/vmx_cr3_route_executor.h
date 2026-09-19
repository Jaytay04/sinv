#ifndef HYPERSINV_VMX_CR3_ROUTE_EXECUTOR_H
#define HYPERSINV_VMX_CR3_ROUTE_EXECUTOR_H

#include "vmcs_plan.h"
#include "vmx_cr3_route_owner.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VMX_CR3_ROUTE_EXECUTION_VERSION UINT32_C(1)

typedef enum VMX_CR3_ROUTE_EXECUTE_STATUS {
    VMX_CR3_ROUTE_EXECUTE_OK = 0,
    VMX_CR3_ROUTE_EXECUTE_INVALID = -1,
    VMX_CR3_ROUTE_EXECUTE_PLAN_FAILED = -2,
    VMX_CR3_ROUTE_EXECUTE_WRITE_CR3_FAILED = -3,
    VMX_CR3_ROUTE_EXECUTE_WRITE_RIP_FAILED = -4,
    VMX_CR3_ROUTE_EXECUTE_COMMIT_FAILED = -5
} VMX_CR3_ROUTE_EXECUTE_STATUS;

typedef enum VMX_CR3_ROUTE_EXECUTION_OUTCOME {
    VMX_CR3_ROUTE_EXECUTION_INVALID = 0,
    VMX_CR3_ROUTE_EXECUTION_CR3_WRITE_FAILED = 1,
    VMX_CR3_ROUTE_EXECUTION_RIP_WRITE_FAILED = 2,
    VMX_CR3_ROUTE_EXECUTION_COMMIT_FAILED = 3,
    VMX_CR3_ROUTE_EXECUTION_COMMITTED = 4
} VMX_CR3_ROUTE_EXECUTION_OUTCOME;

typedef int (*VMX_CR3_ROUTE_WRITE_FN)(
    void* Context, uint64_t Field, uint64_t Value);

typedef struct VMX_CR3_ROUTE_EXECUTION {
    uint32_t Version;
    uint32_t Outcome;
    uint32_t WritesAttempted;
    uint32_t StateCommitted;
    uint64_t FailedField;
    VMX_CR3_ROUTE_PLAN Plan;
} VMX_CR3_ROUTE_EXECUTION;

/* Builds, writes guest CR3, advances guest RIP, then commits per-CPU residency.
 * A false write result stops immediately and never commits. The callback may
 * execute VMX only in a later resident adapter; this module is WDK-free.
 * OutExecution remains zero on invalid/planning failure and publishes exact
 * partial-write diagnostics after the first write attempt. */
VMX_CR3_ROUTE_EXECUTE_STATUS VmxCr3RouteExecute(
    VMX_CR3_ROUTE_OWNER* Owner,
    size_t ProcessorIndex,
    uint64_t Qualification,
    const uint64_t* GuestGprs,
    size_t GuestGprCount,
    uint64_t GuestRip,
    uint32_t InstructionLength,
    void* WriteContext,
    VMX_CR3_ROUTE_WRITE_FN Write,
    VMX_CR3_ROUTE_EXECUTION* OutExecution);

#ifdef __cplusplus
}
#endif

#endif
