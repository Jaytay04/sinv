#include "test_common.h"
#include "vmx_persistent_start_diagnostic.h"
#include <string.h>

#define N 65

typedef struct _FIXTURE {
    VMX_V3D_PERSISTENT_LAUNCH_CONTEXT Launch[N];
    VMX_PERSISTENT_CPU_RECORD Cpu[N];
    VMX_PERSISTENT_KERNEL_RESULT Prepare[N], Activate[N];
    VMX_PERSISTENT_OPERATION_RESULT Operation[N];
    VMX_PERSISTENT_CONTEXT Context[N];
    VMX_PERSISTENT_LIFECYCLE Lifecycle;
} FIXTURE;

static void Init(FIXTURE* f, size_t index)
{
    size_t i;
    memset(f, 0, sizeof(*f));
    f->Lifecycle.Cpus = f->Cpu;
    f->Lifecycle.Count = N;
    f->Lifecycle.State = VMX_PERSISTENT_UNLOADED;
    f->Lifecycle.HasOperationError = true;
    f->Lifecycle.FirstOperationStatus = VMX_PERSISTENT_STATUS_NOT_READY;
    f->Lifecycle.FirstOperationIndex = index;
    for (i = 0; i < N; ++i) {
        f->Cpu[i].Identity.SystemIndex = (uint32_t)i;
        f->Cpu[i].Identity.Group = (uint16_t)(i / 64);
        f->Cpu[i].Identity.Number = (uint8_t)(i % 64);
        f->Context[i].Identity = f->Cpu[i].Identity;
        f->Context[i].Version = VMX_PERSISTENT_CONTEXT_VERSION;
        f->Context[i].Published = true;
        f->Context[i].LaunchStorage = &f->Launch[i];
        f->Context[i].LaunchStorageSize = sizeof(f->Launch[i]);
    }
    f->Cpu[index].Stage = VMX_PERSISTENT_CPU_FAILED;
}

static int CaptureKind(FIXTURE* f, uint32_t kind)
{
    VMX_PERSISTENT_START_DIAGNOSTIC d = { 0 };
    TEST_ASSERT_TRUE(VmxPersistentStartDiagnosticCapture(&f->Lifecycle,
        f->Prepare, f->Activate, f->Operation, f->Context, N, &d), "capture");
    TEST_ASSERT_UINT_EQ(d.Version, VMX_PERSISTENT_START_DIAGNOSTIC_VERSION, "version");
    TEST_ASSERT_UINT_EQ(d.Size, sizeof(d), "size");
    TEST_ASSERT_UINT_EQ(d.Valid, 1, "valid");
    TEST_ASSERT_UINT_EQ(d.Kind, kind, "kind");
    TEST_ASSERT_UINT_EQ(d.SystemIndex, 64, "index");
    TEST_ASSERT_UINT_EQ(d.Group, 1, "group");
    TEST_ASSERT_UINT_EQ(d.Number, 0, "number");
    TEST_ASSERT_INT_EQ(d.LifecycleStatus, VMX_PERSISTENT_STATUS_NOT_READY, "status");
    TEST_ASSERT_TRUE(VmxPersistentStartTraceValid(&d.Trace), "trace valid");
    TEST_ASSERT_UINT_EQ(d.Trace.Boundary, VMX_PERSISTENT_START_BOUNDARY_LIFECYCLE,
        "trace lifecycle boundary");
    return 0;
}

