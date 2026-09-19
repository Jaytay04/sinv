#include <stdint.h>
#include <string.h>

#include "test_common.h"
#include "driver_interface.h"
#include "vmx_process_target_policy.h"

static HYPERVISOR_PROCESS_TARGET_CONFIGURE_REQUEST ConfigureRequest(void)
{
    HYPERVISOR_PROCESS_TARGET_CONFIGURE_REQUEST Request = { 0 };
    Request.Version = HYPERVISOR_PROCESS_TARGET_VERSION;
    Request.Size = HYPERVISOR_PROCESS_TARGET_CONFIGURE_REQUEST_SIZE;
    Request.ProcessId = UINT64_C(4096);
    return Request;
}

static HYPERVISOR_PROCESS_TARGET_RELEASE_MATCHED_REQUEST MatchedRequest(void)
{
    HYPERVISOR_PROCESS_TARGET_RELEASE_MATCHED_REQUEST Request = { 0 };
    Request.Version = HYPERVISOR_PROCESS_TARGET_VERSION;
    Request.Size = HYPERVISOR_PROCESS_TARGET_RELEASE_MATCHED_REQUEST_SIZE;
    Request.ProcessId = UINT64_C(4096);
    Request.ProcessStartKey = UINT64_C(0x1234);
    Request.TargetGeneration = UINT64_C(0x5678);
    return Request;
}

