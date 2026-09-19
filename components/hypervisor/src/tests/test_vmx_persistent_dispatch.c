#include "test_common.h"
#include "vmx.h"
#include "vmx_persistent_dispatch.h"
#include "vmx_persistent_protocol.h"
#include <string.h>

#define COOKIE UINT64_C(0x123456789abcdef0)
#define RIP UINT64_C(0xffff800000005000)

static VMX_V3D_PERSISTENT_IO_RESULT g_reads[6], g_write;
static uint64_t g_fields[6], g_write_field, g_write_value;
static uint64_t g_write_fields[4], g_write_values[4];
static size_t g_read_count, g_write_count;
static VMX_V3D_EPT_VIOLATION_EVENT g_ept_event;
static VMX_V3D_EPT_ROUTE_RESULT g_ept_result;
static size_t g_ept_count;
static VMX_CPUID_REGISTERS g_cpuid_output;
static uint32_t g_cpuid_leaf, g_cpuid_subleaf;
static size_t g_cpuid_count;
static int g_cpuid_success;
static unsigned char g_ept_target;

static VMX_V3D_PERSISTENT_IO_RESULT Read(void* context, uint64_t field)
{
    VMX_V3D_PERSISTENT_IO_RESULT r = { VMX_V3D_INSTRUCTION_FAIL_INVALID, 0, 0, 0 };
    size_t i = g_read_count++; (void)context;
    if (i < 6) { g_fields[i] = field; r = g_reads[i]; }
    return r;
}
static VMX_V3D_PERSISTENT_IO_RESULT Write(void* context, uint64_t field, uint64_t value)
{
    if (g_write_count < 4) {
        g_write_fields[g_write_count] = field;
        g_write_values[g_write_count] = value;
    }
    ++g_write_count; g_write_field = field; g_write_value = value; (void)context;
    return g_write;
}

static VMX_V3D_EPT_ROUTE_RESULT RouteEpt(
    void* context, const VMX_V3D_EPT_VIOLATION_EVENT* event)
{
    ++g_ept_count;
    if (context == NULL || event == NULL)
        return VMX_V3D_EPT_ROUTE_FAIL_CLOSED;
    g_ept_event = *event;
    return g_ept_result;
}
static int Cpuid(
    void* context,
    uint32_t leaf,
    uint32_t subleaf,
    VMX_CPUID_REGISTERS* out_registers)
{
    (void)context;
    ++g_cpuid_count;
    g_cpuid_leaf = leaf;
    g_cpuid_subleaf = subleaf;
    if (!g_cpuid_success || out_registers == NULL)
        return 0;
    *out_registers = g_cpuid_output;
    return 1;
}

static void Init(VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* c, VMX_V3D_GUEST_GPRS* g,
                 VMX_V3D_PERSISTENT_IO* io)
{
    size_t i;
    (void)memset(c, 0, sizeof(*c)); (void)memset(g, 0, sizeof(*g));
    c->Header.Magic = VMX_V3D_PERSISTENT_LAUNCH_MAGIC;
    c->Header.Size = VMX_V3D_CONTEXT_SIZE; c->Header.Version = VMX_V3D_PERSISTENT_LAUNCH_VERSION;
    c->ProtocolStep = VMX_V3D_PROTOCOL_INITIALIZED; c->Cookie = COOKIE;
    c->DispatcherContext = io;
    c->LaunchNativeRsp = UINT64_C(0x1111111111111111); c->ResumeNativeRsp = UINT64_C(0x2222222222222222);
    c->LaunchNativeContinuationRip = UINT64_C(0x3333333333333333); c->ResumeNativeContinuationRip = UINT64_C(0x4444444444444444);
    g->Rax = VMX_V3D_VMCALL_SIGNATURE; g->Rcx = VMX_V3D_VMCALL_READY; g->Rdx = COOKIE;
    (void)memset(g_reads, 0, sizeof(g_reads));
    g_reads[0].Value = VMX_V3D_PROTOCOL_EXIT_REASON_VMCALL;
    g_reads[1].Value = 0; g_reads[2].Value = RIP; g_reads[3].Value = 3;
    for (i = 0; i < 4; ++i) g_reads[i].Outcome = VMX_V3D_INSTRUCTION_SUCCESS;
    g_write = (VMX_V3D_PERSISTENT_IO_RESULT){ VMX_V3D_INSTRUCTION_SUCCESS, 0, 0, 0 };
    g_read_count = g_write_count = 0; g_write_field = g_write_value = 0;
    (void)memset(g_write_fields, 0, sizeof(g_write_fields));
    (void)memset(g_write_values, 0, sizeof(g_write_values));
    (void)memset(io, 0, sizeof(*io));
    io->Context = NULL; io->Vmread = Read; io->Vmwrite = Write;
    (void)memset(&g_ept_event, 0, sizeof(g_ept_event));
    g_ept_result = VMX_V3D_EPT_ROUTE_INVALID; g_ept_count = 0;
    (void)memset(&g_cpuid_output, 0, sizeof(g_cpuid_output));
    g_cpuid_leaf = g_cpuid_subleaf = 0;
    g_cpuid_count = 0;
    g_cpuid_success = 1;
}

static void ArmOwnedTarget(VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* context)
{
    context->DiagnosticTarget = (uintptr_t)context->DiagnosticPayloadStorage;
    context->DiagnosticTargetContract = ~context->DiagnosticTarget;
    context->DiagnosticTargetMode = VMX_V3D_DIAGNOSTIC_TARGET_MODE_OWNED_PAYLOAD;
    context->DiagnosticTargetModeContract =
        ~VMX_V3D_DIAGNOSTIC_TARGET_MODE_OWNED_PAYLOAD;
}

static void ArmEptTarget(VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* context)
{
    g_ept_target = 0xA5;
    context->DiagnosticTarget = (uintptr_t)&g_ept_target;
    context->DiagnosticTargetContract = ~context->DiagnosticTarget;
    context->DiagnosticTargetMode = VMX_V3D_DIAGNOSTIC_TARGET_MODE_DIRECT_EPT_PAGE;
    context->DiagnosticTargetModeContract =
        ~VMX_V3D_DIAGNOSTIC_TARGET_MODE_DIRECT_EPT_PAGE;
}

