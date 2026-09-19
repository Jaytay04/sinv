#include "test_common.h"
#include "vmx_vpid_vmcs_coordinator.h"

#define RAW_REQUIRED ((UINT64_C(1) << 32) | (UINT64_C(1) << 41) | \
    (UINT64_C(1) << 42))

typedef struct FAKE_VMCS {
    uint64_t Secondary, Vpid;
    uint32_t Reads, Writes, FailReadCall, FailWriteCall, CorruptReadback;
} FAKE_VMCS;

static VMX_VPID_VMCS_PLATFORM* g_mutable_vmcs_platform;
static uint32_t g_vmcs_poison_calls;

static int PoisonWrite(void* Context, size_t Field, size_t Value)
{
    (void)Context;
    (void)Field;
    (void)Value;
    ++g_vmcs_poison_calls;
    return 2;
}

static int Read(void* Context, size_t Field, uint64_t* Value)
{
    FAKE_VMCS* Vmcs = (FAKE_VMCS*)Context;
    if (g_mutable_vmcs_platform != NULL)
        g_mutable_vmcs_platform->Write = PoisonWrite;
    ++Vmcs->Reads;
    if (Vmcs->FailReadCall == Vmcs->Reads) return 2;
    if (Field == VMCS_CTRL_SECONDARY) *Value = Vmcs->Secondary;
    else if (Field == VMCS_CTRL_VPID) *Value = Vmcs->Vpid;
    else return 3;
    if (Vmcs->CorruptReadback && Vmcs->Reads > 2) *Value ^= 1;
    return 1;
}

static int Write(void* Context, size_t Field, size_t Value)
{
    FAKE_VMCS* Vmcs = (FAKE_VMCS*)Context;
    ++Vmcs->Writes;
    if (Vmcs->FailWriteCall == Vmcs->Writes) return 2;
    if (Field == VMCS_CTRL_SECONDARY) Vmcs->Secondary = Value;
    else if (Field == VMCS_CTRL_VPID) Vmcs->Vpid = Value;
    else return 3;
    return 1;
}

static VMX_VPID_CAPABILITIES Caps(void)
{
    VMX_VPID_CAPABILITIES Caps = { 0 };
    TEST_ASSERT_INT_EQ(VmxVpidDecodeCapabilities(RAW_REQUIRED, &Caps),
        VMX_VPID_CAPABILITY_OK, "decode");
    return Caps;
}

static int test_activation_and_teardown(void)
{
    VMX_VPID_CAPABILITIES Capabilities = Caps();
    FAKE_VMCS Vmcs = { SECONDARY_ENABLE_EPT, 0, 0, 0, 0, 0, 0 };
    VMX_VPID_VMCS_PLATFORM Platform = { &Vmcs, Read, Write, 1, 0, 0 };
    VMX_VPID_VMCS_REPORT Report = { 0 };
    TEST_ASSERT_INT_EQ(VmxVpidVmcsActivate(
        &Capabilities, 7, &Platform, &Report), VMX_VPID_VMCS_OK,
        "transactional activation");
    TEST_ASSERT_UINT_EQ(Vmcs.Vpid, 7, "live VPID field");
    TEST_ASSERT_TRUE((Vmcs.Secondary & SECONDARY_ENABLE_VPID) != 0,
        "live secondary VPID bit");
    TEST_ASSERT_UINT_EQ(Report.Completed, 1, "activation complete");
    memset(&Report, 0, sizeof(Report));
    TEST_ASSERT_INT_EQ(VmxVpidVmcsDeactivate(7, &Platform, &Report),
        VMX_VPID_VMCS_OK, "ordered teardown");
    TEST_ASSERT_UINT_EQ(Vmcs.Vpid, 0, "teardown VPID zero");
    TEST_ASSERT_FALSE((Vmcs.Secondary & SECONDARY_ENABLE_VPID) != 0,
        "teardown clears VPID bit");
    TEST_ASSERT_INT_EQ(VmxVpidVmcsDeactivate(7, &Platform, &Report),
        VMX_VPID_VMCS_OK, "teardown retry idempotent");
    return 0;
}

