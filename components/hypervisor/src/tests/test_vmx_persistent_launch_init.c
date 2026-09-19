#include "test_common.h"
#include <limits.h>
#include <string.h>
#include "vmx_persistent_launch_init.h"
#define STORAGE_BYTES (VMX_V3D_CONTEXT_SIZE + 48u)
static _Alignas(16) unsigned char g_storage[STORAGE_BYTES];
static _Alignas(16) unsigned char g_host_stack[VMX_PERSISTENT_HOST_STACK_SIZE];
static _Alignas(16) unsigned char g_guest_stack[VMX_PERSISTENT_GUEST_STACK_SIZE];
static VMX_PERSISTENT_CONTEXT g_context;
static VMX_V3D_PERSISTENT_LAUNCH_CONTEXT g_sentinel;
static int g_io_seam;
static uint32_t TestDispatcher(VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* context,
                               VMX_V3D_GUEST_GPRS* gprs, void* seam)
{
    (void)context;
    (void)gprs;
    (void)seam;
    return 0;
}
static void ValidContext(void)
{
    memset(&g_context, 0, sizeof(g_context));
    g_context.LaunchStorage = g_storage;
    g_context.LaunchStorageSize = sizeof(g_storage);
    g_context.HostStack = g_host_stack;
    g_context.HostStackSize = sizeof(g_host_stack);
    g_context.GuestStack = g_guest_stack;
    g_context.GuestStackSize = sizeof(g_guest_stack);
    g_context.VmxonPhysicalAddress = UINT64_C(0x1000);
    g_context.VmcsPhysicalAddress = UINT64_C(0x2000);
    g_context.OwnedResources = VMX_PERSISTENT_RESOURCE_LAUNCH |
        VMX_PERSISTENT_RESOURCE_STACK | VMX_PERSISTENT_RESOURCE_GUEST_STACK |
        VMX_PERSISTENT_RESOURCE_VMXON | VMX_PERSISTENT_RESOURCE_VMCS |
        VMX_PERSISTENT_RESOURCE_MSR_BITMAP;
    g_context.Version = VMX_PERSISTENT_CONTEXT_VERSION;
    g_context.Published = true;
}
static void ExpectFailureWith(VMX_V3D_PERSISTENT_DISPATCHER dispatcher,
                              void* dispatcher_context)
{
    VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* out = &g_sentinel;
    VMX_PERSISTENT_LAUNCH_INIT_STATUS status;
    memset(g_storage, 0xA5, sizeof(g_storage));
    status = VmxPersistentLaunchStorageInitialize(
        &g_context, UINT64_C(0x1122334455667788), dispatcher,
        dispatcher_context, &out);
    TEST_ASSERT(status != VMX_PERSISTENT_LAUNCH_INIT_OK, "invalid input rejected");
    TEST_ASSERT(out == &g_sentinel, "output preserved on failure");
    for (size_t i = 0; i < sizeof(g_storage); ++i)
        TEST_ASSERT(g_storage[i] == 0xA5, "storage preserved on failure");
}
static void ExpectFailure(const char* label)
{
    (void)label;
    ExpectFailureWith(TestDispatcher, &g_io_seam);
}
static int TestSuccessAndExactZero(void)
{
    VMX_V3D_PERSISTENT_LAUNCH_CONTEXT expected;
    VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* out = &g_sentinel;
    uintptr_t host_top = (uintptr_t)g_host_stack + sizeof(g_host_stack);
    uintptr_t guest_top = (uintptr_t)g_guest_stack + sizeof(g_guest_stack);
    ValidContext();
    memset(g_storage, 0xA5, VMX_V3D_CONTEXT_SIZE);
    memset(g_storage + VMX_V3D_CONTEXT_SIZE, 0xCD,
           sizeof(g_storage) - VMX_V3D_CONTEXT_SIZE);
    TEST_ASSERT_INT_EQ(VmxPersistentLaunchStorageInitialize(
        &g_context, UINT64_C(0x1122334455667788), TestDispatcher, &g_io_seam,
        &out), VMX_PERSISTENT_LAUNCH_INIT_OK, "success");
    memset(&expected, 0, sizeof(expected));
    expected.Header.Magic = VMX_V3D_PERSISTENT_LAUNCH_MAGIC;
    expected.Header.Size = VMX_V3D_CONTEXT_SIZE;
    expected.Header.Version = VMX_V3D_PERSISTENT_LAUNCH_VERSION;
    expected.HostStackTop = host_top;
    expected.HostRsp = host_top - VMX_V3D_X64_REGISTER_SIZE;
    expected.GuestStackTop = guest_top;
    expected.GuestRsp = guest_top - VMX_V3D_X64_REGISTER_SIZE;
    expected.GuestRflags = 2;
    expected.Cookie = UINT64_C(0x1122334455667788);
    expected.Dispatcher = TestDispatcher;
    expected.DispatcherContext = &g_io_seam;
    expected.VmxonPhysicalAddress = UINT64_C(0x1000);
    expected.VmcsPhysicalAddress = UINT64_C(0x2000);
    expected.ProtocolStep = VMX_V3D_PROTOCOL_INITIALIZED;
    expected.AssemblyAction = VMX_V3D_ASSEMBLY_ACTION_INVALID;
    expected.ValidationFlags = VMX_V3D_VALIDATION_HEADER_OK;
    TEST_ASSERT(out == (VMX_V3D_PERSISTENT_LAUNCH_CONTEXT*)g_storage,
                "typed output points at storage");
    TEST_ASSERT(memcmp(g_storage, &expected, VMX_V3D_CONTEXT_SIZE) == 0,
                "initialized ABI equals expected zero baseline");
    for (size_t i = VMX_V3D_CONTEXT_SIZE; i < sizeof(g_storage); ++i)
        TEST_ASSERT(g_storage[i] == 0xCD, "oversized storage tail unchanged");
    TEST_ASSERT(out->LaunchFrameValid == 0 && out->ResumeFrameValid == 0 &&
                out->ResumeFrameArmed == 0 && out->ReadyObserved == 0 &&
                out->ShutdownObserved == 0, "frames and proof start clear");
    TEST_ASSERT(out->VmlaunchOutcome == VMX_V3D_INSTRUCTION_NOT_ATTEMPTED &&
                out->VmresumeOutcome == VMX_V3D_INSTRUCTION_NOT_ATTEMPTED &&
                out->ExitVmreadOutcome == VMX_V3D_INSTRUCTION_NOT_ATTEMPTED &&
                out->GuestRipVmwriteOutcome ==
                    VMX_V3D_INSTRUCTION_NOT_ATTEMPTED,
                "instruction outcomes start not-attempted");
    return 0;
}
static int TestMinimumAndPreservation(void)
{
    VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* out = &g_sentinel;
    ValidContext();
    g_context.LaunchStorageSize = VMX_V3D_CONTEXT_SIZE;
    TEST_ASSERT_INT_EQ(VmxPersistentLaunchStorageInitialize(
        &g_context, 1, TestDispatcher, &g_io_seam, &out),
        VMX_PERSISTENT_LAUNCH_INIT_OK, "exact ABI minimum accepted");
    ValidContext();
    g_context.LaunchStorageSize = VMX_V3D_CONTEXT_SIZE - 1u; ExpectFailure("one byte below minimum");
    return 0;
}
static int TestRequiredInputs(void)
{
    const uint32_t resources[] = {
        VMX_PERSISTENT_RESOURCE_LAUNCH, VMX_PERSISTENT_RESOURCE_STACK,
        VMX_PERSISTENT_RESOURCE_GUEST_STACK, VMX_PERSISTENT_RESOURCE_VMXON,
        VMX_PERSISTENT_RESOURCE_VMCS,
        VMX_PERSISTENT_RESOURCE_MSR_BITMAP
    };
    for (size_t i = 0; i < sizeof(resources) / sizeof(resources[0]); ++i) {
        ValidContext();
        g_context.OwnedResources &= ~resources[i];
        ExpectFailure("missing ownership");
    }
    ValidContext(); g_context.Version = 0; ExpectFailure("version");
    ValidContext(); memset(g_storage, 0xA5, sizeof(g_storage));
    {
        VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* out = &g_sentinel;
        TEST_ASSERT(VmxPersistentLaunchStorageInitialize(
            NULL, 1, TestDispatcher, &g_io_seam, &out) !=
                VMX_PERSISTENT_LAUNCH_INIT_OK, "null context rejected");
        TEST_ASSERT(out == &g_sentinel && g_storage[0] == 0xA5,
                    "null context preserves state");
    }
    ValidContext(); g_context.Published = false; ExpectFailure("published");
    ValidContext(); g_context.LaunchStorage = NULL; ExpectFailure("storage");
    ValidContext(); g_context.HostStack = NULL; ExpectFailure("host stack");
    ValidContext(); g_context.GuestStack = NULL; ExpectFailure("guest stack");
    ValidContext(); g_context.HostStackSize = VMX_PERSISTENT_HOST_STACK_SIZE - 1u; ExpectFailure("host stack size");
    ValidContext(); g_context.GuestStackSize = VMX_PERSISTENT_GUEST_STACK_SIZE - 1u; ExpectFailure("guest stack size");
    TEST_ASSERT_INT_EQ(VmxPersistentLaunchStorageInitialize(
        &g_context, 1, NULL, &g_io_seam, NULL),
        VMX_PERSISTENT_LAUNCH_INIT_INVALID, "null output rejected");
    ValidContext();
    {
        VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* out = &g_sentinel;
        TEST_ASSERT_INT_EQ(VmxPersistentLaunchStorageInitialize(
            &g_context, 0, TestDispatcher, &g_io_seam, &out),
            VMX_PERSISTENT_LAUNCH_INIT_INVALID, "zero cookie rejected");
    }
    ValidContext(); ExpectFailureWith(NULL, &g_io_seam);
    {
        VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* out = &g_sentinel;
        memset(g_storage, 0xA5, sizeof(g_storage));
        TEST_ASSERT(VmxPersistentLaunchStorageInitialize(
            &g_context, 1, NULL, &g_io_seam, &out) !=
                VMX_PERSISTENT_LAUNCH_INIT_OK, "null dispatcher rejected");
        TEST_ASSERT(out == &g_sentinel && g_storage[0] == 0xA5,
                    "null dispatcher preserves state");
    }
    ValidContext(); ExpectFailureWith(TestDispatcher, NULL);
    return 0;
}
static void ApplyInvalidResourceCase(size_t kind)
{
    switch (kind) {
    case 0: g_context.GuestStack = g_guest_stack + 1; break;
    case 1: g_context.GuestStack = (void*)UINT64_C(0x0000800000000000); break;
    case 2: g_context.GuestStack = (void*)UINT64_C(0x00007FFFFFFFFF00); break;
    case 3: g_context.GuestStack = (void*)g_storage; break;
    case 4: g_context.VmxonPhysicalAddress = 0; break;
    case 5: g_context.VmcsPhysicalAddress = 0; break;
    case 6: g_context.VmcsPhysicalAddress = UINT64_C(0x2001); break;
    case 7: g_context.LaunchStorage = g_storage + 1; break;
    case 8: g_context.LaunchStorage = (void*)UINT64_C(0x0000800000000000); break;
    case 9: g_context.LaunchStorage = (void*)UINT64_C(0x00007FFFFFFFFF00); break;
    default: break; }
}static int TestAddressAndPaValidation(void)
{
    ValidContext(); g_context.HostStack = (void*)((uintptr_t)g_storage); ExpectFailure("stack/storage overlap");
    ValidContext(); g_context.GuestStack = g_host_stack; ExpectFailure("stack/stack overlap");
    ValidContext(); g_context.HostStack = (void*)(UINTPTR_MAX - 4096u); ExpectFailure("range overflow");
    { const uintptr_t bad[] = { UINT64_C(0x0001000000000000), UINT64_C(0x0000800000000000), UINT64_C(0xFFFF7FFFFFFFFFFF) }; for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) { ValidContext(); g_context.HostStack = (void*)bad[i]; ExpectFailure("noncanonical stack"); } }
    { VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* out = &g_sentinel; ValidContext(); g_context.HostStack = (void*)UINT64_C(0xFFFF800000000000); TEST_ASSERT_INT_EQ(VmxPersistentLaunchStorageInitialize(&g_context, 1, TestDispatcher, &g_io_seam, &out), VMX_PERSISTENT_LAUNCH_INIT_OK, "high canonical stack"); }
    ValidContext(); g_context.HostStack = g_host_stack + 1; ExpectFailure("stack alignment");
    ValidContext(); g_context.HostStackSize += 8u; ExpectFailure("stack top alignment");
    ValidContext(); g_context.VmxonPhysicalAddress = UINT64_C(0x1001); ExpectFailure("VMXON alignment");
    { const size_t cases[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 }; for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) { ValidContext(); ApplyInvalidResourceCase(cases[i]); ExpectFailure("guest/resource matrix"); } }
    ValidContext(); g_context.VmcsPhysicalAddress = g_context.VmxonPhysicalAddress; ExpectFailure("VMCS alias");
    return 0;
}
static int TestCopiesAreIndependent(void)
{
    VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* out = &g_sentinel;
    uint64_t vmxon;
    uint64_t vmcs;
    ValidContext();
    TEST_ASSERT_INT_EQ(VmxPersistentLaunchStorageInitialize(
        &g_context, 7, TestDispatcher, &g_io_seam, &out),
        VMX_PERSISTENT_LAUNCH_INIT_OK, "copy setup");
    vmxon = out->VmxonPhysicalAddress;
    vmcs = out->VmcsPhysicalAddress;
    g_context.VmxonPhysicalAddress = UINT64_C(0x3000);
    g_context.VmcsPhysicalAddress = UINT64_C(0x4000);
    g_context.HostStack = g_guest_stack;
    g_context.LaunchStorage = NULL;
    TEST_ASSERT_UINT_EQ(out->VmxonPhysicalAddress, vmxon, "VMXON copied");
    TEST_ASSERT_UINT_EQ(out->VmcsPhysicalAddress, vmcs, "VMCS copied");
    TEST_ASSERT_UINT_EQ(out->Cookie, 7, "cookie copied");
    TEST_ASSERT(out->DispatcherContext == &g_io_seam,
                "dispatcher seam copied");
    return 0;
}
TEST_MAIN_BEGIN()
    TEST_RUN(TestSuccessAndExactZero);
    TEST_RUN(TestMinimumAndPreservation);
    TEST_RUN(TestRequiredInputs);
    TEST_RUN(TestAddressAndPaValidation);
    TEST_RUN(TestCopiesAreIndependent);
TEST_MAIN_END()
