#include "test_common.h"
#include "vmx_ept_native_context.h"

#include <stddef.h>

_Static_assert(VMX_EPT_NATIVE_CONTEXT_MAGIC == UINT32_C(0x33435045),
    "native context magic");
_Static_assert(VMX_EPT_NATIVE_CONTEXT_VERSION == 1, "native context version");
_Static_assert(VMX_EPT_NATIVE_MAX_GROUPS == 64, "maximum groups");
_Static_assert(VMX_EPT_NATIVE_MAX_PROCESSORS_PER_GROUP == 64,
    "maximum processors per group");
_Static_assert(VMX_EPT_NATIVE_MAX_SYSTEM_PROCESSORS == 4096,
    "maximum system processors");
_Static_assert(sizeof(VMX_EPT_NATIVE_CONTEXT) == 24, "context size");
_Static_assert(offsetof(VMX_EPT_NATIVE_CONTEXT, RootOwned) == 16,
    "root-owned offset");

static VMX_EPT_NATIVE_CONTEXT ValidContext(void)
{
    VMX_EPT_NATIVE_CONTEXT context = { 0 };
    context.Magic = VMX_EPT_NATIVE_CONTEXT_MAGIC;
    context.Version = VMX_EPT_NATIVE_CONTEXT_VERSION;
    context.Group = 1;
    context.Number = 2;
    context.SystemIndex = 65;
    return context;
}

static int test_valid_boundaries(void)
{
    VMX_EPT_NATIVE_CONTEXT context = ValidContext();
    TEST_ASSERT_TRUE(VmxEptNativeContextValidate(&context),
        "ordinary context valid");
    context.RootOwned = 1;
    TEST_ASSERT_TRUE(VmxEptNativeContextValidate(&context),
        "owned context valid");
    context.Group = VMX_EPT_NATIVE_MAX_GROUPS - 1;
    context.Number = VMX_EPT_NATIVE_MAX_PROCESSORS_PER_GROUP - 1;
    context.SystemIndex = VMX_EPT_NATIVE_MAX_SYSTEM_PROCESSORS - 1;
    TEST_ASSERT_TRUE(VmxEptNativeContextValidate(&context),
        "maximum identity valid");
    return 0;
}

static void ExpectInvalid(VMX_EPT_NATIVE_CONTEXT context, const char* label)
{
    TEST_ASSERT_FALSE(VmxEptNativeContextValidate(&context), label);
}

static int test_authentication_rejections(void)
{
    VMX_EPT_NATIVE_CONTEXT context = ValidContext();
    TEST_ASSERT_FALSE(VmxEptNativeContextValidate(NULL), "null rejected");

    context.Magic ^= 1;
    ExpectInvalid(context, "magic rejected");
    context = ValidContext();
    context.Version++;
    ExpectInvalid(context, "version rejected");
    context = ValidContext();
    context.Reserved8 = 1;
    ExpectInvalid(context, "reserved8 rejected");
    context = ValidContext();
    context.Reserved32 = 1;
    ExpectInvalid(context, "reserved32 rejected");
    return 0;
}

static int test_identity_and_state_rejections(void)
{
    VMX_EPT_NATIVE_CONTEXT context = ValidContext();
    context.Group = VMX_EPT_NATIVE_MAX_GROUPS;
    ExpectInvalid(context, "group bound rejected");
    context = ValidContext();
    context.Number = VMX_EPT_NATIVE_MAX_PROCESSORS_PER_GROUP;
    ExpectInvalid(context, "number bound rejected");
    context = ValidContext();
    context.SystemIndex = VMX_EPT_NATIVE_MAX_SYSTEM_PROCESSORS;
    ExpectInvalid(context, "system index bound rejected");
    context = ValidContext();
    context.RootOwned = -1;
    ExpectInvalid(context, "negative ownership rejected");
    context = ValidContext();
    context.RootOwned = 2;
    ExpectInvalid(context, "unknown ownership rejected");
    return 0;
}

TEST_MAIN_BEGIN()
    TEST_RUN(test_valid_boundaries);
    TEST_RUN(test_authentication_rejections);
    TEST_RUN(test_identity_and_state_rejections);
TEST_MAIN_END()
