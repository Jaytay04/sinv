#include <string.h>
#include "test_common.h"
#include "vmx_cr3_exit_decode.h"

static VMX_CR3_TARGET_SET BuildSet(uint32_t Pcide)
{
    const uint64_t Roots[2] = { UINT64_C(0x12345000), UINT64_C(0x6789a000) };
    VMX_CR3_TARGET_SET Set = { 0 };
    TEST_ASSERT_INT_EQ(VmxCr3TargetSetBuild(48, Pcide, Roots, 2, 17, &Set),
        VMX_CR3_TARGET_OK, "fixture build");
    return Set;
}

static void DecodeEveryGpr(void)
{
    VMX_CR3_TARGET_SET Set = BuildSet(1);
    uint64_t Gprs[VMX_CR3_EXIT_GPR_COUNT] = { 0 };
    uint32_t Index;
    for (Index = 0; Index < VMX_CR3_EXIT_GPR_COUNT; ++Index) {
        VMX_CR3_EXIT_DECISION Out = { 0 };
        const uint64_t Operand = Set.Roots[Index & 1] | Index |
            (Index == 15 ? VMX_CR3_TARGET_NO_FLUSH : 0);
        Gprs[Index] = Operand;
        TEST_ASSERT_INT_EQ(VmxCr3ExitDecode(&Set,
            VMX_CR3_EXIT_CR3 | ((uint64_t)Index << VMX_CR3_EXIT_GPR_SHIFT),
            Gprs, VMX_CR3_EXIT_GPR_COUNT, &Out), VMX_CR3_TARGET_OK,
            "GPR decode");
        TEST_ASSERT_UINT_EQ(Out.Version, VMX_CR3_EXIT_DECISION_VERSION,
            "decision version");
        TEST_ASSERT_UINT_EQ(Out.Action,
            VMX_CR3_EXIT_ACTION_WRITE_GUEST_CR3_ADVANCE, "decision action");
        TEST_ASSERT_UINT_EQ(Out.GprIndex, Index, "GPR index");
        TEST_ASSERT_UINT_EQ(Out.Target.Operand, Operand, "operand selected");
        TEST_ASSERT_UINT_EQ(Out.Target.Matched, 1, "target matched");
        TEST_ASSERT_UINT_EQ(Out.Target.MatchedIndex, Index & 1,
            "target index");
        TEST_ASSERT_UINT_EQ(Out.Target.Pcid, Index, "PCID selected");
        TEST_ASSERT_UINT_EQ(Out.Target.NoFlush, Index == 15,
            "no-flush selected");
        Gprs[Index] = 0;
    }
}

static void NonTargetIsWellFormed(void)
{
    VMX_CR3_TARGET_SET Set = BuildSet(0);
    uint64_t Gprs[VMX_CR3_EXIT_GPR_COUNT] = { 0 };
    VMX_CR3_EXIT_DECISION Out = { 0 };
    Gprs[9] = UINT64_C(0xabcde000);
    TEST_ASSERT_INT_EQ(VmxCr3ExitDecode(&Set,
        VMX_CR3_EXIT_CR3 | (UINT64_C(9) << VMX_CR3_EXIT_GPR_SHIFT),
        Gprs, VMX_CR3_EXIT_GPR_COUNT, &Out), VMX_CR3_TARGET_OK,
        "non-target decode");
    TEST_ASSERT_UINT_EQ(Out.Target.Matched, 0, "non-target unmatched");
    TEST_ASSERT_UINT_EQ(Out.Target.MatchedIndex, UINT32_MAX,
        "non-target no index");
    TEST_ASSERT_UINT_EQ(Out.Target.NormalizedRoot, Gprs[9],
        "non-target normalized");
}

static void RejectsAndPreserves(void)
{
    VMX_CR3_TARGET_SET Set = BuildSet(0);
    uint64_t Gprs[VMX_CR3_EXIT_GPR_COUNT] = { 0 };
    VMX_CR3_EXIT_DECISION Out, Before;
#define REJECT(qual, count, status, message) do { \
    memset(&Out, 0xA5, sizeof(Out)); Before = Out; \
    TEST_ASSERT_INT_EQ(VmxCr3ExitDecode(&Set, (qual), Gprs, (count), &Out), \
        (status), (message)); \
    TEST_ASSERT_TRUE(memcmp(&Out, &Before, sizeof(Out)) == 0, \
        message " preserves"); \
} while (0)
    REJECT(0, VMX_CR3_EXIT_GPR_COUNT, VMX_CR3_TARGET_UNSUPPORTED,
        "CR0 access");
    REJECT(VMX_CR3_EXIT_CR3 | UINT64_C(0x10), VMX_CR3_EXIT_GPR_COUNT,
        VMX_CR3_TARGET_UNSUPPORTED, "MOV-from CR3");
    REJECT(VMX_CR3_EXIT_CR3 | UINT64_C(0x40), VMX_CR3_EXIT_GPR_COUNT,
        VMX_CR3_TARGET_UNSUPPORTED, "reserved bit 6");
    REJECT(VMX_CR3_EXIT_CR3 | UINT64_C(0x1000), VMX_CR3_EXIT_GPR_COUNT,
        VMX_CR3_TARGET_UNSUPPORTED, "reserved LMSW source");
    REJECT(VMX_CR3_EXIT_CR3 | (UINT64_C(1) << 32),
        VMX_CR3_EXIT_GPR_COUNT, VMX_CR3_TARGET_UNSUPPORTED,
        "reserved high qualification");
    REJECT(VMX_CR3_EXIT_CR3, VMX_CR3_EXIT_GPR_COUNT - 1,
        VMX_CR3_TARGET_INVALID, "short GPR file");
#undef REJECT

    memset(&Out, 0, sizeof(Out));
    TEST_ASSERT_INT_EQ(VmxCr3ExitDecode(&Set, VMX_CR3_EXIT_CR3,
        (const uint64_t*)&Out, VMX_CR3_EXIT_GPR_COUNT, &Out),
        VMX_CR3_TARGET_INVALID, "GPR/output alias");
    memset(&Out, 0, sizeof(Out));
    TEST_ASSERT_INT_EQ(VmxCr3ExitDecode((const VMX_CR3_TARGET_SET*)&Out,
        VMX_CR3_EXIT_CR3, Gprs, VMX_CR3_EXIT_GPR_COUNT, &Out),
        VMX_CR3_TARGET_INVALID, "set/output alias");
    Set.Reserved = 1;
    memset(&Out, 0xA5, sizeof(Out)); Before = Out;
    TEST_ASSERT_INT_EQ(VmxCr3ExitDecode(&Set, VMX_CR3_EXIT_CR3,
        Gprs, VMX_CR3_EXIT_GPR_COUNT, &Out), VMX_CR3_TARGET_INVALID,
        "corrupt target set");
    TEST_ASSERT_TRUE(memcmp(&Out, &Before, sizeof(Out)) == 0,
        "corrupt set preserves");
}

int main(void)
{
    DecodeEveryGpr();
    NonTargetIsWellFormed();
    RejectsAndPreserves();
    return 0;
}