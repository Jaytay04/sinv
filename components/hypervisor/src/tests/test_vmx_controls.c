/*
 * test_vmx_controls.c — VM-execution control planning tests (R5)
 *
 * Tests VmxPlanVmControls with true/legacy MSR pairs, CET rejection,
 * secondary control activation, and forced unsupported features.
 */

#include "test_common.h"
#include "vmx_controls.h"

/* VMX capability pairs: low bits require 1; high bits permit 1. */
#define PERMISSIVE_MUST_BE_1  0ULL
#define PERMISSIVE_ALLOWED_1  (~0ULL)


static int test_split_control_msr(void) {
    uint32_t must_be_1 = 0, allowed_1 = 0;
    VmxSplitControlMsr(0xAABBCCDD11223344ULL, &must_be_1, &allowed_1);
    TEST_ASSERT_UINT_EQ(must_be_1, 0x11223344u, "MSR low32=must-be-1");
    TEST_ASSERT_UINT_EQ(allowed_1, 0xAABBCCDDu, "MSR high32=allowed-1");
    VmxSplitControlMsr(0, NULL, &allowed_1);
    VmxSplitControlMsr(0, &must_be_1, NULL);
    return 0;
}

static int test_true_legacy_selection(void) {
    TEST_ASSERT_UINT_EQ(VmxSelectTrueControl(VMX_BASIC_TRUE_CONTROLS,
                                             0x1111, 0x2222),
                        0x1111, "True control address selected");
    TEST_ASSERT_UINT_EQ(VmxSelectTrueControl(0, 0x1111, 0x2222),
                        0x2222, "Legacy control address selected");
    return 0;
}

#define REALISTIC_PIN_CTLS      0x0000001F00000016ULL
#define REALISTIC_PRIMARY_CTLS  0xFFFFFFFF04006172ULL
#define REALISTIC_EXIT_CTLS     0x007FFFFF00036DFBULL
#define REALISTIC_ENTRY_CTLS    0xFFFFFFFF000011FBULL
#define REALISTIC_SECONDARY_CTLS 0x0000000700000000ULL

static int test_realistic_capability_fixture(void) {
    VMX_CONTROL_PLAN p = {0};
    uint32_t pin_must = 0, pin_allow = 0;
    uint32_t pri_must = 0, pri_allow = 0;
    uint32_t exit_must = 0, exit_allow = 0;
    uint32_t entry_must = 0, entry_allow = 0;
    uint32_t sec_must = 0, sec_allow = 0;

    VmxSplitControlMsr(REALISTIC_PIN_CTLS, &pin_must, &pin_allow);
    VmxSplitControlMsr(REALISTIC_PRIMARY_CTLS, &pri_must, &pri_allow);
    VmxSplitControlMsr(REALISTIC_EXIT_CTLS, &exit_must, &exit_allow);
    VmxSplitControlMsr(REALISTIC_ENTRY_CTLS, &entry_must, &entry_allow);
    VmxSplitControlMsr(REALISTIC_SECONDARY_CTLS, &sec_must, &sec_allow);

    TEST_ASSERT_UINT_EQ(pin_must, 0x16u, "real pin low mask is must-be-1");
    TEST_ASSERT_UINT_EQ(exit_allow, 0x007FFFFFu, "real exit high mask");
    TEST_ASSERT_TRUE(VmxPlanVmControls(VMX_BASIC_TRUE_CONTROLS,
        pin_must, pin_allow, pri_must, pri_allow,
        exit_must, exit_allow, entry_must, entry_allow,
        sec_must, sec_allow, &p),
        "realistic capability fixture plans");
    TEST_ASSERT_UINT_EQ(p.PrimaryControls & CPU_BASED_ACTIVATE_SECONDARY_CTLS,
                        0, "realistic primary does not force secondary");
    TEST_ASSERT_UINT_EQ(p.PrimaryControls & CPU_BASED_USE_MSR_BITMAPS,
                        0, "generic plan leaves MSR bitmap disabled");
    TEST_ASSERT_UINT_EQ(p.ExitControls & VM_EXIT_LOAD_CET_STATE,
                        0, "realistic exit keeps CET load clear");
    return 0;
}

