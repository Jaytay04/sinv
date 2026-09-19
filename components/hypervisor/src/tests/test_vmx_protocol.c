/*
 * test_vmx_protocol.c — VMCALL protocol state machine tests (R10)
 *
 * Table-driven tests covering probe, shutdown, invalid exit reason,
 * entry failure, bad signature/cookie, zero length, non-canonical RIP,
 * and wrong op/step combinations.
 */

#include "test_common.h"
#include "vmx_protocol.h"
#include "vmx.h"
#include "vmx_launch_abi.h"

#define COOKIE 0xDEADBEEFCAFEBABEULL

static VMX_PROTOCOL_INPUT g_in;

static void InitInput(void) {
    g_in.ExitReason = EXIT_REASON_VMCALL;
    g_in.ExitQualification = 0;
    g_in.GuestRip = 0xFFFFF80000005000ULL;
    g_in.InstructionLength = 3;
    g_in.GuestRax = VMX_VMCALL_SIGNATURE;
    g_in.GuestRcx = VMX_PROTO_OP_PROBE;
    g_in.GuestRdx = COOKIE;
    g_in.ExpectedCookie = COOKIE;
    g_in.ProtocolStep = 0;
}

/* --- Probe success --- */
static int test_probe_resume(void) {
    InitInput();
    VMX_PROTOCOL_OUTPUT out = VmxProtocolDecide(&g_in);
    TEST_ASSERT_UINT_EQ((unsigned int)out.Decision,
                        (unsigned int)VMX_PROTO_DECISION_RESUME,
                        "Probe: RESUME");
    TEST_ASSERT_UINT_EQ(out.AdvancedRip, g_in.GuestRip + 3,
                        "Probe: RIP advanced by 3");
    return 0;
}

/* --- Shutdown success --- */
static int test_shutdown_offline(void) {
    InitInput();
    g_in.GuestRcx = VMX_PROTO_OP_SHUTDOWN;
    g_in.ProtocolStep = 1;
    VMX_PROTOCOL_OUTPUT out = VmxProtocolDecide(&g_in);
    TEST_ASSERT_UINT_EQ((unsigned int)out.Decision,
                        (unsigned int)VMX_PROTO_DECISION_OFFLINE,
                        "Shutdown: OFFLINE");
    return 0;
}

/* --- Rejections --- */
static int test_rejects_non_vmcall(void) {
    InitInput();
    g_in.ExitReason = 0; /* not VMCALL */
    VMX_PROTOCOL_OUTPUT out = VmxProtocolDecide(&g_in);
    TEST_ASSERT_UINT_EQ((unsigned int)out.Decision,
                        (unsigned int)VMX_PROTO_DECISION_OFFLINE,
                        "Non-VMCALL: OFFLINE");
    return 0;
}

static int test_rejects_entry_failure(void) {
    InitInput();
    g_in.ExitReason = EXIT_REASON_VMCALL | VMX_EXIT_ENTRY_FAILURE_BIT;
    VMX_PROTOCOL_OUTPUT out = VmxProtocolDecide(&g_in);
    TEST_ASSERT_UINT_EQ((unsigned int)out.Decision,
                        (unsigned int)VMX_PROTO_DECISION_OFFLINE,
                        "Entry failure: OFFLINE");
    return 0;
}

static int test_rejects_bad_signature(void) {
    InitInput();
    g_in.GuestRax = 0;
    VMX_PROTOCOL_OUTPUT out = VmxProtocolDecide(&g_in);
    TEST_ASSERT_UINT_EQ((unsigned int)out.Decision,
                        (unsigned int)VMX_PROTO_DECISION_OFFLINE,
                        "Bad signature: OFFLINE");
    return 0;
}

static int test_rejects_bad_cookie(void) {
    InitInput();
    g_in.GuestRdx = 0;
    VMX_PROTOCOL_OUTPUT out = VmxProtocolDecide(&g_in);
    TEST_ASSERT_UINT_EQ((unsigned int)out.Decision,
                        (unsigned int)VMX_PROTO_DECISION_OFFLINE,
                        "Bad cookie: OFFLINE");
    return 0;
}

static int test_rejects_zero_length(void) {
    InitInput();
    g_in.InstructionLength = 0;
    VMX_PROTOCOL_OUTPUT out = VmxProtocolDecide(&g_in);
    TEST_ASSERT_UINT_EQ((unsigned int)out.Decision,
                        (unsigned int)VMX_PROTO_DECISION_OFFLINE,
                        "Zero length: OFFLINE");
    return 0;
}

