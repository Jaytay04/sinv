/*
 * test_vmcs_plan.c — VMCS write plan tests (R6)
 *
 * Tests: exact-once insertion, duplicate rejection, overflow rejection,
 * correct unusable encoding, and no guest RSP/RFLAGS in the plan.
 */

#include "test_common.h"
#include "vmcs_plan.h"

#include <string.h>

static VMX_PROCESSOR_STATE g_state;

static void InitState(void) {
    (void)memset(&g_state, 0, sizeof(g_state));
    g_state.Cs = 0x0010; g_state.Ss = 0x0018; g_state.Tr = 0x0040;
    g_state.Es = g_state.Ds = g_state.Fs = g_state.Gs = 0;  /* null → unusable */
    g_state.Ldtr = 0;

    g_state.CsDesc.present = true;
    g_state.CsDesc.base = 0xFFFFF80000000000ULL;
    g_state.CsDesc.limit = 0xFFFFFFFFu;
    g_state.CsDesc.access_rights = 0xC09B;

    g_state.SsDesc.present = true;
    g_state.SsDesc.base = 0xFFFFF80000000000ULL;
    g_state.SsDesc.limit = 0xFFFFFFFFu;
    g_state.SsDesc.access_rights = 0xC093;

    g_state.TrDesc.present = true;
    g_state.TrDesc.is_system = true;
    g_state.TrDesc.base = 0xFFFFF80001234000ULL;
    g_state.TrDesc.limit = 0x67;
    g_state.TrDesc.access_rights = 0x008B;

    /* Null data segments → unusable */
    g_state.EsDesc.access_rights = VMX_SEGMENT_ACCESS_UNUSABLE;
    g_state.DsDesc.access_rights = VMX_SEGMENT_ACCESS_UNUSABLE;
    g_state.FsDesc.access_rights = VMX_SEGMENT_ACCESS_UNUSABLE;
    g_state.GsDesc.access_rights = VMX_SEGMENT_ACCESS_UNUSABLE;

    g_state.GdtrBase = 0xFFFFF80000001000ULL;
    g_state.IdtrBase = 0xFFFFF80000002000ULL;
    g_state.GdtrLimit = 0x57;
    g_state.IdtrLimit = 0xFFF;
}


static VMX_CONTROL_PLAN ControlPlanForFields(void)
{
    VMX_CONTROL_PLAN controls = {0};
    controls.PinControls = 0x11;
    controls.PrimaryControls = CPU_BASED_ACTIVATE_SECONDARY_CTLS | 0x22;
    controls.SecondaryControls = 0x33;
    controls.ExitControls = 0x44;
    controls.EntryControls = 0x55;
    controls.IsValid = true;
    return controls;
}

static int test_control_fields_exact_order(void)
{
    VMX_VMCS_PLAN plan = {0};
    VMX_CONTROL_PLAN controls = ControlPlanForFields();
    plan.Count = 1;
    TEST_ASSERT_UINT_EQ(VmxPlanControlFields(&plan, &controls), 5,
                        "Five control fields inserted");
    TEST_ASSERT_UINT_EQ(plan.Count, 6, "Sentinel plus five controls");
    TEST_ASSERT_UINT_EQ(plan.Writes[1].Field, VMCS_CTRL_PIN_BASED, "Pin field order");
    TEST_ASSERT_UINT_EQ(plan.Writes[2].Field, VMCS_CTRL_CPU_BASED, "Primary field order");
    TEST_ASSERT_UINT_EQ(plan.Writes[3].Field, VMCS_CTRL_SECONDARY, "Secondary field order");
    TEST_ASSERT_UINT_EQ(plan.Writes[4].Field, VMCS_CTRL_VM_EXIT_CONTROLS, "Exit field order");
    TEST_ASSERT_UINT_EQ(plan.Writes[5].Field, VMCS_CTRL_VM_ENTRY_CONTROLS, "Entry field order");
    TEST_ASSERT_UINT_EQ(plan.Writes[1].Value, controls.PinControls, "Pin value");
    TEST_ASSERT_UINT_EQ(plan.Writes[2].Value, controls.PrimaryControls, "Primary value");
    TEST_ASSERT_UINT_EQ(plan.Writes[3].Value, controls.SecondaryControls, "Secondary value");
    TEST_ASSERT_UINT_EQ(plan.Writes[4].Value, controls.ExitControls, "Exit value");
    TEST_ASSERT_UINT_EQ(plan.Writes[5].Value, controls.EntryControls, "Entry value");
    return 0;
}

static int test_control_fields_omit_inactive_secondary(void)
{
    VMX_VMCS_PLAN plan = {0};
    VMX_CONTROL_PLAN controls = ControlPlanForFields();
    uint32_t i;

    controls.PrimaryControls &= ~CPU_BASED_ACTIVATE_SECONDARY_CTLS;
    plan.Count = 1;
    TEST_ASSERT_UINT_EQ(VmxPlanControlFields(&plan, &controls), 4,
                        "Inactive secondary omits unsupported field");
    TEST_ASSERT_UINT_EQ(plan.Count, 5, "Sentinel plus four controls");
    TEST_ASSERT_UINT_EQ(plan.Writes[1].Field, VMCS_CTRL_PIN_BASED,
                        "Inactive pin field order");
    TEST_ASSERT_UINT_EQ(plan.Writes[2].Field, VMCS_CTRL_CPU_BASED,
                        "Inactive primary field order");
    TEST_ASSERT_UINT_EQ(plan.Writes[3].Field, VMCS_CTRL_VM_EXIT_CONTROLS,
                        "Inactive exit field order");
    TEST_ASSERT_UINT_EQ(plan.Writes[4].Field, VMCS_CTRL_VM_ENTRY_CONTROLS,
                        "Inactive entry field order");
    for (i = 1; i < plan.Count; i++) {
        TEST_ASSERT_TRUE(plan.Writes[i].Field != VMCS_CTRL_SECONDARY,
                         "Inactive plan has no secondary field");
    }
    return 0;
}