static int TestReadyShutdown(void)
{
    VMX_V3D_PERSISTENT_LAUNCH_CONTEXT c; VMX_V3D_GUEST_GPRS g, before_g; VMX_V3D_PERSISTENT_IO io;
    unsigned char frames[VMX_V3D_CONTEXT_PROTOCOL_STEP_OFFSET - VMX_V3D_CONTEXT_HOST_STACK_TOP_OFFSET];
    Init(&c, &g, &io); before_g = g;
    c.LaunchNativeRsp = UINT64_C(0xabcdef0123456789);
    (void)memcpy(frames, (const unsigned char*)&c + VMX_V3D_CONTEXT_HOST_STACK_TOP_OFFSET, sizeof(frames));
    TEST_ASSERT_UINT_EQ(VmxV3dPersistentDispatch(&c, &g, &io), VMX_V3D_ASSEMBLY_ACTION_RETURN_RESIDENT, "READY action");
    TEST_ASSERT_UINT_EQ(c.DispatchSequence, 2, "READY stable dispatch sequence");
    TEST_ASSERT_UINT_EQ(g_read_count, 4, "READY reads"); TEST_ASSERT_UINT_EQ(g_write_count, 1, "READY write");
    TEST_ASSERT_UINT_EQ(g_fields[0], VMX_V3D_DISPATCH_VMCS_EXIT_REASON, "reason field");
    TEST_ASSERT_UINT_EQ(g_fields[1], VMX_V3D_DISPATCH_VMCS_EXIT_QUALIFICATION, "qualification field");
    TEST_ASSERT_UINT_EQ(g_fields[2], VMX_V3D_DISPATCH_VMCS_GUEST_RIP, "RIP field");
    TEST_ASSERT_UINT_EQ(g_fields[3], VMX_V3D_DISPATCH_VMCS_EXIT_LENGTH, "length field");
    TEST_ASSERT_UINT_EQ(g_write_field, VMX_V3D_DISPATCH_VMCS_GUEST_RIP, "READY write field");
    TEST_ASSERT_UINT_EQ(g_write_value, RIP + 3, "READY write value");
    TEST_ASSERT_UINT_EQ(c.ProtocolStep, VMX_V3D_PROTOCOL_READY_EXIT, "READY step");
    TEST_ASSERT_UINT_EQ(c.ProtocolCode, VMX_V3D_PROTOCOL_CHECK_ALL, "READY protocol mask");
    TEST_ASSERT_UINT_EQ(c.ReadyObserved, 1, "READY observed"); TEST_ASSERT_UINT_EQ(c.ShutdownObserved, 0, "READY shutdown");
    TEST_ASSERT_UINT_EQ(c.ValidationFlags & (VMX_V3D_VALIDATION_SHUTDOWN_PROVEN | VMX_V3D_VALIDATION_RESUME_VALID | VMX_V3D_VALIDATION_RESUME_ARMED), 0, "READY no proof/resume");
    TEST_ASSERT_TRUE(memcmp(&g, &before_g, sizeof(g)) == 0, "GPR frame preserved");
    TEST_ASSERT_UINT_EQ(c.LaunchNativeRsp, UINT64_C(0xabcdef0123456789), "native frame preserved");
    TEST_ASSERT_TRUE(memcmp(frames, (const unsigned char*)&c + VMX_V3D_CONTEXT_HOST_STACK_TOP_OFFSET, sizeof(frames)) == 0, "full native region preserved");
    c.ProtocolStep = VMX_V3D_PROTOCOL_RESIDENT; c.ReadyObserved = 1; g.Rcx = VMX_V3D_VMCALL_SHUTDOWN;
    g_read_count = g_write_count = 0; TEST_ASSERT_UINT_EQ(VmxV3dPersistentDispatch(&c, &g, &io), VMX_V3D_ASSEMBLY_ACTION_SHUTDOWN_NATIVE, "SHUTDOWN action");
    TEST_ASSERT_UINT_EQ(c.DispatchSequence, 4, "SHUTDOWN stable dispatch sequence");
    TEST_ASSERT_UINT_EQ(g_write_count, 1, "SHUTDOWN write"); TEST_ASSERT_UINT_EQ(g_write_value, RIP + 3, "SHUTDOWN write value");
    TEST_ASSERT_UINT_EQ(c.ProtocolStep, VMX_V3D_PROTOCOL_SHUTDOWN_EXIT, "SHUTDOWN step");
    TEST_ASSERT_UINT_EQ(c.ReadyObserved, 1, "READY history"); TEST_ASSERT_UINT_EQ(c.ShutdownObserved, 1, "SHUTDOWN observed");
    TEST_ASSERT_UINT_EQ(c.ValidationFlags & (VMX_V3D_VALIDATION_SHUTDOWN_PROVEN | VMX_V3D_VALIDATION_RESUME_VALID | VMX_V3D_VALIDATION_RESUME_ARMED), 0, "no proof or resume");

    Init(&c, &g, &io);
    c.ProtocolStep = VMX_V3D_PROTOCOL_RESIDENT;
    c.ReadyObserved = 1;
    c.DiagnosticState = VMX_V3D_DIAGNOSTIC_STATE_COMPLETE;
    c.DiagnosticStateContract = ~VMX_V3D_DIAGNOSTIC_STATE_COMPLETE;
    c.DiagnosticObserved = 1;
    c.ValidationFlags = VMX_V3D_VALIDATION_HEADER_OK |
        VMX_V3D_VALIDATION_READY_OBSERVED |
        VMX_V3D_VALIDATION_RESUME_ARMED |
        VMX_V3D_VALIDATION_RESUME_VALID;
    g.Rcx = VMX_V3D_VMCALL_OPERATION_DIAGNOSTIC_COMPLETE;
    TEST_ASSERT_UINT_EQ(VmxV3dPersistentDispatch(&c, &g, &io),
        VMX_V3D_ASSEMBLY_ACTION_RETURN_RESIDENT_RESUME,
        "diagnostic completion action");
    TEST_ASSERT_UINT_EQ(c.ProtocolStep, VMX_V3D_PROTOCOL_DIAGNOSTIC_EXIT,
        "diagnostic completion step");
    TEST_ASSERT_UINT_EQ(g_write_count, 1, "diagnostic RIP write");
    TEST_ASSERT_UINT_EQ(c.DispatchSnapshot.AssemblyAction,
        VMX_V3D_ASSEMBLY_ACTION_RETURN_RESIDENT_RESUME,
        "diagnostic snapshot action");
    TEST_ASSERT_UINT_EQ(c.DispatchSnapshot.ProtocolStep,
        VMX_V3D_PROTOCOL_DIAGNOSTIC_EXIT,
        "diagnostic snapshot protocol");
    TEST_ASSERT_UINT_EQ(c.DispatchSnapshot.RawExitReason, 18,
        "diagnostic snapshot reason");
    TEST_ASSERT_UINT_EQ(c.DispatchSnapshot.InstructionLength, 3,
        "diagnostic snapshot length");
    TEST_ASSERT_UINT_EQ(c.DispatchSnapshot.ValidationFlags,
        VMX_V3D_VALIDATION_HEADER_OK |
        VMX_V3D_VALIDATION_READY_OBSERVED |
        VMX_V3D_VALIDATION_RESUME_ARMED |
        VMX_V3D_VALIDATION_RESUME_VALID,
        "diagnostic snapshot preserves live resume validation flags");
    c.AssemblyAction = VMX_V3D_ASSEMBLY_ACTION_RETURN_RESIDENT;
    c.ProtocolStep = VMX_V3D_PROTOCOL_RESIDENT;
    TEST_ASSERT_UINT_EQ(c.DispatchSnapshot.AssemblyAction,
        VMX_V3D_ASSEMBLY_ACTION_RETURN_RESIDENT_RESUME,
        "snapshot survives lifecycle action normalization");
    TEST_ASSERT_UINT_EQ(c.DispatchSnapshot.ProtocolStep,
        VMX_V3D_PROTOCOL_DIAGNOSTIC_EXIT,
        "snapshot survives lifecycle protocol normalization");

    Init(&c, &g, &io);
    c.ProtocolStep = VMX_V3D_PROTOCOL_RESIDENT;
    c.ReadyObserved = 1;
    c.DiagnosticState = VMX_V3D_DIAGNOSTIC_STATE_TSC_COMPLETE;
    c.DiagnosticStateContract = ~VMX_V3D_DIAGNOSTIC_STATE_TSC_COMPLETE;
    c.DiagnosticObserved = 1;
    g.Rcx = VMX_V3D_VMCALL_OPERATION_DIAGNOSTIC_COMPLETE;
    TEST_ASSERT_UINT_EQ(VmxV3dPersistentDispatch(&c, &g, &io),
        VMX_V3D_ASSEMBLY_ACTION_RETURN_RESIDENT_RESUME,
        "TSC diagnostic completion action");
    TEST_ASSERT_UINT_EQ(c.ProtocolStep, VMX_V3D_PROTOCOL_DIAGNOSTIC_EXIT,
        "TSC diagnostic completion step");
    TEST_ASSERT_UINT_EQ(g_write_count, 1, "TSC diagnostic RIP write");

    Init(&c, &g, &io);
    c.ProtocolStep = VMX_V3D_PROTOCOL_RESIDENT;
    c.ReadyObserved = 1;
    c.DiagnosticState = VMX_V3D_DIAGNOSTIC_STATE_INVLPG_COMPLETE;
    c.DiagnosticStateContract = ~VMX_V3D_DIAGNOSTIC_STATE_INVLPG_COMPLETE;
    c.DiagnosticObserved = 1;
    g.Rcx = VMX_V3D_VMCALL_OPERATION_DIAGNOSTIC_COMPLETE;
    TEST_ASSERT_UINT_EQ(VmxV3dPersistentDispatch(&c, &g, &io),
        VMX_V3D_ASSEMBLY_ACTION_RETURN_RESIDENT_RESUME,
        "INVLPG diagnostic completion action");
    TEST_ASSERT_UINT_EQ(c.ProtocolStep, VMX_V3D_PROTOCOL_DIAGNOSTIC_EXIT,
        "INVLPG diagnostic completion step");
    TEST_ASSERT_UINT_EQ(g_write_count, 1, "INVLPG diagnostic RIP write");

    Init(&c, &g, &io);
    c.ProtocolStep = VMX_V3D_PROTOCOL_RESIDENT;
    c.ReadyObserved = 1;
    c.DiagnosticState = VMX_V3D_DIAGNOSTIC_STATE_CET_VMCS_COMPLETE;
    c.DiagnosticStateContract = ~VMX_V3D_DIAGNOSTIC_STATE_CET_VMCS_COMPLETE;
    c.DiagnosticObserved = 1;
    g.Rcx = VMX_V3D_VMCALL_OPERATION_DIAGNOSTIC_COMPLETE;
    TEST_ASSERT_UINT_EQ(VmxV3dPersistentDispatch(&c, &g, &io),
        VMX_V3D_ASSEMBLY_ACTION_RETURN_RESIDENT_RESUME,
        "CET VMCS diagnostic completion action");
    TEST_ASSERT_UINT_EQ(c.ProtocolStep, VMX_V3D_PROTOCOL_DIAGNOSTIC_EXIT,
        "CET VMCS diagnostic completion step");
    TEST_ASSERT_UINT_EQ(g_write_count, 1, "CET VMCS diagnostic RIP write");

    Init(&c, &g, &io);
    c.ProtocolStep = VMX_V3D_PROTOCOL_RESIDENT;
    c.ReadyObserved = 1;
    c.DiagnosticState = VMX_V3D_DIAGNOSTIC_STATE_INVLPG_COMPLETE;
    c.DiagnosticStateContract = VMX_V3D_DIAGNOSTIC_STATE_INVLPG_COMPLETE;
    c.DiagnosticObserved = 1;
    g.Rcx = VMX_V3D_VMCALL_OPERATION_DIAGNOSTIC_COMPLETE;
    TEST_ASSERT_UINT_EQ(VmxV3dPersistentDispatch(&c, &g, &io),
        VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED,
        "INVLPG diagnostic completion contract rejected");
    TEST_ASSERT_UINT_EQ(c.FailClosedCode,
        VMX_V3D_DISPATCH_FAIL_DIAGNOSTIC,
        "INVLPG diagnostic rejection code");
    TEST_ASSERT_UINT_EQ(g_write_count, 0,
        "INVLPG diagnostic rejection no write");

    Init(&c, &g, &io);
    c.ProtocolStep = VMX_V3D_PROTOCOL_RESIDENT;
    c.ReadyObserved = 1;
    c.DiagnosticState = VMX_V3D_DIAGNOSTIC_STATE_ARMED;
    c.DiagnosticStateContract = ~VMX_V3D_DIAGNOSTIC_STATE_ARMED;
    ArmOwnedTarget(&c);
    g.Rcx = VMX_V3D_VMCALL_OPERATION_DIAGNOSTIC_COMPLETE;
    TEST_ASSERT_UINT_EQ(VmxV3dPersistentDispatch(&c, &g, &io),
        VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED,
        "incomplete diagnostic rejected");
    TEST_ASSERT_UINT_EQ(c.FailClosedCode,
        VMX_V3D_DISPATCH_FAIL_DIAGNOSTIC, "diagnostic rejection code");
    TEST_ASSERT_UINT_EQ(g_write_count, 0, "diagnostic rejection no write");
    return 0;
}