static int test_valid_legacy(void) {
    VMX_CONTROL_PLAN p;
    uint64_t vmx_basic = 0; /* bit 55 = 0 → legacy */
    bool ok = VmxPlanVmControls(vmx_basic,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,  /* pin */
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,  /* primary */
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,  /* exit */
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,  /* entry */
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,  /* secondary */
        &p);
    TEST_ASSERT_TRUE(ok, "Legacy valid");
    TEST_ASSERT_TRUE(p.IsValid, "Legacy IsValid");
    TEST_ASSERT_UINT_EQ(p.MissingRequired, 0, "Legacy no missing");
    /* Required exit bits must be set */
    TEST_ASSERT_UINT_EQ(p.ExitControls & VM_EXIT_SAVE_DEBUG_CONTROLS,
                        VM_EXIT_SAVE_DEBUG_CONTROLS, "Legacy: save debug set");
    TEST_ASSERT_UINT_EQ(p.ExitControls & VM_EXIT_HOST_ADDR_SPACE_SIZE,
                        VM_EXIT_HOST_ADDR_SPACE_SIZE, "Legacy: 64-bit host set");
    return 0;
}

static int test_valid_true_controls(void) {
    VMX_CONTROL_PLAN p;
    uint64_t vmx_basic = VMX_BASIC_TRUE_CONTROLS;
    bool ok = VmxPlanVmControls(vmx_basic,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
        &p);
    TEST_ASSERT_TRUE(ok, "True controls valid");
    TEST_ASSERT_TRUE(p.IsValid, "True IsValid");
    return 0;
}

static int test_secondary_inactive_when_bit31_clear(void) {
    VMX_CONTROL_PLAN p;
    /* Primary high mask clears activation; forced secondary capabilities are ignored. */
    uint64_t primary_allowed_1 = ~(uint64_t)CPU_BASED_ACTIVATE_SECONDARY_CTLS;
    uint64_t secondary_must_be_1 = SECONDARY_ENABLE_EPT;
    bool ok = VmxPlanVmControls(0,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
        PERMISSIVE_MUST_BE_1, primary_allowed_1,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
        secondary_must_be_1, PERMISSIVE_ALLOWED_1,
        &p);
    TEST_ASSERT_TRUE(ok, "Secondary inactive");
    TEST_ASSERT_UINT_EQ(p.SecondaryControls, 0, "Secondary zero when inactive");
    TEST_ASSERT_UINT_EQ(p.MissingRequired, 0, "Inactive secondary no evidence");
    return 0;
}

static int test_secondary_active(void) {
    VMX_CONTROL_PLAN p;
    bool ok = VmxPlanVmControls(0,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
        CPU_BASED_ACTIVATE_SECONDARY_CTLS, PERMISSIVE_ALLOWED_1,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
        &p);
    TEST_ASSERT_TRUE(ok, "Secondary active");
    /* Secondary must NOT have forbidden features */
    TEST_ASSERT_UINT_EQ(p.SecondaryControls & SECONDARY_ENABLE_EPT, 0,
                        "EPT not set");
    TEST_ASSERT_UINT_EQ(p.SecondaryControls & SECONDARY_ENABLE_VPID, 0,
                        "VPID not set");
    return 0;
}

static int test_rejects_unavailable_exit_bits(void) {
    VMX_CONTROL_PLAN p;
    /* Exit high capability mask clears a required bit → should fail */
    uint64_t exit_allowed_1 = ~(uint64_t)VM_EXIT_HOST_ADDR_SPACE_SIZE;
    bool ok = VmxPlanVmControls(0,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
        PERMISSIVE_MUST_BE_1, exit_allowed_1,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
        &p);
    TEST_ASSERT_FALSE(ok, "Unavailable exit bit rejected");
    TEST_ASSERT_UINT_EQ(p.MissingRequired, VM_EXIT_HOST_ADDR_SPACE_SIZE,
                        "Missing: 64-bit host");
    return 0;
}

static int test_rejects_unavailable_entry_bits(void) {
    VMX_CONTROL_PLAN p;
    uint64_t entry_allowed_1 = ~(uint64_t)VM_ENTRY_IA32E_MODE_GUEST;
    bool ok = VmxPlanVmControls(0,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
        PERMISSIVE_MUST_BE_1, entry_allowed_1,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
        &p);
    TEST_ASSERT_FALSE(ok, "Unavailable entry bit rejected");
    return 0;
}

static int test_rejects_cet_exit(void) {
    VMX_CONTROL_PLAN p = {0};
    uint64_t exit_must_be_1 = VM_EXIT_LOAD_CET_STATE;
    bool ok = VmxPlanVmControls(0,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
        exit_must_be_1, PERMISSIVE_ALLOWED_1,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
        &p);
    TEST_ASSERT_FALSE(ok, "Forced CET exit rejected");
    TEST_ASSERT_FALSE(p.IsValid, "CET exit plan invalid");
    TEST_ASSERT_UINT_EQ(p.MissingRequired, VM_EXIT_LOAD_CET_STATE,
                        "CET exit rejection evidence");
    return 0;
}