static int test_control_fields_duplicate_is_transactional(void)
{
    VMX_VMCS_PLAN plan;
    VMX_VMCS_PLAN before;
    VMX_CONTROL_PLAN controls = ControlPlanForFields();
    (void)memset(&plan, 0xA5, sizeof(plan));
    plan.Count = 2;
    plan.Writes[1].Field = VMCS_CTRL_CPU_BASED;
    before = plan;
    TEST_ASSERT_UINT_EQ(VmxPlanControlFields(&plan, &controls), 0,
                        "Duplicate control field rejected");
    TEST_ASSERT_TRUE(memcmp(&plan, &before, sizeof(plan)) == 0,
                     "Duplicate failure leaves plan unchanged");
    return 0;
}

static int test_control_fields_overflow_is_transactional(void)
{
    VMX_VMCS_PLAN plan;
    VMX_VMCS_PLAN before;
    VMX_CONTROL_PLAN controls = ControlPlanForFields();
    (void)memset(&plan, 0x5A, sizeof(plan));
    plan.Count = VMX_VMCS_PLAN_CAPACITY - 4;
    before = plan;
    TEST_ASSERT_UINT_EQ(VmxPlanControlFields(&plan, &controls), 0,
                        "Control field overflow rejected");
    TEST_ASSERT_TRUE(memcmp(&plan, &before, sizeof(plan)) == 0,
                     "Overflow failure leaves plan unchanged");
    return 0;
}

static int test_guest_segments_failure_is_transactional(void)
{
    VMX_VMCS_PLAN plan;
    VMX_VMCS_PLAN before;

    InitState();
    (void)memset(&plan, 0xA5, sizeof(plan));
    plan.Count = 2;
    plan.Writes[1].Field = VMCS_GUEST_GS_ACCESS;
    before = plan;
    TEST_ASSERT_UINT_EQ(VmxPlanGuestSegments(&plan, &g_state), 0,
                        "Guest duplicate rejected");
    TEST_ASSERT_TRUE(memcmp(&plan, &before, sizeof(plan)) == 0,
                     "Guest duplicate leaves plan unchanged");

    (void)memset(&plan, 0x5A, sizeof(plan));
    plan.Count = VMX_VMCS_PLAN_CAPACITY - 35;
    before = plan;
    TEST_ASSERT_UINT_EQ(VmxPlanGuestSegments(&plan, &g_state), 0,
                        "Guest overflow rejected");
    TEST_ASSERT_TRUE(memcmp(&plan, &before, sizeof(plan)) == 0,
                     "Guest overflow leaves plan unchanged");
    return 0;
}

static int test_machine_state_failure_is_transactional(void)
{
    VMX_VMCS_PLAN plan;
    VMX_VMCS_PLAN before;

    InitState();
    (void)memset(&plan, 0xA5, sizeof(plan));
    plan.Count = 2;
    plan.Writes[1].Field = VMCS_GUEST_IA32_PAT;
    before = plan;
    TEST_ASSERT_UINT_EQ(VmxPlanGuestMachineState(&plan, &g_state, 0x7000), 0,
                        "Machine-state duplicate rejected");
    TEST_ASSERT_TRUE(memcmp(&plan, &before, sizeof(plan)) == 0,
                     "Machine-state duplicate leaves plan unchanged");

    (void)memset(&plan, 0x5A, sizeof(plan));
    plan.Count = VMX_VMCS_PLAN_CAPACITY - 15;
    before = plan;
    TEST_ASSERT_UINT_EQ(VmxPlanGuestMachineState(&plan, &g_state, 0x7000), 0,
                        "Machine-state overflow rejected");
    TEST_ASSERT_TRUE(memcmp(&plan, &before, sizeof(plan)) == 0,
                     "Machine-state overflow leaves plan unchanged");
    return 0;
}

static int test_host_state_failure_is_transactional(void)
{
    VMX_VMCS_PLAN plan;
    VMX_VMCS_PLAN before;

    InitState();
    (void)memset(&plan, 0xA5, sizeof(plan));
    plan.Count = 2;
    plan.Writes[1].Field = VMCS_HOST_GDTR_BASE;
    before = plan;
    TEST_ASSERT_UINT_EQ(VmxPlanHostState(&plan, &g_state, 0x8000, 0x9000), 0,
                        "Host-state duplicate rejected");
    TEST_ASSERT_TRUE(memcmp(&plan, &before, sizeof(plan)) == 0,
                     "Host-state duplicate leaves plan unchanged");

    (void)memset(&plan, 0x5A, sizeof(plan));
    plan.Count = VMX_VMCS_PLAN_CAPACITY - 21;
    before = plan;
    TEST_ASSERT_UINT_EQ(VmxPlanHostState(&plan, &g_state, 0x8000, 0x9000), 0,
                        "Host-state overflow rejected");
    TEST_ASSERT_TRUE(memcmp(&plan, &before, sizeof(plan)) == 0,
                     "Host-state overflow leaves plan unchanged");
    return 0;
}

/* --- AddWrite --- */
static int test_add_write_valid(void) {
    VMX_VMCS_PLAN p = { .Count = 0 };
    p.Count = 1; /* must be > 0 per contract */
    TEST_ASSERT_TRUE(VmxVmcsPlanAddWrite(&p, VMCS_GUEST_CS_SELECTOR, 0x10), "Add CS selector");
    TEST_ASSERT_UINT_EQ(p.Count, 2, "Count incremented");
    TEST_ASSERT_UINT_EQ((unsigned int)p.Writes[1].Field, (unsigned int)VMCS_GUEST_CS_SELECTOR, "Field stored");
    return 0;
}

static int test_rejects_duplicate(void) {
    VMX_VMCS_PLAN p = { .Count = 0 };
    VMX_VMCS_PLAN before;
    p.Count = 1;
    VmxVmcsPlanAddWrite(&p, VMCS_GUEST_CS_SELECTOR, 0x10);
    before = p;
    TEST_ASSERT_FALSE(VmxVmcsPlanAddWrite(&p, VMCS_GUEST_CS_SELECTOR, 0x20), "Duplicate rejected");
    TEST_ASSERT_TRUE(memcmp(&p, &before, sizeof(p)) == 0,
                     "Duplicate AddWrite leaves plan unchanged");
    return 0;
}

