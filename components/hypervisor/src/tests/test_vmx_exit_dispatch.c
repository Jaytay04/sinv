/*
 * test_vmx_exit_dispatch.c - VM-exit dispatcher tests (R11)
 */

#include "test_common.h"
#include "vmx_exit_dispatch.h"
#include "vmx_runtime_context.h"
#include "vmx.h"

#define COOKIE 0xDEADBEEFCAFEBABEULL

/* Fake VMREAD / VMWRITE state */
static uint64_t g_fake_vmcs_reason;
static uint64_t g_fake_vmcs_qual;
static uint64_t g_fake_vmcs_rip;
static uint64_t g_fake_vmcs_len;
static int      g_write_rc;
static int      g_read_fail_index;
static size_t   g_read_calls;
static size_t   g_write_calls;
static size_t   g_read_fields[4];
static size_t   g_last_write_field;
static size_t   g_last_write_value;

static bool FakeVmread(size_t field, uint64_t* out) {
    size_t index = g_read_calls++;
    if (index < 4) g_read_fields[index] = field;
    if (g_read_fail_index != 0 && (int)(index + 1) == g_read_fail_index) return false;
    if (out == NULL) return false;
    if (field == VMCS_VM_EXIT_REASON) *out = g_fake_vmcs_reason;
    else if (field == VMCS_VM_EXIT_QUALIFICATION) *out = g_fake_vmcs_qual;
    else if (field == VMCS_GUEST_RIP) *out = g_fake_vmcs_rip;
    else if (field == VMCS_VM_EXIT_INSTRUCTION_LEN) *out = g_fake_vmcs_len;
    else *out = 0;
    return true;
}

static int FakeVmwrite(size_t field, size_t value) {
    ++g_write_calls;
    g_last_write_field = field;
    g_last_write_value = value;
    return g_write_rc;
}

static void ResetFakes(void) {
    g_fake_vmcs_reason = EXIT_REASON_VMCALL;
    g_fake_vmcs_qual = 0x123456789ULL;
    g_fake_vmcs_rip = 0xFFFFF80000005000ULL;
    g_fake_vmcs_len = 3;
    g_write_rc = VMX_VMWRITE_SUCCESS;
    g_read_fail_index = 0;
    g_read_calls = 0;
    g_write_calls = 0;
    (void)memset(g_read_fields, 0, sizeof(g_read_fields));
    g_last_write_field = 0;
    g_last_write_value = 0;
}

static void PrepareProbe(VMX_RUNTIME_LAUNCH_CONTEXT* ctx, VMX_GUEST_GPRS* gprs) {
    (void)memset(ctx, 0, sizeof(*ctx));
    (void)memset(gprs, 0, sizeof(*gprs));
    ctx->Assembly.Cookie = COOKIE;
    ctx->Assembly.ExitAction = VMX_EXIT_ACTION_INVALID;
    gprs->Rax = VMX_VMCALL_SIGNATURE;
    gprs->Rcx = VMX_PROTO_OP_PROBE;
    gprs->Rdx = COOKIE;
}

