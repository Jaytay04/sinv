#include "test_common.h"
#include "vmx_persistent_protocol.h"
#include <string.h>

#define COOKIE UINT64_C(0x123456789abcdef0)
#define RIP UINT64_C(0xffff800000005000)

static VMX_V3D_PROTOCOL_INPUT Valid(void)
{
    VMX_V3D_PROTOCOL_INPUT in = { VMX_V3D_PROTOCOL_EXIT_REASON_VMCALL, 0, RIP, 3,
        VMX_V3D_VMCALL_SIGNATURE, VMX_V3D_VMCALL_READY, COOKIE, COOKIE,
        VMX_V3D_PROTOCOL_INITIALIZED };
    return in;
}

static int TestSuccess(void)
{
    VMX_V3D_PROTOCOL_INPUT in = Valid();
    VMX_V3D_PROTOCOL_DECISION out = { 0 };
    TEST_ASSERT_TRUE(VmxV3dProtocolDecide(&in, &out), "READY evaluated");
    TEST_ASSERT_UINT_EQ(out.NextProtocolStep, VMX_V3D_PROTOCOL_READY_EXIT, "READY step");
    TEST_ASSERT_UINT_EQ(out.AssemblyAction, VMX_V3D_ASSEMBLY_ACTION_RETURN_RESIDENT, "READY action");
    TEST_ASSERT_UINT_EQ(out.AdvancedRip, RIP + 3, "READY RIP");
    TEST_ASSERT_UINT_EQ(out.ValidationMask, VMX_V3D_PROTOCOL_CHECK_ALL, "READY mask");
    TEST_ASSERT_UINT_EQ(out.ValidationCode, VMX_V3D_PROTOCOL_VALIDATION_OK, "READY code");
    TEST_ASSERT_UINT_EQ(out.ReadyObserved, 1, "READY observed");
    TEST_ASSERT_UINT_EQ(out.ShutdownObserved, 0, "READY shutdown flag");
    TEST_ASSERT_TRUE(out.AssemblyAction != VMX_V3D_ASSEMBLY_ACTION_RESUME_GUEST, "READY never resumes guest");
    in.GuestRcx = VMX_V3D_VMCALL_SHUTDOWN; in.CurrentProtocolStep = VMX_V3D_PROTOCOL_RESIDENT;
    memset(&out, 0, sizeof(out)); TEST_ASSERT_TRUE(VmxV3dProtocolDecide(&in, &out), "SHUTDOWN evaluated");
    TEST_ASSERT_UINT_EQ(out.NextProtocolStep, VMX_V3D_PROTOCOL_SHUTDOWN_EXIT, "SHUTDOWN step");
    TEST_ASSERT_UINT_EQ(out.AssemblyAction, VMX_V3D_ASSEMBLY_ACTION_SHUTDOWN_NATIVE, "SHUTDOWN action");
    TEST_ASSERT_UINT_EQ(out.AdvancedRip, RIP + 3, "SHUTDOWN RIP");
    TEST_ASSERT_UINT_EQ(out.ShutdownObserved, 1, "SHUTDOWN observed");
    TEST_ASSERT_UINT_EQ(out.ReadyObserved, 0, "SHUTDOWN READY flag");
    TEST_ASSERT_TRUE(out.AssemblyAction != VMX_V3D_ASSEMBLY_ACTION_RESUME_GUEST, "SHUTDOWN never resumes guest");
    in.GuestRcx = VMX_V3D_VMCALL_OPERATION_DIAGNOSTIC_COMPLETE;
    in.CurrentProtocolStep = VMX_V3D_PROTOCOL_RESIDENT;
    memset(&out, 0, sizeof(out));
    TEST_ASSERT_TRUE(VmxV3dProtocolDecide(&in, &out), "diagnostic evaluated");
    TEST_ASSERT_UINT_EQ(out.NextProtocolStep,
        VMX_V3D_PROTOCOL_DIAGNOSTIC_EXIT, "diagnostic step");
    TEST_ASSERT_UINT_EQ(out.AssemblyAction,
        VMX_V3D_ASSEMBLY_ACTION_RETURN_RESIDENT_RESUME,
        "diagnostic return action");
    TEST_ASSERT_UINT_EQ(out.AdvancedRip, RIP + 3, "diagnostic RIP");
    TEST_ASSERT_UINT_EQ(out.ValidationMask, VMX_V3D_PROTOCOL_CHECK_ALL,
        "diagnostic mask");
    TEST_ASSERT_UINT_EQ(out.ReadyObserved, 0, "diagnostic READY flag");
    TEST_ASSERT_UINT_EQ(out.ShutdownObserved, 0, "diagnostic shutdown flag");
    return 0;
}