static int TestKinds(void)
{
    FIXTURE f;
    Init(&f, 64); f.Prepare[64].FinalStatus = VMX_PERSISTENT_KERNEL_PIN_FAILED;
    TEST_ASSERT_INT_EQ(CaptureKind(&f, VMX_PERSISTENT_START_FAILURE_PREPARE_KERNEL), 0, "prepare");
    Init(&f, 64); f.Activate[64].FinalStatus = VMX_PERSISTENT_KERNEL_VERIFY_FAILED;
    TEST_ASSERT_INT_EQ(CaptureKind(&f, VMX_PERSISTENT_START_FAILURE_ACTIVATE_KERNEL), 0, "activate kernel");
    Init(&f, 64); f.Operation[64].Vmxon.Outcome = VMX_V3D_INSTRUCTION_FAIL_INVALID;
    TEST_ASSERT_INT_EQ(CaptureKind(&f, VMX_PERSISTENT_START_FAILURE_VMXON), 0, "vmxon");
    Init(&f, 64); f.Operation[64].InitialVmclear.Outcome = VMX_V3D_INSTRUCTION_FAIL_VALID;
    f.Operation[64].InitialVmclear.ErrorValid = 1; f.Operation[64].InitialVmclear.Error = 7;
    TEST_ASSERT_INT_EQ(CaptureKind(&f, VMX_PERSISTENT_START_FAILURE_INITIAL_VMCLEAR), 0, "vmclear");
    Init(&f, 64); f.Operation[64].Vmptrld.Outcome = VMX_V3D_INSTRUCTION_FAIL_INVALID;
    TEST_ASSERT_INT_EQ(CaptureKind(&f, VMX_PERSISTENT_START_FAILURE_VMPTRLD), 0, "vmptrld");
    Init(&f, 64); f.Operation[64].PrimaryStatus = VMX_PERSISTENT_OPERATION_STATE_FAILED;
    TEST_ASSERT_INT_EQ(CaptureKind(&f, VMX_PERSISTENT_START_FAILURE_STATE), 0, "state");
    Init(&f, 64); f.Operation[64].PrimaryStatus = VMX_PERSISTENT_OPERATION_PLAN_FAILED;
    f.Operation[64].PlanPhase = VMX_PERSISTENT_PLAN_BUILD_VMCS;
    f.Operation[64].VmcsPlanStatus = VMX_PERSISTENT_VMCS_PLAN_INVALID;
    TEST_ASSERT_INT_EQ(CaptureKind(&f, VMX_PERSISTENT_START_FAILURE_PLAN), 0, "plan");
    Init(&f, 64); f.Operation[64].PrimaryStatus = VMX_PERSISTENT_OPERATION_LAUNCH_FAILED;
    TEST_ASSERT_INT_EQ(CaptureKind(&f, VMX_PERSISTENT_START_FAILURE_LAUNCH), 0, "launch");
    Init(&f, 64); f.Activate[64].FinalStatus = VMX_PERSISTENT_KERNEL_RESTORE_FAILED;
    f.Activate[64].RestoreStatus = VMX_PERSISTENT_KERNEL_RESTORE_FAILED;
    f.Activate[64].OperationInvoked = true;
    TEST_ASSERT_INT_EQ(CaptureKind(&f, VMX_PERSISTENT_START_FAILURE_AFFINITY_RESTORE), 0, "restore");
    Init(&f, 64); f.Operation[64].CleanupStatus = VMX_PERSISTENT_OPERATION_CLEANUP_FAILED;
    TEST_ASSERT_INT_EQ(CaptureKind(&f, VMX_PERSISTENT_START_FAILURE_CLEANUP), 0, "cleanup");
    Init(&f, 64);
    TEST_ASSERT_INT_EQ(CaptureKind(&f, VMX_PERSISTENT_START_FAILURE_LIFECYCLE), 0, "lifecycle");
    return 0;
}

static int TestLaunchSnapshot(void)
{
    FIXTURE f;
    VMX_PERSISTENT_START_DIAGNOSTIC d = { 0 };
    VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* l;
    Init(&f, 64);
    f.Operation[64].PrimaryStatus = VMX_PERSISTENT_OPERATION_LAUNCH_FAILED;
    l = &f.Launch[64];
    l->ProtocolStep = 3; l->AssemblyAction = 4; l->ValidationFlags = 5;
    l->LaunchFrameValid = 1; l->ReadyObserved = 1;
    l->LaunchFrameGeneration = UINT64_C(0x123456789);
    l->HostStackTop = UINT64_C(0xFFFF800000002000);
    l->HostRsp = UINT64_C(0xFFFF800000001FF8);
    l->GuestRsp = UINT64_C(0xFFFF800000003000);
    l->VmlaunchOutcome = 2; l->VmlaunchErrorValid = 1; l->VmlaunchError = 7;
    l->GuestRspOutcome = 1; l->GuestRflagsOutcome = 2;
    l->VmcsPlanOutcome = 1; l->VmcsPlanWriteCount = 42;
    l->LastExitReason = UINT64_C(0x80000021); l->LastExitInstructionLength = 3;
    l->LastExitQualification = UINT64_C(0xABC);
    l->LastExitGuestRip = UINT64_C(0xFFFFF80012345678);
    l->ProtocolCode = 9; l->ValidationCode = 10; l->FailClosedCode = 11;
    l->ExitVmreadOutcome = 2; l->ExitVmreadErrorValid = 1; l->ExitVmreadError = 12;
    l->GuestRipVmwriteOutcome = 2; l->GuestRipVmwriteErrorValid = 1;
    l->GuestRipVmwriteError = 13; l->FailedVmcsField = UINT64_C(0x681E);

    TEST_ASSERT_TRUE(VmxPersistentStartDiagnosticCapture(&f.Lifecycle,
        f.Prepare, f.Activate, f.Operation, f.Context, N, &d), "launch capture");
    TEST_ASSERT_UINT_EQ(d.Kind, VMX_PERSISTENT_START_FAILURE_LAUNCH, "launch kind");
    TEST_ASSERT_UINT_EQ(d.Launch.ProtocolStep, 3, "protocol");
    TEST_ASSERT_UINT_EQ(d.Launch.VmlaunchError, 7, "vmlaunch error");
    TEST_ASSERT_UINT_EQ(d.Launch.VmcsPlanWriteCount, 42, "write count");
    TEST_ASSERT_TRUE(d.Launch.LastExitGuestRip == l->LastExitGuestRip, "guest rip");
    TEST_ASSERT_UINT_EQ(d.Launch.FailClosedCode, 11, "fail closed");
    TEST_ASSERT_TRUE(d.Launch.FailedVmcsField == l->FailedVmcsField, "failed field");
    return 0;
}

