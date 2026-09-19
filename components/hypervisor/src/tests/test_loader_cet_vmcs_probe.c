#include "test_common.h"
#include "loader_cet_vmcs_probe.h"
#include "vmx.h"

static int TestSuppressedState(void)
{
    TEST_ASSERT_TRUE(LoaderCetVmcsStateSuppressed(0, 0, 0, 0, 0, 0),
        "all suppression state clear");
    TEST_ASSERT_FALSE(LoaderCetVmcsStateSuppressed(CPU_BASED_HLT_EXITING,
        0, 0, 0, 0, 0), "HLT exiting rejected");
    TEST_ASSERT_FALSE(LoaderCetVmcsStateSuppressed(0,
        VM_EXIT_LOAD_CET_STATE, 0, 0, 0, 0), "exit CET load rejected");
    TEST_ASSERT_FALSE(LoaderCetVmcsStateSuppressed(0, 0,
        VM_ENTRY_LOAD_CET_STATE, 0, 0, 0), "entry CET load rejected");
    TEST_ASSERT_FALSE(LoaderCetVmcsStateSuppressed(0, 0, 0,
        CR4_CET, 0, 0), "guest CR4.CET rejected");
    TEST_ASSERT_FALSE(LoaderCetVmcsStateSuppressed(0, 0, 0,
        0, CR4_CET, 0), "host CR4.CET rejected");
    TEST_ASSERT_FALSE(LoaderCetVmcsStateSuppressed(0, 0, 0,
        0, 0, CR4_CET), "root CR4.CET rejected");
    TEST_ASSERT_TRUE(LoaderCetVmcsStateSuppressed(1u << 12,
        1u << 9, 1u << 15, 1u << 20, 1u << 20, 1u << 20),
        "unrelated bits accepted");
    return 0;
}

TEST_MAIN_BEGIN()
    TEST_RUN(TestSuppressedState);
TEST_MAIN_END()