static int TestNullPreserves(void)
{
    VMX_V3D_PROTOCOL_INPUT in = Valid();
    VMX_V3D_PROTOCOL_DECISION out, before;
    memset(&out, 0xa5, sizeof(out)); before = out;
    TEST_ASSERT_FALSE(VmxV3dProtocolDecide(NULL, &out), "null input rejected");
    TEST_ASSERT_TRUE(memcmp(&out, &before, sizeof(out)) == 0, "null input preserves output");
    TEST_ASSERT_FALSE(VmxV3dProtocolDecide(&in, NULL), "null output rejected");
    return 0;
}

static int TestRejections(void)
{
    struct Case { uint64_t* field; uint64_t value; uint32_t missing; uint32_t code; };
    VMX_V3D_PROTOCOL_INPUT in = Valid(); VMX_V3D_PROTOCOL_DECISION out;
    const struct Case cases[] = {
        { &in.ExitReason, 0, VMX_V3D_PROTOCOL_CHECK_REASON_BASIC, VMX_V3D_PROTOCOL_INVALID_REASON_BASIC },
        { &in.ExitReason, VMX_V3D_PROTOCOL_ENTRY_FAILURE_BIT | VMX_V3D_PROTOCOL_EXIT_REASON_VMCALL, VMX_V3D_PROTOCOL_CHECK_ENTRY_FAILURE, VMX_V3D_PROTOCOL_INVALID_ENTRY_FAILURE },
        { &in.ExitReason, VMX_V3D_PROTOCOL_EXIT_REASON_VMCALL | UINT64_C(1) << 16, VMX_V3D_PROTOCOL_CHECK_REASON_RESERVED, VMX_V3D_PROTOCOL_INVALID_REASON_RESERVED },
        { &in.ExitReason, VMX_V3D_PROTOCOL_EXIT_REASON_VMCALL | UINT64_C(1) << 32, VMX_V3D_PROTOCOL_CHECK_REASON_RESERVED, VMX_V3D_PROTOCOL_INVALID_REASON_RESERVED },
        { &in.Qualification, 1, VMX_V3D_PROTOCOL_CHECK_QUALIFICATION, VMX_V3D_PROTOCOL_INVALID_QUALIFICATION },
        { &in.GuestRax, 0, VMX_V3D_PROTOCOL_CHECK_SIGNATURE, VMX_V3D_PROTOCOL_INVALID_SIGNATURE },
        { &in.GuestRdx, 0, VMX_V3D_PROTOCOL_CHECK_COOKIE, VMX_V3D_PROTOCOL_INVALID_COOKIE },
        { &in.InstructionLength, 0, VMX_V3D_PROTOCOL_CHECK_INSTRUCTION_LENGTH | VMX_V3D_PROTOCOL_CHECK_RIP_ADVANCE | VMX_V3D_PROTOCOL_CHECK_NEXT_RIP_CANONICAL, VMX_V3D_PROTOCOL_INVALID_INSTRUCTION_LENGTH },
        { &in.InstructionLength, 16, VMX_V3D_PROTOCOL_CHECK_INSTRUCTION_LENGTH | VMX_V3D_PROTOCOL_CHECK_RIP_ADVANCE | VMX_V3D_PROTOCOL_CHECK_NEXT_RIP_CANONICAL, VMX_V3D_PROTOCOL_INVALID_INSTRUCTION_LENGTH },
        { &in.InstructionLength, UINT64_MAX, VMX_V3D_PROTOCOL_CHECK_INSTRUCTION_LENGTH | VMX_V3D_PROTOCOL_CHECK_RIP_ADVANCE | VMX_V3D_PROTOCOL_CHECK_NEXT_RIP_CANONICAL, VMX_V3D_PROTOCOL_INVALID_INSTRUCTION_LENGTH },
        { &in.GuestRip, UINT64_C(0x0000800000000000), VMX_V3D_PROTOCOL_CHECK_RIP_CANONICAL | VMX_V3D_PROTOCOL_CHECK_RIP_ADVANCE | VMX_V3D_PROTOCOL_CHECK_NEXT_RIP_CANONICAL, VMX_V3D_PROTOCOL_INVALID_RIP },
        { &in.GuestRip, UINT64_MAX - 2, VMX_V3D_PROTOCOL_CHECK_RIP_ADVANCE | VMX_V3D_PROTOCOL_CHECK_NEXT_RIP_CANONICAL, VMX_V3D_PROTOCOL_INVALID_RIP_ADVANCE }
    };
    size_t i;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        uint32_t expected = VMX_V3D_PROTOCOL_CHECK_ALL & ~cases[i].missing;
        in = Valid(); *cases[i].field = cases[i].value;
        memset(&out, 0, sizeof(out)); TEST_ASSERT_TRUE(VmxV3dProtocolDecide(&in, &out), "rejection evaluated");
        TEST_ASSERT_UINT_EQ(out.NextProtocolStep, VMX_V3D_PROTOCOL_FAILED, "rejection step");
        TEST_ASSERT_UINT_EQ(out.AssemblyAction, VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED, "rejection action");
        TEST_ASSERT_UINT_EQ(out.ValidationMask, expected, "rejection exact mask");
        TEST_ASSERT_UINT_EQ(out.ValidationCode, cases[i].code, "rejection validation code");
        TEST_ASSERT_UINT_EQ(out.FailClosedCode, cases[i].code, "rejection fail-closed code");
        TEST_ASSERT_UINT_EQ(out.AdvancedRip, 0, "rejection RIP");
        TEST_ASSERT_UINT_EQ(out.ReadyObserved, 0, "rejection READY flag");
        TEST_ASSERT_UINT_EQ(out.ShutdownObserved, 0, "rejection SHUTDOWN flag");
    }
    in = Valid(); in.GuestRip = UINT64_C(0x00007fffffffffff); in.InstructionLength = 1;
    TEST_ASSERT_TRUE(VmxV3dProtocolDecide(&in, &out), "next RIP evaluated");
    TEST_ASSERT_UINT_EQ(out.ValidationMask, VMX_V3D_PROTOCOL_CHECK_ALL & ~VMX_V3D_PROTOCOL_CHECK_NEXT_RIP_CANONICAL, "next RIP mask");
    TEST_ASSERT_UINT_EQ(out.ValidationCode, VMX_V3D_PROTOCOL_INVALID_NEXT_RIP, "next RIP code");
    in = Valid(); in.GuestRcx = 99;
    TEST_ASSERT_TRUE(VmxV3dProtocolDecide(&in, &out), "unknown operation evaluated");
    TEST_ASSERT_UINT_EQ(out.ValidationMask, VMX_V3D_PROTOCOL_CHECK_ALL & ~(VMX_V3D_PROTOCOL_CHECK_OPERATION | VMX_V3D_PROTOCOL_CHECK_ORDER), "unknown operation mask");
    TEST_ASSERT_UINT_EQ(out.ValidationCode, VMX_V3D_PROTOCOL_INVALID_OPERATION, "unknown operation code");
    in = Valid(); in.CurrentProtocolStep = VMX_V3D_PROTOCOL_RESIDENT;
    TEST_ASSERT_TRUE(VmxV3dProtocolDecide(&in, &out), "wrong order evaluated");
    TEST_ASSERT_UINT_EQ(out.ValidationMask, VMX_V3D_PROTOCOL_CHECK_ALL & ~VMX_V3D_PROTOCOL_CHECK_ORDER, "wrong order mask");
    TEST_ASSERT_UINT_EQ(out.ValidationCode, VMX_V3D_PROTOCOL_INVALID_ORDER, "wrong order code");
    return 0;
}