static int TestReadFailures(void)
{
    const uint64_t fields[] = { VMX_V3D_DISPATCH_VMCS_EXIT_REASON, VMX_V3D_DISPATCH_VMCS_EXIT_QUALIFICATION, VMX_V3D_DISPATCH_VMCS_GUEST_RIP, VMX_V3D_DISPATCH_VMCS_EXIT_LENGTH };
    const uint32_t outcomes[] = { VMX_V3D_INSTRUCTION_FAIL_VALID, VMX_V3D_INSTRUCTION_FAIL_INVALID };
    size_t i, j;
    for (i = 0; i < 4; ++i) for (j = 0; j < 2; ++j) {
        VMX_V3D_PERSISTENT_LAUNCH_CONTEXT c; VMX_V3D_GUEST_GPRS g; VMX_V3D_PERSISTENT_IO io;
        Init(&c, &g, &io); g_reads[0].Value = VMX_V3D_PROTOCOL_EXIT_REASON_VMCALL | (UINT64_C(1) << 32); g_reads[1].Value = UINT64_C(0xcafe); g_reads[i].Outcome = outcomes[j]; g_reads[i].ErrorValid = j == 0; g_reads[i].Error = 0x55 + (uint32_t)i;
        TEST_ASSERT_UINT_EQ(VmxV3dPersistentDispatch(&c, &g, &io), VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED, "VMREAD fail action");
        TEST_ASSERT_UINT_EQ(g_read_count, i + 1, "VMREAD stops"); TEST_ASSERT_UINT_EQ(g_write_count, 0, "VMREAD no write");
        TEST_ASSERT_UINT_EQ(c.ExitVmreadOutcome, outcomes[j], "VMREAD outcome"); TEST_ASSERT_UINT_EQ(c.ExitVmreadErrorValid, j == 0, "VMREAD error-valid");
        TEST_ASSERT_UINT_EQ(c.ExitVmreadError, 0x55 + (uint32_t)i, "VMREAD error"); TEST_ASSERT_UINT_EQ(c.FailedVmcsField, fields[i], "VMREAD failed field");
        TEST_ASSERT_UINT_EQ(c.LastExitReason, i > 0 ? VMX_V3D_PROTOCOL_EXIT_REASON_VMCALL | (UINT64_C(1) << 32) : 0, "partial reason");
        TEST_ASSERT_UINT_EQ(c.LastExitQualification, i > 1 ? UINT64_C(0xcafe) : 0, "partial qualification");
        TEST_ASSERT_UINT_EQ(c.LastExitGuestRip, i > 2 ? RIP : 0, "partial RIP");
        TEST_ASSERT_UINT_EQ(c.LastExitInstructionLength, i > 3 ? 3 : 0, "partial length");
        TEST_ASSERT_UINT_EQ(c.ProtocolCode, 0, "VMREAD protocol code"); TEST_ASSERT_UINT_EQ(c.ValidationCode, 0, "VMREAD validation code");
        TEST_ASSERT_UINT_EQ(c.FailClosedCode, VMX_V3D_DISPATCH_FAIL_VMREAD, "VMREAD fail code");
        TEST_ASSERT_UINT_EQ(c.ProtocolStep, VMX_V3D_PROTOCOL_FAILED, "VMREAD failed step");
    }
    return 0;
}

static int TestWriteAndReject(void)
{
    const uint32_t outcomes[] = { VMX_V3D_INSTRUCTION_FAIL_VALID, VMX_V3D_INSTRUCTION_FAIL_INVALID };
    size_t i;
    for (i = 0; i < 2; ++i) {
        VMX_V3D_PERSISTENT_LAUNCH_CONTEXT c; VMX_V3D_GUEST_GPRS g; VMX_V3D_PERSISTENT_IO io;
        Init(&c, &g, &io); g_write.Outcome = outcomes[i]; g_write.ErrorValid = i == 0; g_write.Error = 0x77;
        TEST_ASSERT_UINT_EQ(VmxV3dPersistentDispatch(&c, &g, &io), VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED, "VMWRITE fail action");
        TEST_ASSERT_UINT_EQ(g_read_count, 4, "VMWRITE reads"); TEST_ASSERT_UINT_EQ(g_write_count, 1, "VMWRITE count");
        TEST_ASSERT_UINT_EQ(c.GuestRipVmwriteOutcome, outcomes[i], "VMWRITE outcome"); TEST_ASSERT_UINT_EQ(c.GuestRipVmwriteErrorValid, i == 0, "VMWRITE error-valid");
        TEST_ASSERT_UINT_EQ(c.GuestRipVmwriteError, 0x77, "VMWRITE error"); TEST_ASSERT_UINT_EQ(c.FailedVmcsField, VMX_V3D_DISPATCH_VMCS_GUEST_RIP, "VMWRITE failed field");
        TEST_ASSERT_UINT_EQ(c.ProtocolCode, VMX_V3D_PROTOCOL_CHECK_ALL, "VMWRITE protocol mask"); TEST_ASSERT_UINT_EQ(c.FailClosedCode, VMX_V3D_DISPATCH_FAIL_VMWRITE, "VMWRITE fail code");
        TEST_ASSERT_UINT_EQ(c.ProtocolStep, VMX_V3D_PROTOCOL_FAILED, "VMWRITE failed step"); TEST_ASSERT_UINT_EQ(c.ReadyObserved, 0, "VMWRITE no progression");
    }
    {
        VMX_V3D_PERSISTENT_LAUNCH_CONTEXT c; VMX_V3D_GUEST_GPRS g; VMX_V3D_PERSISTENT_IO io;
        Init(&c, &g, &io); g_reads[0].Value = VMX_V3D_PROTOCOL_EXIT_REASON_VMCALL | (UINT64_C(1) << 32);
        TEST_ASSERT_UINT_EQ(VmxV3dPersistentDispatch(&c, &g, &io), VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED, "protocol reject action");
        TEST_ASSERT_UINT_EQ(g_write_count, 0, "protocol reject no write"); TEST_ASSERT_UINT_EQ(c.LastExitReason, VMX_V3D_PROTOCOL_EXIT_REASON_VMCALL | (UINT64_C(1) << 32), "raw reason"); TEST_ASSERT_UINT_EQ(c.ProtocolCode, VMX_V3D_PROTOCOL_CHECK_ALL & ~VMX_V3D_PROTOCOL_CHECK_REASON_RESERVED, "protocol reject mask");
        TEST_ASSERT_UINT_EQ(c.ValidationCode, VMX_V3D_PROTOCOL_INVALID_REASON_RESERVED, "protocol reject code"); TEST_ASSERT_UINT_EQ(c.FailClosedCode, VMX_V3D_PROTOCOL_INVALID_REASON_RESERVED, "protocol reject fail code");
    }
    return 0;
}

