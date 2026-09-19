#include "test_common.h"
#include "vmx_persistent_activate.h"

typedef enum _FAILURE {
    F_NONE, F_PREPARE, F_ENTER, F_CR_READBACK, F_VMXON, F_INITIAL_CLEAR,
    F_VMPTRLD, F_CAPTURE, F_CAPTURE_INVALID, F_CONTROLS,
    F_CONTROLS_INVALID, F_APPLY, F_LAUNCH, F_CLEANUP_CLEAR, F_CLEANUP_OFF,
    F_RESTORE_READBACK, F_LEAVE_CLEANUP, F_LEAVE_RESIDENT, F_AFFINITY_RESTORE
} FAILURE;

typedef struct _FAKE {
    VMX_PROCESSOR_IDENTITY Identity;
    size_t Events[64], EventCount, VmclearCalls, VmxoffCalls, CrWriteCycles;
    uint64_t Cr0, Cr4;
    FAILURE Failure;
    bool CorruptNextRead;
} FAKE;

typedef struct _FIXTURE {
    FAKE Fake;
    VMX_PERSISTENT_CONTEXT Context;
    VMX_PERSISTENT_CPU_RECORD Record;
    VMX_PERSISTENT_KERNEL_RESULT KernelResult;
    VMX_PERSISTENT_OPERATION_RESULT OperationResult;
    VMX_PERSISTENT_KERNEL_PLATFORM Kernel;
    VMX_PERSISTENT_ACTIVATE_PLATFORM Platform;
} FIXTURE;

static _Alignas(16) unsigned char g_host[VMX_PERSISTENT_HOST_STACK_SIZE];
static _Alignas(16) unsigned char g_guest[VMX_PERSISTENT_GUEST_STACK_SIZE];
static _Alignas(4096) unsigned char g_msr_bitmap[4096];
static VMX_V3D_PERSISTENT_LAUNCH_CONTEXT g_launch;