/* --- Probe success --- */
static int test_probe_dispatch_resume(void) {
    VMX_RUNTIME_LAUNCH_CONTEXT ctx;
    VMX_GUEST_GPRS gprs;
    VMX_PROTOCOL_DECISION d;

    ResetFakes();
    PrepareProbe(&ctx, &gprs);
    d = VmxLaunchDispatchExit(&ctx, &gprs, FakeVmread, FakeVmwrite);

    TEST_ASSERT_UINT_EQ((unsigned int)d,
                        (unsigned int)VMX_PROTO_DECISION_RESUME, "Probe: RESUME");
    TEST_ASSERT_UINT_EQ(g_read_calls, 4, "Probe: VMREAD count");
    TEST_ASSERT_UINT_EQ(g_write_calls, 1, "Probe: VMWRITE count");
    TEST_ASSERT_UINT_EQ(g_read_fields[0], VMCS_VM_EXIT_REASON, "Probe: first VMREAD");
    TEST_ASSERT_UINT_EQ(g_read_fields[3], VMCS_VM_EXIT_INSTRUCTION_LEN,
                        "Probe: fourth VMREAD");
    TEST_ASSERT_UINT_EQ(g_last_write_field, VMCS_GUEST_RIP, "Probe: wrote RIP");
    TEST_ASSERT_UINT_EQ(g_last_write_value,
                        g_fake_vmcs_rip + g_fake_vmcs_len, "Probe: RIP advanced");
    TEST_ASSERT_UINT_EQ(ctx.ProbeExit.Report.Reason,
                        g_fake_vmcs_reason, "Probe: reason report");
    TEST_ASSERT_UINT_EQ(ctx.ProbeExit.Report.Qualification,
                        g_fake_vmcs_qual, "Probe: qualification report");
    TEST_ASSERT_UINT_EQ(ctx.ProbeExit.Report.Rip,
                        g_fake_vmcs_rip, "Probe: RIP report");
    TEST_ASSERT_UINT_EQ(ctx.ProbeExit.Report.InstructionLength,
                        g_fake_vmcs_len, "Probe: length report");
    TEST_ASSERT_UINT_EQ(ctx.ProbeExit.Report.Rax,
                        gprs.Rax, "Probe: RAX report");
    TEST_ASSERT_UINT_EQ(ctx.ProbeExit.Report.Rcx,
                        gprs.Rcx, "Probe: RCX report");
    TEST_ASSERT_UINT_EQ(ctx.ProbeExit.Report.Rdx,
                        gprs.Rdx, "Probe: RDX report");
    TEST_ASSERT_UINT_EQ(ctx.ProbeExit.ValidationMask,
                        0x03FFu, "Probe: validation mask");
    TEST_ASSERT_TRUE(ctx.ProbeExit.Observed, "Probe: observed");
    TEST_ASSERT_UINT_EQ(ctx.ProtocolStep,
                        VMX_PROTOCOL_PROBE_COMPLETE, "Probe: completion");
    TEST_ASSERT_UINT_EQ(ctx.Assembly.ExitAction,
                        VMX_EXIT_ACTION_RESUME, "Probe: action");
    return 0;
}

/* --- Shutdown success --- */
static int test_shutdown_dispatch_offline(void) {
    VMX_RUNTIME_LAUNCH_CONTEXT ctx;
    VMX_GUEST_GPRS gprs = {0};
    VMX_PROTOCOL_DECISION d;

    ResetFakes();
    (void)memset(&ctx, 0, sizeof(ctx));
    ctx.Assembly.Cookie = COOKIE;
    ctx.Assembly.ExitAction = VMX_EXIT_ACTION_RESUME;
    gprs.Rax = VMX_VMCALL_SIGNATURE;
    gprs.Rcx = VMX_PROTO_OP_SHUTDOWN;
    gprs.Rdx = COOKIE;
    d = VmxLaunchDispatchExit(&ctx, &gprs, FakeVmread, FakeVmwrite);

    TEST_ASSERT_UINT_EQ((unsigned int)d,
                        (unsigned int)VMX_PROTO_DECISION_OFFLINE, "Shutdown: OFFLINE");
    TEST_ASSERT_UINT_EQ(g_read_calls, 4, "Shutdown: VMREAD count");
    TEST_ASSERT_UINT_EQ(g_write_calls, 0, "Shutdown: no VMWRITE");
    TEST_ASSERT_UINT_EQ(ctx.ShutdownExit.Report.Reason,
                        g_fake_vmcs_reason, "Shutdown: reason report");
    TEST_ASSERT_UINT_EQ(ctx.ShutdownExit.Report.Qualification,
                        g_fake_vmcs_qual, "Shutdown: qualification report");
    TEST_ASSERT_UINT_EQ(ctx.ShutdownExit.Report.Rip,
                        g_fake_vmcs_rip, "Shutdown: RIP report");
    TEST_ASSERT_UINT_EQ(ctx.ShutdownExit.Report.InstructionLength,
                        g_fake_vmcs_len, "Shutdown: length report");
    TEST_ASSERT_UINT_EQ(ctx.ShutdownExit.Report.Rax,
                        gprs.Rax, "Shutdown: RAX report");
    TEST_ASSERT_UINT_EQ(ctx.ShutdownExit.Report.Rcx,
                        gprs.Rcx, "Shutdown: RCX report");
    TEST_ASSERT_UINT_EQ(ctx.ShutdownExit.Report.Rdx,
                        gprs.Rdx, "Shutdown: RDX report");
    TEST_ASSERT_UINT_EQ(ctx.ShutdownExit.ValidationMask,
                        0x01FFu, "Shutdown: validation mask");
    TEST_ASSERT_TRUE(ctx.ShutdownExit.Observed, "Shutdown: observed");
    TEST_ASSERT_UINT_EQ(ctx.ProtocolStep,
                        VMX_PROTOCOL_SHUTDOWN_COMPLETE, "Shutdown: completion");
    TEST_ASSERT_UINT_EQ(ctx.Assembly.ExitAction,
                        VMX_EXIT_ACTION_OFFLINE, "Shutdown: action");
    return 0;
}

