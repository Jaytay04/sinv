#include "test_common.h"
#include "vmx_persistent_vmcs_plan.h"

typedef struct _FIXTURE {
    VMX_PERSISTENT_CONTEXT Context;
    VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* Launch;
    VMX_CONTROL_PLAN Controls;
} FIXTURE;

static _Alignas(16) unsigned char g_host[VMX_PERSISTENT_HOST_STACK_SIZE];
static _Alignas(16) unsigned char g_guest[VMX_PERSISTENT_GUEST_STACK_SIZE];
static _Alignas(4096) unsigned char g_msr_bitmap[4096];
static VMX_V3D_PERSISTENT_LAUNCH_CONTEXT g_launch[2];
static const uint64_t g_guest_rip = UINT64_C(0xFFFFF80000001000);
static const uint64_t g_host_rip = UINT64_C(0xFFFFF80000002000);

static void Initialize(FIXTURE* f, VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* launch)
{
    VMX_PROCESSOR_STATE* s;
    memset(f, 0, sizeof(*f)); memset(launch, 0, sizeof(*launch));
    f->Launch = launch; s = &f->Context.ProcessorState;
    f->Context.Version = VMX_PERSISTENT_CONTEXT_VERSION;
    f->Context.Published = true;
    f->Context.OwnedResources = VMX_PERSISTENT_RESOURCE_LAUNCH |
        VMX_PERSISTENT_RESOURCE_STACK | VMX_PERSISTENT_RESOURCE_GUEST_STACK |
        VMX_PERSISTENT_RESOURCE_VMXON | VMX_PERSISTENT_RESOURCE_VMCS |
        VMX_PERSISTENT_RESOURCE_MSR_BITMAP;
    f->Context.LaunchStorage = f->Launch;
    f->Context.LaunchStorageSize = VMX_V3D_CONTEXT_SIZE;
    f->Context.HostStack = g_host;
    f->Context.HostStackSize = sizeof(g_host);
    f->Context.GuestStack = g_guest;
    f->Context.GuestStackSize = sizeof(g_guest);
    f->Context.VmxonPhysicalAddress = UINT64_C(0x200000);
    f->Context.VmcsPhysicalAddress = UINT64_C(0x201000);
    f->Context.MsrBitmap = g_msr_bitmap;
    f->Context.MsrBitmapPhysicalAddress = UINT64_C(0x202000);
    s->IsValid = true; s->Cr0 = 0x21; s->Cr3 = 0x22; s->Cr4 = 0x23;
    s->Dr7 = 0x400; s->Es = 0x10; s->Cs = 0x18; s->Ss = 0x20;
    s->Ds = 0x28; s->Fs = 0x30; s->Gs = 0x38; s->Tr = 0x40;
    s->EsDesc.present = s->CsDesc.present = s->SsDesc.present = true;
    s->DsDesc.present = s->FsDesc.present = s->GsDesc.present = true;
    s->TrDesc.present = true; s->TrDesc.base = 0x1000;
    s->GdtrBase = 0x2000; s->IdtrBase = 0x3000;
    f->Launch->Header.Magic = VMX_V3D_PERSISTENT_LAUNCH_MAGIC;
    f->Launch->Header.Size = VMX_V3D_CONTEXT_SIZE;
    f->Launch->Header.Version = VMX_V3D_PERSISTENT_LAUNCH_VERSION;
    f->Launch->HostStackTop = (uint64_t)(uintptr_t)(g_host + sizeof(g_host));
    f->Launch->HostRsp = f->Launch->HostStackTop - 8;
    f->Launch->GuestStackTop = (uint64_t)(uintptr_t)(g_guest + sizeof(g_guest));
    f->Launch->GuestRsp = f->Launch->GuestStackTop - 8;
    f->Launch->VmxonPhysicalAddress = f->Context.VmxonPhysicalAddress;
    f->Launch->VmcsPhysicalAddress = f->Context.VmcsPhysicalAddress;
    f->Launch->ProtocolStep = VMX_V3D_PROTOCOL_INITIALIZED;
    f->Launch->AssemblyAction = VMX_V3D_ASSEMBLY_ACTION_INVALID;
    f->Launch->ValidationFlags = VMX_V3D_VALIDATION_HEADER_OK;
    f->Controls.IsValid = true;
    f->Controls.PinControls = 1 | PIN_BASED_ACTIVATE_PREEMPTION_TIMER;
    f->Controls.PrimaryControls = CPU_BASED_USE_MSR_BITMAPS | 2u;
    f->Controls.ExitControls = 3;
    f->Controls.EntryControls = 4;
    f->Controls.PreemptionTimerValue = VMX_PREEMPTION_TIMER_INITIAL_VALUE;
    f->Controls.PreemptionTimerRate = 5;
}