static int test_validates_exact_requests(void)
{
    HYPERVISOR_PROCESS_TARGET_CONFIGURE_REQUEST Configure = ConfigureRequest();
    HYPERVISOR_PROCESS_TARGET_STATUS_QUERY_REQUEST Query = { 0 };
    HYPERVISOR_PROCESS_TARGET_RELEASE_REQUEST Release = { 0 };
    HYPERVISOR_PROCESS_TARGET_RELEASE_MATCHED_REQUEST Matched =
        MatchedRequest();
    HYPERVISOR_PROCESS_TARGET_CONFIGURE_SELF_REQUEST ConfigureSelf = { 0 };

    Query.Version = HYPERVISOR_PROCESS_TARGET_VERSION;
    Query.Size = HYPERVISOR_PROCESS_TARGET_STATUS_QUERY_REQUEST_SIZE;
    Release.Version = HYPERVISOR_PROCESS_TARGET_VERSION;
    Release.Size = HYPERVISOR_PROCESS_TARGET_RELEASE_REQUEST_SIZE;
    ConfigureSelf.Version = HYPERVISOR_PROCESS_TARGET_VERSION;
    ConfigureSelf.Size = HYPERVISOR_PROCESS_TARGET_CONFIGURE_SELF_REQUEST_SIZE;
    TEST_ASSERT_INT_EQ(VmxProcessTargetConfigureRequestValidate(
        &Configure, sizeof(Configure)), VMX_PROCESS_TARGET_POLICY_OK,
        "valid configure request");
    TEST_ASSERT_INT_EQ(VmxProcessTargetStatusQueryRequestValidate(
        &Query, sizeof(Query)), VMX_PROCESS_TARGET_POLICY_OK,
        "valid status query");
    TEST_ASSERT_INT_EQ(VmxProcessTargetReleaseRequestValidate(
        &Release, sizeof(Release)), VMX_PROCESS_TARGET_POLICY_OK,
        "valid release request");
    TEST_ASSERT_INT_EQ(VmxProcessTargetReleaseMatchedRequestValidate(
        &Matched, sizeof(Matched)), VMX_PROCESS_TARGET_POLICY_OK,
        "valid matched release request");
    TEST_ASSERT_INT_EQ(VmxProcessTargetConfigureSelfRequestValidate(
        &ConfigureSelf, sizeof(ConfigureSelf)), VMX_PROCESS_TARGET_POLICY_OK,
        "valid configure-self request");
    TEST_ASSERT_UINT_EQ(IOCTL_HYPERVISOR_PROCESS_TARGET_CONFIGURE,
        UINT32_C(0x80002060), "configure IOCTL value");
    TEST_ASSERT_UINT_EQ(IOCTL_HYPERVISOR_PROCESS_TARGET_STATUS_QUERY,
        UINT32_C(0x80002064), "status IOCTL value");
    TEST_ASSERT_UINT_EQ(IOCTL_HYPERVISOR_PROCESS_TARGET_RELEASE,
        UINT32_C(0x80002068), "release IOCTL value");
    TEST_ASSERT_UINT_EQ(IOCTL_HYPERVISOR_PROCESS_TARGET_RELEASE_MATCHED,
        UINT32_C(0x8000206C), "matched release IOCTL value");
    TEST_ASSERT_UINT_EQ(IOCTL_HYPERVISOR_PROCESS_TARGET_CONFIGURE_SELF,
        UINT32_C(0x80002070), "configure-self IOCTL value");

    Configure.ProcessId = 0;
    TEST_ASSERT_INT_EQ(VmxProcessTargetConfigureRequestValidate(
        &Configure, sizeof(Configure)), VMX_PROCESS_TARGET_POLICY_INVALID,
        "PID zero denied");
    Configure = ConfigureRequest();
    Configure.Flags = 1;
    TEST_ASSERT_INT_EQ(VmxProcessTargetConfigureRequestValidate(
        &Configure, sizeof(Configure)), VMX_PROCESS_TARGET_POLICY_INVALID,
        "unknown configure flags denied");
    Configure = ConfigureRequest();
    TEST_ASSERT_INT_EQ(VmxProcessTargetConfigureRequestValidate(
        &Configure, sizeof(Configure) - 1), VMX_PROCESS_TARGET_POLICY_INVALID,
        "short configure frame denied");
    TEST_ASSERT_INT_EQ(VmxProcessTargetConfigureRequestValidate(
        &Configure, sizeof(Configure) + 1), VMX_PROCESS_TARGET_POLICY_INVALID,
        "long configure frame denied");
    Query.Flags = 1;
    TEST_ASSERT_INT_EQ(VmxProcessTargetStatusQueryRequestValidate(
        &Query, sizeof(Query)), VMX_PROCESS_TARGET_POLICY_INVALID,
        "unknown query flags denied");
    Query.Flags = 0;
    Query.Reserved = 1;
    TEST_ASSERT_INT_EQ(VmxProcessTargetStatusQueryRequestValidate(
        &Query, sizeof(Query)), VMX_PROCESS_TARGET_POLICY_INVALID,
        "query reserved denied");
    Query.Reserved = 0;
    TEST_ASSERT_INT_EQ(VmxProcessTargetStatusQueryRequestValidate(
        &Query, sizeof(Query) - 1), VMX_PROCESS_TARGET_POLICY_INVALID,
        "short query frame denied");
    TEST_ASSERT_INT_EQ(VmxProcessTargetStatusQueryRequestValidate(
        &Query, sizeof(Query) + 1), VMX_PROCESS_TARGET_POLICY_INVALID,
        "long query frame denied");
    Release.Version++;
    TEST_ASSERT_INT_EQ(VmxProcessTargetReleaseRequestValidate(
        &Release, sizeof(Release)), VMX_PROCESS_TARGET_POLICY_INVALID,
        "release version denied");
    Release.Version = HYPERVISOR_PROCESS_TARGET_VERSION;
    Release.Size--;
    TEST_ASSERT_INT_EQ(VmxProcessTargetReleaseRequestValidate(
        &Release, sizeof(Release)), VMX_PROCESS_TARGET_POLICY_INVALID,
        "release size denied");
    Release.Size = HYPERVISOR_PROCESS_TARGET_RELEASE_REQUEST_SIZE;
    Release.Flags = 1;
    TEST_ASSERT_INT_EQ(VmxProcessTargetReleaseRequestValidate(
        &Release, sizeof(Release)), VMX_PROCESS_TARGET_POLICY_INVALID,
        "release flags denied");
    Release.Flags = 0;
    Release.Reserved = 1;
    TEST_ASSERT_INT_EQ(VmxProcessTargetReleaseRequestValidate(
        &Release, sizeof(Release)), VMX_PROCESS_TARGET_POLICY_INVALID,
        "release reserved denied");
    Release.Reserved = 0;
    TEST_ASSERT_INT_EQ(VmxProcessTargetReleaseRequestValidate(
        &Release, sizeof(Release) - 1), VMX_PROCESS_TARGET_POLICY_INVALID,
        "short release frame denied");
    TEST_ASSERT_INT_EQ(VmxProcessTargetReleaseRequestValidate(
        &Release, sizeof(Release) + 1), VMX_PROCESS_TARGET_POLICY_INVALID,
        "long release frame denied");
    Matched.ProcessId = 0;
    TEST_ASSERT_INT_EQ(VmxProcessTargetReleaseMatchedRequestValidate(
        &Matched, sizeof(Matched)), VMX_PROCESS_TARGET_POLICY_INVALID,
        "matched release PID zero denied");
    Matched = MatchedRequest();
    Matched.ProcessStartKey = 0;
    TEST_ASSERT_INT_EQ(VmxProcessTargetReleaseMatchedRequestValidate(
        &Matched, sizeof(Matched)), VMX_PROCESS_TARGET_POLICY_INVALID,
        "matched release start key zero denied");
    Matched = MatchedRequest();
    Matched.TargetGeneration = 0;
    TEST_ASSERT_INT_EQ(VmxProcessTargetReleaseMatchedRequestValidate(
        &Matched, sizeof(Matched)), VMX_PROCESS_TARGET_POLICY_INVALID,
        "matched release generation zero denied");
    Matched = MatchedRequest();
    Matched.Reserved = 1;
    TEST_ASSERT_INT_EQ(VmxProcessTargetReleaseMatchedRequestValidate(
        &Matched, sizeof(Matched)), VMX_PROCESS_TARGET_POLICY_INVALID,
        "matched release reserved denied");
    Matched = MatchedRequest();
    TEST_ASSERT_INT_EQ(VmxProcessTargetReleaseMatchedRequestValidate(
        &Matched, sizeof(Matched) - 1), VMX_PROCESS_TARGET_POLICY_INVALID,
        "short matched release denied");
    TEST_ASSERT_INT_EQ(VmxProcessTargetReleaseMatchedRequestValidate(
        &Matched, sizeof(Matched) + 1), VMX_PROCESS_TARGET_POLICY_INVALID,
        "long matched release denied");
    ConfigureSelf.Reserved = 1;
    TEST_ASSERT_INT_EQ(VmxProcessTargetConfigureSelfRequestValidate(
        &ConfigureSelf, sizeof(ConfigureSelf)), VMX_PROCESS_TARGET_POLICY_INVALID,
        "configure-self reserved denied");
    ConfigureSelf.Reserved = 0;
    TEST_ASSERT_INT_EQ(VmxProcessTargetConfigureSelfRequestValidate(
        &ConfigureSelf, sizeof(ConfigureSelf) - 1),
        VMX_PROCESS_TARGET_POLICY_INVALID, "short configure-self denied");
    TEST_ASSERT_INT_EQ(VmxProcessTargetConfigureSelfRequestValidate(
        &ConfigureSelf, sizeof(ConfigureSelf) + 1),
        VMX_PROCESS_TARGET_POLICY_INVALID, "long configure-self denied");
    return 0;
}