/* --- Rejections --- */
static int test_dispatch_rejects_entry_failure(void) {
    VMX_RUNTIME_LAUNCH_CONTEXT ctx;
    VMX_GUEST_GPRS gprs = {0};
    VMX_PROTOCOL_DECISION d;

    ResetFakes();
    (void)memset(&ctx, 0, sizeof(ctx));
    ctx.Assembly.Cookie = COOKIE;
    g_fake_vmcs_reason = EXIT_REASON_VMCALL | VMX_EXIT_ENTRY_FAILURE_BIT;
    d = VmxLaunchDispatchExit(&ctx, &gprs, FakeVmread, FakeVmwrite);

    TEST_ASSERT_UINT_EQ((unsigned int)d,
                        (unsigned int)VMX_PROTO_DECISION_OFFLINE, "Entry fail: OFFLINE");
    TEST_ASSERT_UINT_EQ(ctx.Assembly.ExitAction,
                        VMX_EXIT_ACTION_OFFLINE, "Entry fail: action");
    return 0;
}

static int test_dispatch_rejects_bad_cookie(void) {
    VMX_RUNTIME_LAUNCH_CONTEXT ctx;
    VMX_GUEST_GPRS gprs = {0};
    VMX_PROTOCOL_DECISION d;

    (void)memset(&ctx, 0, sizeof(ctx));
    ctx.Assembly.Cookie = COOKIE;
    gprs.Rax = VMX_VMCALL_SIGNATURE;
    gprs.Rcx = VMX_PROTO_OP_PROBE;
    gprs.Rdx = 0;
    ResetFakes();
    d = VmxLaunchDispatchExit(&ctx, &gprs, FakeVmread, FakeVmwrite);

    TEST_ASSERT_UINT_EQ((unsigned int)d,
                        (unsigned int)VMX_PROTO_DECISION_OFFLINE, "Bad cookie: OFFLINE");
    TEST_ASSERT_UINT_EQ(ctx.Assembly.ExitAction,
                        VMX_EXIT_ACTION_OFFLINE, "Bad cookie: action");
    return 0;
}