static int TestBoundariesAndOrder(void)
{
    VMX_V3D_PROTOCOL_INPUT in = Valid(); VMX_V3D_PROTOCOL_DECISION out;
    in.InstructionLength = 1; TEST_ASSERT_TRUE(VmxV3dProtocolDecide(&in, &out), "length 1 evaluated");
    TEST_ASSERT_UINT_EQ(out.ValidationMask, VMX_V3D_PROTOCOL_CHECK_ALL, "length 1 mask");
    TEST_ASSERT_UINT_EQ(out.AdvancedRip, RIP + 1, "length 1 RIP");
    TEST_ASSERT_UINT_EQ(out.AssemblyAction, VMX_V3D_ASSEMBLY_ACTION_RETURN_RESIDENT, "length 1 action");
    in = Valid(); in.InstructionLength = 15; TEST_ASSERT_TRUE(VmxV3dProtocolDecide(&in, &out), "length 15 evaluated");
    TEST_ASSERT_UINT_EQ(out.ValidationMask, VMX_V3D_PROTOCOL_CHECK_ALL, "length 15 mask");
    TEST_ASSERT_UINT_EQ(out.AdvancedRip, RIP + 15, "length 15 RIP");
    TEST_ASSERT_UINT_EQ(out.AssemblyAction, VMX_V3D_ASSEMBLY_ACTION_RETURN_RESIDENT, "length 15 action");
    in = Valid(); in.GuestRcx = VMX_V3D_VMCALL_SHUTDOWN;
    TEST_ASSERT_TRUE(VmxV3dProtocolDecide(&in, &out), "SHUTDOWN at INITIALIZED evaluated");
    TEST_ASSERT_UINT_EQ(out.NextProtocolStep, VMX_V3D_PROTOCOL_FAILED, "SHUTDOWN INITIALIZED step");
    TEST_ASSERT_UINT_EQ(out.ValidationCode, VMX_V3D_PROTOCOL_INVALID_ORDER, "SHUTDOWN INITIALIZED order");
    in = Valid(); in.CurrentProtocolStep = VMX_V3D_PROTOCOL_READY_EXIT;
    TEST_ASSERT_TRUE(VmxV3dProtocolDecide(&in, &out), "duplicate READY evaluated");
    TEST_ASSERT_UINT_EQ(out.NextProtocolStep, VMX_V3D_PROTOCOL_FAILED, "duplicate READY step");
    TEST_ASSERT_UINT_EQ(out.ValidationCode, VMX_V3D_PROTOCOL_INVALID_ORDER, "duplicate READY order");
    in = Valid(); in.GuestRcx = VMX_V3D_VMCALL_SHUTDOWN; in.CurrentProtocolStep = VMX_V3D_PROTOCOL_SHUTDOWN_EXIT;
    TEST_ASSERT_TRUE(VmxV3dProtocolDecide(&in, &out), "duplicate SHUTDOWN evaluated");
    TEST_ASSERT_UINT_EQ(out.NextProtocolStep, VMX_V3D_PROTOCOL_FAILED, "duplicate SHUTDOWN step");
    TEST_ASSERT_UINT_EQ(out.ValidationCode, VMX_V3D_PROTOCOL_INVALID_ORDER, "duplicate SHUTDOWN order");
    return 0;
}
TEST_MAIN_BEGIN()
    TEST_RUN(TestSuccess);
    TEST_RUN(TestNullPreserves);
    TEST_RUN(TestRejections);
    TEST_RUN(TestBoundariesAndOrder);
TEST_MAIN_END()
