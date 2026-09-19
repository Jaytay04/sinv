#ifndef HYPERSINV_EFI_RUNTIME_TEARDOWN_H
#define HYPERSINV_EFI_RUNTIME_TEARDOWN_H

/*
 * Pure model of the native VMX teardown rendezvous.
 *
 * The model owns no storage, executes no VMX instruction, and performs no
 * resource release.  A native adapter supplies the results of VMCLEAR,
 * VMXOFF, and restoration.  The adapter may acknowledge a processor only
 * after this model has accepted a complete, read-back-equal restoration.
 * Any native failure or unsafe ordering enters quarantine and cannot be
 * retried in-band.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HV_EFI_TEARDOWN_GUEST_FIELD_CR0 (UINT32_C(1) << 0)
#define HV_EFI_TEARDOWN_GUEST_FIELD_CR3 (UINT32_C(1) << 1)
#define HV_EFI_TEARDOWN_GUEST_FIELD_CR4 (UINT32_C(1) << 2)
#define HV_EFI_TEARDOWN_GUEST_FIELD_RIP (UINT32_C(1) << 3)
#define HV_EFI_TEARDOWN_GUEST_FIELD_RSP (UINT32_C(1) << 4)
#define HV_EFI_TEARDOWN_GUEST_FIELD_RFLAGS (UINT32_C(1) << 5)
#define HV_EFI_TEARDOWN_GUEST_FIELD_ES (UINT32_C(1) << 6)
#define HV_EFI_TEARDOWN_GUEST_FIELD_CS (UINT32_C(1) << 7)
#define HV_EFI_TEARDOWN_GUEST_FIELD_SS (UINT32_C(1) << 8)
#define HV_EFI_TEARDOWN_GUEST_FIELD_DS (UINT32_C(1) << 9)
#define HV_EFI_TEARDOWN_GUEST_FIELD_FS (UINT32_C(1) << 10)
#define HV_EFI_TEARDOWN_GUEST_FIELD_GS (UINT32_C(1) << 11)
#define HV_EFI_TEARDOWN_GUEST_FIELD_LDTR (UINT32_C(1) << 12)
#define HV_EFI_TEARDOWN_GUEST_FIELD_TR (UINT32_C(1) << 13)
#define HV_EFI_TEARDOWN_GUEST_FIELD_ES_BASE (UINT32_C(1) << 14)
#define HV_EFI_TEARDOWN_GUEST_FIELD_CS_BASE (UINT32_C(1) << 15)
#define HV_EFI_TEARDOWN_GUEST_FIELD_SS_BASE (UINT32_C(1) << 16)
#define HV_EFI_TEARDOWN_GUEST_FIELD_DS_BASE (UINT32_C(1) << 17)
#define HV_EFI_TEARDOWN_GUEST_FIELD_FS_BASE (UINT32_C(1) << 18)
#define HV_EFI_TEARDOWN_GUEST_FIELD_GS_BASE (UINT32_C(1) << 19)
#define HV_EFI_TEARDOWN_GUEST_FIELD_LDTR_BASE (UINT32_C(1) << 20)
#define HV_EFI_TEARDOWN_GUEST_FIELD_TR_BASE (UINT32_C(1) << 21)
#define HV_EFI_TEARDOWN_GUEST_FIELD_GDTR (UINT32_C(1) << 22)
#define HV_EFI_TEARDOWN_GUEST_FIELD_IDTR (UINT32_C(1) << 23)
#define HV_EFI_TEARDOWN_GUEST_FIELD_EFER (UINT32_C(1) << 24)
#define HV_EFI_TEARDOWN_GUEST_FIELD_PAT (UINT32_C(1) << 25)
#define HV_EFI_TEARDOWN_GUEST_FIELD_DEBUG (UINT32_C(1) << 26)
#define HV_EFI_TEARDOWN_GUEST_FIELD_SYSENTER (UINT32_C(1) << 27)
#define HV_EFI_TEARDOWN_GUEST_FIELD_INTERRUPTIBILITY (UINT32_C(1) << 28)
#define HV_EFI_TEARDOWN_GUEST_FIELD_ACTIVITY (UINT32_C(1) << 29)
#define HV_EFI_TEARDOWN_GUEST_FIELD_PENDING_DEBUG (UINT32_C(1) << 30)
#define HV_EFI_TEARDOWN_GUEST_FIELD_VMCS_LINK (UINT32_C(1) << 31)
#define HV_EFI_TEARDOWN_GUEST_FIELD_SEGMENT_LIMITS (UINT64_C(1) << 32)
#define HV_EFI_TEARDOWN_GUEST_FIELD_SEGMENT_ACCESS_RIGHTS (UINT64_C(1) << 33)
#define HV_EFI_TEARDOWN_GUEST_FIELD_PDPTE (UINT64_C(1) << 34)
#define HV_EFI_TEARDOWN_GUEST_FIELD_PERF_GLOBAL_CTRL (UINT64_C(1) << 35)
#define HV_EFI_TEARDOWN_GUEST_FIELD_BND_CONFIG (UINT64_C(1) << 36)
#define HV_EFI_TEARDOWN_GUEST_FIELD_XSS (UINT64_C(1) << 37)
#define HV_EFI_TEARDOWN_GUEST_FIELD_CET (UINT64_C(1) << 38)
#define HV_EFI_TEARDOWN_GUEST_FIELD_PKRS (UINT64_C(1) << 39)
#define HV_EFI_TEARDOWN_GUEST_FIELD_RTIT_CTL (UINT64_C(1) << 40)

#define HV_EFI_TEARDOWN_REQUIRED_GUEST_STATE_MASK \
    ((UINT64_C(1) << 41) - UINT64_C(1))
#define HV_EFI_TEARDOWN_NATIVE_SUCCESS UINT32_C(0)
#define HV_EFI_TEARDOWN_NATIVE_FAILURE UINT32_C(1)
#define HV_EFI_TEARDOWN_PAGE_MASK UINT64_C(0xfff)
#define HV_EFI_TEARDOWN_CONTEXT_BYTES UINT32_C(56)
#define HV_EFI_TEARDOWN_GUEST_STATE_BYTES UINT32_C(512)
#define HV_EFI_TEARDOWN_CPU_STATE_BYTES UINT32_C(584)

typedef enum HV_EFI_TEARDOWN_STATUS {
    HV_EFI_TEARDOWN_OK = 0,
    HV_EFI_TEARDOWN_INVALID_ARGUMENT = 1,
    HV_EFI_TEARDOWN_INVALID_CPU = 2,
    HV_EFI_TEARDOWN_DUPLICATE_CPU = 3,
    HV_EFI_TEARDOWN_INVALID_RESOURCE = 4,
    HV_EFI_TEARDOWN_STALE_GENERATION = 5,
    HV_EFI_TEARDOWN_GENERATION_INVALID = 6,
    HV_EFI_TEARDOWN_INVALID_STATE = 7,
    HV_EFI_TEARDOWN_CAPTURE_INCOMPLETE = 8,
    HV_EFI_TEARDOWN_DUPLICATE_CAPTURE = 9,
    HV_EFI_TEARDOWN_VMCLEAR_REQUIRED = 10,
    HV_EFI_TEARDOWN_VMCLEAR_FAILED = 11,
    HV_EFI_TEARDOWN_VMXOFF_REQUIRED = 12,
    HV_EFI_TEARDOWN_VMXOFF_FAILED = 13,
    HV_EFI_TEARDOWN_RESTORATION_NOT_READY = 14,
    HV_EFI_TEARDOWN_RESTORATION_FAILED = 15,
    HV_EFI_TEARDOWN_DUPLICATE_ACK = 16,
    HV_EFI_TEARDOWN_NOT_COMPLETE = 17,
    HV_EFI_TEARDOWN_QUARANTINE_ACTIVE = 18,
    HV_EFI_TEARDOWN_GENERATION_OVERFLOW = 19,
    HV_EFI_TEARDOWN_INVARIANT_VIOLATION = 20
} HV_EFI_TEARDOWN_STATUS;

typedef enum HV_EFI_TEARDOWN_PHASE {
    HV_EFI_TEARDOWN_READY = 0,
    HV_EFI_TEARDOWN_CAPTURING = 1,
    HV_EFI_TEARDOWN_VMCLEAR = 2,
    HV_EFI_TEARDOWN_VMXOFF = 3,
    HV_EFI_TEARDOWN_RESTORING = 4,
    HV_EFI_TEARDOWN_ACKNOWLEDGING = 5,
    HV_EFI_TEARDOWN_COMPLETE = 6,
    HV_EFI_TEARDOWN_QUARANTINED = 7
} HV_EFI_TEARDOWN_PHASE;

typedef enum HV_EFI_TEARDOWN_CPU_PHASE {
    HV_EFI_TEARDOWN_CPU_DISABLED = 0,
    HV_EFI_TEARDOWN_CPU_CAPTURE_REQUIRED = 1,
    HV_EFI_TEARDOWN_CPU_CAPTURED = 2,
    HV_EFI_TEARDOWN_CPU_VMCLEAR_COMPLETE = 3,
    HV_EFI_TEARDOWN_CPU_VMXOFF_REQUIRED = 4,
    HV_EFI_TEARDOWN_CPU_VMXOFF_COMPLETE = 5,
    HV_EFI_TEARDOWN_CPU_RESTORE_REQUIRED = 6,
    HV_EFI_TEARDOWN_CPU_RESTORED = 7,
    HV_EFI_TEARDOWN_CPU_ACKNOWLEDGED = 8,
    HV_EFI_TEARDOWN_CPU_FAILED = 9,
    HV_EFI_TEARDOWN_CPU_QUARANTINED = 10
} HV_EFI_TEARDOWN_CPU_PHASE;

typedef enum HV_EFI_TEARDOWN_RECOVERY_ACTION {
    HV_EFI_TEARDOWN_RECOVERY_RESUME = 0,
    HV_EFI_TEARDOWN_RECOVERY_EXTERNAL_RESET_QUARANTINE = 1
} HV_EFI_TEARDOWN_RECOVERY_ACTION;

/* Every member has one canonical 64-bit slot for stable capture/read-back. */
typedef struct HV_EFI_TEARDOWN_GUEST_STATE {
    uint64_t Cr0;
    uint64_t Cr3;
    uint64_t Cr4;
    uint64_t Rip;
    uint64_t Rsp;
    uint64_t Rflags;
    uint64_t Es;
    uint64_t Cs;
    uint64_t Ss;
    uint64_t Ds;
    uint64_t Fs;
    uint64_t Gs;
    uint64_t Ldtr;
    uint64_t Tr;
    uint64_t EsBase;
    uint64_t CsBase;
    uint64_t SsBase;
    uint64_t DsBase;
    uint64_t FsBase;
    uint64_t GsBase;
    uint64_t LdtrBase;
    uint64_t TrBase;
    uint64_t GdtrBase;
    uint64_t GdtrLimit;
    uint64_t IdtrBase;
    uint64_t IdtrLimit;
    uint64_t Efer;
    uint64_t Pat;
    uint64_t DebugCtl;
    uint64_t Dr7;
    uint64_t SysenterCs;
    uint64_t SysenterEsp;
    uint64_t SysenterEip;
    uint64_t Interruptibility;
    uint64_t ActivityState;
    uint64_t PendingDebug;
    uint64_t VmcsLinkPointer;
    uint64_t EsLimit;
    uint64_t CsLimit;
    uint64_t SsLimit;
    uint64_t DsLimit;
    uint64_t FsLimit;
    uint64_t GsLimit;
    uint64_t LdtrLimit;
    uint64_t TrLimit;
    uint64_t EsAccessRights;
    uint64_t CsAccessRights;
    uint64_t SsAccessRights;
    uint64_t DsAccessRights;
    uint64_t FsAccessRights;
    uint64_t GsAccessRights;
    uint64_t LdtrAccessRights;
    uint64_t TrAccessRights;
    uint64_t Pdpte0;
    uint64_t Pdpte1;
    uint64_t Pdpte2;
    uint64_t Pdpte3;
    uint64_t PerfGlobalCtrl;
    uint64_t BndConfig;
    uint64_t Xss;
    uint64_t CetSsp;
    uint64_t SCet;
    uint64_t Pkrs;
    uint64_t RtitCtl;
} HV_EFI_TEARDOWN_GUEST_STATE;

