#include "test_common.h"
#include "vmx_persistent_activate.h"

typedef struct _FAKE {
    uint32_t Calls;
    uint64_t LastValue;
} FAKE;

static bool Enter(void* context, uint8_t* previous)
{ FAKE* f = context; f->Calls++; *previous = 2; return true; }
static bool Leave(void* context, uint8_t previous)
{ FAKE* f = context; f->Calls++; f->LastValue = previous; return true; }
static uint64_t Read(void* context)
{ FAKE* f = context; f->Calls++; return f->LastValue; }
static void Write(void* context, uint64_t value)
{ FAKE* f = context; f->Calls++; f->LastValue = value; }
static bool PrepareRegisters(void* context, uint64_t cr0, uint64_t cr4,
                             uint64_t* target_cr0, uint64_t* target_cr4)
{ FAKE* f = context; f->Calls++; *target_cr0 = cr0; *target_cr4 = cr4 | (1ull << 13); return true; }
static VMX_PERSISTENT_INSTRUCTION_REPORT InstructionPa(void* context, uint64_t pa)
{
    FAKE* f = context; VMX_PERSISTENT_INSTRUCTION_REPORT r = {0};
    f->Calls++; f->LastValue = pa; r.Outcome = VMX_V3D_INSTRUCTION_SUCCESS;
    return r;
}
static VMX_PERSISTENT_INSTRUCTION_REPORT Instruction(void* context)
{ return InstructionPa(context, 0); }
static bool Capture(void* context, VMX_PROCESSOR_STATE* state)
{ FAKE* f = context; f->Calls++; state->IsValid = true; return true; }
static bool Controls(void* context, VMX_CONTROL_PLAN* controls)
{ FAKE* f = context; f->Calls++; controls->IsValid = true; return true; }
static VMX_PERSISTENT_INSTRUCTION_REPORT Apply(
    void* context, const VMX_VMCS_PLAN* plan,
    VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* launch)
{
    FAKE* f = context; VMX_PERSISTENT_INSTRUCTION_REPORT r = {0};
    f->Calls++; f->LastValue = plan->Count; launch->VmcsPlanWriteCount = plan->Count;
    r.Outcome = VMX_V3D_INSTRUCTION_SUCCESS; return r;
}
static uint32_t Assembly(
    void* context, VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* launch)
{
    FAKE* f = context; f->Calls++; launch->AssemblyAction =
        VMX_V3D_ASSEMBLY_ACTION_RETURN_RESIDENT; return launch->AssemblyAction;
}

static VMX_PERSISTENT_VMX_OPS Ops(FAKE* fake)
{
    VMX_PERSISTENT_VMX_OPS o = {0};
    o.Context = fake; o.EnterInstructionLevel = Enter; o.LeaveInstructionLevel = Leave;
    o.ReadCr0 = Read; o.ReadCr4 = Read; o.WriteCr0 = Write; o.WriteCr4 = Write;
    o.PrepareControlRegisters = PrepareRegisters;
    o.Vmxon = InstructionPa; o.Vmclear = InstructionPa; o.Vmptrld = InstructionPa;
    o.Vmxoff = Instruction; o.CaptureState = Capture; o.BuildControls = Controls;
    o.ApplyPlan = Apply; o.Launch = Assembly; o.Shutdown = Assembly; return o;
}

static int test_numeric_owner_and_status_mapping(void)
{
    TEST_ASSERT_UINT_EQ(VMX_PERSISTENT_OWNER_CONTEXT, 1, "context owner");
    TEST_ASSERT_UINT_EQ(VMX_PERSISTENT_OWNER_VMX_ACTIVE, 2, "vmx owner");
    TEST_ASSERT_UINT_EQ(VMX_PERSISTENT_OWNER_VMCS_CURRENT, 4, "current owner");
    TEST_ASSERT_UINT_EQ(VMX_PERSISTENT_OWNER_RUNTIME, 6, "runtime owners");
    TEST_ASSERT_TRUE((VMX_PERSISTENT_OWNER_CONTEXT &
                      VMX_PERSISTENT_OWNER_RUNTIME) == 0, "owners disjoint");
    TEST_ASSERT_INT_EQ(VMX_PERSISTENT_OPERATION_OK, 0, "success status");
    TEST_ASSERT_TRUE(VMX_PERSISTENT_OPERATION_CLEANUP_FAILED <
                     VMX_PERSISTENT_OPERATION_SHUTDOWN_FAILED,
                     "cleanup status distinct");
    TEST_ASSERT_UINT_EQ(VMX_PERSISTENT_OPERATION_RESIDENT, 7, "resident stage");
    TEST_ASSERT_UINT_EQ(VMX_PERSISTENT_OPERATION_SHUTDOWN_COMPLETE, 9,
                        "shutdown stage");
    TEST_ASSERT_INT_EQ(VmxPersistentOperationLifecycleStatus(
        VMX_PERSISTENT_OPERATION_PLATFORM_INVALID), VMX_PERSISTENT_STATUS_INVALID,
        "invalid maps invalid");
    TEST_ASSERT_INT_EQ(VmxPersistentOperationLifecycleStatus(
        VMX_PERSISTENT_OPERATION_INSTRUCTION_FAILED), VMX_PERSISTENT_STATUS_NOT_READY,
        "instruction maps not ready");
    TEST_ASSERT_INT_EQ(VmxPersistentOperationLifecycleStatus(
        VMX_PERSISTENT_OPERATION_CLEANUP_FAILED), VMX_PERSISTENT_STATUS_NOT_READY,
        "cleanup maps not ready");
    TEST_ASSERT_INT_EQ(VmxPersistentOperationLifecycleStatus(
        VMX_PERSISTENT_OPERATION_ALREADY_ACTIVE), VMX_PERSISTENT_STATUS_BUSY,
        "active maps busy");
    TEST_ASSERT_INT_EQ(VmxPersistentOperationLifecycleStatus(
        VMX_PERSISTENT_OPERATION_NOT_ACTIVE), VMX_PERSISTENT_STATUS_NOT_ACTIVE,
        "inactive maps inactive");
    TEST_ASSERT_INT_EQ(VmxPersistentOperationLifecycleStatus(
        VMX_PERSISTENT_OPERATION_INVARIANT), VMX_PERSISTENT_STATUS_INVARIANT,
        "invariant maps invariant");
    return 0;
}