static int test_rejects_null_plan(void) {
    TEST_ASSERT_FALSE(VmxVmcsPlanAddWrite(NULL, VMCS_GUEST_CS_SELECTOR, 0), "Null plan rejected");
    return 0;
}

static int test_rejects_zero_field(void) {
    VMX_VMCS_PLAN p = { .Count = 0 };
    p.Count = 1;
    TEST_ASSERT_FALSE(VmxVmcsPlanAddWrite(&p, 0, 0), "Zero field rejected");
    return 0;
}

static int test_rejects_overflow(void) {
    VMX_VMCS_PLAN p;
    VMX_VMCS_PLAN before;
    uint32_t i;
    (void)memset(&p, 0, sizeof(p));
    p.Count = 1;
    /* Fill to capacity */
    for (i = 0; i < VMX_VMCS_PLAN_CAPACITY - 1; i++) {
        VmxVmcsPlanAddWrite(&p, VMCS_GUEST_CS_SELECTOR + i + 1000, i);
    }
    TEST_ASSERT_UINT_EQ(p.Count, VMX_VMCS_PLAN_CAPACITY, "Plan full");
    before = p;
    TEST_ASSERT_FALSE(VmxVmcsPlanAddWrite(&p, VMCS_GUEST_CS_SELECTOR, 999), "Overflow rejected");
    TEST_ASSERT_TRUE(memcmp(&p, &before, sizeof(p)) == 0,
                     "Overflow AddWrite leaves plan unchanged");
    return 0;
}

/* --- Guest segments plan --- */
static int test_guest_segments_no_duplicates(void) {
    VMX_VMCS_PLAN p = { .Count = 0 };
    uint32_t i, j;
    InitState();
    p.Count = 1;
    VmxPlanGuestSegments(&p, &g_state);
    /* Check no two writes have the same field */
    for (i = 1; i < p.Count; i++) {
        for (j = i + 1; j < p.Count; j++) {
            if (p.Writes[i].Field == p.Writes[j].Field) {
                fprintf(stderr, "  FAIL: duplicate field at %u and %u\n", i, j);
                return 1;
            }
        }
    }
    TEST_ASSERT_TRUE(true, "No duplicates in plan");
    return 0;
}

static int test_guest_segments_no_rsp_rflags(void) {
    VMX_VMCS_PLAN p = { .Count = 0 };
    uint32_t i;
    InitState();
    p.Count = 1;
    VmxPlanGuestSegments(&p, &g_state);
    for (i = 1; i < p.Count; i++) {
        if (p.Writes[i].Field == VMCS_GUEST_RSP) {
            TEST_ASSERT_TRUE(false, "Guest RSP found — must not be in C plan");
            return 1;
        }
        if (p.Writes[i].Field == VMCS_GUEST_RFLAGS) {
            TEST_ASSERT_TRUE(false, "Guest RFLAGS found — must not be in C plan");
            return 1;
        }
    }
    TEST_ASSERT_TRUE(true, "No RSP or RFLAGS in plan");
    return 0;
}

static int test_null_segments_are_unusable(void) {
    VMX_VMCS_PLAN p = { .Count = 0 };
    InitState();
    p.Count = 1;
    VmxPlanGuestSegments(&p, &g_state);
    /* Verify ES access rights contain the unusable bit */
    uint32_t i;
    int found_es = 0;
    for (i = 1; i < p.Count; i++) {
        if (p.Writes[i].Field == VMCS_GUEST_ES_ACCESS) {
            TEST_ASSERT_UINT_EQ((unsigned int)p.Writes[i].Value,
                                VMX_SEGMENT_ACCESS_UNUSABLE,
                                "ES access = unusable");
            found_es = 1;
        }
    }
    TEST_ASSERT_TRUE(found_es, "ES access field exists in plan");
    return 0;
}

/* --- Guest machine-state plan --- */
static int test_machine_state_has_link_pointer(void) {
    VMX_VMCS_PLAN p = { .Count = 0 };
    uint32_t i;
    InitState();
    p.Count = 1;
    VmxPlanGuestMachineState(&p, &g_state, 0xFFFFF80000005000ULL);
    int found = 0;
    for (i = 1; i < p.Count; i++) {
        if (p.Writes[i].Field == VMCS_GUEST_VMCS_LINK_POINTER) {
            TEST_ASSERT_UINT_EQ((uint64_t)p.Writes[i].Value, UINT64_MAX,
                                "Link pointer = UINT64_MAX");
            found = 1;
        }
    }
    TEST_ASSERT_TRUE(found, "Link pointer field exists");
    return 0;
}

static int test_machine_state_no_rsp_rflags(void) {
    VMX_VMCS_PLAN p = { .Count = 0 };
    uint32_t i;
    InitState();
    p.Count = 1;
    VmxPlanGuestMachineState(&p, &g_state, 0xFFFFF80000005000ULL);
    for (i = 1; i < p.Count; i++) {
        if (p.Writes[i].Field == VMCS_GUEST_RSP) { return 1; }
        if (p.Writes[i].Field == VMCS_GUEST_RFLAGS) { return 1; }
    }
    TEST_ASSERT_TRUE(true, "No RSP or RFLAGS in machine state");
    return 0;
}

static int test_machine_state_zeros_are_zero(void) {
    VMX_VMCS_PLAN p = { .Count = 0 };
    uint32_t i;
    InitState();
    p.Count = 1;
    VmxPlanGuestMachineState(&p, &g_state, 0);
    for (i = 1; i < p.Count; i++) {
        if (p.Writes[i].Field == VMCS_GUEST_ACTIVITY_STATE ||
            p.Writes[i].Field == VMCS_GUEST_INTERRUPTIBILITY_STATE ||
            p.Writes[i].Field == VMCS_GUEST_PENDING_DEBUG_EXCEPTIONS ||
            p.Writes[i].Field == VMCS_GUEST_SMBASE) {
            TEST_ASSERT_UINT_EQ((unsigned int)p.Writes[i].Value, 0,
                                "Zero-init field is zero");
        }
    }
    return 0;
}