static int test_rejects_noncanonical_rip(void) {
    InitInput();
    g_in.GuestRip = 0x0000800000000000ULL;
    VMX_PROTOCOL_OUTPUT out = VmxProtocolDecide(&g_in);
    TEST_ASSERT_UINT_EQ((unsigned int)out.Decision,
                        (unsigned int)VMX_PROTO_DECISION_OFFLINE,
                        "Non-canonical RIP: OFFLINE");
    return 0;
}

static int test_rejects_noncanonical_next_rip(void) {
    InitInput();
    g_in.GuestRip = 0x00007FFFFFFFFFF0ULL; /* last user-mode canonical minus 16 */
    g_in.InstructionLength = 0x20; /* pushes into non-canonical 0x0000800000000010 */
    VMX_PROTOCOL_OUTPUT out = VmxProtocolDecide(&g_in);
    TEST_ASSERT_UINT_EQ((unsigned int)out.Decision,
                        (unsigned int)VMX_PROTO_DECISION_OFFLINE,
                        "Non-canonical RIP+len: OFFLINE");
    return 0;
}

static int test_rejects_probe_at_wrong_step(void) {
    InitInput();
    g_in.ProtocolStep = 1; /* expect shutdown, got probe */
    VMX_PROTOCOL_OUTPUT out = VmxProtocolDecide(&g_in);
    TEST_ASSERT_UINT_EQ((unsigned int)out.Decision,
                        (unsigned int)VMX_PROTO_DECISION_OFFLINE,
                        "Probe at step 1: OFFLINE");
    return 0;
}

static int test_rejects_shutdown_at_wrong_step(void) {
    InitInput();
    g_in.GuestRcx = VMX_PROTO_OP_SHUTDOWN;
    g_in.ProtocolStep = 0; /* expect probe, got shutdown */
    VMX_PROTOCOL_OUTPUT out = VmxProtocolDecide(&g_in);
    TEST_ASSERT_UINT_EQ((unsigned int)out.Decision,
                        (unsigned int)VMX_PROTO_DECISION_OFFLINE,
                        "Shutdown at step 0: OFFLINE");
    return 0;
}

static int test_rejects_unknown_operation(void) {
    InitInput();
    g_in.GuestRcx = 99;
    VMX_PROTOCOL_OUTPUT out = VmxProtocolDecide(&g_in);
    TEST_ASSERT_UINT_EQ((unsigned int)out.Decision,
                        (unsigned int)VMX_PROTO_DECISION_OFFLINE,
                        "Unknown op: OFFLINE");
    return 0;
}