static const VMX_VMCS_WRITE* Find(const VMX_VMCS_PLAN* plan, size_t field)
{
    uint32_t i;
    for (i = 1; i < plan->Count; ++i)
        if (plan->Writes[i].Field == field) return &plan->Writes[i];
    return NULL;
}

static void AssertNoDuplicateOrAssemblyFields(const VMX_VMCS_PLAN* plan)
{
    uint32_t i, j;
    for (i = 1; i < plan->Count; ++i) {
        TEST_ASSERT_TRUE(plan->Writes[i].Field != VMCS_GUEST_RSP &&
                         plan->Writes[i].Field != VMCS_GUEST_RFLAGS,
                         "assembly fields omitted");
        for (j = i + 1; j < plan->Count; ++j)
            TEST_ASSERT_TRUE(plan->Writes[i].Field != plan->Writes[j].Field,
                             "no duplicate field");
    }
}

static int test_success_control_branches_and_boundaries(void)
{
    FIXTURE f; VMX_VMCS_PLAN plan; const VMX_VMCS_WRITE* write;
    Initialize(&f, &g_launch[0]);
    TEST_ASSERT_INT_EQ(VmxPersistentBuildVmcsPlan(
        &f.Context, f.Launch, &f.Controls, g_guest_rip, g_host_rip, &plan), 0,
        "secondary off");
    TEST_ASSERT_UINT_EQ(plan.Count, 81, "off exact count");
    TEST_ASSERT_UINT_EQ(plan.Writes[1].Field, VMCS_CTRL_PIN_BASED, "controls first");
    TEST_ASSERT_UINT_EQ(plan.Writes[5].Field, VMCS_CTRL_MSR_BITMAP_ADDR, "bitmap boundary");
    TEST_ASSERT_UINT_EQ(plan.Writes[5].Value, f.Context.MsrBitmapPhysicalAddress, "bitmap PA");
    TEST_ASSERT_UINT_EQ(plan.Writes[6].Field,
        VMCS_GUEST_VMX_PREEMPTION_TIMER, "timer boundary");
    TEST_ASSERT_UINT_EQ(plan.Writes[6].Value,
        VMX_PREEMPTION_TIMER_INITIAL_VALUE, "timer value");
    TEST_ASSERT_UINT_EQ(plan.Writes[7].Field, VMCS_GUEST_ES_SELECTOR, "segments boundary");
    TEST_ASSERT_UINT_EQ(plan.Writes[43].Field, VMCS_GUEST_CR0, "machine boundary");
    TEST_ASSERT_UINT_EQ(plan.Writes[59].Field, VMCS_HOST_ES_SELECTOR, "host boundary");
    write = Find(&plan, VMCS_GUEST_RIP); TEST_ASSERT_NOT_NULL(write, "guest RIP");
    TEST_ASSERT_UINT_EQ(write->Value, g_guest_rip, "guest RIP value");
    write = Find(&plan, VMCS_HOST_RSP); TEST_ASSERT_UINT_EQ(write->Value, f.Launch->HostRsp, "host RSP value");
    write = Find(&plan, VMCS_HOST_RIP); TEST_ASSERT_UINT_EQ(write->Value, g_host_rip, "host RIP value");
    AssertNoDuplicateOrAssemblyFields(&plan);

    f.Controls.PrimaryControls |= CPU_BASED_ACTIVATE_SECONDARY_CTLS;
    f.Controls.SecondaryControls = 5;
    TEST_ASSERT_INT_EQ(VmxPersistentBuildVmcsPlan(
        &f.Context, f.Launch, &f.Controls, g_guest_rip, g_host_rip, &plan), 0,
        "secondary on");
    TEST_ASSERT_UINT_EQ(plan.Count, 82, "on exact count");
    TEST_ASSERT_UINT_EQ(plan.Writes[3].Field, VMCS_CTRL_SECONDARY, "secondary field");
    TEST_ASSERT_UINT_EQ(plan.Writes[6].Field, VMCS_CTRL_MSR_BITMAP_ADDR, "on bitmap boundary");
    TEST_ASSERT_UINT_EQ(plan.Writes[7].Field, VMCS_GUEST_VMX_PREEMPTION_TIMER, "on timer boundary");
    TEST_ASSERT_UINT_EQ(plan.Writes[8].Field, VMCS_GUEST_ES_SELECTOR, "on segments boundary");
    TEST_ASSERT_UINT_EQ(plan.Writes[44].Field, VMCS_GUEST_CR0, "on machine boundary");
    TEST_ASSERT_UINT_EQ(plan.Writes[60].Field, VMCS_HOST_ES_SELECTOR, "on host boundary");

    f.Controls.PrimaryControls |= CPU_BASED_CR3_LOAD_EXITING;
    TEST_ASSERT_INT_EQ(VmxPersistentBuildVmcsPlan(
        &f.Context, f.Launch, &f.Controls, g_guest_rip, g_host_rip, &plan), 0,
        "CR3 route on");
    TEST_ASSERT_UINT_EQ(plan.Count, 83, "CR3 exact count");
    TEST_ASSERT_UINT_EQ(plan.Writes[8].Field, VMCS_CTRL_CR3_TARGET_COUNT,
        "CR3 target count boundary");
    TEST_ASSERT_UINT_EQ(plan.Writes[8].Value, 0, "CR3 target count zero");
    TEST_ASSERT_UINT_EQ(plan.Writes[9].Field, VMCS_GUEST_ES_SELECTOR,
        "CR3 segments boundary");
    AssertNoDuplicateOrAssemblyFields(&plan);

    f.Controls.PrimaryControls |= CPU_BASED_CR3_STORE_EXITING;
    TEST_ASSERT_INT_EQ(VmxPersistentBuildVmcsPlan(
        &f.Context, f.Launch, &f.Controls, g_guest_rip, g_host_rip, &plan), 0,
        "forced CR3-store route on");
    TEST_ASSERT_UINT_EQ(plan.Count, 83, "forced CR3-store exact count");
    return 0;
}