static int test_machine_state_omits_preemption_timer(void) {
    VMX_VMCS_PLAN p = { .Count = 1 };
    uint32_t i;

    InitState();
    VmxPlanGuestMachineState(&p, &g_state, 0);
    for (i = 1; i < p.Count; i++) {
        TEST_ASSERT_TRUE(p.Writes[i].Field != VMCS_GUEST_VMX_PREEMPTION_TIMER,
                         "Timer field omitted when pin control is inactive");
    }
    TEST_ASSERT_UINT_EQ(p.Count, 17, "Machine state has sixteen writes");
    return 0;
}

static int test_combined_plan_no_rsp_rflags(void) {
    VMX_VMCS_PLAN p = { .Count = 0 };
    uint32_t i;
    InitState();
    p.Count = 1;
    VmxPlanGuestSegments(&p, &g_state);
    VmxPlanGuestMachineState(&p, &g_state, 0xFFFFF80000005000ULL);
    /* Combined: 36 + 16 = 52 fields, none should be RSP or RFLAGS */
    TEST_ASSERT_UINT_EQ(p.Count, 53, "1 + 36 + 16 = 53");
    for (i = 1; i < p.Count; i++) {
        if (p.Writes[i].Field == VMCS_GUEST_RSP ||
            p.Writes[i].Field == VMCS_GUEST_RFLAGS) {
            TEST_ASSERT_FALSE(true, "RSP/RFLAGS in combined plan");
            return 1;
        }
    }
    TEST_ASSERT_TRUE(true, "No RSP/RFLAGS in combined plan");
    return 0;
}

/* --- Host-state plan --- */
static int test_host_state_upper_selector_bits(void) {
    VMX_VMCS_PLAN p = { .Count = 0 };
    uint32_t i;
    /* Use a selector above 0xFF to verify & 0xFFF8, not & 0xF8 */
    g_state.Cs = 0xFF08; /* index 0x1FE1, RPL=0 */
    p.Count = 1;
    VmxPlanHostState(&p, &g_state, 0, 0);
    for (i = 1; i < p.Count; i++) {
        if (p.Writes[i].Field == VMCS_HOST_CS_SELECTOR) {
            TEST_ASSERT_UINT_EQ((unsigned int)p.Writes[i].Value, 0xFF08,
                                "Upper selector bits preserved (not & 0xF8)");
            return 0;
        }
    }
    return 0;
}

static int test_host_state_no_cet(void) {
    VMX_VMCS_PLAN p = { .Count = 0 };
    uint32_t i;
    InitState();
    p.Count = 1;
    VmxPlanHostState(&p, &g_state, 0, 0);
    for (i = 1; i < p.Count; i++) {
        if (p.Writes[i].Field == VMCS_HOST_S_CET) return 1;
    }
    TEST_ASSERT_TRUE(true, "No host CET field in plan");
    return 0;
}

/* --- Unit B: Guest FS/GS use MSR bases, not descriptor bases --- */
static int test_guest_fs_gs_use_msr_bases(void) {
    VMX_VMCS_PLAN p = { .Count = 0 };
    uint32_t i;
    InitState();
    /* Set descriptor base to one value, MSR base to a different value */
    g_state.FsDesc.base = 0xAAAA00000000ULL;
    g_state.GsDesc.base = 0xBBBB00000000ULL;
    g_state.FsBase = 0x111100000000ULL;
    g_state.GsBase = 0x222200000000ULL;
    p.Count = 1;
    VmxPlanGuestSegments(&p, &g_state);
    int found_fs = 0, found_gs = 0;
    for (i = 1; i < p.Count; i++) {
        if (p.Writes[i].Field == VMCS_GUEST_FS_BASE) {
            TEST_ASSERT_UINT_EQ((uint64_t)p.Writes[i].Value, g_state.FsBase,
                                "Guest FS base = MSR value");
            found_fs = 1;
        }
        if (p.Writes[i].Field == VMCS_GUEST_GS_BASE) {
            TEST_ASSERT_UINT_EQ((uint64_t)p.Writes[i].Value, g_state.GsBase,
                                "Guest GS base = MSR value");
            found_gs = 1;
        }
    }
    TEST_ASSERT_TRUE(found_fs, "FS base field found");
    TEST_ASSERT_TRUE(found_gs, "GS base field found");
    return 0;
}

/* --- Unit B: Host selectors sanitized at emission time --- */
static int test_host_selector_sanitized_at_emit(void) {
    VMX_VMCS_PLAN p = { .Count = 0 };
    uint32_t i;
    bool found = false;
    InitState();
    g_state.Cs = 0x0013; /* RPL=3 — should be cleared by host plan */
    p.Count = 1;
    VmxPlanHostState(&p, &g_state, 0, 0);
    for (i = 1; i < p.Count; i++) {
        if (p.Writes[i].Field == VMCS_HOST_CS_SELECTOR) {
            TEST_ASSERT_FALSE(found, "Host CS selector appears exactly once");
            TEST_ASSERT_UINT_EQ((unsigned int)p.Writes[i].Value, 0x0010,
                                "Host CS sanitized (& 0xFFF8)");
            found = true;
        }
    }
    TEST_ASSERT_TRUE(found, "Host CS selector field exists in plan");
    return 0;
}

typedef struct _PLAN_FIELD_EXPECTED {
    size_t field;
    uint64_t value;
} PLAN_FIELD_EXPECTED;

static bool PlanHasDuplicateField(const VMX_VMCS_PLAN* plan)
{
    uint32_t i, j;
    for (i = 1; i < plan->Count; i++) {
        for (j = i + 1; j < plan->Count; j++) {
            if (plan->Writes[i].Field == plan->Writes[j].Field) return true;
        }
    }
    return false;
}

