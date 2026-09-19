/*
 * test_vmx_lifecycle.c — Lifecycle state machine tests (R12)
 */

#include "test_common.h"
#include "vmx_lifecycle.h"

static int test_advance_all_stages(void) {
    VMX_LIFECYCLE_STATE s = {0};
    int count = 0;
    while (s.Stage != VMX_STAGE_COMPLETE) {
        TEST_ASSERT_TRUE(VmxLifecycleAdvance(&s), "Advance succeeds");
        count++;
    }
    TEST_ASSERT_INT_EQ(count, 30, "30 advances to complete");
    return 0;
}

static int test_cleanup_not_required_before_vmxon(void) {
    VMX_LIFECYCLE_STATE s = {0};
    /* Advance to just before VMXON (INSTRUCTION_INTERVAL) */
    int i;
    for (i = 0; i < VMX_STAGE_INSTRUCTION_INTERVAL; i++) VmxLifecycleAdvance(&s);
    VmxLifecycleCleanup(&s);
    TEST_ASSERT_FALSE(s.CleanupRequired, "Cleanup not required before VMXON");
    return 0;
}

static int test_cleanup_required_after_vmxon(void) {
    VMX_LIFECYCLE_STATE s = {0};
    int i;
    for (i = 0; i <= VMX_STAGE_VMXON; i++) VmxLifecycleAdvance(&s);
    VmxLifecycleCleanup(&s);
    TEST_ASSERT_TRUE(s.CleanupRequired, "Cleanup required after VMXON");
    return 0;
}

static int test_vmx_active_after_vmxon(void) {
    VMX_LIFECYCLE_STATE s = {0};
    int i;
    for (i = 0; i <= VMX_STAGE_VMXON; i++) VmxLifecycleAdvance(&s);
    TEST_ASSERT_TRUE(s.VmxActive, "VMX active after VMXON");
    return 0;
}

static int test_vmx_inactive_after_vmxoff(void) {
    VMX_LIFECYCLE_STATE s = {0};
    int i;
    for (i = 0; i < VMX_STAGE_COMPLETE; i++) VmxLifecycleAdvance(&s);
    /* VmxActive is cleared at VMXOFF */
    s.Stage = VMX_STAGE_VMXOFF;
    VmxLifecycleAdvance(&s); /* advance past it */
    TEST_ASSERT_FALSE(s.VmxActive, "VMX inactive after VMXOFF");
    return 0;
}

static int test_resource_accumulation(void) {
    VMX_LIFECYCLE_STATE s = {0};
    int i;
    for (i = 0; i < VMX_STAGE_COMPLETE; i++) VmxLifecycleAdvance(&s);
    TEST_ASSERT_TRUE((s.ResourceMask & VMX_RESOURCE_CONTEXT) != 0, "Context acquired");
    TEST_ASSERT_TRUE((s.ResourceMask & VMX_RESOURCE_STACK) != 0, "Stack acquired");
    TEST_ASSERT_TRUE((s.ResourceMask & VMX_RESOURCE_VMXON_PAGE) != 0, "VMXON page acquired");
    TEST_ASSERT_TRUE((s.ResourceMask & VMX_RESOURCE_VMCS_PAGE) != 0, "VMCS page acquired");
    return 0;
}

static int test_null_state_rejected(void) {
    TEST_ASSERT_FALSE(VmxLifecycleAdvance(NULL), "Null advance rejected");
    return 0;
}

TEST_MAIN_BEGIN()
    TEST_RUN(test_advance_all_stages);
    TEST_RUN(test_cleanup_not_required_before_vmxon);
    TEST_RUN(test_cleanup_required_after_vmxon);
    TEST_RUN(test_vmx_active_after_vmxon);
    TEST_RUN(test_vmx_inactive_after_vmxoff);
    TEST_RUN(test_resource_accumulation);
    TEST_RUN(test_null_state_rejected);
TEST_MAIN_END()
