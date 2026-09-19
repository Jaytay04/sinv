#include <string.h>
#include "test_common.h"
#include "vmx_cr3_route_executor.h"

typedef struct WRITES {
    uint32_t Count, FailAt;
    uint64_t Fields[2], Values[2];
} WRITES;

static int Write(void* Context, uint64_t Field, uint64_t Value)
{
    WRITES* W = (WRITES*)Context;
    uint32_t Index = W->Count++;
    if (Index < 2) { W->Fields[Index] = Field; W->Values[Index] = Value; }
    return W->FailAt == 0 || W->FailAt != W->Count;
}

static VMX_CR3_ROUTE_OWNER Owner(VMX_CR3_RESIDENCY_STATE* States)
{
    const uint64_t Root = UINT64_C(0x12345000);
    VMX_CR3_TARGET_SET Set = { 0 };
    VMX_CR3_ROUTE_OWNER Owner = { 0 };
    TEST_ASSERT_INT_EQ(VmxCr3TargetSetBuild(48, 0, &Root, 1, 7, &Set),
        VMX_CR3_TARGET_OK, "set");
    TEST_ASSERT_INT_EQ(VmxCr3RouteOwnerInitialize(&Set, States, 2, &Owner),
        VMX_CR3_TARGET_OK, "owner");
    return Owner;
}

static void Success(void)
{
    VMX_CR3_RESIDENCY_STATE States[2] = { 0 };
    VMX_CR3_ROUTE_OWNER O = Owner(States);
    VMX_CR3_ROUTE_EXECUTION E = { 0 };
    WRITES W = { 0 };
    uint64_t Gprs[VMX_CR3_EXIT_GPR_COUNT] = { 0 };
    Gprs[3] = O.TargetSet.Roots[0];
    TEST_ASSERT_INT_EQ(VmxCr3RouteExecute(&O, 1,
        VMX_CR3_EXIT_CR3 | (UINT64_C(3) << VMX_CR3_EXIT_GPR_SHIFT),
        Gprs, VMX_CR3_EXIT_GPR_COUNT, 0x1000, 3, &W, Write, &E),
        VMX_CR3_ROUTE_EXECUTE_OK, "execute success");
    TEST_ASSERT_TRUE(W.Count == 2 && W.Fields[0] == VMCS_GUEST_CR3 &&
        W.Values[0] == Gprs[3] && W.Fields[1] == VMCS_GUEST_RIP &&
        W.Values[1] == 0x1003, "write order and values");
    TEST_ASSERT_TRUE(E.Outcome == VMX_CR3_ROUTE_EXECUTION_COMMITTED &&
        E.WritesAttempted == 2 && E.StateCommitted == 1 &&
        E.FailedField == 0, "success diagnostics");
    TEST_ASSERT_TRUE(!States[0].Resident && States[1].Resident &&
        States[1].TransitionSequence == 1, "commit selected processor");
    TEST_ASSERT_INT_EQ(VmxCr3RouteOwnerReset(&O), VMX_CR3_TARGET_OK, "reset");
}

static void WriteFailures(void)
{
    uint32_t FailAt;
    for (FailAt = 1; FailAt <= 2; ++FailAt) {
        VMX_CR3_RESIDENCY_STATE States[2] = { 0 }, Before[2];
        VMX_CR3_ROUTE_OWNER O = Owner(States);
        VMX_CR3_ROUTE_EXECUTION E = { 0 };
        WRITES W = { 0 }; uint64_t Gprs[VMX_CR3_EXIT_GPR_COUNT] = { 0 };
        W.FailAt = FailAt; Gprs[0] = O.TargetSet.Roots[0];
        memcpy(Before, States, sizeof(Before));
        TEST_ASSERT_INT_EQ(VmxCr3RouteExecute(&O, 0, VMX_CR3_EXIT_CR3,
            Gprs, VMX_CR3_EXIT_GPR_COUNT, 0x2000, 3, &W, Write, &E),
            FailAt == 1 ? VMX_CR3_ROUTE_EXECUTE_WRITE_CR3_FAILED :
                VMX_CR3_ROUTE_EXECUTE_WRITE_RIP_FAILED,
            "write failure status");
        TEST_ASSERT_TRUE(memcmp(States, Before, sizeof(States)) == 0,
            "write failure preserves residency");
        TEST_ASSERT_TRUE(E.WritesAttempted == FailAt && !E.StateCommitted &&
            E.FailedField == (FailAt == 1 ? VMCS_GUEST_CR3 : VMCS_GUEST_RIP),
            "write failure diagnostics");
        TEST_ASSERT_TRUE(W.Count == FailAt, "stop after failed write");
        TEST_ASSERT_INT_EQ(VmxCr3RouteOwnerReset(&O), VMX_CR3_TARGET_OK, "reset");
    }
}

static void InvalidDoesNotCall(void)
{
    VMX_CR3_RESIDENCY_STATE States[2] = { 0 };
    VMX_CR3_ROUTE_OWNER O = Owner(States);
    VMX_CR3_ROUTE_EXECUTION E = { 0 };
    WRITES W = { 0 }; uint64_t Gprs[VMX_CR3_EXIT_GPR_COUNT] = { 0 };
    TEST_ASSERT_INT_EQ(VmxCr3RouteExecute(&O, 2, VMX_CR3_EXIT_CR3, Gprs,
        VMX_CR3_EXIT_GPR_COUNT, 0x1000, 3, &W, Write, &E),
        VMX_CR3_ROUTE_EXECUTE_INVALID, "invalid processor");
    TEST_ASSERT_TRUE(W.Count == 0 &&
        memcmp(&E, &(VMX_CR3_ROUTE_EXECUTION){0}, sizeof(E)) == 0,
        "planning failure no write no output");
    TEST_ASSERT_INT_EQ(VmxCr3RouteExecute(&O, 0, VMX_CR3_EXIT_CR3, Gprs,
        VMX_CR3_EXIT_GPR_COUNT, 0x1000, 3, &W, Write,
        (VMX_CR3_ROUTE_EXECUTION*)&States[0]),
        VMX_CR3_ROUTE_EXECUTE_INVALID, "state output alias rejected");
    TEST_ASSERT_TRUE(W.Count == 0 &&
        VmxCr3RouteOwnerValidate(&O) == VMX_CR3_TARGET_OK,
        "state output alias preserves owner");
    TEST_ASSERT_INT_EQ(VmxCr3RouteOwnerReset(&O), VMX_CR3_TARGET_OK, "reset");
}

int main(void) { Success(); WriteFailures(); InvalidDoesNotCall(); return 0; }