static int test_populated_state_all_required_fields_once(void)
{
    InitState();
    g_state.Cr0 = 0x10000001ULL; g_state.Cr3 = 0x12345000ULL; g_state.Cr4 = 0x2000ULL;
    g_state.Dr7 = 0x400ULL; g_state.Ia32Debugctl = 0x11ULL;
    g_state.Ia32Pat = 0x0706050403020100ULL; g_state.Ia32Efer = 0xD01ULL;
    g_state.SysenterCs = 0x10ULL; g_state.SysenterEsp = 0x3000ULL; g_state.SysenterEip = 0x4000ULL;
    g_state.CsDesc.base = 0xFFFF800000010000ULL; g_state.CsDesc.limit = 0x101u; g_state.CsDesc.access_rights = 0xC09Bu; g_state.SsDesc.base = 0xFFFF800000020000ULL; g_state.SsDesc.limit = 0x202u; g_state.SsDesc.access_rights = 0xC093u; g_state.TrDesc.base = 0xFFFF800000030000ULL; g_state.TrDesc.limit = 0x303u; g_state.TrDesc.access_rights = 0x008Bu; g_state.Es = 0x0020; g_state.Ds = 0x0028; g_state.Fs = 0x0030; g_state.Gs = 0x0038; g_state.Ldtr = 0x0048; g_state.EsDesc.present = true; g_state.EsDesc.base = 0xFFFF800000040000ULL; g_state.EsDesc.limit = 0x404u; g_state.EsDesc.access_rights = 0x0093u; g_state.DsDesc.present = true; g_state.DsDesc.base = 0xFFFF800000050000ULL; g_state.DsDesc.limit = 0x505u; g_state.DsDesc.access_rights = 0x009Bu; g_state.FsDesc.present = true; g_state.FsDesc.base = 0xFFFF800000060000ULL; g_state.FsDesc.limit = 0x606u; g_state.FsDesc.access_rights = 0x00F3u; g_state.GsDesc.present = true; g_state.GsDesc.base = 0xFFFF800000070000ULL; g_state.GsDesc.limit = 0x707u; g_state.GsDesc.access_rights = 0x00FBu; g_state.LdtrDesc.present = true; g_state.LdtrDesc.base = 0xFFFF800000080000ULL; g_state.LdtrDesc.limit = 0x808u; g_state.LdtrDesc.access_rights = 0x0082u; g_state.FsBase = 0x5000ULL; g_state.GsBase = 0x6000ULL; g_state.SysenterCs = 0x1111ULL; g_state.SysenterEsp = 0x2222ULL; g_state.SysenterEip = 0x3333ULL;
#define EXPECT(F, V) { (F), (uint64_t)(V) }
    PLAN_FIELD_EXPECTED expected[] = {
        EXPECT(VMCS_GUEST_ES_SELECTOR, g_state.Es), EXPECT(VMCS_GUEST_CS_SELECTOR, g_state.Cs), EXPECT(VMCS_GUEST_SS_SELECTOR, g_state.Ss), EXPECT(VMCS_GUEST_DS_SELECTOR, g_state.Ds), EXPECT(VMCS_GUEST_FS_SELECTOR, g_state.Fs), EXPECT(VMCS_GUEST_GS_SELECTOR, g_state.Gs), EXPECT(VMCS_GUEST_LDTR_SELECTOR, g_state.Ldtr), EXPECT(VMCS_GUEST_TR_SELECTOR, g_state.Tr),
        EXPECT(VMCS_GUEST_ES_BASE, g_state.EsDesc.base), EXPECT(VMCS_GUEST_CS_BASE, g_state.CsDesc.base), EXPECT(VMCS_GUEST_SS_BASE, g_state.SsDesc.base), EXPECT(VMCS_GUEST_DS_BASE, g_state.DsDesc.base), EXPECT(VMCS_GUEST_FS_BASE, g_state.FsBase), EXPECT(VMCS_GUEST_GS_BASE, g_state.GsBase), EXPECT(VMCS_GUEST_LDTR_BASE, g_state.LdtrDesc.base), EXPECT(VMCS_GUEST_TR_BASE, g_state.TrDesc.base),
        EXPECT(VMCS_GUEST_ES_LIMIT, g_state.EsDesc.limit), EXPECT(VMCS_GUEST_CS_LIMIT, g_state.CsDesc.limit), EXPECT(VMCS_GUEST_SS_LIMIT, g_state.SsDesc.limit), EXPECT(VMCS_GUEST_DS_LIMIT, g_state.DsDesc.limit), EXPECT(VMCS_GUEST_FS_LIMIT, g_state.FsDesc.limit), EXPECT(VMCS_GUEST_GS_LIMIT, g_state.GsDesc.limit), EXPECT(VMCS_GUEST_LDTR_LIMIT, g_state.LdtrDesc.limit), EXPECT(VMCS_GUEST_TR_LIMIT, g_state.TrDesc.limit),
        EXPECT(VMCS_GUEST_ES_ACCESS, g_state.EsDesc.access_rights), EXPECT(VMCS_GUEST_CS_ACCESS, g_state.CsDesc.access_rights), EXPECT(VMCS_GUEST_SS_ACCESS, g_state.SsDesc.access_rights), EXPECT(VMCS_GUEST_DS_ACCESS, g_state.DsDesc.access_rights), EXPECT(VMCS_GUEST_FS_ACCESS, g_state.FsDesc.access_rights), EXPECT(VMCS_GUEST_GS_ACCESS, g_state.GsDesc.access_rights), EXPECT(VMCS_GUEST_LDTR_ACCESS, g_state.LdtrDesc.access_rights), EXPECT(VMCS_GUEST_TR_ACCESS, g_state.TrDesc.access_rights),
        EXPECT(VMCS_GUEST_GDTR_BASE, g_state.GdtrBase), EXPECT(VMCS_GUEST_IDTR_BASE, g_state.IdtrBase), EXPECT(VMCS_GUEST_GDTR_LIMIT, g_state.GdtrLimit), EXPECT(VMCS_GUEST_IDTR_LIMIT, g_state.IdtrLimit), EXPECT(VMCS_GUEST_CR0, g_state.Cr0), EXPECT(VMCS_GUEST_CR3, g_state.Cr3), EXPECT(VMCS_GUEST_CR4, g_state.Cr4), EXPECT(VMCS_GUEST_DR7, g_state.Dr7),
        EXPECT(VMCS_GUEST_RIP, 0x7000ULL), EXPECT(VMCS_GUEST_IA32_DEBUGCTL, g_state.Ia32Debugctl), EXPECT(VMCS_GUEST_IA32_PAT, g_state.Ia32Pat), EXPECT(VMCS_GUEST_IA32_EFER, g_state.Ia32Efer), EXPECT(VMCS_GUEST_IA32_SYSENTER_CS, g_state.SysenterCs), EXPECT(VMCS_GUEST_IA32_SYSENTER_ESP, g_state.SysenterEsp), EXPECT(VMCS_GUEST_IA32_SYSENTER_EIP, g_state.SysenterEip), EXPECT(VMCS_GUEST_VMCS_LINK_POINTER, UINT64_MAX),
        EXPECT(VMCS_GUEST_ACTIVITY_STATE, 0), EXPECT(VMCS_GUEST_INTERRUPTIBILITY_STATE, 0), EXPECT(VMCS_GUEST_PENDING_DEBUG_EXCEPTIONS, 0), EXPECT(VMCS_GUEST_SMBASE, 0), EXPECT(VMCS_HOST_ES_SELECTOR, g_state.Es & 0xFFF8u), EXPECT(VMCS_HOST_CS_SELECTOR, g_state.Cs & 0xFFF8u), EXPECT(VMCS_HOST_SS_SELECTOR, g_state.Ss & 0xFFF8u),
        EXPECT(VMCS_HOST_DS_SELECTOR, g_state.Ds & 0xFFF8u), EXPECT(VMCS_HOST_FS_SELECTOR, g_state.Fs & 0xFFF8u), EXPECT(VMCS_HOST_GS_SELECTOR, g_state.Gs & 0xFFF8u), EXPECT(VMCS_HOST_TR_SELECTOR, g_state.Tr & 0xFFF8u), EXPECT(VMCS_HOST_CR0, g_state.Cr0), EXPECT(VMCS_HOST_CR3, g_state.Cr3), EXPECT(VMCS_HOST_CR4, g_state.Cr4), EXPECT(VMCS_HOST_FS_BASE, g_state.FsBase),
        EXPECT(VMCS_HOST_GS_BASE, g_state.GsBase), EXPECT(VMCS_HOST_TR_BASE, g_state.TrDesc.base), EXPECT(VMCS_HOST_GDTR_BASE, g_state.GdtrBase), EXPECT(VMCS_HOST_IDTR_BASE, g_state.IdtrBase), EXPECT(VMCS_HOST_IA32_SYSENTER_CS, g_state.SysenterCs), EXPECT(VMCS_HOST_IA32_SYSENTER_ESP, g_state.SysenterEsp), EXPECT(VMCS_HOST_IA32_SYSENTER_EIP, g_state.SysenterEip), EXPECT(VMCS_HOST_IA32_PAT, g_state.Ia32Pat),
        EXPECT(VMCS_HOST_IA32_EFER, g_state.Ia32Efer), EXPECT(VMCS_HOST_RSP, 0x8000ULL), EXPECT(VMCS_HOST_RIP, 0x9000ULL),
    };
#undef EXPECT
    VMX_VMCS_PLAN p = { .Count = 1 };
    VMX_CONTROL_PLAN controls = ControlPlanForFields();
    uint32_t n, count, i, j;
    TEST_ASSERT_UINT_EQ(VmxPlanControlFields(&p, &controls), 5, "Control plan succeeds");
    uint64_t actual;
    n = VmxPlanGuestSegments(&p, &g_state); TEST_ASSERT_UINT_EQ(n, 36, "Guest plan succeeds");
    n = VmxPlanGuestMachineState(&p, &g_state, 0x7000ULL); TEST_ASSERT_UINT_EQ(n, 16, "Machine plan succeeds");
    n = VmxPlanHostState(&p, &g_state, 0x8000ULL, 0x9000ULL); TEST_ASSERT_UINT_EQ(n, 22, "Host plan succeeds");
    TEST_ASSERT_UINT_EQ(p.Count, 80, "Combined plan count"); TEST_ASSERT_FALSE(PlanHasDuplicateField(&p), "Combined plan has unique fields");
    for (i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
        count = 0; actual = 0;
        for (j = 1; j < p.Count; j++) if (p.Writes[j].Field == expected[i].field) { count++; actual = p.Writes[j].Value; }
        TEST_ASSERT_UINT_EQ(count, 1, "Required field emitted exactly once");
        if (count == 1) TEST_ASSERT_UINT_EQ(actual, expected[i].value, "Required field value");
    }
    for (i = 1; i < p.Count; i++) TEST_ASSERT_TRUE(p.Writes[i].Field != VMCS_GUEST_RSP && p.Writes[i].Field != VMCS_GUEST_RFLAGS, "Guest RSP/RFLAGS remain MASM-owned");
    return 0;
}
/* --- Write adapter --- */