static int TestCleanRollbackCapture(void)
{
    FIXTURE f;
    VMX_PERSISTENT_START_DIAGNOSTIC d = { 0 };
    VMX_PERSISTENT_START_LAUNCH_DIAGNOSTIC zero_launch = { 0 };
    Init(&f, 64);
    f.Prepare[64].FinalStatus = VMX_PERSISTENT_KERNEL_PIN_FAILED;
    f.Prepare[64].PrimaryStatus = VMX_PERSISTENT_KERNEL_PIN_FAILED;
    f.Context[64].Published = false;
    f.Context[64].OwnedResources = 0;
    f.Context[64].LaunchStorage = NULL;
    f.Context[64].LaunchStorageSize = 0;
    TEST_ASSERT_TRUE(VmxPersistentStartDiagnosticCapture(&f.Lifecycle,
        f.Prepare, f.Activate, f.Operation, f.Context, N, &d),
        "clean rollback capture");
    TEST_ASSERT_UINT_EQ(d.Kind, VMX_PERSISTENT_START_FAILURE_PREPARE_KERNEL,
        "rollback kind");
    TEST_ASSERT_UINT_EQ(d.SystemIndex, 64, "rollback cpu");
    TEST_ASSERT_INT_EQ(d.PrepareKernel.FinalStatus,
        VMX_PERSISTENT_KERNEL_PIN_FAILED, "rollback kernel result");
    TEST_ASSERT_TRUE(memcmp(&d.Launch, &zero_launch, sizeof(zero_launch)) == 0,
        "released launch unavailable");
    TEST_ASSERT_UINT_EQ(d.Trace.LifecycleOperationErrorAfter, 1,
        "operation error retained");
    TEST_ASSERT_UINT_EQ(d.Trace.LifecycleOperationIndexAfter, 64,
        "operation index retained");
    TEST_ASSERT_INT_EQ(d.Trace.LifecycleOperationStatusAfter,
        VMX_PERSISTENT_STATUS_NOT_READY, "operation status retained");
    return 0;
}

static int TestInvalidPreserves(void)
{
    FIXTURE f;
    VMX_PERSISTENT_START_DIAGNOSTIC d, before, zero = { 0 };
    Init(&f, 1);
    memset(&d, 0xA5, sizeof(d)); before = d;
    TEST_ASSERT_TRUE(!VmxPersistentStartDiagnosticCapture(&f.Lifecycle,
        f.Prepare, f.Activate, f.Operation, f.Context, N, &d), "nonzero");
    TEST_ASSERT_TRUE(memcmp(&d, &before, sizeof(d)) == 0, "preserve");
    memset(&d, 0, sizeof(d)); f.Lifecycle.HasOperationError = false;
    TEST_ASSERT_TRUE(!VmxPersistentStartDiagnosticCapture(&f.Lifecycle,
        f.Prepare, f.Activate, f.Operation, f.Context, N, &d), "no error");
    TEST_ASSERT_TRUE(memcmp(&d, &zero, sizeof(d)) == 0, "zero");
    Init(&f, 1); f.Context[1].LaunchStorageSize = sizeof(f.Launch[1]) - 1;
    TEST_ASSERT_TRUE(!VmxPersistentStartDiagnosticCapture(&f.Lifecycle,
        f.Prepare, f.Activate, f.Operation, f.Context, N, &d), "short launch");
    Init(&f, 1); f.Context[1].Published = false;
    TEST_ASSERT_TRUE(!VmxPersistentStartDiagnosticCapture(&f.Lifecycle,
        f.Prepare, f.Activate, f.Operation, f.Context, N, &d),
        "partial rollback rejected");
    return 0;
}