static void Event(FAKE* f, size_t e) { f->Events[f->EventCount++] = e; }
static bool Passive(void* x) { Event(x, 1); return true; }
static VMX_PERSISTENT_KERNEL_STATUS Pin(
    void* x, const VMX_PROCESSOR_IDENTITY* id,
    VMX_PERSISTENT_AFFINITY_TOKEN* token)
{ (void)id; Event(x, 2); token->PreviousMask = 1; token->Pinned = true; return 0; }
static VMX_PERSISTENT_KERNEL_STATUS Query(
    void* x, VMX_PROCESSOR_IDENTITY* id)
{ FAKE* f = x; Event(f, 3); *id = f->Identity; return 0; }
static VMX_PERSISTENT_KERNEL_STATUS Restore(
    void* x, VMX_PERSISTENT_AFFINITY_TOKEN* token)
{
    FAKE* f = x; Event(f, 4);
    if (f->Failure == F_AFFINITY_RESTORE) return -55;
    token->Pinned = false; return 0;
}
static bool Enter(void* x, uint8_t* previous)
{
    FAKE* f = x; Event(f, 13);
    if (f->Failure == F_ENTER) return false;
    *previous = 2; return true;
}
static bool Leave(void* x, uint8_t previous)
{
    FAKE* f = x; (void)previous; Event(f, 27);
    return f->Failure != F_LEAVE_CLEANUP &&
           f->Failure != F_LEAVE_RESIDENT;
}
static uint64_t ReadCr0(void* x)
{
    FAKE* f = x; Event(f, 10);
    if (f->CorruptNextRead) {
        f->CorruptNextRead = false; return f->Cr0 ^ 1u;
    }
    return f->Cr0;
}
static uint64_t ReadCr4(void* x)
{ FAKE* f = x; Event(f, 11); return f->Cr4; }
static void WriteCr0(void* x, uint64_t value)
{ FAKE* f = x; Event(f, 14); f->Cr0 = value; }
static void WriteCr4(void* x, uint64_t value)
{
    FAKE* f = x; Event(f, 15); f->Cr4 = value; f->CrWriteCycles++;
    if ((f->Failure == F_CR_READBACK && f->CrWriteCycles == 1) ||
        (f->Failure == F_RESTORE_READBACK && f->VmxoffCalls != 0))
        f->CorruptNextRead = true;
}
static bool PrepareCr(
    void* x, uint64_t cr0, uint64_t cr4,
    uint64_t* target_cr0, uint64_t* target_cr4)
{
    FAKE* f = x; Event(f, 12);
    if (f->Failure == F_PREPARE) return false;
    *target_cr0 = cr0 | 1u; *target_cr4 = cr4 | (UINT64_C(1) << 13);
    return true;
}
static VMX_PERSISTENT_INSTRUCTION_REPORT Report(bool fail)
{
    VMX_PERSISTENT_INSTRUCTION_REPORT r = {
        VMX_V3D_INSTRUCTION_SUCCESS, 0, 0};
    if (fail) {
        r.Outcome = VMX_V3D_INSTRUCTION_FAIL_VALID;
        r.ErrorValid = 1; r.Error = 7;
    }
    return r;
}
static VMX_PERSISTENT_INSTRUCTION_REPORT Vmxon(void* x, uint64_t pa)
{ FAKE* f = x; (void)pa; Event(f, 20); return Report(f->Failure == F_VMXON); }
static VMX_PERSISTENT_INSTRUCTION_REPORT Vmclear(void* x, uint64_t pa)
{
    FAKE* f = x; bool fail; (void)pa; Event(f, 21); f->VmclearCalls++;
    fail = (f->Failure == F_INITIAL_CLEAR && f->VmclearCalls == 1) ||
           (f->Failure == F_CLEANUP_CLEAR && f->VmclearCalls > 1);
    return Report(fail);
}
static VMX_PERSISTENT_INSTRUCTION_REPORT Vmptrld(void* x, uint64_t pa)
{ FAKE* f = x; (void)pa; Event(f, 22); return Report(f->Failure == F_VMPTRLD); }
static VMX_PERSISTENT_INSTRUCTION_REPORT Vmxoff(void* x)
{
    FAKE* f = x; Event(f, 28); f->VmxoffCalls++;
    return Report(f->Failure == F_CLEANUP_OFF);
}
static void FillState(VMX_PROCESSOR_STATE* s)
{
    memset(s, 0, sizeof(*s)); s->Cs = 0x10; s->Ss = 0x18; s->Tr = 0x20;
    s->CsDesc.present = s->SsDesc.present = s->TrDesc.present = true;
    s->TrDesc.is_system = true;
}
static bool Capture(void* x, VMX_PROCESSOR_STATE* state)
{
    FAKE* f = x; Event(f, 23); FillState(state);
    if (f->Failure == F_CAPTURE_INVALID) state->Cs = 0;
    return f->Failure != F_CAPTURE && f->Failure != F_CLEANUP_CLEAR &&
           f->Failure != F_CLEANUP_OFF &&
           f->Failure != F_RESTORE_READBACK &&
           f->Failure != F_LEAVE_CLEANUP;
}
static bool Controls(void* x, VMX_CONTROL_PLAN* controls)
{
    FAKE* f = x; Event(f, 24); memset(controls, 0, sizeof(*controls));
    controls->IsValid = f->Failure != F_CONTROLS_INVALID;
    controls->PinControls = PIN_BASED_ACTIVATE_PREEMPTION_TIMER;
    controls->PrimaryControls = CPU_BASED_USE_MSR_BITMAPS;
    controls->PreemptionTimerValue = VMX_PREEMPTION_TIMER_INITIAL_VALUE;
    return f->Failure != F_CONTROLS;
}
static VMX_PERSISTENT_INSTRUCTION_REPORT Apply(
    void* x, const VMX_VMCS_PLAN* plan,
    VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* launch)
{ FAKE* f = x; (void)plan; (void)launch; Event(f, 25); return Report(f->Failure == F_APPLY); }
static uint32_t Launch(
    void* x, VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* launch)
{
    FAKE* f = x; Event(f, 26);
    if (f->Failure == F_LAUNCH) return 0;
    launch->ProtocolStep = VMX_V3D_PROTOCOL_READY_EXIT;
    launch->AssemblyAction = VMX_V3D_ASSEMBLY_ACTION_RETURN_RESIDENT;
    launch->ValidationFlags = VMX_V3D_VALIDATION_HEADER_OK |
                              VMX_V3D_VALIDATION_READY_OBSERVED;
    launch->ReadyObserved = 1;
    launch->GuestRspOutcome = launch->GuestRflagsOutcome =
        launch->VmlaunchOutcome = VMX_V3D_INSTRUCTION_SUCCESS;
    return launch->AssemblyAction;
}
static uint32_t Shutdown(
    void* x, VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* launch)
{ (void)x; (void)launch; return 0; }