static int TestContextOwnership(void)
{
    VMX_V3D_PERSISTENT_LAUNCH_CONTEXT c;
    VMX_V3D_GUEST_GPRS g, before_g;
    VMX_V3D_PERSISTENT_IO io, other;
    unsigned char frames[VMX_V3D_CONTEXT_PROTOCOL_STEP_OFFSET - VMX_V3D_CONTEXT_HOST_STACK_TOP_OFFSET];

    Init(&c, &g, &io); other = io; before_g = g;
    (void)memcpy(frames, (const unsigned char*)&c + VMX_V3D_CONTEXT_HOST_STACK_TOP_OFFSET, sizeof(frames));
    TEST_ASSERT_UINT_EQ(VmxV3dPersistentDispatch(&c, &g, &other), VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED, "mismatch action");
    TEST_ASSERT_UINT_EQ(g_read_count, 0, "mismatch no read"); TEST_ASSERT_UINT_EQ(g_write_count, 0, "mismatch no write");
    TEST_ASSERT_UINT_EQ(c.FailClosedCode, VMX_V3D_DISPATCH_FAIL_INVALID_INPUT, "mismatch fail code");
    TEST_ASSERT_UINT_EQ(c.ProtocolCode, 0, "mismatch protocol code"); TEST_ASSERT_UINT_EQ(c.ValidationCode, 0, "mismatch validation code");
    TEST_ASSERT_UINT_EQ(c.FailedVmcsField, 0, "mismatch failed field");
    TEST_ASSERT_UINT_EQ(c.ExitVmreadOutcome, VMX_V3D_INSTRUCTION_NOT_ATTEMPTED, "mismatch read diagnostic");
    TEST_ASSERT_UINT_EQ(c.GuestRipVmwriteOutcome, VMX_V3D_INSTRUCTION_NOT_ATTEMPTED, "mismatch write diagnostic");
    TEST_ASSERT_UINT_EQ(c.ProtocolStep, VMX_V3D_PROTOCOL_FAILED, "mismatch failed step");
    TEST_ASSERT_UINT_EQ(c.AssemblyAction, VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED, "mismatch assembly action");
    TEST_ASSERT_TRUE((c.ValidationFlags & VMX_V3D_VALIDATION_FAIL_CLOSED) != 0, "mismatch fail-closed flag");
    TEST_ASSERT_TRUE(memcmp(&g, &before_g, sizeof(g)) == 0, "mismatch GPR preserved");
    TEST_ASSERT_TRUE(memcmp(frames, (const unsigned char*)&c + VMX_V3D_CONTEXT_HOST_STACK_TOP_OFFSET, sizeof(frames)) == 0, "mismatch native region preserved");
    TEST_ASSERT_TRUE(c.DispatcherContext == &io, "mismatch context pointer preserved");

    Init(&c, &g, &io); before_g = g;
    (void)memcpy(frames, (const unsigned char*)&c + VMX_V3D_CONTEXT_HOST_STACK_TOP_OFFSET, sizeof(frames));
    TEST_ASSERT_UINT_EQ(VmxV3dPersistentDispatch(&c, &g, NULL), VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED, "null mismatch action");
    TEST_ASSERT_UINT_EQ(g_read_count, 0, "null mismatch no read"); TEST_ASSERT_UINT_EQ(g_write_count, 0, "null mismatch no write");
    TEST_ASSERT_UINT_EQ(c.FailClosedCode, VMX_V3D_DISPATCH_FAIL_INVALID_INPUT, "null mismatch fail code");
    TEST_ASSERT_UINT_EQ(c.ProtocolCode, 0, "null mismatch protocol code"); TEST_ASSERT_UINT_EQ(c.ValidationCode, 0, "null mismatch validation code");
    TEST_ASSERT_UINT_EQ(c.FailedVmcsField, 0, "null mismatch failed field");
    TEST_ASSERT_UINT_EQ(c.ExitVmreadOutcome, VMX_V3D_INSTRUCTION_NOT_ATTEMPTED, "null mismatch read diagnostic");
    TEST_ASSERT_UINT_EQ(c.GuestRipVmwriteOutcome, VMX_V3D_INSTRUCTION_NOT_ATTEMPTED, "null mismatch write diagnostic");
    TEST_ASSERT_TRUE(memcmp(&g, &before_g, sizeof(g)) == 0, "null mismatch GPR preserved");
    TEST_ASSERT_TRUE(memcmp(frames, (const unsigned char*)&c + VMX_V3D_CONTEXT_HOST_STACK_TOP_OFFSET, sizeof(frames)) == 0, "null mismatch native region preserved");
    TEST_ASSERT_TRUE(c.DispatcherContext == &io, "null mismatch context pointer preserved");
    return 0;
}
static int TestEptViolationRoute(void)
{
    VMX_V3D_PERSISTENT_LAUNCH_CONTEXT c;
    VMX_V3D_GUEST_GPRS g, before_g;
    VMX_V3D_PERSISTENT_IO io;
    int route_context = 1;
    const uint64_t gpa = UINT64_C(0x12345000);
    const uint64_t eptp = UINT64_C(0x2345601e);
    const uint64_t qualification = UINT64_C(2) | (UINT64_C(1) << 8);

    Init(&c, &g, &io);
    c.ProtocolStep = VMX_V3D_PROTOCOL_RESIDENT;
    c.ReadyObserved = 1;
    c.AssemblyAction = VMX_V3D_ASSEMBLY_ACTION_RETURN_RESIDENT;
    c.DiagnosticState = VMX_V3D_DIAGNOSTIC_STATE_ARMED;
    c.DiagnosticStateContract = ~VMX_V3D_DIAGNOSTIC_STATE_ARMED;
    ArmEptTarget(&c);
    io.EptContext = &route_context;
    io.EptViolation = RouteEpt;
    g_ept_result = VMX_V3D_EPT_ROUTE_RESUME;
    g_reads[0].Value = VMX_V3D_DISPATCH_EXIT_REASON_EPT_VIOLATION |
        (UINT64_C(1) << 31);
    g_reads[1].Value = qualification;
    g_reads[2].Value = RIP;
    g_reads[3] = (VMX_V3D_PERSISTENT_IO_RESULT){
        VMX_V3D_INSTRUCTION_SUCCESS, 0, 0, gpa };
    g_reads[4] = (VMX_V3D_PERSISTENT_IO_RESULT){
        VMX_V3D_INSTRUCTION_SUCCESS, 0, 0, eptp };
    before_g = g;
    TEST_ASSERT_UINT_EQ(VmxV3dPersistentDispatch(&c, &g, &io),
        VMX_V3D_ASSEMBLY_ACTION_RESUME_GUEST, "EPT resume action");
    TEST_ASSERT_UINT_EQ(g_read_count, 5, "EPT exact VMREAD count");
    TEST_ASSERT_UINT_EQ(g_write_count, 0, "EPT does not advance RIP");
    TEST_ASSERT_UINT_EQ(g_ept_count, 1, "EPT route count");
    TEST_ASSERT_UINT_EQ(g_fields[3],
        VMX_V3D_DISPATCH_VMCS_GUEST_PHYSICAL_ADDRESS, "EPT GPA field");
    TEST_ASSERT_UINT_EQ(g_fields[4], VMX_V3D_DISPATCH_VMCS_EPT_POINTER,
        "EPTP field");
    TEST_ASSERT_UINT_EQ(g_ept_event.GuestPhysicalAddress, gpa, "EPT GPA");
    TEST_ASSERT_UINT_EQ(g_ept_event.EptPointer, eptp, "EPTP");
    TEST_ASSERT_UINT_EQ(g_ept_event.Qualification, qualification,
        "EPT qualification");
    TEST_ASSERT_UINT_EQ(g_ept_event.GuestRip, RIP, "EPT RIP");
    TEST_ASSERT_UINT_EQ(g_ept_event.AccessMask, 2, "EPT write access");
    TEST_ASSERT_UINT_EQ(c.ProtocolStep, VMX_V3D_PROTOCOL_RESIDENT,
        "EPT preserves resident step");
    TEST_ASSERT_TRUE(memcmp(&g, &before_g, sizeof(g)) == 0,
        "EPT GPR frame preserved");

    Init(&c, &g, &io);
    c.ProtocolStep = VMX_V3D_PROTOCOL_RESIDENT;
    c.ReadyObserved = 1;
    c.DiagnosticState = VMX_V3D_DIAGNOSTIC_STATE_ARMED;
    c.DiagnosticStateContract = ~VMX_V3D_DIAGNOSTIC_STATE_ARMED;
    ArmOwnedTarget(&c);
    io.EptContext = &route_context;
    io.EptViolation = RouteEpt;
    g_ept_result = VMX_V3D_EPT_ROUTE_FAIL_CLOSED;
    g_reads[0].Value = VMX_V3D_DISPATCH_EXIT_REASON_EPT_VIOLATION;
    g_reads[1].Value = qualification;
    g_reads[2].Value = RIP;
    g_reads[3] = (VMX_V3D_PERSISTENT_IO_RESULT){
        VMX_V3D_INSTRUCTION_SUCCESS, 0, 0, gpa };
    g_reads[4] = (VMX_V3D_PERSISTENT_IO_RESULT){
        VMX_V3D_INSTRUCTION_SUCCESS, 0, 0, eptp };
    TEST_ASSERT_UINT_EQ(VmxV3dPersistentDispatch(&c, &g, &io),
        VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED, "EPT rejected route");
    TEST_ASSERT_UINT_EQ(c.FailClosedCode, VMX_V3D_DISPATCH_FAIL_EPT_ROUTE,
        "EPT rejected code");
    TEST_ASSERT_UINT_EQ(g_write_count, 0, "EPT reject no RIP write");

    Init(&c, &g, &io);
    c.ProtocolStep = VMX_V3D_PROTOCOL_RESIDENT;
    c.ReadyObserved = 1;
    c.DiagnosticState = VMX_V3D_DIAGNOSTIC_STATE_ARMED;
    c.DiagnosticStateContract = ~VMX_V3D_DIAGNOSTIC_STATE_ARMED;
    ArmEptTarget(&c);
    c.DiagnosticTargetModeContract = 0;
    io.EptContext = &route_context;
    io.EptViolation = RouteEpt;
    g_ept_result = VMX_V3D_EPT_ROUTE_RESUME;
    g_reads[0].Value = VMX_V3D_DISPATCH_EXIT_REASON_EPT_VIOLATION;
    g_reads[1].Value = qualification;
    g_reads[2].Value = RIP;
    g_reads[3] = (VMX_V3D_PERSISTENT_IO_RESULT){
        VMX_V3D_INSTRUCTION_SUCCESS, 0, 0, gpa };
    g_reads[4] = (VMX_V3D_PERSISTENT_IO_RESULT){
        VMX_V3D_INSTRUCTION_SUCCESS, 0, 0, eptp };
    TEST_ASSERT_UINT_EQ(VmxV3dPersistentDispatch(&c, &g, &io),
        VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED,
        "corrupt EPT target mode rejected");
    TEST_ASSERT_UINT_EQ(c.FailClosedCode, VMX_V3D_DISPATCH_FAIL_EPT_ROUTE,
        "corrupt EPT target mode failure code");
    TEST_ASSERT_UINT_EQ(g_ept_count, 0,
        "corrupt EPT target mode has no callback");
    return 0;
}
static int TestCr3Route(void)
{
    const uint64_t root = UINT64_C(0x12345000);
    VMX_CR3_TARGET_SET targets = { 0 };
    VMX_CR3_RESIDENCY_STATE states[2] = { 0 };
    VMX_CR3_ROUTE_OWNER owner = { 0 };
    VMX_V3D_PERSISTENT_LAUNCH_CONTEXT c;
    VMX_V3D_GUEST_GPRS g, before_g;
    VMX_V3D_PERSISTENT_IO io;
    TEST_ASSERT_INT_EQ(VmxCr3TargetSetBuild(48, 0, &root, 1, 5, &targets),
        VMX_CR3_TARGET_OK, "CR3 target fixture");
    TEST_ASSERT_INT_EQ(VmxCr3RouteOwnerInitialize(
        &targets, states, 2, &owner), VMX_CR3_TARGET_OK, "CR3 owner fixture");

    Init(&c, &g, &io);
    c.ProtocolStep = VMX_V3D_PROTOCOL_RESIDENT;
    c.ReadyObserved = 1;
    c.AssemblyAction = VMX_V3D_ASSEMBLY_ACTION_RETURN_RESIDENT;
    g.Rbx = root;
    before_g = g;
    io.Cr3Owner = &owner;
    io.ProcessorIndex = 1;
    g_reads[0].Value = VMX_V3D_DISPATCH_EXIT_REASON_CR_ACCESS;
    g_reads[1].Value = VMX_CR3_EXIT_CR3 |
        (UINT64_C(3) << VMX_CR3_EXIT_GPR_SHIFT);
    g_reads[2].Value = RIP;
    g_reads[3].Value = 3;
    TEST_ASSERT_UINT_EQ(VmxV3dPersistentDispatch(&c, &g, &io),
        VMX_V3D_ASSEMBLY_ACTION_RESUME_GUEST, "CR3 route resumes");
    TEST_ASSERT_UINT_EQ(g_read_count, 4, "CR3 exact VMREAD count");
    TEST_ASSERT_UINT_EQ(g_write_count, 2, "CR3 exact VMWRITE count");
    TEST_ASSERT_UINT_EQ(g_write_field, VMX_V3D_DISPATCH_VMCS_GUEST_RIP,
        "CR3 route writes RIP last");
    TEST_ASSERT_UINT_EQ(g_write_value, RIP + 3, "CR3 route advances RIP");
    TEST_ASSERT_TRUE(!states[0].Resident && states[1].Resident &&
        states[1].NormalizedRoot == root, "CR3 route commits selected CPU");
    TEST_ASSERT_TRUE(memcmp(&g, &before_g, sizeof(g)) == 0,
        "CR3 route preserves GPR frame");

    Init(&c, &g, &io);
    c.ProtocolStep = VMX_V3D_PROTOCOL_RESIDENT;
    c.ReadyObserved = 1;
    g.Rbx = root;
    g_reads[0].Value = VMX_V3D_DISPATCH_EXIT_REASON_CR_ACCESS;
    g_reads[1].Value = VMX_CR3_EXIT_CR3 |
        (UINT64_C(3) << VMX_CR3_EXIT_GPR_SHIFT);
    g_reads[2].Value = RIP;
    g_reads[3].Value = 3;
    TEST_ASSERT_UINT_EQ(VmxV3dPersistentDispatch(&c, &g, &io),
        VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED, "unbound CR3 route rejected");
    TEST_ASSERT_UINT_EQ(c.FailClosedCode, VMX_V3D_DISPATCH_FAIL_CR3_ROUTE,
        "unbound CR3 route code");
    TEST_ASSERT_UINT_EQ(g_write_count, 0, "unbound CR3 route no write");

    Init(&c, &g, &io);
    c.ProtocolStep = VMX_V3D_PROTOCOL_RESIDENT;
    c.ReadyObserved = 1;
    before_g = g;
    g_reads[0].Value = VMX_V3D_DISPATCH_EXIT_REASON_CR_ACCESS;
    g_reads[1].Value = VMX_CR3_EXIT_CR3 |
        (VMX_V3D_DISPATCH_CR_MOV_FROM <<
            VMX_V3D_DISPATCH_CR_ACCESS_SHIFT) |
        (UINT64_C(13) << VMX_V3D_DISPATCH_CR_GPR_SHIFT);
    g_reads[2].Value = RIP;
    g_reads[3].Value = 3;
    g_reads[4].Outcome = VMX_V3D_INSTRUCTION_SUCCESS;
    g_reads[4].Value = root;
    TEST_ASSERT_UINT_EQ(VmxV3dPersistentDispatch(&c, &g, &io),
        VMX_V3D_ASSEMBLY_ACTION_RESUME_GUEST,
        "CR3-store route resumes");
    TEST_ASSERT_UINT_EQ(g_read_count, 5, "CR3-store reads guest CR3");
    TEST_ASSERT_UINT_EQ(g_fields[4], VMX_V3D_DISPATCH_VMCS_GUEST_CR3,
        "CR3-store guest CR3 field");
    TEST_ASSERT_UINT_EQ(g_write_count, 1, "CR3-store writes RIP once");
    TEST_ASSERT_UINT_EQ(g_write_fields[0], VMX_V3D_DISPATCH_VMCS_GUEST_RIP,
        "CR3-store writes RIP");
    TEST_ASSERT_UINT_EQ(g.R13, root, "CR3-store returns CR3 in selected GPR");
    before_g.R13 = root;
    TEST_ASSERT_TRUE(memcmp(&g, &before_g, sizeof(g)) == 0,
        "CR3-store preserves other GPRs");

    Init(&c, &g, &io);
    c.ProtocolStep = VMX_V3D_PROTOCOL_RESIDENT;
    c.ReadyObserved = 1;
    before_g = g;
    g_reads[0].Value = VMX_V3D_DISPATCH_EXIT_REASON_CR_ACCESS;
    g_reads[1].Value = VMX_CR3_EXIT_CR3 |
        (VMX_V3D_DISPATCH_CR_MOV_FROM <<
            VMX_V3D_DISPATCH_CR_ACCESS_SHIFT) |
        ((uint64_t)VMX_V3D_DISPATCH_CR_RSP_INDEX <<
            VMX_V3D_DISPATCH_CR_GPR_SHIFT);
    g_reads[2].Value = RIP;
    g_reads[3].Value = 3;
    g_reads[4].Outcome = VMX_V3D_INSTRUCTION_SUCCESS;
    g_reads[4].Value = root;
    TEST_ASSERT_UINT_EQ(VmxV3dPersistentDispatch(&c, &g, &io),
        VMX_V3D_ASSEMBLY_ACTION_RESUME_GUEST,
        "CR3-store RSP route resumes");
    TEST_ASSERT_UINT_EQ(g_write_count, 2, "CR3-store RSP exact writes");
    TEST_ASSERT_UINT_EQ(g_write_fields[0], VMX_V3D_DISPATCH_VMCS_GUEST_RSP,
        "CR3-store writes guest RSP first");
    TEST_ASSERT_UINT_EQ(g_write_values[0], root,
        "CR3-store writes CR3 value to RSP");
    TEST_ASSERT_UINT_EQ(g_write_fields[1], VMX_V3D_DISPATCH_VMCS_GUEST_RIP,
        "CR3-store RSP writes RIP last");
    TEST_ASSERT_TRUE(memcmp(&g, &before_g, sizeof(g)) == 0,
        "CR3-store RSP preserves saved GPR frame");

    Init(&c, &g, &io);
    c.ProtocolStep = VMX_V3D_PROTOCOL_RESIDENT;
    c.ReadyObserved = 1;
    before_g = g;
    g_reads[0].Value = VMX_V3D_DISPATCH_EXIT_REASON_CR_ACCESS;
    g_reads[1].Value = VMX_CR3_EXIT_CR3 |
        (VMX_V3D_DISPATCH_CR_MOV_FROM <<
            VMX_V3D_DISPATCH_CR_ACCESS_SHIFT);
    g_reads[2].Value = RIP;
    g_reads[3].Value = 3;
    g_reads[4].Outcome = VMX_V3D_INSTRUCTION_SUCCESS;
    g_reads[4].Value = root;
    g_write.Outcome = VMX_V3D_INSTRUCTION_FAIL_VALID;
    g_write.ErrorValid = 1;
    g_write.Error = UINT32_C(0x99);
    TEST_ASSERT_UINT_EQ(VmxV3dPersistentDispatch(&c, &g, &io),
        VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED,
        "CR3-store RIP failure fails closed");
    TEST_ASSERT_UINT_EQ(c.FailClosedCode,
        VMX_V3D_DISPATCH_FAIL_VMWRITE, "CR3-store RIP failure code");
    TEST_ASSERT_TRUE(memcmp(&g, &before_g, sizeof(g)) == 0,
        "CR3-store write failure preserves GPRs");

    TEST_ASSERT_INT_EQ(VmxCr3RouteOwnerReset(&owner), VMX_CR3_TARGET_OK,
        "CR3 owner reset");
    return 0;
}