static int g_fake_write_count;
static int g_fake_fail_at;  /* 0-based index to fail, -1 = never */
static int g_error_read_count;
static uint64_t g_error_value;

static int FakeWriter(size_t field, size_t value) {
    (void)field; (void)value;
    g_fake_write_count++;
    if (g_fake_fail_at >= 0 && g_fake_write_count - 1 == g_fake_fail_at) {
        return VMX_VMWRITE_FAIL_VALID;
    }
    return VMX_VMWRITE_SUCCESS;
}

static int FakeInvalidWriter(size_t field, size_t value) {
    (void)field; (void)value;
    g_fake_write_count++;
    return VMX_VMWRITE_FAIL_INVALID;
}

static int FakeUnexpectedWriter(size_t field, size_t value) {
    (void)field; (void)value;
    g_fake_write_count++;
    return 99;
}

static bool FakeErrorReader(uint64_t* out) {
    g_error_read_count++;
    if (out == NULL) return false;
    *out = g_error_value;
    return true;
}

static int test_adapter_success(void) {
    VMX_VMCS_PLAN p = { .Count = 0 };
    p.Count = 1;
    VmxVmcsPlanAddWrite(&p, VMCS_GUEST_CR0, 0x100);
    VmxVmcsPlanAddWrite(&p, VMCS_GUEST_CR3, 0x200);
    g_fake_write_count = 0; g_fake_fail_at = -1;
    VMX_VMWRITE_RESULT r = VmxApplyVmcsPlan(&p, FakeWriter, NULL, true);
    TEST_ASSERT_UINT_EQ((unsigned int)r.Outcome, VMX_VMWRITE_SUCCESS, "Success");
    TEST_ASSERT_FALSE(r.VmInstructionErrorValid, "Success: error invalid");
    TEST_ASSERT_UINT_EQ(r.VmInstructionError, 0, "Success: no error read");
    TEST_ASSERT_UINT_EQ(g_fake_write_count, 2, "Both writes executed");
    return 0;
}

