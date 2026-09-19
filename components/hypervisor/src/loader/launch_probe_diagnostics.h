#ifndef HYPERSINV_LOADER_LAUNCH_PROBE_DIAGNOSTICS_H
#define HYPERSINV_LOADER_LAUNCH_PROBE_DIAGNOSTICS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include "vmx_diagnostic.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum _LOADER_LAUNCH_PROBE_VALIDATION_RESULT {
    LOADER_LAUNCH_PROBE_VALID = 0,
    LOADER_LAUNCH_PROBE_NULL = 1,
    LOADER_LAUNCH_PROBE_VERSION = 2,
    LOADER_LAUNCH_PROBE_SIZE = 3,
    LOADER_LAUNCH_PROBE_DOMAIN = 4,
    LOADER_LAUNCH_PROBE_STAGE = 5,
    LOADER_LAUNCH_PROBE_FLAGS = 6,
    LOADER_LAUNCH_PROBE_CLEANUP = 7,
    LOADER_LAUNCH_PROBE_PROTOCOL = 8,
    LOADER_LAUNCH_PROBE_RESOURCE_MASK = 9,
    LOADER_LAUNCH_PROBE_RESERVED = 10,
    LOADER_LAUNCH_PROBE_INSTRUCTION_REPORT = 11
} LOADER_LAUNCH_PROBE_VALIDATION_RESULT;

typedef enum _LOADER_LAUNCH_PROBE_CLASSIFICATION {
    LOADER_LAUNCH_PROBE_SUCCESS = 0,
    LOADER_LAUNCH_PROBE_REJECTED = 1,
    LOADER_LAUNCH_PROBE_VERSION_ERROR = 2,
    LOADER_LAUNCH_PROBE_FAILURE = 3
} LOADER_LAUNCH_PROBE_CLASSIFICATION;

/* The loader must recognize the kernel's expected CET preflight status
 * without depending on the Windows headers used by the driver. */
#define LOADER_STATUS_NOT_SUPPORTED ((int32_t)0xC00000BBu)

LOADER_LAUNCH_PROBE_VALIDATION_RESULT LoaderValidateLaunchProbeResponse(
    const VMX_LAUNCH_PROBE_RESPONSE_V2* response);

bool LoaderLaunchProbeSucceeded(const VMX_LAUNCH_PROBE_RESPONSE_V2* response);

/* Classify an already parsed, fixed-size response. */
LOADER_LAUNCH_PROBE_CLASSIFICATION LoaderClassifyLaunchProbe(
    const VMX_LAUNCH_PROBE_RESPONSE_V2* response);

/* Parse a DeviceIoControl-style byte buffer and classify it.  The output is
 * written only after the exact length and both version-2 header words pass. */
LOADER_LAUNCH_PROBE_CLASSIFICATION LoaderParseLaunchProbeBuffer(
    const void* buffer,
    size_t returned_length,
    VMX_LAUNCH_PROBE_RESPONSE_V2* response);

int LoaderLaunchProbeClassificationExitCode(
    LOADER_LAUNCH_PROBE_CLASSIFICATION classification);

const char* LoaderLaunchProbeClassificationName(
    LOADER_LAUNCH_PROBE_CLASSIFICATION classification);

const char* LoaderLaunchProbeValidationResultName(
    LOADER_LAUNCH_PROBE_VALIDATION_RESULT result);

bool LoaderPrintLaunchProbeDiagnostics(
    FILE* stream,
    const VMX_LAUNCH_PROBE_RESPONSE_V2* response);

#ifdef __cplusplus
}
#endif

#endif /* HYPERSINV_LOADER_LAUNCH_PROBE_DIAGNOSTICS_H */