static int Transition(uint32_t Current, uint32_t Event, uint32_t Expected)
{
    uint32_t Next = 0;
    TEST_ASSERT_INT_EQ(VmxProcessTargetStateTransition(Current, Event, &Next),
        VMX_PROCESS_TARGET_POLICY_OK, "transition accepted");
    TEST_ASSERT_UINT_EQ(Next, Expected, "next state");
    return 0;
}

static int test_accepts_canonical_lifecycle(void)
{
    TEST_ASSERT_INT_EQ(Transition(HYPERVISOR_PROCESS_TARGET_STATE_EMPTY,
        VMX_PROCESS_TARGET_EVENT_CONFIGURE,
        HYPERVISOR_PROCESS_TARGET_STATE_CONFIGURED), 0, "configure");
    TEST_ASSERT_INT_EQ(Transition(HYPERVISOR_PROCESS_TARGET_STATE_CONFIGURED,
        VMX_PROCESS_TARGET_EVENT_ACTIVATE,
        HYPERVISOR_PROCESS_TARGET_STATE_ACTIVE), 0, "activate");
    TEST_ASSERT_INT_EQ(Transition(HYPERVISOR_PROCESS_TARGET_STATE_ACTIVE,
        VMX_PROCESS_TARGET_EVENT_DEACTIVATE,
        HYPERVISOR_PROCESS_TARGET_STATE_CONFIGURED), 0, "deactivate");
    TEST_ASSERT_INT_EQ(Transition(HYPERVISOR_PROCESS_TARGET_STATE_CONFIGURED,
        VMX_PROCESS_TARGET_EVENT_RELEASE,
        HYPERVISOR_PROCESS_TARGET_STATE_EMPTY), 0, "release");
    return 0;
}

