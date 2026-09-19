#ifndef HYPERSINV_EFI_INTERRUPT_POLICY_H
#define HYPERSINV_EFI_INTERRUPT_POLICY_H

/*
 * Freestanding interrupt admission, ordering, and reinjection policy.
 *
 * This module is deliberately a pure model.  It does not read or write a
 * VMCS, acknowledge an APIC, execute an interrupt, or allocate memory.  The
 * caller supplies one fixed-size event buffer per processor.  A successful
 * EvaluateDelivery call returns metadata that the VMX adapter may translate
 * into VM-entry interruption-information fields; CommitDelivery is called
 * only after that adapter has completed the VM-entry.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HV_EFI_INTERRUPT_POLICY_MAX_CPUS UINT32_C(256)
#define HV_EFI_INTERRUPT_POLICY_NMI_VECTOR UINT32_C(2)
#define HV_EFI_INTERRUPT_POLICY_EXTERNAL_MIN_VECTOR UINT32_C(0x10)
#define HV_EFI_INTERRUPT_POLICY_EXTERNAL_MAX_VECTOR UINT32_C(0xfe)

/* RFLAGS.IF and the VMX guest-interruptibility blocking bits. */
#define HV_EFI_INTERRUPT_POLICY_RFLAGS_IF (UINT64_C(1) << 9)
#define HV_EFI_INTERRUPT_POLICY_BLOCKING_STI UINT32_C(1) << 0
#define HV_EFI_INTERRUPT_POLICY_BLOCKING_MOV_SS UINT32_C(1) << 1
#define HV_EFI_INTERRUPT_POLICY_BLOCKING_SMI UINT32_C(1) << 2
#define HV_EFI_INTERRUPT_POLICY_BLOCKING_NMI UINT32_C(1) << 3
#define HV_EFI_INTERRUPT_POLICY_BLOCKING_MASK UINT32_C(0x0f)

/* VM-entry interruption-information encoding used by reinjection metadata. */
#define HV_EFI_INTERRUPT_POLICY_VM_ENTRY_VALID (UINT32_C(1) << 31)
#define HV_EFI_INTERRUPT_POLICY_VM_ENTRY_TYPE_SHIFT UINT32_C(8)
#define HV_EFI_INTERRUPT_POLICY_VM_ENTRY_TYPE_MASK UINT32_C(0x07)
#define HV_EFI_INTERRUPT_POLICY_VM_ENTRY_TYPE_EXTERNAL UINT32_C(0)
#define HV_EFI_INTERRUPT_POLICY_VM_ENTRY_TYPE_NMI UINT32_C(2)

typedef enum HV_EFI_INTERRUPT_POLICY_STATUS {
    HV_EFI_INTERRUPT_POLICY_OK = 0,
    HV_EFI_INTERRUPT_POLICY_INVALID_ARGUMENT = 1,
    HV_EFI_INTERRUPT_POLICY_INVALID_CPU = 2,
    HV_EFI_INTERRUPT_POLICY_INVALID_VECTOR = 3,
    HV_EFI_INTERRUPT_POLICY_UNSUPPORTED_INTERRUPT = 4,
    HV_EFI_INTERRUPT_POLICY_UNSUPPORTED_DEVICE = 5,
    HV_EFI_INTERRUPT_POLICY_QUEUE_FULL = 6,
    HV_EFI_INTERRUPT_POLICY_POLICY_FAILED = 7,
    HV_EFI_INTERRUPT_POLICY_DUPLICATE_EVENT = 8,
    HV_EFI_INTERRUPT_POLICY_INVALID_SEQUENCE = 9,
    HV_EFI_INTERRUPT_POLICY_NOT_PENDING = 10,
    HV_EFI_INTERRUPT_POLICY_STALE_DECISION = 11,
    HV_EFI_INTERRUPT_POLICY_SEQUENCE_OVERFLOW = 12,
    HV_EFI_INTERRUPT_POLICY_INVALID_STATE = 13
} HV_EFI_INTERRUPT_POLICY_STATUS;