static int test_protocol_exact_masks(void)
{
    struct CASE {
        uint64_t reason;
        uint64_t rax;
        uint64_t rcx;
        uint64_t rdx;
        uint32_t len;
        uint64_t rip;
        uint8_t step;
        uint16_t mask;
    } cases[] = {
        { EXIT_REASON_VMCALL | (1ULL << 16), VMX_VMCALL_SIGNATURE,
          VMX_PROTO_OP_PROBE, COOKIE, 3, 0xFFFFF80000005000ULL, 0, 0x00FE },
        { EXIT_REASON_VMCALL | (1ULL << 32), VMX_VMCALL_SIGNATURE,
          VMX_PROTO_OP_PROBE, COOKIE, 3, 0xFFFFF80000005000ULL, 0, 0x00FE },
        { EXIT_REASON_VMCALL | VMX_EXIT_ENTRY_FAILURE_BIT,
          VMX_VMCALL_SIGNATURE, VMX_PROTO_OP_PROBE, COOKIE, 3,
          0xFFFFF80000005000ULL, 0, 0x00FE },
        { EXIT_REASON_VMCALL, 0, VMX_PROTO_OP_PROBE, COOKIE, 3,
          0xFFFFF80000005000ULL, 0, 0x00FD },
        { EXIT_REASON_VMCALL, VMX_VMCALL_SIGNATURE, VMX_PROTO_OP_PROBE,
          COOKIE, 0, 0xFFFFF80000005000ULL, 0, 0x00D7 },
        { EXIT_REASON_VMCALL, VMX_VMCALL_SIGNATURE, VMX_PROTO_OP_PROBE,
          COOKIE, 16, 0xFFFFF80000005000ULL, 0, 0x00D7 },
        { EXIT_REASON_VMCALL, VMX_VMCALL_SIGNATURE, VMX_PROTO_OP_PROBE,
          COOKIE, 15, UINT64_MAX - 14ULL, 0, 0x00DF },
        { EXIT_REASON_VMCALL, VMX_VMCALL_SIGNATURE, 99, COOKIE, 3,
          0xFFFFF80000005000ULL, 0, 0x003F },
        { EXIT_REASON_VMCALL, VMX_VMCALL_SIGNATURE, VMX_PROTO_OP_PROBE,
          COOKIE, 3, 0xFFFFF80000005000ULL, 1, 0x007F }
    };
    size_t i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        VMX_PROTOCOL_INPUT input;
        VMX_PROTOCOL_OUTPUT output;

        InitInput();
        input = g_in;
        input.ExitReason = cases[i].reason;
        input.GuestRax = cases[i].rax;
        input.GuestRcx = cases[i].rcx;
        input.GuestRdx = cases[i].rdx;
        input.InstructionLength = cases[i].len;
        input.GuestRip = cases[i].rip;
        input.ProtocolStep = cases[i].step;
        output = VmxProtocolDecide(&input);

        TEST_ASSERT_UINT_EQ(output.Decision,
                            VMX_PROTO_DECISION_OFFLINE,
                            "Exact-mask rejection");
        TEST_ASSERT_UINT_EQ(output.ValidationMask, cases[i].mask,
                            "Exact-mask result");
        TEST_ASSERT_FALSE(output.Accepted,
                          "Exact-mask rejection not accepted");
    }
    return 0;
}

static int test_protocol_address_masks(void)
{
    VMX_PROTOCOL_OUTPUT output;

    InitInput();
    g_in.GuestRip = 0x00007FFFFFFFFFFFULL;
    g_in.InstructionLength = 1;
    output = VmxProtocolDecide(&g_in);
    TEST_ASSERT_UINT_EQ(output.Decision, VMX_PROTO_DECISION_OFFLINE,
                        "Noncanonical next RIP decision");
    TEST_ASSERT_UINT_EQ(output.ValidationMask, 0x00DFu,
                        "Noncanonical next RIP mask");
    TEST_ASSERT_FALSE(output.Accepted, "Noncanonical next RIP rejected");

    InitInput();
    g_in.GuestRip = 0x0000800000000000ULL;
    output = VmxProtocolDecide(&g_in);
    TEST_ASSERT_UINT_EQ(output.Decision, VMX_PROTO_DECISION_OFFLINE,
                        "Noncanonical RIP decision");
    TEST_ASSERT_UINT_EQ(output.ValidationMask, 0x00CFu,
                        "Noncanonical RIP mask");
    TEST_ASSERT_FALSE(output.Accepted, "Noncanonical RIP rejected");

    InitInput();
    g_in.GuestRip = UINT64_MAX - 14ULL;
    g_in.InstructionLength = 15;
    output = VmxProtocolDecide(&g_in);
    TEST_ASSERT_UINT_EQ(output.Decision, VMX_PROTO_DECISION_OFFLINE,
                        "RIP overflow decision");
    TEST_ASSERT_UINT_EQ(output.ValidationMask, 0x00DFu,
                        "RIP overflow mask");
    TEST_ASSERT_FALSE(output.Accepted, "RIP overflow rejected");
    return 0;
}
TEST_MAIN_BEGIN()
    TEST_RUN(test_probe_resume);
    TEST_RUN(test_shutdown_offline);
    TEST_RUN(test_rejects_non_vmcall);
    TEST_RUN(test_rejects_entry_failure);
    TEST_RUN(test_rejects_bad_signature);
    TEST_RUN(test_rejects_bad_cookie);
    TEST_RUN(test_rejects_zero_length);
    TEST_RUN(test_rejects_noncanonical_rip);
    TEST_RUN(test_rejects_noncanonical_next_rip);
    TEST_RUN(test_rejects_probe_at_wrong_step);
    TEST_RUN(test_rejects_shutdown_at_wrong_step);
    TEST_RUN(test_rejects_unknown_operation);
    TEST_RUN(test_protocol_exact_masks);
    TEST_RUN(test_protocol_address_masks);
TEST_MAIN_END()
