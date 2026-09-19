/*
 * vmx_diagnostic.h — Versioned diagnostic ABI (R14)
 *
 * Packed, fixed-size (280 bytes) version-2 response for the launch-probe
 * IOCTL. Shared between kernel driver and user-mode loader.
 */

#ifndef HYPERSINV_VMX_DIAGNOSTIC_H
#define HYPERSINV_VMX_DIAGNOSTIC_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VMX_LAUNCH_PROBE_RESPONSE_V2_SIZE  280u
#define VMX_LAUNCH_PROBE_VERSION_V2        2u

/* --- Enums --- */
typedef enum _VMX_DOMAIN_STATUS {
    VMX_DOMAIN_UNKNOWN  = 0,
    VMX_DOMAIN_SUCCESS  = 1,
    VMX_DOMAIN_REJECTED = 2,
    VMX_DOMAIN_FAILED   = 3
} VMX_DOMAIN_STATUS;

typedef enum _VMX_CLEANUP_REQUIREMENT {
    VMX_CLEANUP_NONE     = 0,
    VMX_CLEANUP_REQUIRED = 1
} VMX_CLEANUP_REQUIREMENT;

typedef enum _VMX_CLEANUP_OWNER {
    VMX_CLEANUP_OWNER_NONE = 0,
    VMX_CLEANUP_OWNER_C    = 1,
    VMX_CLEANUP_OWNER_MASM = 2
} VMX_CLEANUP_OWNER;

typedef enum _VMX_PROTOCOL_STEP {
    VMX_PROTOCOL_NONE             = 0,
    VMX_PROTOCOL_PROBE_COMPLETE   = 1,
    VMX_PROTOCOL_SHUTDOWN_COMPLETE = 2
} VMX_PROTOCOL_STEP;

/* Public Runtime V2 stage contract; values mirror the private lifecycle order. */
typedef enum _VMX_V2_RUNTIME_STAGE {
    VMX_V2_STAGE_NOT_STARTED = 0,
    VMX_V2_STAGE_PASSIVE_LEVEL = 1,
    VMX_V2_STAGE_SERIALIZATION = 2,
    VMX_V2_STAGE_CPU0_PINNED = 3,
    VMX_V2_STAGE_CET_PREFLIGHT = 4,
    VMX_V2_STAGE_CPUID_VALIDATED = 5,
    VMX_V2_STAGE_FEATURE_CONTROL = 6,
    VMX_V2_STAGE_VMX_BASIC = 7,
    VMX_V2_STAGE_FIXED_CR = 8,
    VMX_V2_STAGE_CONTEXT_ALLOCATED = 9,
    VMX_V2_STAGE_HOST_STACK = 10,
    VMX_V2_STAGE_VMXON_PAGE = 11,
    VMX_V2_STAGE_VMCS_PAGE = 12,
    VMX_V2_STAGE_RESOURCES_VALIDATED = 13,
    VMX_V2_STAGE_COOKIE_GENERATED = 14,
    VMX_V2_STAGE_INSTRUCTION_INTERVAL = 15,
    VMX_V2_STAGE_VMXON = 16,
    VMX_V2_STAGE_VMCLEAR = 17,
    VMX_V2_STAGE_VMPTRLD = 18,
    VMX_V2_STAGE_CONTROLS_PLANNED = 19,
    VMX_V2_STAGE_PROCESSOR_STATE = 20,
    VMX_V2_STAGE_GUEST_PLAN = 21,
    VMX_V2_STAGE_HOST_PLAN = 22,
    VMX_V2_STAGE_VMLAUNCH = 23,
    VMX_V2_STAGE_PROBE_EXIT = 24,
    VMX_V2_STAGE_VMRESUME = 25,
    VMX_V2_STAGE_SHUTDOWN_EXIT = 26,
    VMX_V2_STAGE_VMXOFF = 27,
    VMX_V2_STAGE_CRS_RESTORED = 28,
    VMX_V2_STAGE_RESOURCES_RELEASED = 29,
    VMX_V2_STAGE_COMPLETE = 30
} VMX_V2_RUNTIME_STAGE;

/* Public Runtime V2 resource-mask contract. */
#define VMX_V2_RESOURCE_CONTEXT     (1u << 0)
#define VMX_V2_RESOURCE_STACK       (1u << 1)
#define VMX_V2_RESOURCE_VMXON_PAGE  (1u << 2)
#define VMX_V2_RESOURCE_VMCS_PAGE   (1u << 3)
#define VMX_V2_RESOURCE_SERIALIZED  (1u << 4)
#define VMX_V2_RESOURCE_AFFINITY    (1u << 5)
#define VMX_V2_RESOURCE_ALL         ((uint32_t)0x3Fu)

