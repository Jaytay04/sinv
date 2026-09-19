#include <string.h>
#include "test_common.h"
#include "vmx_cr3_control_policy.h"

static VMX_CONTROL_PLAN Base(void)
{
    VMX_CONTROL_PLAN Plan = { 0 };
    Plan.PinControls = 1; Plan.PrimaryControls = 2;
    Plan.SecondaryControls = 0; Plan.ExitControls = 4;
    Plan.EntryControls = 8; Plan.IsValid = true;
    return Plan;
}

static void EnableAndAppend(void)
{
    VMX_CONTROL_PLAN Input = Base(), Enabled = { 0 };
    VMX_VMCS_PLAN Vmcs = { 0 };
    Vmcs.Count = 1;
    TEST_ASSERT_INT_EQ(VmxCr3ControlsEnable(&Input,
        CPU_BASED_CR3_LOAD_EXITING, &Enabled), VMX_CR3_CONTROL_OK,
        "enable load exiting");
    TEST_ASSERT_TRUE((Enabled.PrimaryControls & CPU_BASED_CR3_LOAD_EXITING) != 0,
        "load bit set");
    TEST_ASSERT_TRUE((Enabled.PrimaryControls & CPU_BASED_CR3_STORE_EXITING) == 0,
        "store bit clear");
    TEST_ASSERT_INT_EQ(VmxCr3ControlsAppendTargetCount(&Enabled, &Vmcs),
        VMX_CR3_CONTROL_OK, "append target count");
    TEST_ASSERT_UINT_EQ(Vmcs.Count, 2, "one write appended");
    TEST_ASSERT_UINT_EQ(Vmcs.Writes[1].Field, VMCS_CTRL_CR3_TARGET_COUNT,
        "target-count field");
    TEST_ASSERT_UINT_EQ(Vmcs.Writes[1].Value, 0, "target-count zero");

    Input = Base();
    Input.PrimaryControls |= CPU_BASED_CR3_STORE_EXITING;
    memset(&Enabled, 0, sizeof(Enabled));
    memset(&Vmcs, 0, sizeof(Vmcs)); Vmcs.Count = 1;
    TEST_ASSERT_INT_EQ(VmxCr3ControlsEnable(&Input,
        CPU_BASED_CR3_LOAD_EXITING, &Enabled), VMX_CR3_CONTROL_OK,
        "hardware-forced store accepted");
    TEST_ASSERT_TRUE((Enabled.PrimaryControls &
        CPU_BASED_CR3_STORE_EXITING) != 0, "forced store preserved");
    TEST_ASSERT_TRUE((Enabled.PrimaryControls &
        CPU_BASED_CR3_LOAD_EXITING) != 0, "forced store gains load");
    TEST_ASSERT_INT_EQ(VmxCr3ControlsAppendTargetCount(&Enabled, &Vmcs),
        VMX_CR3_CONTROL_OK, "forced store target count");
}

static void RejectsAndPreserves(void)
{
    VMX_CONTROL_PLAN Input = Base(), Out, Before;
    VMX_VMCS_PLAN Vmcs, VmcsBefore;
#define ENABLE_BAD(change, status, message) do { \
    Input = Base(); change; memset(&Out, 0, sizeof(Out)); Before = Out; \
    TEST_ASSERT_INT_EQ(VmxCr3ControlsEnable(&Input, \
        CPU_BASED_CR3_LOAD_EXITING, &Out), (status), (message)); \
    TEST_ASSERT_TRUE(memcmp(&Out, &Before, sizeof(Out)) == 0, \
        message " preserves"); \
} while (0)
    ENABLE_BAD(Input.IsValid = false, VMX_CR3_CONTROL_INVALID,
        "invalid base");
    ENABLE_BAD(Input.PrimaryControls |= CPU_BASED_CR3_LOAD_EXITING,
        VMX_CR3_CONTROL_INVARIANT, "pre-enabled load");
#undef ENABLE_BAD
    Input = Base(); memset(&Out, 0, sizeof(Out)); Before = Out;
    TEST_ASSERT_INT_EQ(VmxCr3ControlsEnable(&Input, 0, &Out),
        VMX_CR3_CONTROL_UNSUPPORTED, "load exit unsupported");
    TEST_ASSERT_TRUE(memcmp(&Out, &Before, sizeof(Out)) == 0,
        "unsupported capability preserves");
    Input = Base(); memset(&Out, 0, sizeof(Out));
    TEST_ASSERT_INT_EQ(VmxCr3ControlsEnable(&Input,
        CPU_BASED_CR3_LOAD_EXITING, &Out), VMX_CR3_CONTROL_OK, "fixture");
    memset(&Vmcs, 0, sizeof(Vmcs)); Vmcs.Count = 1;
    TEST_ASSERT_TRUE(VmxVmcsPlanAddWrite(&Vmcs,
        VMCS_CTRL_CR3_TARGET_COUNT, 1), "duplicate fixture");
    VmcsBefore = Vmcs;
    TEST_ASSERT_INT_EQ(VmxCr3ControlsAppendTargetCount(&Out, &Vmcs),
        VMX_CR3_CONTROL_INVARIANT, "duplicate target count");
    TEST_ASSERT_TRUE(memcmp(&Vmcs, &VmcsBefore, sizeof(Vmcs)) == 0,
        "VMCS plan preserves");
}

int main(void)
{
    EnableAndAppend(); RejectsAndPreserves(); return 0;
}