static int test_vmread_partial_matrix(void) {
    int failure;

    for (failure = 1; failure <= 4; ++failure) {
        VMX_RUNTIME_LAUNCH_CONTEXT ctx;
        VMX_GUEST_GPRS gprs;
        VMX_PROTOCOL_DECISION d;

        ResetFakes();
        PrepareProbe(&ctx, &gprs);
        g_read_fail_index = failure;
        d = VmxLaunchDispatchExit(&ctx, &gprs, FakeVmread, FakeVmwrite);

        TEST_ASSERT_UINT_EQ(d, VMX_PROTO_DECISION_OFFLINE,
                            "VMREAD failure: OFFLINE");
        TEST_ASSERT_UINT_EQ(ctx.Assembly.ExitAction,
                            VMX_EXIT_ACTION_OFFLINE, "VMREAD failure: action");
        TEST_ASSERT_UINT_EQ(g_read_calls, (size_t)failure,
                            "VMREAD failure: exact reads");
        TEST_ASSERT_UINT_EQ(g_write_calls, 0,
                            "VMREAD failure: no write");
        TEST_ASSERT_UINT_EQ(ctx.ProbeExit.ValidationMask, 0,
                            "VMREAD failure: mask");
        TEST_ASSERT_TRUE(ctx.ProbeExit.Observed,
                         "VMREAD failure: observed");
        TEST_ASSERT_UINT_EQ(ctx.ProbeExit.Report.Rax,
                            gprs.Rax, "VMREAD failure: RAX");
        TEST_ASSERT_UINT_EQ(ctx.ProbeExit.Report.Rcx,
                            gprs.Rcx, "VMREAD failure: RCX");
        TEST_ASSERT_UINT_EQ(ctx.ProbeExit.Report.Rdx,
                            gprs.Rdx, "VMREAD failure: RDX");
        if (failure > 1) {
            TEST_ASSERT_UINT_EQ(ctx.ProbeExit.Report.Reason,
                                g_fake_vmcs_reason, "VMREAD failure: reason");
        } else {
            TEST_ASSERT_UINT_EQ(ctx.ProbeExit.Report.Reason,
                                0, "VMREAD failure: no reason");
        }
        if (failure > 2) {
            TEST_ASSERT_UINT_EQ(ctx.ProbeExit.Report.Qualification,
                                g_fake_vmcs_qual, "VMREAD failure: qualification");
        } else {
            TEST_ASSERT_UINT_EQ(ctx.ProbeExit.Report.Qualification,
                                0, "VMREAD failure: no qualification");
        }
        if (failure > 3) {
            TEST_ASSERT_UINT_EQ(ctx.ProbeExit.Report.Rip,
                                g_fake_vmcs_rip, "VMREAD failure: RIP");
        } else {
            TEST_ASSERT_UINT_EQ(ctx.ProbeExit.Report.Rip,
                                0, "VMREAD failure: no RIP");
        }
        TEST_ASSERT_UINT_EQ(ctx.ProbeExit.Report.InstructionLength,
                            0, "VMREAD failure: no length");
        TEST_ASSERT_UINT_EQ(ctx.ProtocolStep,
                            VMX_PROTOCOL_NONE, "VMREAD failure: completion");
    }
    return 0;
}

static int test_raw_high_length_rejected(void) {
    VMX_RUNTIME_LAUNCH_CONTEXT ctx;
    VMX_GUEST_GPRS gprs;
    VMX_PROTOCOL_DECISION d;

    ResetFakes();
    PrepareProbe(&ctx, &gprs);
    g_fake_vmcs_len = UINT64_MAX;
    d = VmxLaunchDispatchExit(&ctx, &gprs, FakeVmread, FakeVmwrite);

    TEST_ASSERT_UINT_EQ(d, VMX_PROTO_DECISION_OFFLINE,
                        "High length: OFFLINE");
    TEST_ASSERT_UINT_EQ(ctx.Assembly.ExitAction,
                        VMX_EXIT_ACTION_OFFLINE, "High length: action");
    TEST_ASSERT_UINT_EQ(g_read_calls, 4, "High length: exact reads");
    TEST_ASSERT_UINT_EQ(g_write_calls, 0, "High length: no write");
    TEST_ASSERT_UINT_EQ(ctx.ProbeExit.ValidationMask,
                        VMX_PROTOCOL_CHECK_VMREAD, "High length: mask");
    TEST_ASSERT_TRUE(ctx.ProbeExit.Observed, "High length: observed");
    TEST_ASSERT_UINT_EQ(ctx.ProbeExit.Report.Reason,
                        g_fake_vmcs_reason, "High length: reason");
    TEST_ASSERT_UINT_EQ(ctx.ProbeExit.Report.Qualification,
                        g_fake_vmcs_qual, "High length: qualification");
    TEST_ASSERT_UINT_EQ(ctx.ProbeExit.Report.Rip,
                        g_fake_vmcs_rip, "High length: RIP");
    TEST_ASSERT_UINT_EQ(ctx.ProbeExit.Report.InstructionLength,
                        0, "High length: no narrowing");
    TEST_ASSERT_UINT_EQ(ctx.ProtocolStep,
                        VMX_PROTOCOL_NONE, "High length: completion");
    return 0;
}