static int test_activation_rollback(void)
{
    VMX_VPID_CAPABILITIES Capabilities = Caps();
    FAKE_VMCS Vmcs = { SECONDARY_ENABLE_EPT, 0, 0, 0, 0, 2, 0 };
    VMX_VPID_VMCS_PLATFORM Platform = { &Vmcs, Read, Write, 1, 0, 0 };
    VMX_VPID_VMCS_REPORT Report = { 0 };
    TEST_ASSERT_INT_EQ(VmxVpidVmcsActivate(
        &Capabilities, 3, &Platform, &Report),
        VMX_VPID_VMCS_INSTRUCTION_FAILED, "failed activation rolls back");
    TEST_ASSERT_UINT_EQ(Report.RollbackAttempted, 1, "rollback attempted");
    TEST_ASSERT_UINT_EQ(Report.RollbackComplete, 1, "rollback verified");
    TEST_ASSERT_UINT_EQ(Vmcs.Vpid, 0, "rollback VPID zero");
    TEST_ASSERT_UINT_EQ(Vmcs.Secondary, SECONDARY_ENABLE_EPT,
        "rollback controls exact");
    return 0;
}

static int test_dirty_and_preflight(void)
{
    VMX_VPID_CAPABILITIES Capabilities = Caps();
    FAKE_VMCS Vmcs = { SECONDARY_ENABLE_EPT, 0, 0, 0, 0, 0, 1 };
    VMX_VPID_VMCS_PLATFORM Platform = { &Vmcs, Read, Write, 1, 0, 0 };
    VMX_VPID_VMCS_REPORT Report = { 0 };
    /* Corrupt activation and rollback verification reads. */
    TEST_ASSERT_INT_EQ(VmxVpidVmcsActivate(&Capabilities, 2,
        &Platform, &Report), VMX_VPID_VMCS_DIRTY,
        "rollback failure remains dirty");
    Vmcs.CorruptReadback = 0; Vmcs.Reads = 0; Vmcs.Writes = 0;
    Vmcs.Secondary = 0;
    TEST_ASSERT_INT_EQ(VmxVpidVmcsActivate(&Capabilities, 2,
        &Platform, &Report), VMX_VPID_VMCS_INVALID,
        "EPT prerequisite required");
    Vmcs.Secondary = SECONDARY_ENABLE_EPT; Vmcs.Vpid = 2;
    TEST_ASSERT_INT_EQ(VmxVpidVmcsActivate(&Capabilities, 2,
        &Platform, &Report), VMX_VPID_VMCS_INVALID,
        "preexisting VPID rejected");
    return 0;
}

static int test_preflight_failure_is_reported_without_mutation(void)
{
    VMX_VPID_CAPABILITIES Capabilities = Caps();
    FAKE_VMCS Vmcs = { SECONDARY_ENABLE_EPT, 0, 0, 0, 2, 0, 0 };
    VMX_VPID_VMCS_PLATFORM Platform = { &Vmcs, Read, Write, 1, 0, 0 };
    VMX_VPID_VMCS_REPORT Report = { 0 };
    TEST_ASSERT_INT_EQ(VmxVpidVmcsActivate(&Capabilities, 9,
        &Platform, &Report), VMX_VPID_VMCS_INSTRUCTION_FAILED,
        "VPID preflight read failure reported");
    TEST_ASSERT_UINT_EQ(Report.ReadCalls, 2, "two preflight reads recorded");
    TEST_ASSERT_UINT_EQ(Report.FailedField, VMCS_CTRL_VPID,
        "VPID preflight failed field preserved");
    TEST_ASSERT_UINT_EQ(Report.Attempted, 0,
        "preflight failure did not attempt mutation");
    TEST_ASSERT_UINT_EQ(Report.WriteCalls, 0,
        "preflight failure issued no writes");
    TEST_ASSERT_UINT_EQ(Vmcs.Secondary, SECONDARY_ENABLE_EPT,
        "preflight failure preserved controls");
    TEST_ASSERT_UINT_EQ(Vmcs.Vpid, 0,
        "preflight failure preserved VPID");
    return 0;
}

