#ifndef HYPERSINV_VMX_PERSISTENT_PROTOCOL_H
#define HYPERSINV_VMX_PERSISTENT_PROTOCOL_H

/* WDK-free V3D2a READY/SHUTDOWN exit protocol. */
#include <stdbool.h>
#include <stdint.h>
#include "vmx_persistent_launch_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VMX_V3D_PROTOCOL_EXIT_REASON_VMCALL UINT64_C(18)
#define VMX_V3D_PROTOCOL_BASIC_REASON_MASK UINT64_C(0x0000ffff)
#define VMX_V3D_PROTOCOL_ENTRY_FAILURE_BIT UINT64_C(0x80000000)
#define VMX_V3D_PROTOCOL_MAX_INSTRUCTION_LENGTH UINT64_C(15)

/* Bits describe checks which passed; a failed check is intentionally absent. */
#define VMX_V3D_PROTOCOL_CHECK_REASON_BASIC       (UINT32_C(1) << 0)
#define VMX_V3D_PROTOCOL_CHECK_ENTRY_FAILURE      (UINT32_C(1) << 1)
#define VMX_V3D_PROTOCOL_CHECK_REASON_RESERVED    (UINT32_C(1) << 2)
#define VMX_V3D_PROTOCOL_CHECK_QUALIFICATION      (UINT32_C(1) << 3)
#define VMX_V3D_PROTOCOL_CHECK_SIGNATURE          (UINT32_C(1) << 4)
#define VMX_V3D_PROTOCOL_CHECK_COOKIE             (UINT32_C(1) << 5)
#define VMX_V3D_PROTOCOL_CHECK_INSTRUCTION_LENGTH (UINT32_C(1) << 6)
#define VMX_V3D_PROTOCOL_CHECK_RIP_CANONICAL      (UINT32_C(1) << 7)
#define VMX_V3D_PROTOCOL_CHECK_RIP_ADVANCE        (UINT32_C(1) << 8)
#define VMX_V3D_PROTOCOL_CHECK_NEXT_RIP_CANONICAL (UINT32_C(1) << 9)
#define VMX_V3D_PROTOCOL_CHECK_OPERATION          (UINT32_C(1) << 10)
#define VMX_V3D_PROTOCOL_CHECK_ORDER              (UINT32_C(1) << 11)
#define VMX_V3D_PROTOCOL_CHECK_ALL                UINT32_C(0x0fff)

typedef enum VMX_V3D_PROTOCOL_VALIDATION_CODE {
    VMX_V3D_PROTOCOL_VALIDATION_OK = 0,
    VMX_V3D_PROTOCOL_INVALID_REASON_BASIC = 1,
    VMX_V3D_PROTOCOL_INVALID_ENTRY_FAILURE = 2,
    VMX_V3D_PROTOCOL_INVALID_REASON_RESERVED = 3,
    VMX_V3D_PROTOCOL_INVALID_QUALIFICATION = 4,
    VMX_V3D_PROTOCOL_INVALID_SIGNATURE = 5,
    VMX_V3D_PROTOCOL_INVALID_COOKIE = 6,
    VMX_V3D_PROTOCOL_INVALID_INSTRUCTION_LENGTH = 7,
    VMX_V3D_PROTOCOL_INVALID_RIP = 8,
    VMX_V3D_PROTOCOL_INVALID_RIP_ADVANCE = 9,
    VMX_V3D_PROTOCOL_INVALID_NEXT_RIP = 10,
    VMX_V3D_PROTOCOL_INVALID_OPERATION = 11,
    VMX_V3D_PROTOCOL_INVALID_ORDER = 12
} VMX_V3D_PROTOCOL_VALIDATION_CODE;
typedef VMX_V3D_PROTOCOL_VALIDATION_CODE VMX_V3D_PROTOCOL_FAIL_CLOSED_CODE;

typedef struct VMX_V3D_PROTOCOL_INPUT {
    uint64_t ExitReason, Qualification, GuestRip, InstructionLength;
    uint64_t GuestRax, GuestRcx, GuestRdx, ExpectedCookie;
    uint32_t CurrentProtocolStep;
} VMX_V3D_PROTOCOL_INPUT;

typedef struct VMX_V3D_PROTOCOL_DECISION {
    uint32_t NextProtocolStep;
    uint32_t AssemblyAction;
    uint64_t AdvancedRip;
    uint32_t ValidationMask;
    uint32_t ValidationCode;
    uint32_t FailClosedCode;
    uint32_t ReadyObserved;
    uint32_t ShutdownObserved;
} VMX_V3D_PROTOCOL_DECISION;

/* Returns false only for null pointers; output remains byte-for-byte unchanged. */
bool VmxV3dProtocolDecide(const VMX_V3D_PROTOCOL_INPUT* input,
                          VMX_V3D_PROTOCOL_DECISION* output);

#ifdef __cplusplus
}
#endif

#endif /* HYPERSINV_VMX_PERSISTENT_PROTOCOL_H */