/* --- Flags --- */
#define VMX_FLAG_CET_REJECTED       (1u << 0)
#define VMX_FLAG_PROBE_OBSERVED     (1u << 1)
#define VMX_FLAG_SHUTDOWN_OBSERVED  (1u << 2)
#define VMX_FLAG_CR0_RESTORED       (1u << 3)
#define VMX_FLAG_CR4_RESTORED       (1u << 4)
#define VMX_FLAG_RESOURCES_RELEASED (1u << 5)
#define VMX_FLAG_PROTOCOL_VALID     (1u << 6)

/* --- Sub-structs --- */
#pragma pack(push, 1)

typedef struct _VMX_INSTRUCTION_REPORT {
    uint8_t  Outcome;
    uint8_t  ErrorValid;
    uint16_t Reserved;
    uint32_t Error;
} VMX_INSTRUCTION_REPORT;

typedef struct _VMX_EXIT_REPORT {
    uint32_t Reason;
    uint32_t InstructionLength;
    uint64_t Qualification;
    uint64_t Rip;
    uint64_t Rax;
    uint64_t Rcx;
    uint64_t Rdx;
} VMX_EXIT_REPORT;

/* --- Main response struct (280 bytes exactly) --- */
typedef struct _VMX_LAUNCH_PROBE_RESPONSE_V2 {
    /* Header (bytes 0-111) */
    uint32_t Version;               /* 0 */
    uint32_t Size;                  /* 4 */
    uint32_t DomainStatus;          /* 8 */
    int32_t  NtStatus;              /* 12 */
    uint32_t RuntimeStage;          /* 16 */
    uint32_t Flags;                 /* 20 */
    uint16_t ProcessorGroup;        /* 24 */
    uint8_t  ProcessorNumber;       /* 26 */
    uint8_t  ProcessorReserved;     /* 27 */
    uint8_t  CleanupRequirement;    /* 28 */
    uint8_t  CleanupOwner;          /* 29 */
    uint8_t  ProtocolStep;          /* 30 */
    uint8_t  Reserved0;             /* 31 */
    uint32_t ResourceMask;          /* 32 */
    uint32_t VmxRevisionId;         /* 36 */
    uint32_t VmxRegionSize;         /* 40 */
    uint32_t Reserved1;             /* 44 */
    uint64_t VmxBasic;              /* 48 */
    uint64_t VmxonPa;               /* 56 */
    uint64_t VmcsPa;                /* 64 */
    uint64_t OriginalCr0;           /* 72 */
    uint64_t FinalCr0;              /* 80 */
    uint64_t OriginalCr4;           /* 88 */
    uint64_t FinalCr4;              /* 96 */
    uint64_t ExpectedCookie;        /* 104 */

    /* Instruction reports (bytes 112-183, 9 × 8 bytes) */
    VMX_INSTRUCTION_REPORT Vmxon;       /* 112 */
    VMX_INSTRUCTION_REPORT Vmclear;     /* 120 */
    VMX_INSTRUCTION_REPORT Vmptrld;     /* 128 */
    VMX_INSTRUCTION_REPORT VmwritePlan; /* 136 */
    VMX_INSTRUCTION_REPORT GuestRsp;    /* 144 */
    VMX_INSTRUCTION_REPORT GuestRflags; /* 152 */
    VMX_INSTRUCTION_REPORT Vmlaunch;    /* 160 */
    VMX_INSTRUCTION_REPORT Vmresume;    /* 168 */
    VMX_INSTRUCTION_REPORT Vmxoff;      /* 176 */

    /* Exit reports (bytes 184-279, 2 × 48 bytes) */
    VMX_EXIT_REPORT ProbeExit;          /* 184 */
    VMX_EXIT_REPORT ShutdownExit;       /* 232 */
} VMX_LAUNCH_PROBE_RESPONSE_V2;

#pragma pack(pop)

/* --- Compile-time assertions --- */
#ifdef __cplusplus
#define VMX_DIAG_ASSERT(name, cond) static_assert((cond), #name)
#else
#define VMX_DIAG_ASSERT(name, cond) _Static_assert((cond), #name)
#endif