static int test_success_without_optional_preemption_timer(void)
{
    FIXTURE f;
    VMX_VMCS_PLAN plan;

    Initialize(&f, &g_launch[0]);
    f.Controls.PinControls &= ~PIN_BASED_ACTIVATE_PREEMPTION_TIMER;
    f.Controls.PreemptionTimerValue = 0;
    f.Controls.PreemptionTimerRate = 0;
    TEST_ASSERT_INT_EQ(VmxPersistentBuildVmcsPlan(
        &f.Context, f.Launch, &f.Controls, g_guest_rip, g_host_rip, &plan), 0,
        "optional timer absent");
    TEST_ASSERT_UINT_EQ(plan.Count, 80, "timer-absent exact count");
    TEST_ASSERT_NULL(Find(&plan, VMCS_GUEST_VMX_PREEMPTION_TIMER),
        "timer field omitted");
    TEST_ASSERT_UINT_EQ(plan.Writes[6].Field, VMCS_GUEST_ES_SELECTOR,
        "timer-absent segments boundary");
    AssertNoDuplicateOrAssemblyFields(&plan);
    return 0;
}

static int test_invalid_matrix_preserves_output(void)
{
    FIXTURE f, other; VMX_VMCS_PLAN plan, before; const VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* launch;
    uint64_t guest_rip, host_rip; size_t i;
    for (i = 0; i < 32; ++i) {
        Initialize(&f, &g_launch[0]); Initialize(&other, &g_launch[1]); launch = f.Launch;
        guest_rip = g_guest_rip; host_rip = g_host_rip;
        switch (i) {
        case 0: f.Context.Version++; break;
        case 1: f.Context.Published = false; break;
        case 2: f.Context.OwnedResources &= ~VMX_PERSISTENT_RESOURCE_VMCS; break;
        case 3: f.Context.OwnedResources &= ~VMX_PERSISTENT_RESOURCE_MSR_BITMAP; break;
        case 4: f.Context.MsrBitmap = NULL; break;
        case 5: f.Context.MsrBitmapPhysicalAddress++; break;
        case 6: f.Context.MsrBitmapPhysicalAddress = f.Context.VmcsPhysicalAddress; break;
        case 7: f.Context.ProcessorState.IsValid = false; break;
        case 8: launch = other.Launch; break;
        case 9: f.Launch->Header.Magic++; break;
        case 10: f.Launch->Header.Size--; break;
        case 11: f.Launch->Header.Version++; break;
        case 12: f.Launch->VmcsPhysicalAddress += 0x1000; break;
        case 13: f.Context.VmxonPhysicalAddress = f.Context.VmcsPhysicalAddress; f.Launch->VmxonPhysicalAddress = f.Context.VmxonPhysicalAddress; break;
        case 14: f.Launch->HostRsp -= 1; break;
        case 15: f.Launch->GuestStackTop += 16; f.Launch->GuestRsp += 16; break;
        case 16: f.Launch->ProtocolStep = VMX_V3D_PROTOCOL_RESIDENT; break;
        case 17: f.Launch->AssemblyAction = VMX_V3D_ASSEMBLY_ACTION_RETURN_RESIDENT; break;
        case 18: f.Launch->ValidationFlags |= VMX_V3D_VALIDATION_READY_OBSERVED; break;
        case 19: f.Controls.IsValid = false; break;
        case 20: guest_rip = UINT64_C(0x0000800000000000); break;
        case 21: f.Controls.MissingRequired = 1; break;
        case 22: f.Controls.PrimaryControls |= CPU_BASED_HLT_EXITING; break;
        case 23: f.Controls.PrimaryControls |= CPU_BASED_USE_TSC_OFFSETTING; break;
        case 24: f.Controls.PrimaryControls |= CPU_BASED_RDTSC_EXITING; break;
        case 25: f.Controls.PrimaryControls |= CPU_BASED_INVLPG_EXITING; break;
        case 26: f.Controls.PrimaryControls &= ~CPU_BASED_USE_MSR_BITMAPS; break;
        case 27: f.Controls.PinControls &=
            ~PIN_BASED_ACTIVATE_PREEMPTION_TIMER; break;
        case 28: f.Controls.ExitControls |=
            VM_EXIT_SAVE_VMX_PREEMPTION_TIMER; break;
        case 29: f.Controls.PreemptionTimerValue = 0; break;
        case 30: f.Controls.PinControls &=
            ~PIN_BASED_ACTIVATE_PREEMPTION_TIMER;
            f.Controls.PreemptionTimerValue = 0;
            break;
        default: host_rip = 0; break;
        }
        memset(&plan, 0xA5, sizeof(plan)); before = plan;
        TEST_ASSERT_INT_EQ(VmxPersistentBuildVmcsPlan(
            &f.Context, launch, &f.Controls, guest_rip, host_rip, &plan),
            VMX_PERSISTENT_VMCS_PLAN_INVALID, "invalid case");
        TEST_ASSERT_TRUE(memcmp(&plan, &before, sizeof(plan)) == 0,
                         "invalid preserves plan");
    }
    memset(&plan, 0x5A, sizeof(plan)); before = plan;
    TEST_ASSERT_INT_EQ(VmxPersistentBuildVmcsPlan(
        NULL, NULL, NULL, 0, 0, &plan), VMX_PERSISTENT_VMCS_PLAN_INVALID,
        "null inputs");
    TEST_ASSERT_TRUE(memcmp(&plan, &before, sizeof(plan)) == 0, "null preserves");
    return 0;
}

