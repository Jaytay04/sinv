/* WDK-free ABI compile contract.  Compile as ordinary C or C++ in CI. */
#include "hvr1_protocol.h"

#if defined(__cplusplus)
static_assert(sizeof(HVR1_MAILBOX) == 65536, "mailbox size");
static_assert(offsetof(HVR1_MAILBOX, AuthTag) == 52, "tag offset");
static_assert(offsetof(HVR1_MAILBOX, Payload) == 84, "payload offset");
static_assert(offsetof(HVR1_MAILBOX, AckByProcessor) == 49152,
              "ack table offset");
static_assert(sizeof(HVR1_IOCTL_REQUEST) == 24, "request size");
static_assert(sizeof(HVR1_MAILBOX_DESCRIPTOR) == 80, "descriptor size");
static_assert(sizeof(HVR1_IOCTL_STATUS) == 56, "status size");
static_assert(sizeof(HVR1_RUNTIME_SESSION) == 64, "session size");
static_assert(sizeof(HVR1_PREPARE_ARGUMENTS) == 56, "prepare args size");
static_assert(sizeof(HVR1_PREPARE_RESULT) == 64, "prepare result size");
static_assert(sizeof(HVR1_COMMIT_ARGUMENTS) == 48, "commit args size");
static_assert(sizeof(HVR1_SCAN_ARGUMENTS) == 64, "scan args size");
static_assert(sizeof(HVR1_PROCESS_ARGUMENTS) == 64, "process args size");
static_assert(sizeof(HVR1_SHADOW_ARGUMENTS) == 64, "shadow args size");
#else
_Static_assert(sizeof(HVR1_MAILBOX) == 65536, "mailbox size");
_Static_assert(offsetof(HVR1_MAILBOX, AuthTag) == 52, "tag offset");
_Static_assert(offsetof(HVR1_MAILBOX, Payload) == 84, "payload offset");
_Static_assert(offsetof(HVR1_MAILBOX, AckByProcessor) == 49152,
               "ack table offset");
_Static_assert(sizeof(HVR1_IOCTL_REQUEST) == 24, "request size");
_Static_assert(sizeof(HVR1_MAILBOX_DESCRIPTOR) == 80, "descriptor size");
_Static_assert(sizeof(HVR1_IOCTL_STATUS) == 56, "status size");
_Static_assert(sizeof(HVR1_RUNTIME_SESSION) == 64, "session size");
_Static_assert(sizeof(HVR1_PREPARE_ARGUMENTS) == 56, "prepare args size");
_Static_assert(sizeof(HVR1_PREPARE_RESULT) == 64, "prepare result size");
_Static_assert(sizeof(HVR1_COMMIT_ARGUMENTS) == 48, "commit args size");
_Static_assert(sizeof(HVR1_SCAN_ARGUMENTS) == 64, "scan args size");
_Static_assert(sizeof(HVR1_PROCESS_ARGUMENTS) == 64, "process args size");
_Static_assert(sizeof(HVR1_SHADOW_ARGUMENTS) == 64, "shadow args size");
#endif

int hvr1_abi_contract_translation_unit(void)
{
    return HVR1_IOCTL_GET_MAILBOX != HVR1_IOCTL_SUBMIT &&
        HVR1_IOCTL_SUBMIT != HVR1_IOCTL_QUERY_STATUS &&
        HVR1_EFI_CALL_QUERY_SESSION == 3 &&
        HVR1_EFI_CALL_SHUTDOWN_PREPARE == 4 &&
        HVR1_EFI_CALL_SHUTDOWN_COMMIT == 5 &&
        HVR1_EFI_CALL_RUNTIME_SCAN == 6 &&
        HVR1_EFI_CALL_PROCESS_LOCATE == 7 &&
        HVR1_EFI_CALL_SHADOW_CONTROL == 8 &&
        HVR1_EFI_CALL_ANALYSIS_CONTROL == 9 &&
        HVR1_RUNTIME_CAP_SCAN == (UINT64_C(1) << 4) &&
        HVR1_RUNTIME_CAP_PROCESS == (UINT64_C(1) << 5) &&
        HVR1_RUNTIME_CAP_SHADOW_EPT == (UINT64_C(1) << 6) &&
        HVR1_RUNTIME_CAP_ANALYSIS == (UINT64_C(1) << 7) ? 0 : 1;
}
