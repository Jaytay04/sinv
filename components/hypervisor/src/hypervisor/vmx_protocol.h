/*
 * vmx_protocol.h — Pure VMCALL protocol state machine (R10)
 *
 * Decides resume/offline action based on exit reason, guest registers,
 * and current protocol step. Table-driven — no hardware access.
 */

#ifndef HYPERSINV_VMX_PROTOCOL_H
#define HYPERSINV_VMX_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Intel SDM: exit reason bits 15:0 = basic reason; bit 31 = entry failure */
#define VMX_EXIT_ENTRY_FAILURE_BIT  (1u << 31)
#define VMX_EXIT_BASIC_REASON_MASK  0x0000FFFFu

/* Deterministic protocol validation bits. Bits 8 and 9 are reserved for
 * dispatcher-level VMREAD and RIP VMWRITE success respectively. */
#define VMX_PROTOCOL_CHECK_EXIT_REASON         (1u << 0)
#define VMX_PROTOCOL_CHECK_SIGNATURE           (1u << 1)
#define VMX_PROTOCOL_CHECK_COOKIE              (1u << 2)
#define VMX_PROTOCOL_CHECK_INSTRUCTION_LENGTH (1u << 3)
#define VMX_PROTOCOL_CHECK_RIP_CANONICAL       (1u << 4)
#define VMX_PROTOCOL_CHECK_NEXT_RIP_CANONICAL  (1u << 5)
#define VMX_PROTOCOL_CHECK_OPERATION           (1u << 6)
#define VMX_PROTOCOL_CHECK_ORDER               (1u << 7)
#define VMX_PROTOCOL_CHECK_VMREAD              (1u << 8)
#define VMX_PROTOCOL_CHECK_RIP_WRITE           (1u << 9)

/* Protocol operations (matches MASM constants). */
#define VMX_PROTO_OP_PROBE     1u
#define VMX_PROTO_OP_SHUTDOWN  2u
#define VMX_PROTOCOL_MAX_INSTRUCTION_LENGTH 15u

typedef enum _VMX_PROTOCOL_DECISION {
    VMX_PROTO_DECISION_INVALID = 0,
    VMX_PROTO_DECISION_RESUME  = 1,
    VMX_PROTO_DECISION_OFFLINE = 2
} VMX_PROTOCOL_DECISION;

/* Observation from the VM-exit dispatcher. */
typedef struct _VMX_PROTOCOL_INPUT {
    uint64_t ExitReason;        /* Raw reason; preserve reserved/high bits */
    uint64_t ExitQualification;
    uint64_t GuestRip;
    uint32_t InstructionLength;
    uint64_t GuestRax;          /* Signature */
    uint64_t GuestRcx;          /* Operation */
    uint64_t GuestRdx;          /* Cookie */
    uint64_t ExpectedCookie;
    uint8_t  ProtocolStep;      /* 0 = probe, 1 = shutdown expected */
} VMX_PROTOCOL_INPUT;

/* Output decision with optional advanced RIP. */
typedef struct _VMX_PROTOCOL_OUTPUT {
    VMX_PROTOCOL_DECISION Decision;
    uint64_t             AdvancedRip;  /* Valid only for RESUME */
    uint16_t             ValidationMask; /* Checks passed (bits 0-7) */
    bool                 Accepted;      /* Valid probe or shutdown */
} VMX_PROTOCOL_OUTPUT;

/*
 * VmxProtocolDecide — Evaluate a VM-exit and decide next action.
 *
 * Probe (step 0): VMCALL, no entry failure, signature/cookie match,
 *   op=PROBE, instruction length > 0, RIP and RIP+len canonical.
 *   Advances RIP and returns RESUME.
 *
 * Shutdown (step 1): same validation, op=SHUTDOWN. Returns OFFLINE
 *   without advancing RIP.
 *
 * Everything else returns OFFLINE.
 */
VMX_PROTOCOL_OUTPUT VmxProtocolDecide(const VMX_PROTOCOL_INPUT* input);

#ifdef __cplusplus
}
#endif

#endif /* HYPERSINV_VMX_PROTOCOL_H */