static void ArmCpuid(VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* context)
{
    context->DiagnosticState = VMX_V3D_DIAGNOSTIC_STATE_CPUID_ARMED;
    context->DiagnosticStateContract =
        ~VMX_V3D_DIAGNOSTIC_STATE_CPUID_ARMED;
    context->DiagnosticObserved = 0;
    ArmOwnedTarget(context);
}
static int TestCpuidRoute(void)
{
    VMX_V3D_PERSISTENT_LAUNCH_CONTEXT c;
    VMX_V3D_GUEST_GPRS g, before_g;
    VMX_V3D_PERSISTENT_IO io;

    Init(&c, &g, &io);
    c.ProtocolStep = VMX_V3D_PROTOCOL_RESIDENT;
    c.ReadyObserved = 1;
    ArmCpuid(&c);
    c.AssemblyAction = VMX_V3D_ASSEMBLY_ACTION_RETURN_RESIDENT;
    io.Cpuid = Cpuid;
    g.Rax = UINT64_C(0xdeadbeef00000001);
    g.Rcx = UINT64_C(0xcafebabe00000000);
    g.Rbx = UINT64_C(0x1111111122222222);
    g.Rdx = UINT64_C(0x3333333344444444);
    g_cpuid_output = (VMX_CPUID_REGISTERS){
        UINT32_C(0xaaaaaaaa), UINT32_C(0xbbbbbbbb),
        UINT32_C(0xffffffff), UINT32_C(0xdddddddd)
    };
    g_reads[0].Value = VMX_V3D_DISPATCH_EXIT_REASON_CPUID;
    g_reads[1].Value = 0;
    g_reads[2].Value = RIP;
    g_reads[3].Value = VMX_CPUID_INSTRUCTION_LENGTH;

    TEST_ASSERT_UINT_EQ(VmxV3dPersistentDispatch(&c, &g, &io),
        VMX_V3D_ASSEMBLY_ACTION_RESUME_GUEST, "CPUID route resumes");
    TEST_ASSERT_UINT_EQ(g_read_count, 4, "CPUID exact VMREAD count");
    TEST_ASSERT_UINT_EQ(g_write_count, 1, "CPUID exact VMWRITE count");
    TEST_ASSERT_UINT_EQ(g_write_field, VMX_V3D_DISPATCH_VMCS_GUEST_RIP,
        "CPUID writes guest RIP");
    TEST_ASSERT_UINT_EQ(g_write_value, RIP + 2, "CPUID advances RIP");
    TEST_ASSERT_UINT_EQ(g_cpuid_count, 1, "CPUID callback count");
    TEST_ASSERT_UINT_EQ(g_cpuid_leaf, 1, "CPUID leaf truncation");
    TEST_ASSERT_UINT_EQ(g_cpuid_subleaf, 0, "CPUID subleaf truncation");
    TEST_ASSERT_UINT_EQ(g.Rax, UINT32_C(0xaaaaaaaa), "CPUID EAX");
    TEST_ASSERT_UINT_EQ(g.Rbx, UINT32_C(0xbbbbbbbb), "CPUID EBX");
    TEST_ASSERT_UINT_EQ(g.Rcx, UINT32_C(0x7fffffff),
        "CPUID hypervisor bit cleared");
    TEST_ASSERT_UINT_EQ(g.Rdx, UINT32_C(0xdddddddd), "CPUID EDX");
    TEST_ASSERT_UINT_EQ(c.ProtocolStep, VMX_V3D_PROTOCOL_RESIDENT,
        "CPUID preserves protocol step");

    Init(&c, &g, &io);
    c.ProtocolStep = VMX_V3D_PROTOCOL_RESIDENT;
    c.ReadyObserved = 1;
    ArmCpuid(&c);
    io.Cpuid = Cpuid;
    g_reads[0].Value = VMX_V3D_DISPATCH_EXIT_REASON_CPUID;
    g_reads[2].Value = RIP;
    g_reads[3].Value = VMX_CPUID_INSTRUCTION_LENGTH;
    before_g = g;
    g_cpuid_success = 0;
    TEST_ASSERT_UINT_EQ(VmxV3dPersistentDispatch(&c, &g, &io),
        VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED, "CPUID callback failure");
    TEST_ASSERT_UINT_EQ(c.FailClosedCode, VMX_V3D_DISPATCH_FAIL_CPUID_ROUTE,
        "CPUID callback failure code");
    TEST_ASSERT_UINT_EQ(g_write_count, 0, "CPUID callback failure no write");
    TEST_ASSERT_TRUE(memcmp(&g, &before_g, sizeof(g)) == 0,
        "CPUID callback failure preserves GPRs");

    Init(&c, &g, &io);
    c.ProtocolStep = VMX_V3D_PROTOCOL_RESIDENT;
    c.ReadyObserved = 1;
    ArmCpuid(&c);
    io.Cpuid = Cpuid;
    g_reads[0].Value = VMX_V3D_DISPATCH_EXIT_REASON_CPUID;
    g_reads[2].Value = RIP;
    g_reads[3].Value = VMX_CPUID_INSTRUCTION_LENGTH;
    g_cpuid_output = (VMX_CPUID_REGISTERS){ 1, 2, 3, 4 };
    g_write.Outcome = VMX_V3D_INSTRUCTION_FAIL_VALID;
    g_write.ErrorValid = 1;
    g_write.Error = UINT32_C(0x66);
    before_g = g;
    TEST_ASSERT_UINT_EQ(VmxV3dPersistentDispatch(&c, &g, &io),
        VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED, "CPUID VMWRITE failure");
    TEST_ASSERT_UINT_EQ(c.FailClosedCode, VMX_V3D_DISPATCH_FAIL_VMWRITE,
        "CPUID VMWRITE failure code");
    TEST_ASSERT_UINT_EQ(g_cpuid_count, 1, "CPUID callback before write");
    TEST_ASSERT_TRUE(memcmp(&g, &before_g, sizeof(g)) == 0,
        "CPUID write failure preserves GPRs");

    Init(&c, &g, &io);
    c.ProtocolStep = VMX_V3D_PROTOCOL_RESIDENT;
    c.ReadyObserved = 1;
    ArmCpuid(&c);
    io.Cpuid = Cpuid;
    g_reads[0].Value = VMX_V3D_DISPATCH_EXIT_REASON_CPUID;
    g_reads[2].Value = RIP;
    g_reads[3].Value = 3;
    TEST_ASSERT_UINT_EQ(VmxV3dPersistentDispatch(&c, &g, &io),
        VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED, "CPUID length rejected");
    TEST_ASSERT_UINT_EQ(c.FailClosedCode, VMX_V3D_DISPATCH_FAIL_CPUID_ROUTE,
        "CPUID length failure code");
    TEST_ASSERT_UINT_EQ(g_cpuid_count, 0, "CPUID length no callback");
    TEST_ASSERT_UINT_EQ(g_write_count, 0, "CPUID length no write");

    Init(&c, &g, &io);
    c.ProtocolStep = VMX_V3D_PROTOCOL_RESIDENT;
    c.ReadyObserved = 1;
    ArmCpuid(&c);
    g_reads[0].Value = VMX_V3D_DISPATCH_EXIT_REASON_CPUID;
    g_reads[2].Value = RIP;
    g_reads[3].Value = VMX_CPUID_INSTRUCTION_LENGTH;
    TEST_ASSERT_UINT_EQ(VmxV3dPersistentDispatch(&c, &g, &io),
        VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED, "unbound CPUID rejected");
    TEST_ASSERT_UINT_EQ(c.FailClosedCode, VMX_V3D_DISPATCH_FAIL_CPUID_ROUTE,
        "unbound CPUID code");

    Init(&c, &g, &io);
    c.ProtocolStep = VMX_V3D_PROTOCOL_RESIDENT;
    c.ReadyObserved = 1;
    ArmCpuid(&c);
    c.DiagnosticTarget = UINT64_C(0xffff800000001000);
    c.DiagnosticTargetContract = ~c.DiagnosticTarget;
    c.DiagnosticTargetMode = VMX_V3D_DIAGNOSTIC_TARGET_MODE_OWNED_PAYLOAD;
    c.DiagnosticTargetModeContract =
        ~VMX_V3D_DIAGNOSTIC_TARGET_MODE_OWNED_PAYLOAD;
    io.Cpuid = Cpuid;
    g_reads[0].Value = VMX_V3D_DISPATCH_EXIT_REASON_CPUID;
    g_reads[2].Value = RIP;
    g_reads[3].Value = VMX_CPUID_INSTRUCTION_LENGTH;
    TEST_ASSERT_UINT_EQ(VmxV3dPersistentDispatch(&c, &g, &io),
        VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED,
        "caller-owned CPUID target rejected");
    TEST_ASSERT_UINT_EQ(g_cpuid_count, 0,
        "caller-owned CPUID target has no callback");

    Init(&c, &g, &io);
    c.ProtocolStep = VMX_V3D_PROTOCOL_RESIDENT;
    c.ReadyObserved = 1;
    ArmCpuid(&c);
    c.DiagnosticTargetMode = VMX_V3D_DIAGNOSTIC_TARGET_MODE_DIRECT_EPT_PAGE;
    c.DiagnosticTargetModeContract =
        ~VMX_V3D_DIAGNOSTIC_TARGET_MODE_DIRECT_EPT_PAGE;
    io.Cpuid = Cpuid;
    g_reads[0].Value = VMX_V3D_DISPATCH_EXIT_REASON_CPUID;
    g_reads[2].Value = RIP;
    g_reads[3].Value = VMX_CPUID_INSTRUCTION_LENGTH;
    TEST_ASSERT_UINT_EQ(VmxV3dPersistentDispatch(&c, &g, &io),
        VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED,
        "wrong CPUID target mode rejected");
    TEST_ASSERT_UINT_EQ(c.FailClosedCode, VMX_V3D_DISPATCH_FAIL_CPUID_ROUTE,
        "wrong CPUID target mode failure code");
    TEST_ASSERT_UINT_EQ(g_cpuid_count, 0,
        "wrong CPUID target mode has no callback");
    return 0;
}

