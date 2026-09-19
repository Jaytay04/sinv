#include <string.h>
#include "test_common.h"
#include "vmx_cr3_target_selection.h"

static VMX_CR3_TARGET_DECISION DecisionSentinel(void)
{
    VMX_CR3_TARGET_DECISION value;
    memset(&value, 0xA5, sizeof(value));
    return value;
}

static void BuildSingleAndSelect(void)
{
    const uint64_t roots[1] = { UINT64_C(0x12345000) };
    VMX_CR3_TARGET_SET set = { 0 };
    VMX_CR3_TARGET_DECISION decision = { 0 };

    TEST_ASSERT_INT_EQ(VmxCr3TargetSetBuild(48, 0, roots, 1, 7, &set),
        VMX_CR3_TARGET_OK, "single build");
    TEST_ASSERT_UINT_EQ(set.Count, 1, "single count");
    TEST_ASSERT_UINT_EQ(set.Roots[1], 0, "unused root zero");
    TEST_ASSERT_INT_EQ(VmxCr3TargetSelect(&set, roots[0], &decision),
        VMX_CR3_TARGET_OK, "single select");
    TEST_ASSERT_UINT_EQ(decision.Matched, 1, "single matched");
    TEST_ASSERT_UINT_EQ(decision.MatchedIndex, 0, "single index");
    TEST_ASSERT_UINT_EQ(decision.NormalizedRoot, roots[0], "single root");
    TEST_ASSERT_UINT_EQ(decision.Pcid, 0, "PCID off");
    TEST_ASSERT_UINT_EQ(decision.NoFlush, 0, "no-flush off");
    TEST_ASSERT_UINT_EQ(decision.Generation, 7, "generation copied");
}

static void BuildKvaPairAndSelectPcids(void)
{
    const uint64_t roots[2] = {
        UINT64_C(0x0000000123456000), UINT64_C(0x0000000765432000) };
    VMX_CR3_TARGET_SET set = { 0 };
    VMX_CR3_TARGET_DECISION decision = { 0 };
    uint64_t operand = roots[1] | UINT64_C(0xabc) | VMX_CR3_TARGET_NO_FLUSH;

    TEST_ASSERT_INT_EQ(VmxCr3TargetSetBuild(52, 1, roots, 2, 99, &set),
        VMX_CR3_TARGET_OK, "KVA pair build");
    TEST_ASSERT_INT_EQ(VmxCr3TargetSelect(&set, operand, &decision),
        VMX_CR3_TARGET_OK, "KVA user root select");
    TEST_ASSERT_UINT_EQ(decision.Matched, 1, "KVA matched");
    TEST_ASSERT_UINT_EQ(decision.MatchedIndex, 1, "KVA user index");
    TEST_ASSERT_UINT_EQ(decision.Pcid, 0xabc, "PCID decoded");
    TEST_ASSERT_UINT_EQ(decision.NoFlush, 1, "no-flush decoded");
    TEST_ASSERT_UINT_EQ(decision.Operand, operand, "operand preserved");
    TEST_ASSERT_UINT_EQ(decision.NormalizedRoot, roots[1], "KVA normalized");
}

static void WellFormedNonTarget(void)
{
    const uint64_t roots[2] = { UINT64_C(0x1000), UINT64_C(0x2000) };
    VMX_CR3_TARGET_SET set = { 0 };
    VMX_CR3_TARGET_DECISION decision = { 0 };

    TEST_ASSERT_INT_EQ(VmxCr3TargetSetBuild(36, 1, roots, 2, 1, &set),
        VMX_CR3_TARGET_OK, "36-bit build");
    TEST_ASSERT_INT_EQ(VmxCr3TargetSelect(&set, UINT64_C(0x3005), &decision),
        VMX_CR3_TARGET_OK, "non-target valid");
    TEST_ASSERT_UINT_EQ(decision.Matched, 0, "non-target unmatched");
    TEST_ASSERT_UINT_EQ(decision.MatchedIndex, UINT32_MAX, "no index");
    TEST_ASSERT_UINT_EQ(decision.NormalizedRoot, UINT64_C(0x3000),
        "non-target normalized");
    TEST_ASSERT_UINT_EQ(decision.Pcid, 5, "non-target PCID");
}