/* Identity and VMX resource addresses are supplied before Initialize. */
typedef struct HV_EFI_TEARDOWN_CPU_STATE {
    uint32_t ProcessorIndex;
    uint32_t GroupId;
    uint32_t Enabled;
    uint32_t Phase;
    uint64_t Generation;
    uint64_t VmxonPhysicalAddress;
    uint64_t VmcsPhysicalAddress;
    uint64_t CapturedMask;
    uint32_t VmclearCompleted;
    uint32_t VmxoffCompleted;
    uint32_t Restored;
    uint32_t Acknowledged;
    uint32_t FailureCode;
    uint32_t Reserved;
    HV_EFI_TEARDOWN_GUEST_STATE GuestState;
} HV_EFI_TEARDOWN_CPU_STATE;

typedef struct HV_EFI_TEARDOWN {
    HV_EFI_TEARDOWN_CPU_STATE *CpuStates;
    uint32_t CpuCount;
    uint32_t EnabledCount;
    uint32_t AcknowledgedCount;
    uint32_t Phase;
    uint32_t RecoveryAction;
    uint32_t Reserved;
    uint64_t Generation;
    uint64_t LastGeneration;
    uint64_t Marker;
} HV_EFI_TEARDOWN;

/* Initializes caller-supplied identity/resource records; no native action. */
HV_EFI_TEARDOWN_STATUS HvEfiTeardownInitialize(
    HV_EFI_TEARDOWN *teardown,
    HV_EFI_TEARDOWN_CPU_STATE *cpuStates,
    uint32_t cpuCount);