static int test_vmwrite_failure_matrix(void) {
    int status[] = { VMX_VMWRITE_FAIL_VALID, VMX_VMWRITE_FAIL_INVALID };
    size_t i;

    for (i = 0; i < sizeof(status) / sizeof(status[0]); ++i) {
        VMX_RUNTIME_LAUNCH_CONTEXT ctx;
        VMX_GUEST_GPRS gprs;
        VMX_PROTOCOL_DECISION d;

        ResetFakes();
        PrepareProbe(&ctx, &gprs);
        g_write_rc = status[i];
        d = VmxLaunchDispatchExit(&ctx, &gprs, FakeVmread, FakeVmwrite);

        TEST_ASSERT_UINT_EQ(d, VMX_PROTO_DECISION_OFFLINE,
                            "VMWRITE failure: OFFLINE");
        TEST_ASSERT_UINT_EQ(ctx.Assembly.ExitAction,
                            VMX_EXIT_ACTION_OFFLINE, "VMWRITE failure: action");
        TEST_ASSERT_UINT_EQ(g_read_calls, 4,
                            "VMWRITE failure: exact reads");
        TEST_ASSERT_UINT_EQ(g_write_calls, 1,
                            "VMWRITE failure: exact writes");
        TEST_ASSERT_UINT_EQ(g_last_write_field,
                            VMCS_GUEST_RIP, "VMWRITE failure: field");
        TEST_ASSERT_UINT_EQ(ctx.ProbeExit.ValidationMask,
                            0x01FFu, "VMWRITE failure: mask");
        TEST_ASSERT_UINT_EQ(ctx.ProtocolStep,
                            VMX_PROTOCOL_NONE, "VMWRITE failure: completion");
    }
    return 0;
}

