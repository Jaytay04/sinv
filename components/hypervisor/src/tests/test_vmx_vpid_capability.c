#include "test_common.h"
#include "vmx_vpid_capability.h"

#include <string.h>

#define CAP_INVVPID (UINT64_C(1) << 32)
#define CAP_INDIVIDUAL_ADDRESS (UINT64_C(1) << 40)
#define CAP_SINGLE_CONTEXT (UINT64_C(1) << 41)
#define CAP_ALL_CONTEXTS (UINT64_C(1) << 42)
#define CAP_SINGLE_RETAINING_GLOBALS (UINT64_C(1) << 43)
#define SECONDARY_VPID_ALLOWED1 (UINT64_C(1) << (32 + 5))

static int TestExactRequiredCapabilities(void)
{
    const uint64_t Raw = CAP_INVVPID | CAP_SINGLE_CONTEXT | CAP_ALL_CONTEXTS;
    VMX_VPID_CAPABILITIES Capabilities = {0};

    TEST_ASSERT_INT_EQ(VmxVpidDecodeCapabilities(Raw, &Capabilities),
        VMX_VPID_CAPABILITY_OK, "required decode");
    TEST_ASSERT_UINT_EQ(Capabilities.Raw, Raw, "required raw");
    TEST_ASSERT_UINT_EQ(Capabilities.Flags,
        VMX_VPID_CAP_RUNTIME_REQUIRED, "required flags");
    TEST_ASSERT_UINT_EQ(Capabilities.Reserved, 0, "required reserved");
    TEST_ASSERT_INT_EQ(VmxVpidValidateCapabilities(&Capabilities),
        VMX_VPID_CAPABILITY_OK, "required validate");
    return 0;
}

static int TestOptionalCapabilities(void)
{
    const uint64_t Raw = CAP_INVVPID | CAP_INDIVIDUAL_ADDRESS |
        CAP_SINGLE_CONTEXT | CAP_ALL_CONTEXTS |
        CAP_SINGLE_RETAINING_GLOBALS;
    const uint32_t Expected = VMX_VPID_CAP_RUNTIME_REQUIRED |
        VMX_VPID_CAP_INDIVIDUAL_ADDRESS |
        VMX_VPID_CAP_SINGLE_CONTEXT_RETAINING_GLOBALS;
    VMX_VPID_CAPABILITIES Capabilities = {0};

    TEST_ASSERT_INT_EQ(VmxVpidDecodeCapabilities(Raw, &Capabilities),
        VMX_VPID_CAPABILITY_OK, "optional decode");
    TEST_ASSERT_UINT_EQ(Capabilities.Flags, Expected, "optional flags");
    TEST_ASSERT_INT_EQ(VmxVpidValidateCapabilities(&Capabilities),
        VMX_VPID_CAPABILITY_OK, "optional validate");
    return 0;
}

static int TestMissingRequiredCapabilities(void)
{
    const uint64_t Cases[] = {
        CAP_SINGLE_CONTEXT | CAP_ALL_CONTEXTS,
        CAP_INVVPID | CAP_ALL_CONTEXTS,
        CAP_INVVPID | CAP_SINGLE_CONTEXT,
        CAP_INVVPID | CAP_INDIVIDUAL_ADDRESS |
            CAP_SINGLE_RETAINING_GLOBALS
    };
    size_t Index;

    for (Index = 0; Index < sizeof(Cases) / sizeof(Cases[0]); ++Index) {
        VMX_VPID_CAPABILITIES Capabilities = {0};
        TEST_ASSERT_INT_EQ(VmxVpidDecodeCapabilities(
            Cases[Index], &Capabilities), VMX_VPID_CAPABILITY_OK,
            "unsupported decode");
        TEST_ASSERT_INT_EQ(VmxVpidValidateCapabilities(&Capabilities),
            VMX_VPID_CAPABILITY_UNSUPPORTED, "missing required rejected");
    }
    return 0;
}

static int TestMalformedCapabilities(void)
{
    const uint64_t Raw = CAP_INVVPID | CAP_SINGLE_CONTEXT | CAP_ALL_CONTEXTS;
    VMX_VPID_CAPABILITIES Capabilities = {0};

    TEST_ASSERT_INT_EQ(VmxVpidDecodeCapabilities(Raw, 0),
        VMX_VPID_CAPABILITY_INVALID, "null decode output");
    TEST_ASSERT_INT_EQ(VmxVpidValidateCapabilities(0),
        VMX_VPID_CAPABILITY_INVALID, "null validate input");

    TEST_ASSERT_INT_EQ(VmxVpidDecodeCapabilities(Raw, &Capabilities),
        VMX_VPID_CAPABILITY_OK, "malformed baseline");
    Capabilities.Flags ^= VMX_VPID_CAP_SINGLE_CONTEXT;
    TEST_ASSERT_INT_EQ(VmxVpidValidateCapabilities(&Capabilities),
        VMX_VPID_CAPABILITY_INVALID, "raw flag mismatch");

    (void)memset(&Capabilities, 0, sizeof(Capabilities));
    TEST_ASSERT_INT_EQ(VmxVpidDecodeCapabilities(Raw, &Capabilities),
        VMX_VPID_CAPABILITY_OK, "reserved baseline");
    Capabilities.Reserved = 1;
    TEST_ASSERT_INT_EQ(VmxVpidValidateCapabilities(&Capabilities),
        VMX_VPID_CAPABILITY_INVALID, "reserved rejected");
    return 0;
}

static int TestSecondaryControlAuthorization(void)
{
    TEST_ASSERT_INT_EQ(VmxVpidValidateSecondaryControls(
        SECONDARY_VPID_ALLOWED1), VMX_VPID_CAPABILITY_OK,
        "VPID secondary allowed-1 accepted");
    TEST_ASSERT_INT_EQ(VmxVpidValidateSecondaryControls(0),
        VMX_VPID_CAPABILITY_UNSUPPORTED,
        "missing VPID secondary allowed-1 rejected");
    TEST_ASSERT_INT_EQ(VmxVpidValidateSecondaryControls(
        UINT64_C(0xffffffff)), VMX_VPID_CAPABILITY_UNSUPPORTED,
        "allowed-0 half cannot authorize VPID");
    return 0;
}

TEST_MAIN_BEGIN()
    TEST_RUN(TestExactRequiredCapabilities);
    TEST_RUN(TestOptionalCapabilities);
    TEST_RUN(TestMissingRequiredCapabilities);
    TEST_RUN(TestMalformedCapabilities);
    TEST_RUN(TestSecondaryControlAuthorization);
TEST_MAIN_END()
