/*
 * vmx_preflight.h — Kernel preflight (Unit E1 contract)
 */

#ifndef HYPERSINV_VMX_PREFLIGHT_H
#define HYPERSINV_VMX_PREFLIGHT_H

#include <stdint.h>
#include <stdbool.h>
#include "vmx_runtime_context.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum _VMX_PREFLIGHT_RESULT {
    VMX_PREFLIGHT_SUCCESS = 0,
    VMX_PREFLIGHT_NOT_PASSIVE_LEVEL,
    VMX_PREFLIGHT_SERIALIZATION_FAILED,
    VMX_PREFLIGHT_PIN_FAILED,
    VMX_PREFLIGHT_CET_ACTIVE,
    VMX_PREFLIGHT_NO_VMX_CPUID,
    VMX_PREFLIGHT_FEATURE_CONTROL,
    VMX_PREFLIGHT_BAD_VMX_BASIC,
    VMX_PREFLIGHT_CR0_CHANGE_REQUIRED,
    VMX_PREFLIGHT_CR4_CHANGE_REQUIRED,
    VMX_PREFLIGHT_CONTEXT_FAILED,
    VMX_PREFLIGHT_COOKIE_FAILED,
    VMX_PREFLIGHT_INVALID_PLATFORM
} VMX_PREFLIGHT_RESULT;

typedef bool   (*VmxCheckPassiveLevelFn)(void);
typedef bool   (*VmxAcquireSerializationFn)(void);
typedef void   (*VmxReleaseSerializationFn)(void);
typedef bool   (*VmxPinCpu0Fn)(uint16_t* group, uint8_t* number);
typedef void   (*VmxUnpinCpuFn)(void);
typedef uint64_t (*VmxReadCr4Fn)(void);
typedef uint64_t (*VmxReadCr0Fn)(void);
typedef bool   (*VmxCheckCpuidVmxFn)(void);
typedef bool   (*VmxReadMsrFn)(uint32_t msr, uint64_t* value);
typedef bool   (*VmxGenerateRandomFn)(uint8_t* buffer, size_t size);

typedef struct _VMX_PREFLIGHT_PLATFORM {
    VmxCheckPassiveLevelFn    CheckPassiveLevel;
    VmxAcquireSerializationFn AcquireSerialization;
    VmxReleaseSerializationFn ReleaseSerialization;
    VmxPinCpu0Fn              PinCpu0;
    VmxUnpinCpuFn             UnpinCpu;
    VmxReadCr4Fn              ReadCr4;
    VmxReadCr0Fn              ReadCr0;
    VmxCheckCpuidVmxFn        CheckCpuidVmx;
    VmxReadMsrFn              ReadMsr;
    VmxGenerateRandomFn       GenerateRandom;
} VMX_PREFLIGHT_PLATFORM;

/* Tracks platform resource acquisition for cleanup. */
typedef struct _VMX_PREFLIGHT_OWNERSHIP {
    bool SerializationHeld;
    bool CpuPinned;
} VMX_PREFLIGHT_OWNERSHIP;

VMX_PREFLIGHT_RESULT VmxPreflightCreateContext(
    const VMX_PREFLIGHT_PLATFORM* plat,
    const VMX_ALLOCATION_INTERFACE* alloc,
    VMX_RUNTIME_LAUNCH_CONTEXT** out_context,
    VMX_PREFLIGHT_OWNERSHIP* ownership);

/* VmxPreflightDestroyContext — reverse-order cleanup + idempotent.
 * Clears caller's context pointer after successful cleanup.
 * Safe on partial state.
 * If the allocator is invalid (NULL or missing callbacks), the function
 * returns immediately without modifying the context pointer, ownership
 * flags, or any resources — allowing the caller to retry with a valid
 * allocator. */
void VmxPreflightDestroyContext(
    const VMX_PREFLIGHT_PLATFORM* plat,
    const VMX_ALLOCATION_INTERFACE* alloc,
    VMX_RUNTIME_LAUNCH_CONTEXT** context,
    VMX_PREFLIGHT_OWNERSHIP* ownership);

#ifdef __cplusplus
}
#endif
#endif