static void Initialize(FIXTURE* f, FAILURE failure)
{
    VMX_PERSISTENT_CONTEXT* c = &f->Context; FAKE* fake = &f->Fake;
    memset(f, 0, sizeof(*f)); memset(&g_launch, 0, sizeof(g_launch));
    fake->Identity.Group = 1; fake->Identity.Number = 1;
    fake->Identity.SystemIndex = 65; fake->Cr0 = 0x20; fake->Cr4 = 0x100;
    fake->Failure = failure; c->Identity = f->Record.Identity = fake->Identity;
    c->Version = VMX_PERSISTENT_CONTEXT_VERSION; c->Published = true;
    c->OwnedResources = VMX_PERSISTENT_RESOURCE_LAUNCH |
        VMX_PERSISTENT_RESOURCE_STACK | VMX_PERSISTENT_RESOURCE_GUEST_STACK |
        VMX_PERSISTENT_RESOURCE_VMXON | VMX_PERSISTENT_RESOURCE_VMCS |
        VMX_PERSISTENT_RESOURCE_MSR_BITMAP;
    c->LaunchStorage = &g_launch; c->LaunchStorageSize = sizeof(g_launch);
    c->HostStack = g_host; c->HostStackSize = sizeof(g_host);
    c->GuestStack = g_guest; c->GuestStackSize = sizeof(g_guest);
    c->VmxonPhysicalAddress = 0x200000; c->VmcsPhysicalAddress = 0x201000;
    c->MsrBitmap = g_msr_bitmap; c->MsrBitmapPhysicalAddress = 0x202000;
    f->Record.OwnedMask = VMX_PERSISTENT_OWNER_CONTEXT;
    g_launch.Header.Magic = VMX_V3D_PERSISTENT_LAUNCH_MAGIC;
    g_launch.Header.Size = VMX_V3D_CONTEXT_SIZE;
    g_launch.Header.Version = VMX_V3D_PERSISTENT_LAUNCH_VERSION;
    g_launch.ProtocolStep = VMX_V3D_PROTOCOL_INITIALIZED;
    g_launch.ValidationFlags = VMX_V3D_VALIDATION_HEADER_OK;
    g_launch.HostStackTop = (uint64_t)(uintptr_t)(g_host + sizeof(g_host));
    g_launch.HostRsp = g_launch.HostStackTop - 8;
    g_launch.GuestStackTop = (uint64_t)(uintptr_t)(g_guest + sizeof(g_guest));
    g_launch.GuestRsp = g_launch.GuestStackTop - 8;
    g_launch.VmxonPhysicalAddress = c->VmxonPhysicalAddress;
    g_launch.VmcsPhysicalAddress = c->VmcsPhysicalAddress;
    f->Kernel.Context = fake; f->Kernel.CheckPassive = Passive;
    f->Kernel.Pin = Pin; f->Kernel.QueryCurrent = Query; f->Kernel.Restore = Restore;
    f->Platform.Kernel = &f->Kernel;
    f->Platform.GuestRip = UINT64_C(0xFFFFF80000001000);
    f->Platform.HostRip = UINT64_C(0xFFFFF80000002000);
    f->Platform.KernelResult = &f->KernelResult;
    f->Platform.OperationResult = &f->OperationResult;
    f->Platform.Operations = (VMX_PERSISTENT_VMX_OPS){
        fake, Enter, Leave, ReadCr0, ReadCr4, WriteCr0, WriteCr4, PrepareCr,
        Vmxon, Vmclear, Vmptrld, Vmxoff, Capture, Controls, Apply,
        Launch, Shutdown};
}
static VMX_PERSISTENT_STATUS Activate(FIXTURE* f)
{ return VmxPersistentActivateContext(&f->Platform, &f->Context, &f->Record); }