VMX_DIAG_ASSERT(size_280,  sizeof(VMX_LAUNCH_PROBE_RESPONSE_V2) == 280);
VMX_DIAG_ASSERT(off_version,        offsetof(VMX_LAUNCH_PROBE_RESPONSE_V2, Version) == 0);
VMX_DIAG_ASSERT(off_size,           offsetof(VMX_LAUNCH_PROBE_RESPONSE_V2, Size) == 4);
VMX_DIAG_ASSERT(off_domain,         offsetof(VMX_LAUNCH_PROBE_RESPONSE_V2, DomainStatus) == 8);
VMX_DIAG_ASSERT(off_ntstatus,       offsetof(VMX_LAUNCH_PROBE_RESPONSE_V2, NtStatus) == 12);
VMX_DIAG_ASSERT(off_stage,          offsetof(VMX_LAUNCH_PROBE_RESPONSE_V2, RuntimeStage) == 16);
VMX_DIAG_ASSERT(off_flags,          offsetof(VMX_LAUNCH_PROBE_RESPONSE_V2, Flags) == 20);
VMX_DIAG_ASSERT(off_procgroup,      offsetof(VMX_LAUNCH_PROBE_RESPONSE_V2, ProcessorGroup) == 24);
VMX_DIAG_ASSERT(off_procnum,        offsetof(VMX_LAUNCH_PROBE_RESPONSE_V2, ProcessorNumber) == 26);
VMX_DIAG_ASSERT(off_cleanup_req,    offsetof(VMX_LAUNCH_PROBE_RESPONSE_V2, CleanupRequirement) == 28);
VMX_DIAG_ASSERT(off_cleanup_owner,  offsetof(VMX_LAUNCH_PROBE_RESPONSE_V2, CleanupOwner) == 29);
VMX_DIAG_ASSERT(off_protocol_step,  offsetof(VMX_LAUNCH_PROBE_RESPONSE_V2, ProtocolStep) == 30);
VMX_DIAG_ASSERT(off_resource_mask,  offsetof(VMX_LAUNCH_PROBE_RESPONSE_V2, ResourceMask) == 32);
VMX_DIAG_ASSERT(off_vmx_revid,      offsetof(VMX_LAUNCH_PROBE_RESPONSE_V2, VmxRevisionId) == 36);
VMX_DIAG_ASSERT(off_vmx_region_sz,  offsetof(VMX_LAUNCH_PROBE_RESPONSE_V2, VmxRegionSize) == 40);
VMX_DIAG_ASSERT(off_vmx_basic,      offsetof(VMX_LAUNCH_PROBE_RESPONSE_V2, VmxBasic) == 48);
VMX_DIAG_ASSERT(off_vmxon_pa,       offsetof(VMX_LAUNCH_PROBE_RESPONSE_V2, VmxonPa) == 56);
VMX_DIAG_ASSERT(off_vmcs_pa,        offsetof(VMX_LAUNCH_PROBE_RESPONSE_V2, VmcsPa) == 64);
VMX_DIAG_ASSERT(off_orig_cr0,       offsetof(VMX_LAUNCH_PROBE_RESPONSE_V2, OriginalCr0) == 72);
VMX_DIAG_ASSERT(off_final_cr0,      offsetof(VMX_LAUNCH_PROBE_RESPONSE_V2, FinalCr0) == 80);
VMX_DIAG_ASSERT(off_orig_cr4,       offsetof(VMX_LAUNCH_PROBE_RESPONSE_V2, OriginalCr4) == 88);
VMX_DIAG_ASSERT(off_final_cr4,      offsetof(VMX_LAUNCH_PROBE_RESPONSE_V2, FinalCr4) == 96);
VMX_DIAG_ASSERT(off_cookie,         offsetof(VMX_LAUNCH_PROBE_RESPONSE_V2, ExpectedCookie) == 104);
VMX_DIAG_ASSERT(off_vmxon,          offsetof(VMX_LAUNCH_PROBE_RESPONSE_V2, Vmxon) == 112);
VMX_DIAG_ASSERT(off_probe_exit,     offsetof(VMX_LAUNCH_PROBE_RESPONSE_V2, ProbeExit) == 184);
VMX_DIAG_ASSERT(off_shutdown_exit,  offsetof(VMX_LAUNCH_PROBE_RESPONSE_V2, ShutdownExit) == 232);

#undef VMX_DIAG_ASSERT

#ifdef __cplusplus
}
#endif

#endif /* HYPERSINV_VMX_DIAGNOSTIC_H */