typedef enum HV_EFI_INTERRUPT_POLICY_STATE {
    HV_EFI_INTERRUPT_POLICY_READY = 0,
    /* Any safety-critical queue failure quarantines the whole policy. */
    HV_EFI_INTERRUPT_POLICY_FAILED = 1
} HV_EFI_INTERRUPT_POLICY_STATE;

typedef enum HV_EFI_INTERRUPT_POLICY_CPU_STARTUP_STATE {
    HV_EFI_INTERRUPT_POLICY_CPU_RUNNING = 0,
    HV_EFI_INTERRUPT_POLICY_CPU_WAIT_FOR_SIPI = 1
} HV_EFI_INTERRUPT_POLICY_CPU_STARTUP_STATE;

typedef enum HV_EFI_INTERRUPT_POLICY_EVENT_KIND {
    HV_EFI_INTERRUPT_POLICY_EVENT_EXTERNAL = 1,
    HV_EFI_INTERRUPT_POLICY_EVENT_NMI = 2,
    HV_EFI_INTERRUPT_POLICY_EVENT_INIT = 3,
    HV_EFI_INTERRUPT_POLICY_EVENT_SIPI = 4
} HV_EFI_INTERRUPT_POLICY_EVENT_KIND;

typedef enum HV_EFI_INTERRUPT_POLICY_INTERRUPT_KIND {
    HV_EFI_INTERRUPT_POLICY_INTERRUPT_EXTERNAL = 1,
    HV_EFI_INTERRUPT_POLICY_INTERRUPT_NMI = 2,
    HV_EFI_INTERRUPT_POLICY_INTERRUPT_INIT = 3,
    HV_EFI_INTERRUPT_POLICY_INTERRUPT_SIPI = 4,
    HV_EFI_INTERRUPT_POLICY_INTERRUPT_EXCEPTION = 5,
    HV_EFI_INTERRUPT_POLICY_INTERRUPT_SOFTWARE = 6,
    HV_EFI_INTERRUPT_POLICY_INTERRUPT_SMI = 7
} HV_EFI_INTERRUPT_POLICY_INTERRUPT_KIND;

typedef enum HV_EFI_INTERRUPT_POLICY_DEVICE_KIND {
    HV_EFI_INTERRUPT_POLICY_DEVICE_LOCAL_APIC = 1,
    HV_EFI_INTERRUPT_POLICY_DEVICE_IO_APIC = 2,
    HV_EFI_INTERRUPT_POLICY_DEVICE_MSI = 3,
    HV_EFI_INTERRUPT_POLICY_DEVICE_PCI = 4,
    HV_EFI_INTERRUPT_POLICY_DEVICE_HPET = 5,
    HV_EFI_INTERRUPT_POLICY_DEVICE_UNKNOWN = 6
} HV_EFI_INTERRUPT_POLICY_DEVICE_KIND;

typedef enum HV_EFI_INTERRUPT_POLICY_EXIT_REASON {
    HV_EFI_INTERRUPT_POLICY_EXIT_EXTERNAL_INTERRUPT = 1,
    HV_EFI_INTERRUPT_POLICY_EXIT_INTERRUPT_WINDOW = 2,
    HV_EFI_INTERRUPT_POLICY_EXIT_NMI = 3,
    HV_EFI_INTERRUPT_POLICY_EXIT_NMI_WINDOW = 4,
    HV_EFI_INTERRUPT_POLICY_EXIT_INIT = 5,
    HV_EFI_INTERRUPT_POLICY_EXIT_SIPI = 6
} HV_EFI_INTERRUPT_POLICY_EXIT_REASON;

typedef enum HV_EFI_INTERRUPT_POLICY_WINDOW_KIND {
    HV_EFI_INTERRUPT_POLICY_WINDOW_NONE = 0,
    HV_EFI_INTERRUPT_POLICY_WINDOW_INTERRUPT = 1,
    HV_EFI_INTERRUPT_POLICY_WINDOW_NMI = 2
} HV_EFI_INTERRUPT_POLICY_WINDOW_KIND;