static void AssertCleanupResult(
    FIXTURE* f, VMX_PERSISTENT_OPERATION_STATUS primary)
{
    TEST_ASSERT_INT_EQ(Activate(f), VMX_PERSISTENT_STATUS_NOT_READY,
                       "cleanup lifecycle");
    TEST_ASSERT_INT_EQ(f->OperationResult.PrimaryStatus, primary,
                       "cleanup primary");
    TEST_ASSERT_INT_EQ(f->OperationResult.CleanupStatus,
                       VMX_PERSISTENT_OPERATION_CLEANUP_FAILED,
                       "cleanup detail");
}

static int test_primary_failure_matrix(void)
{
    static const struct {
        FAILURE Failure; VMX_PERSISTENT_OPERATION_STATUS Primary;
    } cases[] = {
        {F_PREPARE, VMX_PERSISTENT_OPERATION_STATE_FAILED},
        {F_ENTER, VMX_PERSISTENT_OPERATION_STATE_FAILED},
        {F_CR_READBACK, VMX_PERSISTENT_OPERATION_STATE_FAILED},
        {F_VMXON, VMX_PERSISTENT_OPERATION_INSTRUCTION_FAILED},
        {F_INITIAL_CLEAR, VMX_PERSISTENT_OPERATION_INSTRUCTION_FAILED},
        {F_VMPTRLD, VMX_PERSISTENT_OPERATION_INSTRUCTION_FAILED},
        {F_CAPTURE, VMX_PERSISTENT_OPERATION_STATE_FAILED},
        {F_CAPTURE_INVALID, VMX_PERSISTENT_OPERATION_STATE_FAILED},
        {F_CONTROLS, VMX_PERSISTENT_OPERATION_PLAN_FAILED},
        {F_CONTROLS_INVALID, VMX_PERSISTENT_OPERATION_PLAN_FAILED},
        {F_APPLY, VMX_PERSISTENT_OPERATION_PLAN_FAILED},
        {F_LAUNCH, VMX_PERSISTENT_OPERATION_LAUNCH_FAILED}
    };
    size_t i;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        FIXTURE f; Initialize(&f, cases[i].Failure);
        TEST_ASSERT_INT_EQ(Activate(&f), VMX_PERSISTENT_STATUS_NOT_READY,
                           "primary lifecycle");
        TEST_ASSERT_INT_EQ(f.OperationResult.PrimaryStatus, cases[i].Primary,
                           "primary detail");
        TEST_ASSERT_INT_EQ(f.OperationResult.CleanupStatus,
                           VMX_PERSISTENT_OPERATION_OK, "clean rollback");
        TEST_ASSERT_UINT_EQ(f.OperationResult.Stage,
                            VMX_PERSISTENT_OPERATION_FAILED, "failed stage");
        if (cases[i].Failure == F_CONTROLS) {
            TEST_ASSERT_UINT_EQ(f.OperationResult.PlanPhase,
                VMX_PERSISTENT_PLAN_BUILD_CONTROLS, "control-build phase");
            TEST_ASSERT_INT_EQ(f.OperationResult.VmcsPlanStatus,
                VMX_PERSISTENT_VMCS_PLAN_OK, "control-build VMCS status");
        } else if (cases[i].Failure == F_CONTROLS_INVALID) {
            TEST_ASSERT_UINT_EQ(f.OperationResult.PlanPhase,
                VMX_PERSISTENT_PLAN_BUILD_VMCS, "VMCS-build phase");
            TEST_ASSERT_INT_EQ(f.OperationResult.VmcsPlanStatus,
                VMX_PERSISTENT_VMCS_PLAN_INVALID, "VMCS invalid detail");
        } else if (cases[i].Failure == F_APPLY) {
            TEST_ASSERT_UINT_EQ(f.OperationResult.PlanPhase,
                VMX_PERSISTENT_PLAN_APPLY_VMCS, "VMCS-apply phase");
            TEST_ASSERT_INT_EQ(f.OperationResult.VmcsPlanStatus,
                VMX_PERSISTENT_VMCS_PLAN_OK, "VMCS apply build status");
        }
        TEST_ASSERT_TRUE(!f.Record.Active && !f.Record.CleanupRequired,
                         "record clean");
        TEST_ASSERT_UINT_EQ(f.Record.OwnedMask,
                            VMX_PERSISTENT_OWNER_CONTEXT, "owner clean");
        TEST_ASSERT_TRUE(!f.Context.CpuState.VmxActive &&
                         !f.Context.CpuState.VmxCurrent &&
                         !f.Context.CpuState.RestorationRequired &&
                         !f.Context.CpuState.IrqlRaised, "cpu clean");
        TEST_ASSERT_UINT_EQ(f.Fake.Cr0, 0x20, "CR0 restored");
        TEST_ASSERT_UINT_EQ(f.Fake.Cr4, 0x100, "CR4 restored");
        TEST_ASSERT_TRUE(f.KernelResult.AffinityRestored, "affinity restored");
        TEST_ASSERT_UINT_EQ(f.Fake.Events[f.Fake.EventCount - 1], 4,
                            "restore last");
    }
    return 0;
}