static void ArmMsrRead(VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* context)
{
    context->DiagnosticState = VMX_V3D_DIAGNOSTIC_STATE_MSR_READ_ARMED;
    context->DiagnosticStateContract =
        ~VMX_V3D_DIAGNOSTIC_STATE_MSR_READ_ARMED;
    context->DiagnosticObserved = 0;
    ArmOwnedTarget(context);
}

static int TestMsrReadRoute(void)
{
    VMX_V3D_PERSISTENT_LAUNCH_CONTEXT c;
    VMX_V3D_GUEST_GPRS g, before_g;
    VMX_V3D_PERSISTENT_IO io;

    Init(&c, &g, &io);
    c.ProtocolStep = VMX_V3D_PROTOCOL_RESIDENT;
    c.ReadyObserved = 1;
    ArmMsrRead(&c);
    g.Rcx = IA32_FEATURE_CONTROL;
    g.Rax = UINT64_C(0xaaaaaaaaaaaaaaaa);
    g.Rdx = UINT64_C(0xbbbbbbbbbbbbbbbb);
    g_reads[0].Value = VMX_V3D_DISPATCH_EXIT_REASON_MSR_READ;
    g_reads[1].Value = 0;
    g_reads[2].Value = RIP;
    g_reads[3].Value = VMX_MSR_READ_INSTRUCTION_LENGTH;

    TEST_ASSERT_UINT_EQ(VmxV3dPersistentDispatch(&c, &g, &io),
        VMX_V3D_ASSEMBLY_ACTION_RESUME_GUEST, "MSR read route resumes");
    TEST_ASSERT_UINT_EQ(g_read_count, 4, "MSR read exact VMREAD count");
    TEST_ASSERT_UINT_EQ(g_write_count, 1, "MSR read exact VMWRITE count");
    TEST_ASSERT_UINT_EQ(g_write_field, VMX_V3D_DISPATCH_VMCS_GUEST_RIP,
        "MSR read writes guest RIP");
    TEST_ASSERT_UINT_EQ(g_write_value, RIP + 2, "MSR read advances RIP");
    TEST_ASSERT_UINT_EQ(g.Rax, FEATURE_CONTROL_LOCK_BIT,
        "MSR read low result");
    TEST_ASSERT_UINT_EQ(g.Rdx, 0, "MSR read high result");
    TEST_ASSERT_UINT_EQ(c.ProtocolStep, VMX_V3D_PROTOCOL_RESIDENT,
        "MSR read preserves protocol step");

    Init(&c, &g, &io);
    c.ProtocolStep = VMX_V3D_PROTOCOL_RESIDENT;
    c.ReadyObserved = 1;
    ArmMsrRead(&c);
    g.Rcx = IA32_EFER;
    g.Rax = UINT64_C(0x1111111122222222);
    g.Rdx = UINT64_C(0x3333333344444444);
    g_reads[0].Value = VMX_V3D_DISPATCH_EXIT_REASON_MSR_READ;
    g_reads[2].Value = RIP;
    g_reads[3].Value = VMX_MSR_READ_INSTRUCTION_LENGTH;
    before_g = g;
    TEST_ASSERT_UINT_EQ(VmxV3dPersistentDispatch(&c, &g, &io),
        VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED,
        "unsupported MSR read fails closed");
    TEST_ASSERT_UINT_EQ(c.FailClosedCode,
        VMX_V3D_DISPATCH_FAIL_MSR_READ_ROUTE,
        "unsupported MSR read code");
    TEST_ASSERT_UINT_EQ(g_write_count, 0, "unsupported MSR no write");
    TEST_ASSERT_TRUE(memcmp(&g, &before_g, sizeof(g)) == 0,
        "unsupported MSR preserves GPRs");

    Init(&c, &g, &io);
    c.ProtocolStep = VMX_V3D_PROTOCOL_RESIDENT;
    c.ReadyObserved = 1;
    ArmMsrRead(&c);
    g.Rcx = IA32_VMX_BASIC;
    g_reads[0].Value = VMX_V3D_DISPATCH_EXIT_REASON_MSR_READ;
    g_reads[2].Value = RIP;
    g_reads[3].Value = 3;
    TEST_ASSERT_UINT_EQ(VmxV3dPersistentDispatch(&c, &g, &io),
        VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED,
        "MSR read length rejected");
    TEST_ASSERT_UINT_EQ(c.FailClosedCode,
        VMX_V3D_DISPATCH_FAIL_MSR_READ_ROUTE,
        "MSR read length code");
    TEST_ASSERT_UINT_EQ(g_write_count, 0, "MSR read length no write");

    Init(&c, &g, &io);
    c.ProtocolStep = VMX_V3D_PROTOCOL_RESIDENT;
    c.ReadyObserved = 1;
    ArmMsrRead(&c);
    g.Rcx = IA32_U_CET;
    g_reads[0].Value = VMX_V3D_DISPATCH_EXIT_REASON_MSR_READ;
    g_reads[2].Value = RIP;
    g_reads[3].Value = VMX_MSR_READ_INSTRUCTION_LENGTH;
    g_write.Outcome = VMX_V3D_INSTRUCTION_FAIL_VALID;
    g_write.ErrorValid = 1;
    g_write.Error = UINT32_C(0x77);
    before_g = g;
    TEST_ASSERT_UINT_EQ(VmxV3dPersistentDispatch(&c, &g, &io),
        VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED,
        "MSR read VMWRITE failure");
    TEST_ASSERT_UINT_EQ(c.FailClosedCode, VMX_V3D_DISPATCH_FAIL_VMWRITE,
        "MSR read VMWRITE failure code");
    TEST_ASSERT_TRUE(memcmp(&g, &before_g, sizeof(g)) == 0,
        "MSR read write failure preserves GPRs");
    return 0;
}