typedef enum HV_EFI_INTERRUPT_POLICY_ACTION {
    HV_EFI_INTERRUPT_POLICY_ACTION_NONE = 0,
    /* The VMX adapter must acknowledge the source before resuming. */
    HV_EFI_INTERRUPT_POLICY_ACTION_ACK_EXTERNAL = 1,
    HV_EFI_INTERRUPT_POLICY_ACTION_QUEUE_NMI = 2,
    HV_EFI_INTERRUPT_POLICY_ACTION_QUEUE_INIT = 3,
    HV_EFI_INTERRUPT_POLICY_ACTION_QUEUE_SIPI = 4,
    HV_EFI_INTERRUPT_POLICY_ACTION_REINJECT = 5,
    HV_EFI_INTERRUPT_POLICY_ACTION_REQUEST_INTERRUPT_WINDOW = 6,
    HV_EFI_INTERRUPT_POLICY_ACTION_REQUEST_NMI_WINDOW = 7,
    HV_EFI_INTERRUPT_POLICY_ACTION_REJECT = 8,
    HV_EFI_INTERRUPT_POLICY_ACTION_FAIL_CLOSED = 9
} HV_EFI_INTERRUPT_POLICY_ACTION;

/* The source is retained for auditing and for rejecting device paths. */
typedef enum HV_EFI_INTERRUPT_POLICY_SOURCE_KIND {
    HV_EFI_INTERRUPT_POLICY_SOURCE_CPU_EXTERNAL = 1,
    HV_EFI_INTERRUPT_POLICY_SOURCE_CPU_NMI = 2,
    HV_EFI_INTERRUPT_POLICY_SOURCE_LOCAL_APIC_INIT = 3,
    HV_EFI_INTERRUPT_POLICY_SOURCE_LOCAL_APIC_SIPI = 4,
    HV_EFI_INTERRUPT_POLICY_SOURCE_IO_APIC = 5,
    HV_EFI_INTERRUPT_POLICY_SOURCE_MSI = 6,
    HV_EFI_INTERRUPT_POLICY_SOURCE_PCI_DEVICE = 7,
    HV_EFI_INTERRUPT_POLICY_SOURCE_UNKNOWN = 8
} HV_EFI_INTERRUPT_POLICY_SOURCE_KIND;

typedef struct HV_EFI_INTERRUPT_POLICY_EVENT {
    uint32_t Kind;
    uint32_t Source;
    uint32_t Vector;
    uint32_t Reserved;
    uint64_t Sequence;
} HV_EFI_INTERRUPT_POLICY_EVENT;

typedef struct HV_EFI_INTERRUPT_POLICY_CPU_STATE {
    /* ProcessorId is a stable hardware identity, not an array position. */
    uint32_t ProcessorId;
    uint32_t StartupState;
    HV_EFI_INTERRUPT_POLICY_EVENT *Events;
    uint32_t EventCapacity;
    uint32_t EventCount;
    uint32_t Reserved;
} HV_EFI_INTERRUPT_POLICY_CPU_STATE;

typedef struct HV_EFI_INTERRUPT_POLICY {
    HV_EFI_INTERRUPT_POLICY_CPU_STATE *CpuStates;
    uint32_t CpuCount;
    uint32_t State;
    uint32_t Reserved;
    uint64_t NextSequence;
} HV_EFI_INTERRUPT_POLICY;

typedef struct HV_EFI_INTERRUPT_POLICY_GUEST_STATE {
    uint64_t Rflags;
    uint32_t Interruptibility;
    uint32_t ActivityState;
} HV_EFI_INTERRUPT_POLICY_GUEST_STATE;

typedef struct HV_EFI_INTERRUPT_POLICY_EXIT {
    uint32_t Reason;
    uint32_t Source;
    uint32_t Vector;
    uint32_t Reserved;
} HV_EFI_INTERRUPT_POLICY_EXIT;

typedef struct HV_EFI_INTERRUPT_POLICY_REINJECTION {
    uint32_t Valid;
    uint32_t Kind;
    uint32_t Source;
    uint32_t Vector;
    uint64_t Sequence;
    uint32_t VmEntryInterruptionInformation;
    uint32_t DeliverErrorCode;
    uint32_t ErrorCode;
    uint32_t InstructionLength;
} HV_EFI_INTERRUPT_POLICY_REINJECTION;