static int test_vmcs_input_spans(void)
{
    union VMCS_ALIAS_STORAGE {
        VMX_VPID_CAPABILITIES Capabilities;
        VMX_VPID_VMCS_PLATFORM Platform;
        VMX_VPID_VMCS_REPORT Report;
        unsigned char Bytes[sizeof(VMX_VPID_VMCS_PLATFORM) +
            sizeof(VMX_VPID_VMCS_REPORT) + 16];
    } Storage, Before;
    VMX_VPID_CAPABILITIES Capabilities = Caps();
    VMX_VPID_VMCS_REPORT Report;

    (void)memset(&Storage, 0xA5, sizeof(Storage));
    Before = Storage;
    TEST_ASSERT_INT_EQ(VmxVpidVmcsActivate(&Capabilities, 7,
        (const VMX_VPID_VMCS_PLATFORM*)Storage.Bytes,
        (VMX_VPID_VMCS_REPORT*)Storage.Bytes), VMX_VPID_VMCS_INVALID,
        "exact VMCS platform/report overlap rejected");
    TEST_ASSERT_TRUE(memcmp(&Storage, &Before, sizeof(Storage)) == 0,
        "exact VMCS platform/report overlap preserves");

    (void)memset(&Storage, 0x5A, sizeof(Storage));
    Before = Storage;
    TEST_ASSERT_INT_EQ(VmxVpidVmcsDeactivate(7,
        (const VMX_VPID_VMCS_PLATFORM*)Storage.Bytes,
        (VMX_VPID_VMCS_REPORT*)(Storage.Bytes + 8)),
        VMX_VPID_VMCS_INVALID, "partial VMCS overlap rejected");
    TEST_ASSERT_TRUE(memcmp(&Storage, &Before, sizeof(Storage)) == 0,
        "partial VMCS overlap preserves");

    (void)memset(&Storage, 0xC3, sizeof(Storage));
    (void)memset(&Report, 0xC3, sizeof(Report));
    TEST_ASSERT_INT_EQ(VmxVpidVmcsActivate(
        (const VMX_VPID_CAPABILITIES*)Storage.Bytes, 7,
        (const VMX_VPID_VMCS_PLATFORM*)Storage.Bytes, &Report),
        VMX_VPID_VMCS_INVALID, "capabilities/platform overlap rejected");
    TEST_ASSERT_UINT_EQ(Report.Attempted, UINT32_C(0xC3C3C3C3),
        "VMCS input overlap preserves report");

    (void)memset(&Report, 0x3C, sizeof(Report));
    TEST_ASSERT_INT_EQ(VmxVpidVmcsActivate(&Capabilities, 7,
        (const VMX_VPID_VMCS_PLATFORM*)
            (UINTPTR_MAX - sizeof(VMX_VPID_VMCS_PLATFORM) + 1u),
        &Report), VMX_VPID_VMCS_INVALID,
        "wrapping VMCS platform rejected");
    TEST_ASSERT_UINT_EQ(Report.Attempted, UINT32_C(0x3C3C3C3C),
        "wrapping VMCS platform preserves report");
    return 0;
}

static int test_vmcs_platform_snapshot(void)
{
    VMX_VPID_CAPABILITIES Capabilities = Caps();
    FAKE_VMCS Vmcs = { SECONDARY_ENABLE_EPT, 0, 0, 0, 0, 0, 0 };
    VMX_VPID_VMCS_PLATFORM Platform = { &Vmcs, Read, Write, 1, 0, 0 };
    VMX_VPID_VMCS_REPORT Report = { 0 };

    g_mutable_vmcs_platform = &Platform;
    g_vmcs_poison_calls = 0;
    TEST_ASSERT_INT_EQ(VmxVpidVmcsActivate(
        &Capabilities, 7, &Platform, &Report), VMX_VPID_VMCS_OK,
        "frozen VMCS platform completes");
    g_mutable_vmcs_platform = NULL;
    TEST_ASSERT_TRUE(Platform.Write == PoisonWrite,
        "caller VMCS platform was mutated");
    TEST_ASSERT_UINT_EQ(g_vmcs_poison_calls, 0,
        "VMCS snapshot blocks poison");
    TEST_ASSERT_UINT_EQ(Report.Completed, 1,
        "VMCS snapshot retains completion");
    return 0;
}

TEST_MAIN_BEGIN()
    TEST_RUN(test_activation_and_teardown);
    TEST_RUN(test_activation_rollback);
    TEST_RUN(test_dirty_and_preflight);
    TEST_RUN(test_preflight_failure_is_reported_without_mutation);
    TEST_RUN(test_vmcs_input_spans);
    TEST_RUN(test_vmcs_platform_snapshot);
TEST_MAIN_END()