static int TestMsrWriteRoute(void)
{
    VMX_V3D_PERSISTENT_LAUNCH_CONTEXT c;
    VMX_V3D_GUEST_GPRS g, before_g;
    VMX_V3D_PERSISTENT_IO io;

    Init(&c, &g, &io);
    c.ProtocolStep = VMX_V3D_PROTOCOL_RESIDENT;
    c.ReadyObserved = 1;
    ArmMsrRead(&c);
    g.Rcx = IA32_U_CET;
    g.Rax = UINT64_C(0xaaaaaaaa01234567);
    g.Rdx = UINT64_C(0xbbbbbbbb89abcdef);
    g_reads[0].Value = VMX_V3D_DISPATCH_EXIT_REASON_MSR_WRITE;
    g_reads[1].Value = 0;
    g_reads[2].Value = RIP;
    g_reads[3].Value = VMX_MSR_WRITE_INSTRUCTION_LENGTH;
    before_g = g;

    TEST_ASSERT_UINT_EQ(VmxV3dPersistentDispatch(&c, &g, &io),
        VMX_V3D_ASSEMBLY_ACTION_RESUME_GUEST, "MSR write route resumes");
    TEST_ASSERT_UINT_EQ(g_read_count, 4, "MSR write exact VMREAD count");
    TEST_ASSERT_UINT_EQ(g_write_count, 1, "MSR write exact VMWRITE count");
    TEST_ASSERT_UINT_EQ(g_write_field, VMX_V3D_DISPATCH_VMCS_GUEST_RIP,
        "MSR write writes guest RIP");
    TEST_ASSERT_UINT_EQ(g_write_value, RIP + 2, "MSR write advances RIP");
    TEST_ASSERT_TRUE(memcmp(&g, &before_g, sizeof(g)) == 0,
        "MSR write preserves every guest GPR");
    TEST_ASSERT_UINT_EQ(c.ProtocolStep, VMX_V3D_PROTOCOL_RESIDENT,
        "MSR write preserves protocol step");

    Init(&c, &g, &io);
    c.ProtocolStep = VMX_V3D_PROTOCOL_RESIDENT;
    c.ReadyObserved = 1;
    ArmMsrRead(&c);
    g.Rcx = IA32_EFER;
    g.Rax = UINT64_C(0x1111111122222222);
    g.Rdx = UINT64_C(0x3333333344444444);
    g_reads[0].Value = VMX_V3D_DISPATCH_EXIT_REASON_MSR_WRITE;
    g_reads[2].Value = RIP;
    g_reads[3].Value = VMX_MSR_WRITE_INSTRUCTION_LENGTH;
    before_g = g;
    TEST_ASSERT_UINT_EQ(VmxV3dPersistentDispatch(&c, &g, &io),
        VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED,
        "unsupported MSR write fails closed");
    TEST_ASSERT_UINT_EQ(c.FailClosedCode,
        VMX_V3D_DISPATCH_FAIL_MSR_WRITE_ROUTE,
        "unsupported MSR write code");
    TEST_ASSERT_UINT_EQ(g_write_count, 0, "unsupported MSR write no write");
    TEST_ASSERT_TRUE(memcmp(&g, &before_g, sizeof(g)) == 0,
        "unsupported MSR write preserves GPRs");

    Init(&c, &g, &io);
    c.ProtocolStep = VMX_V3D_PROTOCOL_RESIDENT;
    c.ReadyObserved = 1;
    ArmMsrRead(&c);
    g.Rcx = IA32_U_CET;
    g_reads[0].Value = VMX_V3D_DISPATCH_EXIT_REASON_MSR_WRITE;
    g_reads[2].Value = RIP;
    g_reads[3].Value = 3;
    TEST_ASSERT_UINT_EQ(VmxV3dPersistentDispatch(&c, &g, &io),
        VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED,
        "MSR write length rejected");
    TEST_ASSERT_UINT_EQ(c.FailClosedCode,
        VMX_V3D_DISPATCH_FAIL_MSR_WRITE_ROUTE,
        "MSR write length code");
    TEST_ASSERT_UINT_EQ(g_write_count, 0, "MSR write length no write");

    Init(&c, &g, &io);
    c.ProtocolStep = VMX_V3D_PROTOCOL_RESIDENT;
    c.ReadyObserved = 1;
    ArmMsrRead(&c);
    g.Rcx = IA32_U_CET;
    g_reads[0].Value = VMX_V3D_DISPATCH_EXIT_REASON_MSR_WRITE;
    g_reads[2].Value = RIP;
    g_reads[3].Value = VMX_MSR_WRITE_INSTRUCTION_LENGTH;
    g_write.Outcome = VMX_V3D_INSTRUCTION_FAIL_VALID;
    g_write.ErrorValid = 1;
    g_write.Error = UINT32_C(0x88);
    before_g = g;
    TEST_ASSERT_UINT_EQ(VmxV3dPersistentDispatch(&c, &g, &io),
        VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED,
        "MSR write VMWRITE failure");
    TEST_ASSERT_UINT_EQ(c.FailClosedCode, VMX_V3D_DISPATCH_FAIL_VMWRITE,
        "MSR write VMWRITE failure code");
    TEST_ASSERT_TRUE(memcmp(&g, &before_g, sizeof(g)) == 0,
        "MSR write failure preserves GPRs");
    return 0;
}