/* Starts one non-retryable generation and establishes a capture barrier. */
HV_EFI_TEARDOWN_STATUS HvEfiTeardownBegin(
    HV_EFI_TEARDOWN *teardown,
    uint64_t generation);

/* Captures all required VMCS guest-state fields before VMCLEAR. */
HV_EFI_TEARDOWN_STATUS HvEfiTeardownCaptureGuestState(
    HV_EFI_TEARDOWN *teardown,
    uint32_t processorIndex,
    uint64_t generation,
    const HV_EFI_TEARDOWN_GUEST_STATE *guestState,
    uint64_t capturedMask);

/* Records the native VMCLEAR result; success is the only advancing result. */
HV_EFI_TEARDOWN_STATUS HvEfiTeardownCompleteVmclear(
    HV_EFI_TEARDOWN *teardown,
    uint32_t processorIndex,
    uint64_t generation,
    uint32_t nativeResult);

/* Records the native VMXOFF result; no acknowledgement is made here. */
HV_EFI_TEARDOWN_STATUS HvEfiTeardownCompleteVmxoff(
    HV_EFI_TEARDOWN *teardown,
    uint32_t processorIndex,
    uint64_t generation,
    uint32_t nativeResult);

/* Returns the captured state only after VMXOFF completion. */
HV_EFI_TEARDOWN_STATUS HvEfiTeardownGetRestorationState(
    const HV_EFI_TEARDOWN *teardown,
    uint32_t processorIndex,
    uint64_t generation,
    HV_EFI_TEARDOWN_GUEST_STATE *guestState);

