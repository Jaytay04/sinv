/*
 * ioctl_launch_probe.h — Preflight-to-domain/NTSTATUS mapping (G1)
 *
 * Pure mapping layer between VMX_PREFLIGHT_RESULT (from vmx_preflight.h)
 * and the public diagnostic ABI enums (VMX_DOMAIN_STATUS). No kernel
 * dependencies — usable from both the kernel driver and user-mode tests.
 */

#ifndef HYPERSINV_IOCTL_LAUNCH_PROBE_H
#define HYPERSINV_IOCTL_LAUNCH_PROBE_H

#include "vmx_preflight.h"
#include "vmx_diagnostic.h"
#include "vmx_transient.h"
#include <stdint.h>

bool IoctlCopyRuntimeDiagnosticsChecked(
    const VMX_RUNTIME_LAUNCH_CONTEXT* context,
    const VMX_INSTRUCTION_REPORTS* reports,
    VMX_LAUNCH_PROBE_RESPONSE_V2* response);
typedef struct _VMX_LAUNCH_PROBE_MAP_INPUT {
    VMX_PREFLIGHT_RESULT PreflightResult;
    VMX_LIFECYCLE_STAGE TransientStage;
    const VMX_RUNTIME_LAUNCH_CONTEXT* Context;
    const VMX_INSTRUCTION_REPORTS* Reports;
    uint32_t ResourceMask;
    VMX_CLEANUP_REQUIREMENT CleanupRequirement;
    VMX_CLEANUP_OWNER CleanupOwner;
    bool ResourcesReleased;
} VMX_LAUNCH_PROBE_MAP_INPUT;

typedef VMX_PREFLIGHT_RESULT (*IoctlLaunchProbePreflightCreateFn)(
    const VMX_PREFLIGHT_PLATFORM* platform,
    const VMX_ALLOCATION_INTERFACE* allocation,
    VMX_RUNTIME_LAUNCH_CONTEXT** context,
    VMX_PREFLIGHT_OWNERSHIP* ownership);

typedef VMX_LIFECYCLE_STAGE (*IoctlLaunchProbeTransientExecuteFn)(
    const VMX_TRANSIENT_PLATFORM* platform,
    VMX_RUNTIME_LAUNCH_CONTEXT* context,
    VMX_INSTRUCTION_REPORTS* reports);

typedef void (*IoctlLaunchProbeDestroyContextFn)(
    const VMX_PREFLIGHT_PLATFORM* platform,
    const VMX_ALLOCATION_INTERFACE* allocation,
    VMX_RUNTIME_LAUNCH_CONTEXT** context,
    VMX_PREFLIGHT_OWNERSHIP* ownership);

typedef bool (*IoctlLaunchProbeMapResponseFn)(
    const VMX_LAUNCH_PROBE_MAP_INPUT* input,
    VMX_LAUNCH_PROBE_RESPONSE_V2* response);

typedef struct _VMX_LAUNCH_PROBE_LIFECYCLE_OPERATIONS {
    IoctlLaunchProbePreflightCreateFn CreateContext;
    IoctlLaunchProbeTransientExecuteFn ExecuteTransient;
    IoctlLaunchProbeDestroyContextFn DestroyContext;
    IoctlLaunchProbeMapResponseFn MapResponse;
    const VMX_PREFLIGHT_PLATFORM* PreflightPlatform;
    const VMX_ALLOCATION_INTERFACE* AllocationInterface;
    const VMX_TRANSIENT_PLATFORM* TransientPlatform;
    VMX_EXIT_DISPATCH_ROUTINE ExitDispatcher;
} VMX_LAUNCH_PROBE_LIFECYCLE_OPERATIONS;

bool IoctlRunLaunchProbeLifecycle(
    const VMX_LAUNCH_PROBE_LIFECYCLE_OPERATIONS* operations,
    VMX_LAUNCH_PROBE_RESPONSE_V2* response);

/* Operations and response must be complete, non-wrapping and separate spans.
 * A present operations table is copied, validated, and used as one frozen
 * local snapshot. Null or invalid separate operations retain the established
 * zeroed response failure contract. */

bool IoctlLaunchProbeResponseBufferIsSufficient(size_t output_length);

bool IoctlValidateLaunchProbeMapInput(
    const VMX_LAUNCH_PROBE_MAP_INPUT* input);

bool IoctlMapLaunchProbeResponse(
    const VMX_LAUNCH_PROBE_MAP_INPUT* input,
    VMX_LAUNCH_PROBE_RESPONSE_V2* response);

VMX_DOMAIN_STATUS IoctlMapPreflightToDomain(VMX_PREFLIGHT_RESULT rc);
int32_t           IoctlMapPreflightToNtStatus(VMX_PREFLIGHT_RESULT rc);
VMX_LIFECYCLE_STAGE IoctlMapPreflightToStage(VMX_PREFLIGHT_RESULT rc);

void IoctlInitializePreflightFailureResponse(
    VMX_LAUNCH_PROBE_RESPONSE_V2* response,
    VMX_PREFLIGHT_RESULT preflight_rc);

VMX_DOMAIN_STATUS IoctlMapTransientResult(
    VMX_PREFLIGHT_RESULT preflight_rc,
    VMX_LIFECYCLE_STAGE transient_stage,
    const VMX_INSTRUCTION_REPORTS* reports);

int32_t IoctlMapTransientToNtStatus(
    VMX_PREFLIGHT_RESULT preflight_rc,
    VMX_LIFECYCLE_STAGE transient_stage,
    const VMX_INSTRUCTION_REPORTS* reports);

void IoctlCopyInstructionReports(
    const VMX_INSTRUCTION_REPORTS* src,
    VMX_LAUNCH_PROBE_RESPONSE_V2* dst);

#endif