static int test_callbacks_are_operation_local(void)
{
    FAKE first = {0}, second = {0}; VMX_PERSISTENT_VMX_OPS a = Ops(&first), b = Ops(&second);
    VMX_PROCESSOR_STATE state = {0}; VMX_CONTROL_PLAN controls = {0};
    VMX_VMCS_PLAN plan = {0}; VMX_V3D_PERSISTENT_LAUNCH_CONTEXT launch = {0};
    uint8_t previous = 0; VMX_PERSISTENT_INSTRUCTION_REPORT report;
    uint64_t target_cr0, target_cr4;
    first.LastValue = 0x10; second.LastValue = 0x20; plan.Count = 79;
    TEST_ASSERT_TRUE(a.EnterInstructionLevel(a.Context, &previous), "enter");
    TEST_ASSERT_UINT_EQ(previous, 2, "previous level");
    TEST_ASSERT_UINT_EQ(a.ReadCr0(a.Context), 0x10, "read first");
    TEST_ASSERT_TRUE(a.PrepareControlRegisters(a.Context, 0x10, 0x20,
                                               &target_cr0, &target_cr4),
                     "prepare registers");
    TEST_ASSERT_UINT_EQ(target_cr0, 0x10, "target CR0");
    TEST_ASSERT_UINT_EQ(target_cr4, 0x2020, "target CR4");
    a.WriteCr4(a.Context, 0x30);
    report = a.Vmxon(a.Context, 0x2000); TEST_ASSERT_UINT_EQ(report.Outcome, 1, "vmxon");
    report = a.Vmclear(a.Context, 0x3000); TEST_ASSERT_UINT_EQ(report.Outcome, 1, "vmclear");
    report = a.Vmptrld(a.Context, 0x3000); TEST_ASSERT_UINT_EQ(report.Outcome, 1, "vmptrld");
    TEST_ASSERT_TRUE(a.CaptureState(a.Context, &state) && state.IsValid, "capture");
    TEST_ASSERT_TRUE(a.BuildControls(a.Context, &controls) && controls.IsValid, "controls");
    report = a.ApplyPlan(a.Context, &plan, &launch);
    TEST_ASSERT_UINT_EQ(report.Outcome, 1, "apply");
    TEST_ASSERT_UINT_EQ(launch.VmcsPlanWriteCount, 79, "plan count");
    TEST_ASSERT_UINT_EQ(a.Launch(a.Context, &launch),
                        VMX_V3D_ASSEMBLY_ACTION_RETURN_RESIDENT, "launch");
    report = a.Vmxoff(a.Context); TEST_ASSERT_UINT_EQ(report.Outcome, 1, "vmxoff");
    TEST_ASSERT_TRUE(a.LeaveInstructionLevel(a.Context, previous), "leave");
    TEST_ASSERT_UINT_EQ(second.Calls, 0, "second untouched");
    TEST_ASSERT_UINT_EQ(b.ReadCr4(b.Context), 0x20, "second independent");
    TEST_ASSERT_UINT_EQ(first.Calls, 13, "first exact calls");
    TEST_ASSERT_UINT_EQ(second.Calls, 1, "second exact calls");
    return 0;
}

static int test_platform_and_result_storage_are_caller_owned(void)
{
    FAKE fake = {0}; VMX_PERSISTENT_KERNEL_RESULT kernel = {0};
    VMX_PERSISTENT_OPERATION_RESULT operation = {0};
    VMX_PERSISTENT_ACTIVATE_PLATFORM platform = {0};
    platform.Operations = Ops(&fake); platform.GuestRip = 0x1000;
    platform.HostRip = 0x2000; platform.KernelResult = &kernel;
    platform.OperationResult = &operation;
    operation.Stage = VMX_PERSISTENT_OPERATION_NOT_STARTED;
    operation.Vmxon.Outcome = VMX_V3D_INSTRUCTION_NOT_ATTEMPTED;
    TEST_ASSERT_TRUE(platform.Operations.Context == &fake, "operation context");
    TEST_ASSERT_TRUE(platform.KernelResult == &kernel, "kernel result");
    TEST_ASSERT_TRUE(platform.OperationResult == &operation, "operation result");
    TEST_ASSERT_UINT_EQ(platform.GuestRip, 0x1000, "guest RIP");
    TEST_ASSERT_UINT_EQ(platform.HostRip, 0x2000, "host RIP");
    TEST_ASSERT_UINT_EQ(operation.Vmxon.Outcome, 0, "not attempted");
    return 0;
}

TEST_MAIN_BEGIN()
    TEST_RUN(test_numeric_owner_and_status_mapping);
    TEST_RUN(test_callbacks_are_operation_local);
    TEST_RUN(test_platform_and_result_storage_are_caller_owned);
TEST_MAIN_END()