static int test_output_overlap_preserves_every_input(void)
{
    FIXTURE f;
    FIXTURE before;
    VMX_V3D_PERSISTENT_LAUNCH_CONTEXT launch_before;
    VMX_VMCS_PLAN* outputs[4];
    size_t i;

    Initialize(&f, &g_launch[0]);
    before = f;
    launch_before = *f.Launch;
    outputs[0] = (VMX_VMCS_PLAN*)(void*)&f.Context;
    outputs[1] = (VMX_VMCS_PLAN*)(void*)((unsigned char*)&f.Context +
        _Alignof(VMX_VMCS_PLAN));
    outputs[2] = (VMX_VMCS_PLAN*)(void*)f.Launch;
    outputs[3] = (VMX_VMCS_PLAN*)(void*)&f.Controls;

    for (i = 0; i < sizeof(outputs) / sizeof(outputs[0]); ++i) {
        TEST_ASSERT_INT_EQ(VmxPersistentBuildVmcsPlan(
            &f.Context, f.Launch, &f.Controls, g_guest_rip, g_host_rip,
            outputs[i]), VMX_PERSISTENT_VMCS_PLAN_INVALID,
            "overlapping output rejected");
        TEST_ASSERT_TRUE(memcmp(&f, &before, sizeof(f)) == 0,
                         "overlap preserves fixture");
        TEST_ASSERT_TRUE(memcmp(f.Launch, &launch_before,
                         sizeof(*f.Launch)) == 0,
                         "overlap preserves launch context");
    }
    return 0;
}