static int test_repeat_unexpected_preserves_probe(void) {
    VMX_RUNTIME_LAUNCH_CONTEXT ctx;
    VMX_GUEST_GPRS gprs;
    VMX_RUNTIME_EXIT_DIAGNOSTIC previous_probe;
    VMX_PROTOCOL_DECISION d;

    ResetFakes();
    PrepareProbe(&ctx, &gprs);
    d = VmxLaunchDispatchExit(&ctx, &gprs, FakeVmread, FakeVmwrite);
    TEST_ASSERT_UINT_EQ(d, VMX_PROTO_DECISION_RESUME,
                        "Initial probe: decision");
    TEST_ASSERT_UINT_EQ(ctx.Assembly.ExitAction,
                        VMX_EXIT_ACTION_RESUME, "Initial probe: action");
    previous_probe = ctx.ProbeExit;

    ResetFakes();
    d = VmxLaunchDispatchExit(&ctx, &gprs, FakeVmread, FakeVmwrite);
    TEST_ASSERT_UINT_EQ(d, VMX_PROTO_DECISION_OFFLINE,
                        "Repeated probe: decision");
    TEST_ASSERT_UINT_EQ(ctx.Assembly.ExitAction,
                        VMX_EXIT_ACTION_OFFLINE, "Repeated probe: action");
    TEST_ASSERT_UINT_EQ(g_read_calls, 4,
                        "Repeated probe: reads");
    TEST_ASSERT_TRUE(memcmp(&ctx.ProbeExit, &previous_probe,
                            sizeof(previous_probe)) == 0,
                     "Repeated probe: ProbeExit unchanged");

    ctx.Assembly.ExitAction = VMX_EXIT_ACTION_RESUME;
    gprs.Rcx = VMX_PROTO_OP_SHUTDOWN;
    ResetFakes();
    d = VmxLaunchDispatchExit(&ctx, &gprs, FakeVmread, FakeVmwrite);
    TEST_ASSERT_UINT_EQ(d, VMX_PROTO_DECISION_OFFLINE,
                        "Shutdown transition: decision");
    TEST_ASSERT_UINT_EQ(ctx.Assembly.ExitAction,
                        VMX_EXIT_ACTION_OFFLINE, "Shutdown transition: action");
    TEST_ASSERT_UINT_EQ(ctx.ProtocolStep,
                        VMX_PROTOCOL_SHUTDOWN_COMPLETE,
                        "Shutdown transition: completion");
    TEST_ASSERT_TRUE(memcmp(&ctx.ProbeExit, &previous_probe,
                            sizeof(previous_probe)) == 0,
                     "Shutdown transition: ProbeExit unchanged");

    ResetFakes();
    d = VmxLaunchDispatchExit(&ctx, &gprs, FakeVmread, FakeVmwrite);
    TEST_ASSERT_UINT_EQ(d, VMX_PROTO_DECISION_OFFLINE,
                        "Repeated shutdown: decision");
    TEST_ASSERT_UINT_EQ(ctx.Assembly.ExitAction,
                        VMX_EXIT_ACTION_OFFLINE, "Repeated shutdown: action");
    TEST_ASSERT_UINT_EQ(g_read_calls, 0,
                        "Repeated shutdown: no reads");
    TEST_ASSERT_TRUE(memcmp(&ctx.ProbeExit, &previous_probe,
                            sizeof(previous_probe)) == 0,
                     "Repeated shutdown: ProbeExit unchanged");
    return 0;
}

static int test_dispatch_null_context(void) {
    VMX_GUEST_GPRS gprs = {0};
    VMX_PROTOCOL_DECISION d = VmxLaunchDispatchExit(
        NULL, &gprs, FakeVmread, FakeVmwrite);
    TEST_ASSERT_UINT_EQ((unsigned int)d,
                        (unsigned int)VMX_PROTO_DECISION_OFFLINE, "Null ctx: OFFLINE");
    return 0;
}

static int test_dispatch_null_read(void) {
    VMX_RUNTIME_LAUNCH_CONTEXT ctx = {0};
    VMX_GUEST_GPRS gprs = {0};
    VMX_PROTOCOL_DECISION d = VmxLaunchDispatchExit(
        &ctx, &gprs, NULL, FakeVmwrite);
    TEST_ASSERT_UINT_EQ((unsigned int)d,
                        (unsigned int)VMX_PROTO_DECISION_OFFLINE, "Null read: OFFLINE");
    return 0;
}

TEST_MAIN_BEGIN()
    TEST_RUN(test_probe_dispatch_resume);
    TEST_RUN(test_shutdown_dispatch_offline);
    TEST_RUN(test_dispatch_rejects_entry_failure);
    TEST_RUN(test_dispatch_rejects_bad_cookie);
    TEST_RUN(test_vmread_partial_matrix);
    TEST_RUN(test_raw_high_length_rejected);
    TEST_RUN(test_vmwrite_failure_matrix);
    TEST_RUN(test_repeat_unexpected_preserves_probe);
    TEST_RUN(test_dispatch_null_context);
    TEST_RUN(test_dispatch_null_read);
TEST_MAIN_END()