static int TestLifecycleFallback(void)
{
    VMX_PERSISTENT_START_DIAGNOSTIC d = { 0 }, before;
    VMX_PERSISTENT_START_TRACE trace = { 0 }, success = { 0 };
    TEST_ASSERT_TRUE(VmxPersistentStartTraceMake(
        VMX_PERSISTENT_START_BOUNDARY_TOPOLOGY_PRE,
        VMX_PERSISTENT_STATUS_NOT_READY, &trace), "make trace");
    trace.TopologyResult = VMX_PERSISTENT_TOPOLOGY_COUNT_INITIAL_MISMATCH;
    trace.ExpectedCount = 4; trace.ObservedCount = 1;
    TEST_ASSERT_TRUE(VmxPersistentStartDiagnosticCaptureLifecycle(
        VMX_PERSISTENT_STATUS_NOT_READY, &trace, &d), "fallback");
    TEST_ASSERT_UINT_EQ(d.Version, VMX_PERSISTENT_START_DIAGNOSTIC_VERSION, "version");
    TEST_ASSERT_UINT_EQ(d.Valid, 1, "valid");
    TEST_ASSERT_UINT_EQ(d.Kind, VMX_PERSISTENT_START_FAILURE_LIFECYCLE, "kind");
    TEST_ASSERT_UINT_EQ(d.SystemIndex, VMX_PERSISTENT_START_DIAGNOSTIC_NO_CPU_INDEX, "no cpu");
    TEST_ASSERT_UINT_EQ(d.Group, UINT16_MAX, "group sentinel");
    TEST_ASSERT_UINT_EQ(d.Number, UINT8_MAX, "number sentinel");
    TEST_ASSERT_INT_EQ(d.LifecycleStatus, VMX_PERSISTENT_STATUS_NOT_READY, "status");
    TEST_ASSERT_UINT_EQ(d.LifecycleState, VMX_PERSISTENT_START_TRACE_UNKNOWN_STATE,
        "state sentinel");
    TEST_ASSERT_UINT_EQ(d.Trace.Boundary,
        VMX_PERSISTENT_START_BOUNDARY_TOPOLOGY_PRE, "boundary");
    TEST_ASSERT_UINT_EQ(d.Trace.TopologyResult,
        VMX_PERSISTENT_TOPOLOGY_COUNT_INITIAL_MISMATCH, "topology reason");
    TEST_ASSERT_UINT_EQ(d.Trace.ExpectedCount, 4, "expected count");
    TEST_ASSERT_UINT_EQ(d.Trace.ObservedCount, 1, "observed count");
    before = d;
    TEST_ASSERT_TRUE(!VmxPersistentStartDiagnosticCaptureLifecycle(
        VMX_PERSISTENT_STATUS_NOT_READY, &trace, &d), "nonzero preserve");
    TEST_ASSERT_TRUE(memcmp(&d, &before, sizeof(d)) == 0, "preserved");
    memset(&d, 0, sizeof(d));
    TEST_ASSERT_TRUE(VmxPersistentStartTraceMake(
        VMX_PERSISTENT_START_BOUNDARY_COMPLETE,
        VMX_PERSISTENT_STATUS_OK, &success), "make success");
    TEST_ASSERT_TRUE(!VmxPersistentStartDiagnosticCaptureLifecycle(
        VMX_PERSISTENT_STATUS_OK, &success, &d), "success rejected");
    trace.ReturnedStatus = VMX_PERSISTENT_STATUS_BUSY;
    TEST_ASSERT_TRUE(!VmxPersistentStartDiagnosticCaptureLifecycle(
        VMX_PERSISTENT_STATUS_NOT_READY, &trace, &d), "status mismatch");
    return 0;
}
TEST_MAIN_BEGIN()
    TEST_RUN(TestKinds);
    TEST_RUN(TestLaunchSnapshot);
    TEST_RUN(TestCleanRollbackCapture);
    TEST_RUN(TestInvalidPreserves);
    TEST_RUN(TestLifecycleFallback);
TEST_MAIN_END()