typedef struct HV_EFI_INTERRUPT_POLICY_DECISION {
    uint32_t Action;
    uint32_t AcknowledgeExternal;
    uint32_t Reserved;
    uint32_t Reserved2;
    uint64_t PendingSequence;
    HV_EFI_INTERRUPT_POLICY_REINJECTION Reinjection;
} HV_EFI_INTERRUPT_POLICY_DECISION;

/* Returns OK only for the interrupt kinds handled by this policy model. */
HV_EFI_INTERRUPT_POLICY_STATUS HvEfiInterruptPolicyAdmitInterrupt(
    uint32_t interruptKind);

/* Local APIC delivery is modeled; device interrupt paths are explicit rejects. */
HV_EFI_INTERRUPT_POLICY_STATUS HvEfiInterruptPolicyAdmitDevice(
    uint32_t deviceKind);

/* Links caller-owned CPU records and one fixed event buffer per CPU. */
HV_EFI_INTERRUPT_POLICY_STATUS HvEfiInterruptPolicyInitialize(
    HV_EFI_INTERRUPT_POLICY *policy,
    HV_EFI_INTERRUPT_POLICY_CPU_STATE *cpuStates,
    uint32_t cpuCount,
    HV_EFI_INTERRUPT_POLICY_EVENT *eventStorage,
    uint32_t eventCapacityPerCpu);

HV_EFI_INTERRUPT_POLICY_STATUS HvEfiInterruptPolicyQueueExternal(
    HV_EFI_INTERRUPT_POLICY *policy,
    uint32_t processorId,
    uint32_t source,
    uint32_t vector,
    uint64_t *sequence);

HV_EFI_INTERRUPT_POLICY_STATUS HvEfiInterruptPolicyQueueNmi(
    HV_EFI_INTERRUPT_POLICY *policy,
    uint32_t processorId,
    uint32_t source,
    uint64_t *sequence);

/* INIT changes the startup state to WAIT_FOR_SIPI; SIPI is accepted only then. */
HV_EFI_INTERRUPT_POLICY_STATUS HvEfiInterruptPolicyQueueInit(
    HV_EFI_INTERRUPT_POLICY *policy,
    uint32_t processorId,
    uint64_t *sequence);

HV_EFI_INTERRUPT_POLICY_STATUS HvEfiInterruptPolicyQueueSipi(
    HV_EFI_INTERRUPT_POLICY *policy,
    uint32_t processorId,
    uint32_t startupVector,
    uint64_t *sequence);

/* Selects the highest-priority pending event without removing it. */
HV_EFI_INTERRUPT_POLICY_STATUS HvEfiInterruptPolicyEvaluateDelivery(
    const HV_EFI_INTERRUPT_POLICY *policy,
    uint32_t processorId,
    const HV_EFI_INTERRUPT_POLICY_GUEST_STATE *guest,
    uint32_t windowKind,
    HV_EFI_INTERRUPT_POLICY_DECISION *decision);

/* Removes exactly the event described by a prior successful evaluation. */
HV_EFI_INTERRUPT_POLICY_STATUS HvEfiInterruptPolicyCommitDelivery(
    HV_EFI_INTERRUPT_POLICY *policy,
    uint32_t processorId,
    const HV_EFI_INTERRUPT_POLICY_REINJECTION *reinjection);

/* Models VM-exit admission/acknowledgement and window exits; no VMCS access. */
HV_EFI_INTERRUPT_POLICY_STATUS HvEfiInterruptPolicyHandleExit(
    HV_EFI_INTERRUPT_POLICY *policy,
    uint32_t processorId,
    const HV_EFI_INTERRUPT_POLICY_EXIT *exit,
    const HV_EFI_INTERRUPT_POLICY_GUEST_STATE *guest,
    HV_EFI_INTERRUPT_POLICY_DECISION *decision);

const HV_EFI_INTERRUPT_POLICY_CPU_STATE *HvEfiInterruptPolicyGetCpuState(
    const HV_EFI_INTERRUPT_POLICY *policy,
    uint32_t processorId);

uint32_t HvEfiInterruptPolicyIsFailed(
    const HV_EFI_INTERRUPT_POLICY *policy);

#ifdef __cplusplus
}
#endif

#endif /* HYPERSINV_EFI_INTERRUPT_POLICY_H */