static int test_rejects_cet_entry(void) {
    VMX_CONTROL_PLAN p = {0};
    uint64_t entry_must_be_1 = VM_ENTRY_LOAD_CET_STATE;
    bool ok = VmxPlanVmControls(0,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
        entry_must_be_1, PERMISSIVE_ALLOWED_1,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
        &p);
    TEST_ASSERT_FALSE(ok, "Forced CET entry rejected");
    TEST_ASSERT_FALSE(p.IsValid, "CET entry plan invalid");
    TEST_ASSERT_UINT_EQ(p.MissingRequired, VM_ENTRY_LOAD_CET_STATE,
                        "CET entry rejection evidence");
    return 0;
}

static int test_rejects_forced_ept(void) {
    VMX_CONTROL_PLAN p;
    /* Force EPT through the low capability mask; no secondary backing exists. */
    uint64_t secondary_must_be_1 = SECONDARY_ENABLE_EPT;
    bool ok = VmxPlanVmControls(0,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
        CPU_BASED_ACTIVATE_SECONDARY_CTLS, PERMISSIVE_ALLOWED_1,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
        secondary_must_be_1, PERMISSIVE_ALLOWED_1,
        &p);
    TEST_ASSERT_FALSE(ok, "Forced EPT rejected");
    TEST_ASSERT_TRUE((p.MissingRequired & SECONDARY_ENABLE_EPT) != 0,
                     "Missing evidence preserves EPT");
    return 0;
}

static int test_rejects_any_forced_secondary(void) {
    VMX_CONTROL_PLAN p;
    uint64_t secondary_must_be_1 = SECONDARY_ENABLE_RDTSCP;
    bool ok = VmxPlanVmControls(0,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
        CPU_BASED_ACTIVATE_SECONDARY_CTLS, PERMISSIVE_ALLOWED_1,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
        secondary_must_be_1, PERMISSIVE_ALLOWED_1,
        &p);
    TEST_ASSERT_FALSE(ok, "Any active secondary control rejected");
    TEST_ASSERT_TRUE((p.MissingRequired & SECONDARY_ENABLE_RDTSCP) != 0,
                     "Missing evidence preserves secondary bit");
    return 0;
}

static int test_rejects_forced_preemption_timer(void) {
    VMX_CONTROL_PLAN p;
    bool ok = VmxPlanVmControls(0,
        PIN_BASED_ACTIVATE_PREEMPTION_TIMER, PERMISSIVE_ALLOWED_1,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
        &p);
    TEST_ASSERT_FALSE(ok, "Forced VMX-preemption timer rejected");
    TEST_ASSERT_UINT_EQ(p.MissingRequired,
                        PIN_BASED_ACTIVATE_PREEMPTION_TIMER,
                        "Missing evidence preserves preemption timer bit");
    return 0;
}

static int test_rejects_forced_preemption_timer_save(void) {
    VMX_CONTROL_PLAN p = {0};
    bool ok = VmxPlanVmControls(0,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
        VM_EXIT_SAVE_VMX_PREEMPTION_TIMER, PERMISSIVE_ALLOWED_1,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
        &p);
    TEST_ASSERT_FALSE(ok, "Forced preemption timer save rejected");
    TEST_ASSERT_FALSE(p.IsValid, "Timer-save plan invalid");
    TEST_ASSERT_UINT_EQ(p.MissingRequired,
        VM_EXIT_SAVE_VMX_PREEMPTION_TIMER,
        "Timer-save rejection evidence");
    return 0;
}

static int test_enables_required_msr_bitmaps(void) {
    VMX_CONTROL_PLAN p, decorated;
    bool ok = VmxPlanVmControls(0,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
        &p);
    TEST_ASSERT_TRUE(ok, "Generic controls accepted");
    TEST_ASSERT_TRUE(VmxEnableMsrBitmapControl(
        &p, PERMISSIVE_ALLOWED_1, &decorated), "MSR bitmap enabled");
    TEST_ASSERT_UINT_EQ(decorated.PrimaryControls & CPU_BASED_USE_MSR_BITMAPS,
        CPU_BASED_USE_MSR_BITMAPS, "MSR bitmap bit enabled");
    return 0;
}

static int test_rejects_unavailable_msr_bitmaps(void) {
    VMX_CONTROL_PLAN p = {0}, decorated;
    bool ok = VmxPlanVmControls(0,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
        PERMISSIVE_MUST_BE_1,
        PERMISSIVE_ALLOWED_1 & ~(uint64_t)CPU_BASED_USE_MSR_BITMAPS,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
        &p);
    TEST_ASSERT_TRUE(ok, "Generic controls remain valid");
    memset(&decorated, 0xA5, sizeof(decorated));
    TEST_ASSERT_FALSE(VmxEnableMsrBitmapControl(
        &p, PERMISSIVE_ALLOWED_1 & ~(uint32_t)CPU_BASED_USE_MSR_BITMAPS,
        &decorated), "Unavailable MSR bitmaps rejected");
    TEST_ASSERT_FALSE(decorated.IsValid, "Failed decoration clears output");
    return 0;
}