static int test_adapter_stops_on_first_failure(void) {
    VMX_VMCS_PLAN p = { .Count = 0 };
    p.Count = 1;
    VmxVmcsPlanAddWrite(&p, VMCS_GUEST_CR0, 0x100);
    VmxVmcsPlanAddWrite(&p, VMCS_GUEST_CR3, 0x200);
    VmxVmcsPlanAddWrite(&p, VMCS_GUEST_CR4, 0x300);
    g_fake_write_count = 0;
    g_fake_fail_at = 1;
    VMX_VMWRITE_RESULT r = VmxApplyVmcsPlan(&p, FakeWriter, NULL, true);
    TEST_ASSERT_UINT_EQ((unsigned int)r.Outcome, VMX_VMWRITE_FAIL_VALID, "Fail-valid");
    TEST_ASSERT_UINT_EQ((unsigned int)r.FailedField, (unsigned int)VMCS_GUEST_CR3,
                        "Failed field = CR3");
    TEST_ASSERT_UINT_EQ(g_fake_write_count, 2, "Stopped after 2 writes");
    return 0;
}

static int test_adapter_reads_error_on_fail_valid(void) {
    VMX_VMCS_PLAN p = { .Count = 0 };
    p.Count = 1;
    VmxVmcsPlanAddWrite(&p, VMCS_GUEST_CR0, 0x100);
    g_fake_write_count = 0; g_fake_fail_at = 0;
    g_error_read_count = 0; g_error_value = 0xDEAD;
    VMX_VMWRITE_RESULT r = VmxApplyVmcsPlan(&p, FakeWriter, FakeErrorReader, true);
    TEST_ASSERT_UINT_EQ((unsigned int)r.Outcome, VMX_VMWRITE_FAIL_VALID, "Fail-valid");
    TEST_ASSERT_TRUE(r.VmInstructionErrorValid, "Error read valid");
    TEST_ASSERT_UINT_EQ(r.VmInstructionError, 0xDEAD, "Error read on fail-valid");
    TEST_ASSERT_INT_EQ(g_error_read_count, 1, "Error reader called once");
    return 0;
}

static int test_adapter_skips_error_on_fail_invalid(void) {
    VMX_VMCS_PLAN p = { .Count = 0 };
    p.Count = 1;
    VmxVmcsPlanAddWrite(&p, VMCS_GUEST_CR0, 0x100);
    g_error_read_count = 0;
    g_fake_write_count = 0;
    VMX_VMWRITE_RESULT r = VmxApplyVmcsPlan(&p, FakeInvalidWriter, FakeErrorReader, true);
    TEST_ASSERT_UINT_EQ((unsigned int)r.Outcome, VMX_VMWRITE_FAIL_INVALID,
                        "Fail-invalid outcome");
    TEST_ASSERT_FALSE(r.VmInstructionErrorValid,
                        "Fail-invalid: error invalid");
    TEST_ASSERT_UINT_EQ(r.VmInstructionError, 0,
                        "Fail-invalid: error NOT read");
    TEST_ASSERT_INT_EQ(g_error_read_count, 0,
                        "Fail-invalid: error reader NOT called");
    TEST_ASSERT_INT_EQ(g_fake_write_count, 1,
                        "Fail-invalid writer called once");
    return 0;
}

static int AssertMalformedPlanRejected(VMX_VMCS_PLAN* plan, const char* label)
{
    VMX_VMWRITE_RESULT r;
    g_fake_write_count = 0;
    g_error_read_count = 0;
    r = VmxApplyVmcsPlan(plan, FakeWriter, FakeErrorReader, true);
    TEST_ASSERT_UINT_EQ((unsigned int)r.Outcome, VMX_VMWRITE_FAIL_INVALID,
                        label);
    TEST_ASSERT_UINT_EQ((unsigned int)r.FailedField, 0,
                        "Malformed plan has no failed hardware field");
    TEST_ASSERT_FALSE(r.VmInstructionErrorValid,
                      "Malformed plan has no instruction error");
    TEST_ASSERT_INT_EQ(g_fake_write_count, 0,
                       "Malformed plan performs no VMWRITE");
    TEST_ASSERT_INT_EQ(g_error_read_count, 0,
                       "Malformed plan performs no VMREAD");
    return 0;
}

static int test_adapter_preflights_complete_plan(void)
{
    VMX_VMCS_PLAN p;

    (void)memset(&p, 0, sizeof(p));
    TEST_ASSERT_INT_EQ(AssertMalformedPlanRejected(&p, "Count zero rejected"),
                       0, "Count zero preflight");

    p.Count = 1;
    TEST_ASSERT_INT_EQ(AssertMalformedPlanRejected(&p, "Empty plan rejected"),
                       0, "Empty plan preflight");

    p.Count = VMX_VMCS_PLAN_CAPACITY + 1;
    TEST_ASSERT_INT_EQ(AssertMalformedPlanRejected(&p, "Overflow count rejected"),
                       0, "Overflow count preflight");

    (void)memset(&p, 0, sizeof(p));
    p.Count = 2;
    p.Writes[0].Value = 1;
    p.Writes[1].Field = VMCS_GUEST_CR0;
    TEST_ASSERT_INT_EQ(AssertMalformedPlanRejected(&p, "Sentinel rejected"),
                       0, "Sentinel preflight");

    (void)memset(&p, 0, sizeof(p));
    p.Count = 2;
    p.Writes[1].Value = 1;
    TEST_ASSERT_INT_EQ(AssertMalformedPlanRejected(&p, "Zero field rejected"),
                       0, "Zero-field preflight");

    (void)memset(&p, 0, sizeof(p));
    p.Count = 3;
    p.Writes[1].Field = VMCS_GUEST_CR0;
    p.Writes[2].Field = VMCS_GUEST_CR0;
    TEST_ASSERT_INT_EQ(AssertMalformedPlanRejected(&p, "Duplicate rejected"),
                       0, "Duplicate preflight");
    return 0;
}