static int test_cleanup_failure_matrix(void)
{
    FIXTURE f;
    Initialize(&f, F_CLEANUP_CLEAR);
    AssertCleanupResult(&f, VMX_PERSISTENT_OPERATION_STATE_FAILED);
    TEST_ASSERT_TRUE(f.Fake.VmclearCalls == 2 && f.Fake.VmxoffCalls == 1,
                     "clear failure still off");
    TEST_ASSERT_TRUE(f.Record.CleanupRequired &&
                     !f.Context.CpuState.VmxActive &&
                     !f.Context.CpuState.VmxCurrent, "clear residual history");

    Initialize(&f, F_CLEANUP_OFF);
    AssertCleanupResult(&f, VMX_PERSISTENT_OPERATION_STATE_FAILED);
    TEST_ASSERT_TRUE(f.Fake.VmclearCalls == 2 && f.Fake.VmxoffCalls == 1,
                     "off attempted");
    TEST_ASSERT_TRUE(f.Record.CleanupRequired &&
                     f.Context.CpuState.VmxActive &&
                     !f.Context.CpuState.VmxCurrent, "off residual");

    Initialize(&f, F_RESTORE_READBACK);
    AssertCleanupResult(&f, VMX_PERSISTENT_OPERATION_STATE_FAILED);
    TEST_ASSERT_TRUE(f.Record.CleanupRequired &&
                     f.Context.CpuState.RestorationRequired,
                     "CR restore residual");

    Initialize(&f, F_LEAVE_CLEANUP);
    AssertCleanupResult(&f, VMX_PERSISTENT_OPERATION_STATE_FAILED);
    TEST_ASSERT_TRUE(f.Record.CleanupRequired &&
                     f.Context.CpuState.IrqlRaised, "IRQL residual");

    Initialize(&f, F_LEAVE_RESIDENT);
    AssertCleanupResult(&f, VMX_PERSISTENT_OPERATION_OK);
    TEST_ASSERT_TRUE(f.Record.Active && f.Record.CleanupRequired &&
                     f.Context.CpuState.VmxActive, "resident leave failure");
    TEST_ASSERT_INT_EQ(f.OperationResult.CleanupStatus,
                       VMX_PERSISTENT_OPERATION_CLEANUP_FAILED, "leave status");

    Initialize(&f, F_AFFINITY_RESTORE);
    AssertCleanupResult(&f, VMX_PERSISTENT_OPERATION_OK);
    TEST_ASSERT_TRUE(f.Record.Active && f.Record.CleanupRequired,
                     "affinity residual");
    TEST_ASSERT_INT_EQ(f.KernelResult.FinalStatus,
                       VMX_PERSISTENT_KERNEL_RESTORE_FAILED, "restore status");
    return 0;
}

TEST_MAIN_BEGIN()
    TEST_RUN(test_primary_failure_matrix);
    TEST_RUN(test_cleanup_failure_matrix);
TEST_MAIN_END()