static int test_enables_preemption_timer_watchdog(void) {
    VMX_CONTROL_PLAN p = {0}, decorated;
    bool ok = VmxPlanVmControls(0,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
        &p);
    TEST_ASSERT_TRUE(ok, "Generic controls accepted");
    TEST_ASSERT_TRUE(VmxEnablePreemptionTimerControl(
        &p, UINT64_C(5), UINT32_MAX,
        &decorated), "Preemption watchdog enabled");
    TEST_ASSERT_UINT_EQ(decorated.PinControls &
        PIN_BASED_ACTIVATE_PREEMPTION_TIMER,
        PIN_BASED_ACTIVATE_PREEMPTION_TIMER, "Pin timer enabled");
    TEST_ASSERT_UINT_EQ(decorated.ExitControls &
        VM_EXIT_SAVE_VMX_PREEMPTION_TIMER,
        0, "Exit timer save remains disabled");
    TEST_ASSERT_UINT_EQ(decorated.PreemptionTimerValue,
        VMX_PREEMPTION_TIMER_INITIAL_VALUE, "Maximum timer budget");
    TEST_ASSERT_UINT_EQ(decorated.PreemptionTimerRate, 5, "Timer rate decoded");
    return 0;
}

static int test_rejects_unavailable_preemption_timer_watchdog(void) {
    VMX_CONTROL_PLAN p = {0}, decorated;
    TEST_ASSERT_TRUE(VmxPlanVmControls(0,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
        &p), "Generic controls accepted");
    memset(&decorated, 0xA5, sizeof(decorated));
    TEST_ASSERT_FALSE(VmxEnablePreemptionTimerControl(
        &p, 0, UINT32_MAX &
            ~(uint32_t)PIN_BASED_ACTIVATE_PREEMPTION_TIMER,
        &decorated), "Missing pin support rejected");
    TEST_ASSERT_FALSE(decorated.IsValid, "Failed pin decoration clears output");
    return 0;
}

static int test_rejects_forced_transparency_intercepts(void) {
    static const uint32_t forbidden[] = {
        CPU_BASED_HLT_EXITING,
        CPU_BASED_USE_TSC_OFFSETTING,
        CPU_BASED_RDTSC_EXITING,
        CPU_BASED_INVLPG_EXITING
    };
    size_t index;

    for (index = 0; index < sizeof(forbidden) / sizeof(forbidden[0]); ++index) {
        VMX_CONTROL_PLAN p = {0};
        bool ok = VmxPlanVmControls(0,
            PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
            forbidden[index], PERMISSIVE_ALLOWED_1,
            PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
            PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
            PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
            &p);
        TEST_ASSERT_FALSE(ok, "Forced transparent-instruction control rejected");
        TEST_ASSERT_UINT_EQ(p.MissingRequired, forbidden[index],
            "Rejection evidence identifies forced primary control");
    }
    return 0;
}

static int test_null_plan_rejected(void) {
    bool ok = VmxPlanVmControls(0,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
        PERMISSIVE_MUST_BE_1, PERMISSIVE_ALLOWED_1,
        NULL);
    TEST_ASSERT_FALSE(ok, "NULL plan rejected");
    return 0;
}

TEST_MAIN_BEGIN()
    TEST_RUN(test_split_control_msr);
    TEST_RUN(test_true_legacy_selection);
    TEST_RUN(test_realistic_capability_fixture);
    TEST_RUN(test_valid_legacy);
    TEST_RUN(test_valid_true_controls);
    TEST_RUN(test_secondary_inactive_when_bit31_clear);
    TEST_RUN(test_secondary_active);
    TEST_RUN(test_rejects_unavailable_exit_bits);
    TEST_RUN(test_rejects_unavailable_entry_bits);
    TEST_RUN(test_rejects_cet_exit);
    TEST_RUN(test_rejects_cet_entry);
    TEST_RUN(test_rejects_forced_preemption_timer_save);
    TEST_RUN(test_rejects_forced_ept);
    TEST_RUN(test_rejects_any_forced_secondary);
    TEST_RUN(test_null_plan_rejected);
    TEST_RUN(test_rejects_forced_preemption_timer);
    TEST_RUN(test_enables_required_msr_bitmaps);
    TEST_RUN(test_rejects_unavailable_msr_bitmaps);
    TEST_RUN(test_enables_preemption_timer_watchdog);
    TEST_RUN(test_rejects_unavailable_preemption_timer_watchdog);
    TEST_RUN(test_rejects_forced_transparency_intercepts);
TEST_MAIN_END()