static int test_adapter_normalizes_unexpected_writer_result(void)
{
    VMX_VMCS_PLAN p = { .Count = 1 };
    VMX_VMWRITE_RESULT r;
    VmxVmcsPlanAddWrite(&p, VMCS_GUEST_CR0, 0x100);
    g_fake_write_count = 0;
    g_error_read_count = 0;
    r = VmxApplyVmcsPlan(&p, FakeUnexpectedWriter, FakeErrorReader, true);
    TEST_ASSERT_UINT_EQ((unsigned int)r.Outcome, VMX_VMWRITE_FAIL_INVALID,
                        "Unexpected writer result normalized");
    TEST_ASSERT_UINT_EQ((unsigned int)r.FailedField,
                        (unsigned int)VMCS_GUEST_CR0,
                        "Unexpected result records attempted field");
    TEST_ASSERT_INT_EQ(g_fake_write_count, 1, "Writer called once");
    TEST_ASSERT_INT_EQ(g_error_read_count, 0, "Error reader not called");
    return 0;
}

static bool FakeErrorReaderFailure(uint64_t* out) {
    g_error_read_count++;
    (void)out;
    return false;
}

static int test_adapter_error_validity_gate(void) {
    VMX_VMCS_PLAN p = { .Count = 1 };
    VmxVmcsPlanAddWrite(&p, VMCS_GUEST_CR0, 0x100);
    g_fake_fail_at = 0; g_fake_write_count = 0; g_error_read_count = 0;

    VMX_VMWRITE_RESULT r = VmxApplyVmcsPlan(&p, FakeWriter, FakeErrorReader, false);
    TEST_ASSERT_FALSE(r.VmInstructionErrorValid, "Current VMCS invalid suppresses read");
    TEST_ASSERT_UINT_EQ(r.VmInstructionError, 0, "Current VMCS invalid zeros error");
    TEST_ASSERT_INT_EQ(g_error_read_count, 0, "Current VMCS invalid no read");

    g_fake_write_count = 0;
    r = VmxApplyVmcsPlan(&p, FakeWriter, FakeErrorReaderFailure, true);
    TEST_ASSERT_FALSE(r.VmInstructionErrorValid, "Read failure suppresses validity");
    TEST_ASSERT_UINT_EQ(r.VmInstructionError, 0, "Read failure zeros error");
    TEST_ASSERT_INT_EQ(g_error_read_count, 1, "Read failure called once");
    return 0;
}

static int test_adapter_null_plan(void) {
    g_fake_write_count = 0;
    VMX_VMWRITE_RESULT r = VmxApplyVmcsPlan(NULL, FakeWriter, NULL, false);
    TEST_ASSERT_UINT_EQ((unsigned int)r.Outcome, VMX_VMWRITE_FAIL_INVALID, "Null plan");
    return 0;
}

static int test_adapter_null_writer(void) {
    VMX_VMCS_PLAN p = { .Count = 0 };
    p.Count = 1;
    VMX_VMWRITE_RESULT r = VmxApplyVmcsPlan(&p, NULL, NULL, false);
    TEST_ASSERT_UINT_EQ((unsigned int)r.Outcome, VMX_VMWRITE_FAIL_INVALID, "Null writer");
    return 0;
}

TEST_MAIN_BEGIN()
    TEST_RUN(test_control_fields_exact_order);
    TEST_RUN(test_control_fields_duplicate_is_transactional);
    TEST_RUN(test_control_fields_omit_inactive_secondary);
    TEST_RUN(test_control_fields_overflow_is_transactional);
    TEST_RUN(test_guest_segments_failure_is_transactional);
    TEST_RUN(test_machine_state_failure_is_transactional);
    TEST_RUN(test_host_state_failure_is_transactional);
    TEST_RUN(test_add_write_valid);
    TEST_RUN(test_rejects_duplicate);
    TEST_RUN(test_rejects_null_plan);
    TEST_RUN(test_rejects_zero_field);
    TEST_RUN(test_rejects_overflow);
    TEST_RUN(test_guest_segments_no_duplicates);
    TEST_RUN(test_guest_segments_no_rsp_rflags);
    TEST_RUN(test_null_segments_are_unusable);
    TEST_RUN(test_machine_state_has_link_pointer);
    TEST_RUN(test_machine_state_no_rsp_rflags);
    TEST_RUN(test_machine_state_zeros_are_zero);
    TEST_RUN(test_combined_plan_no_rsp_rflags);
    TEST_RUN(test_machine_state_omits_preemption_timer);
    TEST_RUN(test_host_state_upper_selector_bits);
    TEST_RUN(test_host_state_no_cet);
    TEST_RUN(test_guest_fs_gs_use_msr_bases);
    TEST_RUN(test_host_selector_sanitized_at_emit);
    TEST_RUN(test_populated_state_all_required_fields_once);
    TEST_RUN(test_adapter_success);
    TEST_RUN(test_adapter_stops_on_first_failure);
    TEST_RUN(test_adapter_reads_error_on_fail_valid);
    TEST_RUN(test_adapter_skips_error_on_fail_invalid);
    TEST_RUN(test_adapter_preflights_complete_plan);
    TEST_RUN(test_adapter_normalizes_unexpected_writer_result);
    TEST_RUN(test_adapter_error_validity_gate);
    TEST_RUN(test_adapter_null_plan);
    TEST_RUN(test_adapter_null_writer);
TEST_MAIN_END()