/* Requires native success and exact state read-back before marking restored. */
HV_EFI_TEARDOWN_STATUS HvEfiTeardownCompleteRestoration(
    HV_EFI_TEARDOWN *teardown,
    uint32_t processorIndex,
    uint64_t generation,
    const HV_EFI_TEARDOWN_GUEST_STATE *restoredState,
    uint32_t nativeResult);

/* Acknowledgement is legal only after CompleteRestoration succeeds. */
HV_EFI_TEARDOWN_STATUS HvEfiTeardownAcknowledgeRestored(
    HV_EFI_TEARDOWN *teardown,
    uint32_t processorIndex,
    uint64_t generation);

/* True only when every enabled processor is restored and acknowledged. */
uint32_t HvEfiTeardownCanReleaseResources(
    const HV_EFI_TEARDOWN *teardown);

uint32_t HvEfiTeardownAllEnabledAcknowledged(
    const HV_EFI_TEARDOWN *teardown);

HV_EFI_TEARDOWN_PHASE HvEfiTeardownGetPhase(
    const HV_EFI_TEARDOWN *teardown);

HV_EFI_TEARDOWN_RECOVERY_ACTION HvEfiTeardownGetRecoveryAction(
    const HV_EFI_TEARDOWN *teardown);

#ifdef __cplusplus
}

static_assert(sizeof(HV_EFI_TEARDOWN_GUEST_STATE) ==
                  HV_EFI_TEARDOWN_GUEST_STATE_BYTES,
              "teardown guest-state ABI drift");
static_assert(sizeof(HV_EFI_TEARDOWN_CPU_STATE) ==
                  HV_EFI_TEARDOWN_CPU_STATE_BYTES,
              "teardown CPU-state ABI drift");
static_assert(sizeof(HV_EFI_TEARDOWN) == HV_EFI_TEARDOWN_CONTEXT_BYTES,
              "teardown context ABI drift");
#endif

#endif /* HYPERSINV_EFI_RUNTIME_TEARDOWN_H */