static int test_wrapping_spans_are_rejected_before_use(void)
{
    FIXTURE f;
    FIXTURE before;
    VMX_V3D_PERSISTENT_LAUNCH_CONTEXT launch_before;
    VMX_VMCS_PLAN plan;
    VMX_VMCS_PLAN plan_before;
    const VMX_PERSISTENT_CONTEXT* bad_context =
        (const VMX_PERSISTENT_CONTEXT*)(uintptr_t)
        (UINTPTR_MAX - sizeof(VMX_PERSISTENT_CONTEXT) + 2u);
    const VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* bad_launch =
        (const VMX_V3D_PERSISTENT_LAUNCH_CONTEXT*)(uintptr_t)
        (UINTPTR_MAX - sizeof(VMX_V3D_PERSISTENT_LAUNCH_CONTEXT) + 2u);
    const VMX_CONTROL_PLAN* bad_controls =
        (const VMX_CONTROL_PLAN*)(uintptr_t)
        (UINTPTR_MAX - sizeof(VMX_CONTROL_PLAN) + 2u);
    VMX_VMCS_PLAN* bad_output = (VMX_VMCS_PLAN*)(uintptr_t)
        (UINTPTR_MAX - sizeof(VMX_VMCS_PLAN) + 2u);

    Initialize(&f, &g_launch[0]);
    before = f;
    launch_before = *f.Launch;
    (void)memset(&plan, 0xA5, sizeof(plan));
    plan_before = plan;

    TEST_ASSERT_INT_EQ(VmxPersistentBuildVmcsPlan(
        bad_context, f.Launch, &f.Controls, g_guest_rip, g_host_rip, &plan),
        VMX_PERSISTENT_VMCS_PLAN_INVALID, "wrapping context rejected");
    TEST_ASSERT_INT_EQ(VmxPersistentBuildVmcsPlan(
        &f.Context, bad_launch, &f.Controls, g_guest_rip, g_host_rip, &plan),
        VMX_PERSISTENT_VMCS_PLAN_INVALID, "wrapping launch rejected");
    TEST_ASSERT_INT_EQ(VmxPersistentBuildVmcsPlan(
        &f.Context, f.Launch, bad_controls, g_guest_rip, g_host_rip, &plan),
        VMX_PERSISTENT_VMCS_PLAN_INVALID, "wrapping controls rejected");
    TEST_ASSERT_INT_EQ(VmxPersistentBuildVmcsPlan(
        &f.Context, f.Launch, &f.Controls, g_guest_rip, g_host_rip, bad_output),
        VMX_PERSISTENT_VMCS_PLAN_INVALID, "wrapping output rejected");
    TEST_ASSERT_TRUE(memcmp(&plan, &plan_before, sizeof(plan)) == 0,
                     "wrapping inputs preserve output");
    TEST_ASSERT_TRUE(memcmp(&f, &before, sizeof(f)) == 0,
                     "wrapping spans preserve fixture");
    TEST_ASSERT_TRUE(memcmp(f.Launch, &launch_before,
                     sizeof(*f.Launch)) == 0,
                     "wrapping spans preserve launch context");
    return 0;
}

TEST_MAIN_BEGIN()
    TEST_RUN(test_success_control_branches_and_boundaries);
    TEST_RUN(test_success_without_optional_preemption_timer);
    TEST_RUN(test_invalid_matrix_preserves_output);
    TEST_RUN(test_output_overlap_preserves_every_input);
    TEST_RUN(test_wrapping_spans_are_rejected_before_use);
TEST_MAIN_END()