static int test_expiry_is_sticky_until_release(void)
{
    uint32_t Next = 0;
    TEST_ASSERT_INT_EQ(Transition(HYPERVISOR_PROCESS_TARGET_STATE_ACTIVE,
        VMX_PROCESS_TARGET_EVENT_EXPIRE,
        HYPERVISOR_PROCESS_TARGET_STATE_EXPIRED), 0, "active expires");
    TEST_ASSERT_INT_EQ(Transition(HYPERVISOR_PROCESS_TARGET_STATE_EXPIRED,
        VMX_PROCESS_TARGET_EVENT_DEACTIVATE,
        HYPERVISOR_PROCESS_TARGET_STATE_EXPIRED), 0, "expiry remains");
    TEST_ASSERT_INT_EQ(VmxProcessTargetStateTransition(
        HYPERVISOR_PROCESS_TARGET_STATE_EXPIRED,
        VMX_PROCESS_TARGET_EVENT_ACTIVATE, &Next),
        VMX_PROCESS_TARGET_POLICY_DENIED, "expired target cannot reactivate");
    TEST_ASSERT_UINT_EQ(Next, 0U, "denial preserves output");
    TEST_ASSERT_INT_EQ(Transition(HYPERVISOR_PROCESS_TARGET_STATE_EXPIRED,
        VMX_PROCESS_TARGET_EVENT_RELEASE,
        HYPERVISOR_PROCESS_TARGET_STATE_EMPTY), 0, "expired release");
    return 0;
}

static int test_denies_out_of_order_transitions(void)
{
    uint32_t Next = 0;
    TEST_ASSERT_INT_EQ(VmxProcessTargetStateTransition(
        HYPERVISOR_PROCESS_TARGET_STATE_EMPTY,
        VMX_PROCESS_TARGET_EVENT_ACTIVATE, &Next),
        VMX_PROCESS_TARGET_POLICY_DENIED, "empty cannot activate");
    TEST_ASSERT_INT_EQ(VmxProcessTargetStateTransition(
        HYPERVISOR_PROCESS_TARGET_STATE_ACTIVE,
        VMX_PROCESS_TARGET_EVENT_RELEASE, &Next),
        VMX_PROCESS_TARGET_POLICY_DENIED, "active cannot release");
    TEST_ASSERT_INT_EQ(VmxProcessTargetStateTransition(UINT32_MAX,
        VMX_PROCESS_TARGET_EVENT_RELEASE, &Next),
        VMX_PROCESS_TARGET_POLICY_INVALID, "invalid state");
    TEST_ASSERT_INT_EQ(VmxProcessTargetStateTransition(
        HYPERVISOR_PROCESS_TARGET_STATE_EMPTY, UINT32_MAX, &Next),
        VMX_PROCESS_TARGET_POLICY_INVALID, "invalid event");
    return 0;
}

TEST_MAIN_BEGIN()
    TEST_RUN(test_validates_exact_requests);
    TEST_RUN(test_accepts_canonical_lifecycle);
    TEST_RUN(test_expiry_is_sticky_until_release);
    TEST_RUN(test_denies_out_of_order_transitions);
TEST_MAIN_END()