static void BuildRejectsAndPreserves(void)
{
    uint64_t roots[2] = { UINT64_C(0x1000), UINT64_C(0x2000) };
    VMX_CR3_TARGET_SET out, before;
#define REJECT_BUILD(bits, pcide, count, generation, message) do { \
    memset(&out, 0xA5, sizeof(out)); before = out; \
    TEST_ASSERT_INT_EQ(VmxCr3TargetSetBuild((bits), (pcide), roots, (count), \
        (generation), &out), VMX_CR3_TARGET_INVALID, (message)); \
    TEST_ASSERT_TRUE(memcmp(&out, &before, sizeof(out)) == 0, \
        message " preserves"); \
} while (0)
    REJECT_BUILD(35, 0, 1, 1, "physical bits low");
    REJECT_BUILD(53, 0, 1, 1, "physical bits high");
    REJECT_BUILD(48, 2, 1, 1, "PCIDE range");
    REJECT_BUILD(48, 0, 0, 1, "zero roots");
    REJECT_BUILD(48, 0, 3, 1, "too many roots");
    REJECT_BUILD(48, 0, 1, 0, "zero generation");
    roots[0] = UINT64_C(0x1234);
    REJECT_BUILD(48, 0, 1, 1, "unaligned root");
    roots[0] = UINT64_C(0x1000000000000);
    REJECT_BUILD(48, 0, 1, 1, "root above width");
    roots[0] = roots[1] = UINT64_C(0x2000);
    REJECT_BUILD(48, 0, 2, 1, "duplicate roots");
#undef REJECT_BUILD

    memset(&out, 0, sizeof(out));
    TEST_ASSERT_INT_EQ(VmxCr3TargetSetBuild(48, 0, out.Roots, 1, 1, &out),
        VMX_CR3_TARGET_INVALID, "alias roots/output");
}

static void SelectRejectsAndPreserves(void)
{
    const uint64_t roots[1] = { UINT64_C(0x12345000) };
    VMX_CR3_TARGET_SET set = { 0 };
    VMX_CR3_TARGET_DECISION out, before;
#define REJECT_SELECT(operand, status, message) do { \
    out = DecisionSentinel(); before = out; \
    TEST_ASSERT_INT_EQ(VmxCr3TargetSelect(&set, (operand), &out), (status), \
        (message)); \
    TEST_ASSERT_TRUE(memcmp(&out, &before, sizeof(out)) == 0, \
        message " preserves"); \
} while (0)
    TEST_ASSERT_INT_EQ(VmxCr3TargetSetBuild(48, 0, roots, 1, 4, &set),
        VMX_CR3_TARGET_OK, "select fixture");
    REJECT_SELECT(roots[0] | 1, VMX_CR3_TARGET_UNSUPPORTED,
        "PCID while disabled");
    REJECT_SELECT(roots[0] | VMX_CR3_TARGET_NO_FLUSH,
        VMX_CR3_TARGET_UNSUPPORTED, "no-flush while disabled");
    REJECT_SELECT(UINT64_C(1) << 62, VMX_CR3_TARGET_UNSUPPORTED,
        "reserved operand high bit");
    set.Reserved = 1;
    REJECT_SELECT(roots[0], VMX_CR3_TARGET_INVALID, "corrupt set");
#undef REJECT_SELECT
}

int main(void)
{
    BuildSingleAndSelect();
    BuildKvaPairAndSelectPcids();
    WellFormedNonTarget();
    BuildRejectsAndPreserves();
    SelectRejectsAndPreserves();
    return 0;
}