static int TestInvalidInputs(void)
{
    VMX_V3D_PERSISTENT_LAUNCH_CONTEXT c, before; VMX_V3D_GUEST_GPRS g; VMX_V3D_PERSISTENT_IO io;
    Init(&c, &g, &io); c.Header.Magic = 0; before = c;
    TEST_ASSERT_UINT_EQ(VmxV3dPersistentDispatch(&c, &g, &io), VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED, "bad header action");
    TEST_ASSERT_TRUE(memcmp(&c, &before, sizeof(c)) == 0, "bad header unchanged"); TEST_ASSERT_UINT_EQ(g_read_count, 0, "bad header no read");
    Init(&c, &g, &io); TEST_ASSERT_UINT_EQ(VmxV3dPersistentDispatch(&c, NULL, &io), VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED, "null GPR action");
    TEST_ASSERT_UINT_EQ(c.DispatchSequence, 2, "failure stable dispatch sequence");
    TEST_ASSERT_UINT_EQ(c.FailClosedCode, VMX_V3D_DISPATCH_FAIL_INVALID_INPUT, "null GPR code"); TEST_ASSERT_UINT_EQ(g_read_count, 0, "null GPR no read"); TEST_ASSERT_UINT_EQ(c.ProtocolCode, 0, "null GPR protocol"); TEST_ASSERT_UINT_EQ(c.ValidationCode, 0, "null GPR validation");
    Init(&c, &g, &io); io.Vmread = NULL; TEST_ASSERT_UINT_EQ(VmxV3dPersistentDispatch(&c, &g, &io), VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED, "missing read action");
    TEST_ASSERT_UINT_EQ(c.FailClosedCode, VMX_V3D_DISPATCH_FAIL_INVALID_INPUT, "missing read code"); TEST_ASSERT_UINT_EQ(g_read_count, 0, "missing read no I/O"); TEST_ASSERT_UINT_EQ(c.ProtocolCode, 0, "missing read protocol"); TEST_ASSERT_UINT_EQ(c.ValidationCode, 0, "missing read validation");
    Init(&c, &g, &io); TEST_ASSERT_UINT_EQ(VmxV3dPersistentDispatch(&c, &g, NULL), VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED, "null seam action");
    TEST_ASSERT_UINT_EQ(c.FailClosedCode, VMX_V3D_DISPATCH_FAIL_INVALID_INPUT, "null seam code"); TEST_ASSERT_UINT_EQ(g_read_count, 0, "null seam no I/O"); TEST_ASSERT_UINT_EQ(c.ProtocolCode, 0, "null seam protocol"); TEST_ASSERT_UINT_EQ(c.ValidationCode, 0, "null seam validation");
    Init(&c, &g, &io); io.Vmwrite = NULL; TEST_ASSERT_UINT_EQ(VmxV3dPersistentDispatch(&c, &g, &io), VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED, "missing write action");
    TEST_ASSERT_UINT_EQ(c.FailClosedCode, VMX_V3D_DISPATCH_FAIL_INVALID_INPUT, "missing write code"); TEST_ASSERT_UINT_EQ(g_read_count, 0, "missing write no I/O"); TEST_ASSERT_UINT_EQ(c.ProtocolCode, 0, "missing write protocol"); TEST_ASSERT_UINT_EQ(c.ValidationCode, 0, "missing write validation");
    TEST_ASSERT_UINT_EQ(VmxV3dPersistentDispatch(NULL, &g, &io), VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED, "null context action");
    return 0;
}

static VMX_V3D_HLT_PROBE_PAYLOAD* ArmHlt(
    VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* context)
{
    VMX_V3D_HLT_PROBE_PAYLOAD* payload =
        (VMX_V3D_HLT_PROBE_PAYLOAD*)context->DiagnosticPayloadStorage;
    (void)memset(payload, 0, sizeof(*payload));
    payload->Magic = VMX_V3D_HLT_PROBE_PAYLOAD_MAGIC;
    payload->Size = VMX_V3D_HLT_PROBE_PAYLOAD_SIZE;
    payload->Version = VMX_V3D_HLT_PROBE_PAYLOAD_VERSION;
    payload->PrimaryControlsBefore = UINT32_C(0x80000000);
    payload->PrimaryControlsArmed =
        payload->PrimaryControlsBefore | UINT32_C(0x80);
    context->ProtocolStep = VMX_V3D_PROTOCOL_RESIDENT;
    context->ReadyObserved = 1;
    context->DiagnosticState = VMX_V3D_DIAGNOSTIC_STATE_HLT_ARMED;
    context->DiagnosticStateContract = ~VMX_V3D_DIAGNOSTIC_STATE_HLT_ARMED;
    context->DiagnosticTarget = (uintptr_t)payload;
    context->DiagnosticTargetContract = ~(uintptr_t)payload;
    context->DiagnosticTargetMode = VMX_V3D_DIAGNOSTIC_TARGET_MODE_OWNED_PAYLOAD;
    context->DiagnosticTargetModeContract =
        ~VMX_V3D_DIAGNOSTIC_TARGET_MODE_OWNED_PAYLOAD;
    return payload;
}

static int TestHltRoute(void)
{
    VMX_V3D_PERSISTENT_LAUNCH_CONTEXT c;
    VMX_V3D_GUEST_GPRS g, before_g;
    VMX_V3D_PERSISTENT_IO io;
    VMX_V3D_HLT_PROBE_PAYLOAD* payload;

    Init(&c, &g, &io);
    payload = ArmHlt(&c);
    g_reads[0].Value = VMX_V3D_DISPATCH_EXIT_REASON_HLT;
    g_reads[1].Value = 0;
    g_reads[2].Value = RIP;
    g_reads[3].Value = 1;
    before_g = g;
    TEST_ASSERT_UINT_EQ(VmxV3dPersistentDispatch(&c, &g, &io),
        VMX_V3D_ASSEMBLY_ACTION_RESUME_GUEST, "HLT route resumes");
    TEST_ASSERT_UINT_EQ(g_write_count, 1, "HLT route write count");
    TEST_ASSERT_UINT_EQ(g_write_field, VMX_V3D_DISPATCH_VMCS_GUEST_RIP,
        "HLT route write field");
    TEST_ASSERT_UINT_EQ(g_write_value, RIP + 1, "HLT route advances RIP");
    TEST_ASSERT_UINT_EQ(payload->ExitReason, VMX_V3D_DISPATCH_EXIT_REASON_HLT,
        "HLT route records reason");
    TEST_ASSERT_UINT_EQ(payload->ExitInstructionLength, 1,
        "HLT route records length");
    TEST_ASSERT_TRUE(memcmp(&g, &before_g, sizeof(g)) == 0,
        "HLT route preserves GPRs");

    Init(&c, &g, &io);
    payload = ArmHlt(&c);
    g_reads[0].Value = VMX_V3D_DISPATCH_EXIT_REASON_HLT;
    g_reads[1].Value = 1;
    g_reads[2].Value = RIP;
    g_reads[3].Value = 1;
    TEST_ASSERT_UINT_EQ(VmxV3dPersistentDispatch(&c, &g, &io),
        VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED,
        "HLT qualification rejected");
    TEST_ASSERT_UINT_EQ(c.FailClosedCode, VMX_V3D_DISPATCH_FAIL_HLT_ROUTE,
        "HLT qualification code");
    TEST_ASSERT_UINT_EQ(g_write_count, 0, "HLT reject no write");

    Init(&c, &g, &io);
    payload = ArmHlt(&c);
    g_reads[0].Value = VMX_V3D_DISPATCH_EXIT_REASON_HLT;
    g_reads[1].Value = 0;
    g_reads[2].Value = RIP;
    g_reads[3].Value = 2;
    TEST_ASSERT_UINT_EQ(VmxV3dPersistentDispatch(&c, &g, &io),
        VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED, "HLT length rejected");
    TEST_ASSERT_UINT_EQ(c.FailClosedCode, VMX_V3D_DISPATCH_FAIL_HLT_ROUTE,
        "HLT length code");

    Init(&c, &g, &io);
    payload = ArmHlt(&c);
    g_reads[0].Value = VMX_V3D_DISPATCH_EXIT_REASON_HLT;
    g_reads[1].Value = 0;
    g_reads[2].Value = RIP;
    g_reads[3].Value = 1;
    g_write.Outcome = VMX_V3D_INSTRUCTION_FAIL_VALID;
    g_write.ErrorValid = 1;
    g_write.Error = UINT32_C(0x91);
    TEST_ASSERT_UINT_EQ(VmxV3dPersistentDispatch(&c, &g, &io),
        VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED, "HLT VMWRITE failure");
    TEST_ASSERT_UINT_EQ(c.FailClosedCode, VMX_V3D_DISPATCH_FAIL_VMWRITE,
        "HLT VMWRITE failure code");
    TEST_ASSERT_UINT_EQ(payload->ExitReason, 0,
        "HLT failed write does not publish reason");
    return 0;
}

static int TestPreemptionTimerFailsClosed(void)
{
    VMX_V3D_PERSISTENT_LAUNCH_CONTEXT c;
    VMX_V3D_GUEST_GPRS g;
    VMX_V3D_PERSISTENT_IO io;

    Init(&c, &g, &io);
    c.ProtocolStep = VMX_V3D_PROTOCOL_RESIDENT;
    c.ReadyObserved = 1;
    g_reads[0].Value = VMX_V3D_DISPATCH_EXIT_REASON_PREEMPTION_TIMER;
    g_reads[1].Value = 0;
    g_reads[2].Value = RIP;
    TEST_ASSERT_UINT_EQ(VmxV3dPersistentDispatch(&c, &g, &io),
        VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED,
        "preemption timer expiry fails closed");
    TEST_ASSERT_UINT_EQ(c.FailClosedCode,
        VMX_V3D_DISPATCH_FAIL_PREEMPTION_TIMER,
        "preemption timer failure code");
    TEST_ASSERT_UINT_EQ(c.LastExitReason,
        VMX_V3D_DISPATCH_EXIT_REASON_PREEMPTION_TIMER,
        "preemption timer reason retained");
    TEST_ASSERT_UINT_EQ(g_read_count, 3, "timer expiry bounded reads");
    TEST_ASSERT_UINT_EQ(g_write_count, 0, "timer expiry performs no writes");
    return 0;
}

TEST_MAIN_BEGIN()
    TEST_RUN(TestReadyShutdown); TEST_RUN(TestReadFailures); TEST_RUN(TestWriteAndReject); TEST_RUN(TestContextOwnership); TEST_RUN(TestEptViolationRoute); TEST_RUN(TestCr3Route); TEST_RUN(TestCpuidRoute); TEST_RUN(TestMsrReadRoute); TEST_RUN(TestMsrWriteRoute); TEST_RUN(TestHltRoute); TEST_RUN(TestPreemptionTimerFailsClosed); TEST_RUN(TestInvalidInputs);
TEST_MAIN_END()
