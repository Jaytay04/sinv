/*
 * test_vmx_runtime_context.c — Contract, acquisition, and unwind tests (R3)
 */

#include "test_common.h"
#include "vmx_runtime_context.h"

#include <string.h>   /* memset */
#include <stdlib.h>   /* calloc, free */
#include <malloc.h>   /* _aligned_malloc, _aligned_free */

/* Success allocator */
static void* PoolAlloc(size_t sz)  { return calloc(1, sz); }
static void  PoolFree(void* p)    { free(p); }
static void* ContigAlloc(size_t sz, uint64_t highest){ (void)highest; return _aligned_malloc(sz, 4096); }
static void  ContigFree(void* p)  { _aligned_free(p); }
static uint64_t GetPa(const void* va) { (void)va; return 0x10000; }

static uint32_t DummyDispatcher(VMX_LAUNCH_CONTEXT* context, VMX_GUEST_GPRS* guest_gprs) {
    (void)context;
    (void)guest_gprs;
    return 0;
}

static VMX_ALLOCATION_INTERFACE g_alloc = {
    PoolAlloc, PoolFree, ContigAlloc, ContigFree, GetPa
};

/* Instrumented allocator for contract verification */
static size_t g_log_contig_sizes[8];
static int    g_log_contig_count;
static int    g_free_contig_count;
static int    g_free_pool_count;
static uint64_t g_inject_pa;  /* 0 = real, non-zero = return this */
static int    g_pa_call_count;

static uint64_t g_log_contig_highest[8];
static void* g_log_contig_ptrs[8];
static void* InstrContig(size_t sz, uint64_t highest) {
    void* p = _aligned_malloc(sz, 4096);
    if (p != NULL) (void)memset(p, 0xA5, sz);
    if (g_log_contig_count < 8) {
        g_log_contig_sizes[g_log_contig_count] = sz;
        g_log_contig_highest[g_log_contig_count] = highest;
        g_log_contig_ptrs[g_log_contig_count] = p;
    }
    g_log_contig_count++;
    return p;
}
static void InstrFreeContig(void* p) { g_free_contig_count++; _aligned_free(p); }
static uint64_t InstrGetPa(const void* va) {
    (void)va;
    g_pa_call_count++;
    return g_inject_pa ? g_inject_pa : 0x10000;
}
static void* InstrPool(size_t sz)  { return calloc(1, sz); }
static void  InstrFreePool(void* p){ g_free_pool_count++; free(p); }

static VMX_ALLOCATION_INTERFACE g_instr = {
    InstrPool, InstrFreePool, InstrContig, InstrFreeContig, InstrGetPa
};

static void ResetInstr(void) {
    (void)memset(g_log_contig_sizes, 0, sizeof(g_log_contig_sizes));
    (void)memset(g_log_contig_highest, 0, sizeof(g_log_contig_highest));
    (void)memset(g_log_contig_ptrs, 0, sizeof(g_log_contig_ptrs));
    g_log_contig_count = 0;
    g_free_contig_count = 0;
    g_free_pool_count = 0;
    g_inject_pa = 0;
    g_pa_call_count = 0;
}
static int  g_call_pool, g_call_contig;
static int  g_fail_pool, g_fail_contig;  /* 1-based step to fail, 0=never */

static void* FailPool(size_t sz) {
    g_call_pool++;
    return (g_fail_pool > 0 && g_call_pool == g_fail_pool) ? NULL : PoolAlloc(sz);
}
static void FailFreePool(void* p) { g_free_pool_count++; PoolFree(p); }
static void* FailContig(size_t sz, uint64_t highest) {
    g_call_contig++;
    return (g_fail_contig > 0 && g_call_contig == g_fail_contig) ? NULL : ContigAlloc(sz, highest);
}
static void FailFreeContig(void* p) { g_free_contig_count++; ContigFree(p); }

static VMX_ALLOCATION_INTERFACE g_fail = {
    FailPool, FailFreePool, FailContig, FailFreeContig, GetPa
};

static void ResetFail(void) {
    g_call_pool = g_call_contig = 0;
    g_fail_pool = g_fail_contig = 0;
}

/* --- Contract tests --- */
static int test_assembly_offset_zero(void) {
    VMX_RUNTIME_LAUNCH_CONTEXT ctx;
    size_t off = (size_t)((uintptr_t)&ctx.Assembly - (uintptr_t)&ctx);
    TEST_ASSERT_UINT_EQ((unsigned int)off, 0, "Assembly at offset 0");
    return 0;
}
static int test_align_down_16(void) {
    TEST_ASSERT_UINT_EQ(VmxAlignDown16(0x00000260BB9A4010ULL),
                        0x00000260BB9A4010ULL, "AlignDown16 64-bit");
    return 0;
}
static int test_canonical(void) {
    TEST_ASSERT_FALSE(VmxIsAddressCanonical(0x0000800000000000ULL), "Non-canonical");
    return 0;
}
static int test_in_stack(void) {
    uint8_t s[4096];
    TEST_ASSERT_TRUE(VmxIsAddressInStack((uint64_t)(uintptr_t)s, s, 4096), "Base in");
    return 0;
}
static int test_resource_bits(void) {
    TEST_ASSERT_UINT_EQ(VMX_RESOURCE_CONTEXT_BIT, 1u, "Context=1");
    return 0;
}

/* --- Success --- */
static int test_create_success(void) {
    uint64_t vmx_basic = (uint64_t)0x1234 | ((uint64_t)4096 << 32)
                       | (6ull << 50) | (1ull << 55);
    VMX_RUNTIME_LAUNCH_CONTEXT* ctx = NULL;
    bool ok = VmxLaunchCreateContextInternal(vmx_basic, &g_alloc, NULL, &ctx);
    TEST_ASSERT_TRUE(ok, "Success");
    TEST_ASSERT_NOT_NULL(ctx, "Non-null");
    TEST_ASSERT_TRUE(ctx->Assembly.HostRsp != 0, "HostRsp set");
    TEST_ASSERT_TRUE(VmxRuntimeContextFromAssembly(&ctx->Assembly) == ctx, "Container recovery");
    VmxLaunchDestroyContextInternal(&g_alloc, ctx);
    return 0;
}

static int test_context_abi_fields_initialized(void) {
    uint64_t vmx_basic = (uint64_t)0x1234 | ((uint64_t)4096 << 32)
                       | (6ull << 50) | (1ull << 55);
    VMX_RUNTIME_LAUNCH_CONTEXT* ctx = NULL;
    bool ok = VmxLaunchCreateContextInternal(vmx_basic, &g_alloc, NULL, &ctx);
    TEST_ASSERT_TRUE(ok, "ABI: creation succeeded");
    TEST_ASSERT_UINT_EQ(ctx->Assembly.Magic, VMX_LAUNCH_CONTEXT_MAGIC,
                        "ABI: Magic set");
    TEST_ASSERT_UINT_EQ(ctx->Assembly.Size, VMX_LAUNCH_CONTEXT_SIZE,
                        "ABI: Size = 352");
    TEST_ASSERT_UINT_EQ(ctx->Assembly.Version, VMX_LAUNCH_ABI_VERSION,
                        "ABI: Version = 2");
    TEST_ASSERT_TRUE(ctx->Assembly.ExitDispatcher == NULL,
                     "ABI: Dispatcher = NULL (test injects NULL)");
    VmxLaunchDestroyContextInternal(&g_alloc, ctx);
    return 0;
}

static int test_context_dispatcher_injection(void) {
    uint64_t vmx_basic = (uint64_t)0x1234 | ((uint64_t)4096 << 32)
                       | (6ull << 50);
    VMX_RUNTIME_LAUNCH_CONTEXT* ctx = NULL;
    bool ok = VmxLaunchCreateContextInternal(
        vmx_basic, &g_alloc, DummyDispatcher, &ctx);
    TEST_ASSERT_TRUE(ok, "Dispatcher: creation succeeded");
    TEST_ASSERT_TRUE(ctx->Assembly.ExitDispatcher == DummyDispatcher,
                     "Dispatcher: exact callback stored");
    TEST_ASSERT_TRUE(
        *(VMX_RUNTIME_LAUNCH_CONTEXT**)(uintptr_t)ctx->Assembly.HostRsp == ctx,
        "Dispatcher: host-stack slot still publishes context");
    VmxLaunchDestroyContextInternal(&g_alloc, ctx);
    return 0;
}

static int test_rejects_invalid_basic(void) {
    uint64_t vmx_basic = (1ull << 31) | ((uint64_t)4096 << 32) | (6ull << 50);
    VMX_RUNTIME_LAUNCH_CONTEXT* ctx = (void*)0xBAD;
    TEST_ASSERT_FALSE(VmxLaunchCreateContextInternal(vmx_basic, &g_alloc, NULL, &ctx), "Bad BASIC");
    TEST_ASSERT_NULL(ctx, "Output NULL");
    return 0;
}

/* --- Failure injection at each acquisition boundary --- */
static int test_fails_context_alloc(void) {
    ResetFail();
    g_fail_pool = 1;
    uint64_t vmx_basic = (uint64_t)0x1234 | ((uint64_t)4096 << 32) | (6ull << 50);
    VMX_RUNTIME_LAUNCH_CONTEXT* ctx = (void*)0xBAD;
    TEST_ASSERT_FALSE(VmxLaunchCreateContextInternal(vmx_basic, &g_fail, NULL, &ctx), "Fail ctx");
    TEST_ASSERT_NULL(ctx, "Output NULL");
    return 0;
}

static int test_fails_host_stack(void) {
    ResetFail();
    g_fail_pool = 2;
    uint64_t vmx_basic = (uint64_t)0x1234 | ((uint64_t)4096 << 32) | (6ull << 50);
    VMX_RUNTIME_LAUNCH_CONTEXT* ctx = (void*)0xBAD;
    TEST_ASSERT_FALSE(VmxLaunchCreateContextInternal(vmx_basic, &g_fail, NULL, &ctx), "Fail stack");
    TEST_ASSERT_NULL(ctx, "Output NULL");
    return 0;
}

static int test_fails_vmxon(void) {
    ResetFail();
    g_fail_contig = 1;
    uint64_t vmx_basic = (uint64_t)0x1234 | ((uint64_t)4096 << 32) | (6ull << 50);
    VMX_RUNTIME_LAUNCH_CONTEXT* ctx = (void*)0xBAD;
    TEST_ASSERT_FALSE(VmxLaunchCreateContextInternal(vmx_basic, &g_fail, NULL, &ctx), "Fail VMXON");
    TEST_ASSERT_NULL(ctx, "Output NULL");
    return 0;
}

static int test_fails_vmcs(void) {
    ResetFail();
    g_fail_contig = 2;
    uint64_t vmx_basic = (uint64_t)0x1234 | ((uint64_t)4096 << 32) | (6ull << 50);
    VMX_RUNTIME_LAUNCH_CONTEXT* ctx = (void*)0xBAD;
    TEST_ASSERT_FALSE(VmxLaunchCreateContextInternal(vmx_basic, &g_fail, NULL, &ctx), "Fail VMCS");
    TEST_ASSERT_NULL(ctx, "Output NULL");
    return 0;
}

/* --- Destroy safety --- */
static int test_destroy_null_context(void) {
    VmxLaunchDestroyContextInternal(&g_alloc, NULL);
    TEST_ASSERT_TRUE(true, "Destroy NULL safe");
    return 0;
}
static int test_destroy_null_allocator(void) {
    VMX_RUNTIME_LAUNCH_CONTEXT d = {0};
    VmxLaunchDestroyContextInternal(NULL, &d);
    TEST_ASSERT_TRUE(true, "Destroy NULL alloc safe");
    return 0;
}

/* --- Unit A: Allocation-size contract --- */
static int test_allocation_size_always_4096(void) {
    ResetInstr();
    /* Advertise vmcs_size = 2048 — allocations must still request 4096 */
    uint64_t vmx_basic = (uint64_t)0x1234 | ((uint64_t)2048 << 32) | (6ull << 50);
    VMX_RUNTIME_LAUNCH_CONTEXT* ctx = NULL;
    bool ok = VmxLaunchCreateContextInternal(vmx_basic, &g_instr, NULL, &ctx);
    TEST_ASSERT_TRUE(ok, "Size contract: creation succeeded");
    /* Two contiguous allocations: VMXON then VMCS */
    TEST_ASSERT_INT_EQ(g_log_contig_count, 2, "Size contract: 2 contiguous allocs");
    TEST_ASSERT_UINT_EQ((unsigned int)g_log_contig_sizes[0], 4096,
                        "Size contract: VMXON alloc = 4096");
    TEST_ASSERT_UINT_EQ((unsigned int)g_log_contig_sizes[1], 4096,
                        "Size contract: VMCS alloc = 4096");
    TEST_ASSERT_UINT_EQ(g_log_contig_highest[0], UINT64_MAX,
                        "Size contract: VMXON highest PA");
    TEST_ASSERT_UINT_EQ(g_log_contig_highest[1], UINT64_MAX,
                        "Size contract: VMCS highest PA");
    VmxLaunchDestroyContextInternal(&g_instr, ctx);
    return 0;
}

/* --- Unit A: Post-allocation unwind with invalid PA --- */

static int test_allocation_respects_advertised_size(void) {
    ResetInstr();
    uint64_t vmx_basic = (uint64_t)0x1234 | ((uint64_t)5000 << 32) |
                         (6ull << 50);
    VMX_RUNTIME_LAUNCH_CONTEXT* ctx = NULL;
    TEST_ASSERT_TRUE(VmxLaunchCreateContextInternal(vmx_basic, &g_instr, NULL, &ctx),
                     "Advertised size: creation succeeded");
    TEST_ASSERT_UINT_EQ(g_log_contig_sizes[0], 5000,
                        "Advertised size: VMXON allocation");
    TEST_ASSERT_UINT_EQ(g_log_contig_sizes[1], 5000,
                        "Advertised size: VMCS allocation");
    TEST_ASSERT_UINT_EQ(g_log_contig_highest[0], UINT64_MAX,
                        "Advertised size: VMXON highest PA");
    TEST_ASSERT_UINT_EQ(g_log_contig_highest[1], UINT64_MAX,
                        "Advertised size: VMCS highest PA");
    {   const uint8_t* vmxon = (const uint8_t*)g_log_contig_ptrs[0];
        const uint8_t* vmcs = (const uint8_t*)g_log_contig_ptrs[1];
        uint32_t revision;
        size_t i;
        (void)memcpy(&revision, vmxon, sizeof(revision));
        TEST_ASSERT_UINT_EQ(revision, 0x1234, "Advertised size: VMXON revision");
        (void)memcpy(&revision, vmcs, sizeof(revision));
        TEST_ASSERT_UINT_EQ(revision, 0x1234, "Advertised size: VMCS revision");
        for (i = sizeof(revision); i < 5000; ++i) {
            if (vmxon[i] != 0 || vmcs[i] != 0) {
                fprintf(stderr, "  FAIL: %s ? advertised region byte %zu not zero\n",
                        _test_current_name, i);
                return 1;
            }
        }
    }
    VmxLaunchDestroyContextInternal(&g_instr, ctx);
    return 0;
}

static int test_allocation_propagates_32bit_limit(void) {
    ResetInstr();
    uint64_t vmx_basic = (uint64_t)0x1234 | ((uint64_t)4096 << 32) |
                         (6ull << 50) | (1ull << 48);
    VMX_RUNTIME_LAUNCH_CONTEXT* ctx = NULL;
    TEST_ASSERT_TRUE(VmxLaunchCreateContextInternal(vmx_basic, &g_instr, NULL, &ctx),
                     "PA limit: creation succeeded");
    TEST_ASSERT_UINT_EQ(g_log_contig_highest[0], UINT32_MAX,
                        "PA limit: VMXON highest PA");
    TEST_ASSERT_UINT_EQ(g_log_contig_highest[1], UINT32_MAX,
                        "PA limit: VMCS highest PA");
    VmxLaunchDestroyContextInternal(&g_instr, ctx);
    return 0;
}

static int test_unwind_invalid_vmxon_pa(void) {
    ResetInstr();
    g_inject_pa = 1; /* non-page-aligned PA causes validation failure */
    uint64_t vmx_basic = (uint64_t)0x1234 | ((uint64_t)4096 << 32) | (6ull << 50);
    VMX_RUNTIME_LAUNCH_CONTEXT* ctx = (void*)0xBAD;
    bool ok = VmxLaunchCreateContextInternal(vmx_basic, &g_instr, NULL, &ctx);
    TEST_ASSERT_FALSE(ok, "Unwind VMXON PA: creation fails");
    TEST_ASSERT_NULL(ctx, "Unwind VMXON PA: output NULL");
    TEST_ASSERT_INT_EQ(g_free_contig_count, 1, "Unwind VMXON PA: 1 contig freed");
    TEST_ASSERT_INT_EQ(g_free_pool_count, 2, "Unwind VMXON PA: 2 pool freed");
    return 0;
}

/* Stateful GetPa: first call returns valid PA, second returns g_inject_pa */
static int g_stateful_pa_call;

static uint64_t StatefulGetPa(const void* va) {
    (void)va;
    g_stateful_pa_call++;
    /* First call = VMXON: always valid */
    if (g_stateful_pa_call == 1) return 0x10000;
    /* Second call = VMCS: use injected value */
    return g_inject_pa ? g_inject_pa : 0x11000;
}

static void* StatefulContig(size_t sz, uint64_t highest) {
    if (g_log_contig_count < 8)
        g_log_contig_sizes[g_log_contig_count] = sz;
    if (g_log_contig_count < 8)
        g_log_contig_highest[g_log_contig_count] = highest;
    g_log_contig_count++;
    return _aligned_malloc(sz, 4096);
}
static void StatefulFreeContig(void* p) { g_free_contig_count++; _aligned_free(p); }
static void* StatefulPool(size_t sz)  { return calloc(1, sz); }
static void  StatefulFreePool(void* p){ g_free_pool_count++; free(p); }

static VMX_ALLOCATION_INTERFACE g_stateful_instr = {
    StatefulPool, StatefulFreePool,
    StatefulContig, StatefulFreeContig,
    StatefulGetPa
};

static int test_unwind_invalid_vmcs_pa(void) {
    ResetInstr();
    g_stateful_pa_call = 0;
    g_inject_pa = 1; /* non-page-aligned — will be returned on 2nd GetPa call */
    uint64_t vmx_basic = (uint64_t)0x1234 | ((uint64_t)4096 << 32) | (6ull << 50);
    VMX_RUNTIME_LAUNCH_CONTEXT* ctx = (void*)0xBAD;
    bool ok = VmxLaunchCreateContextInternal(vmx_basic, &g_stateful_instr, NULL, &ctx);
    TEST_ASSERT_FALSE(ok, "Unwind VMCS PA: creation fails");
    TEST_ASSERT_NULL(ctx, "Unwind VMCS PA: output NULL");
    /* VMXON alloc'd + validated OK, VMCS alloc'd + validated → fails.
     * Unwind: VMCS freed (1 contig), VMXON freed (1 contig),
     * stack freed (1 pool), context freed (1 pool). */
    TEST_ASSERT_INT_EQ(g_free_contig_count, 2, "Unwind VMCS PA: 2 contig freed (VMCS+VMXON)");
    TEST_ASSERT_INT_EQ(g_free_pool_count, 2, "Unwind VMCS PA: 2 pool freed (stack+context)");
    return 0;
}

/* --- Unit A: Failure-boundary matrix with exact-once assertions --- */
static int test_boundary_vmxon_alloc_fails(void) {
    ResetFail(); ResetInstr();
    g_fail_contig = 1;
    uint64_t vmx_basic = (uint64_t)0x1234 | ((uint64_t)4096 << 32) | (6ull << 50);
    VMX_RUNTIME_LAUNCH_CONTEXT* ctx = (void*)0xBAD;
    bool ok = VmxLaunchCreateContextInternal(vmx_basic, &g_fail, NULL, &ctx);
    TEST_ASSERT_FALSE(ok, "Boundary VMXON alloc: fails");
    TEST_ASSERT_NULL(ctx, "Boundary VMXON alloc: output NULL");
    TEST_ASSERT_INT_EQ(g_free_contig_count, 0,
                       "Boundary VMXON alloc: 0 contig freed (none allocated)");
    TEST_ASSERT_INT_EQ(g_free_pool_count, 2,
                       "Boundary VMXON alloc: 2 pool freed (context + stack)");
    return 0;
}

static int test_boundary_vmcs_alloc_fails(void) {
    ResetFail(); ResetInstr();
    g_fail_contig = 2;
    uint64_t vmx_basic = (uint64_t)0x1234 | ((uint64_t)4096 << 32) | (6ull << 50);
    VMX_RUNTIME_LAUNCH_CONTEXT* ctx = (void*)0xBAD;
    bool ok = VmxLaunchCreateContextInternal(vmx_basic, &g_fail, NULL, &ctx);
    TEST_ASSERT_FALSE(ok, "Boundary VMCS alloc: fails");
    TEST_ASSERT_NULL(ctx, "Boundary VMCS alloc: output NULL");
    TEST_ASSERT_INT_EQ(g_free_contig_count, 1,
                       "Boundary VMCS alloc: 1 contig freed (VMXON)");
    TEST_ASSERT_INT_EQ(g_free_pool_count, 2,
                       "Boundary VMCS alloc: 2 pool freed (context + stack)");
    return 0;
}

/* --- Recovery and destruction guard matrix --- */
static void NoopFreePool(void* p) { (void)p; }
static VMX_ALLOCATION_INTERFACE g_noop_free = {
    NULL, NoopFreePool, NULL, NULL, NULL
};

static int test_inner_recovery_validation(void)
{
    VMX_LAUNCH_CONTEXT inner = {0};
    uint64_t stack_top = 0x00200000ULL + VMX_DEDICATED_HOST_STACK_SIZE;
    uint64_t host_rsp = (stack_top & ~15ULL) - VMX_HOST_STACK_SLOT_SIZE;
    VMX_LAUNCH_CONTEXT* saved = &inner;

    inner.Magic = VMX_LAUNCH_CONTEXT_MAGIC;
    inner.Size = VMX_LAUNCH_CONTEXT_SIZE;
    inner.Version = VMX_LAUNCH_ABI_VERSION;
    inner.HostStackTop = stack_top;
    inner.HostRsp = host_rsp;
    TEST_ASSERT_TRUE(VmxRuntimeContextFromAssembly(&inner) ==
                     (VMX_RUNTIME_LAUNCH_CONTEXT*)saved,
                     "Inner-only ABI recovery");

    inner.Magic = 0;
    TEST_ASSERT_NULL(VmxRuntimeContextFromAssembly(&inner), "Bad Magic rejected");
    inner.Magic = VMX_LAUNCH_CONTEXT_MAGIC;
    inner.Size = VMX_LAUNCH_CONTEXT_SIZE - 8u;
    TEST_ASSERT_NULL(VmxRuntimeContextFromAssembly(&inner), "Bad Size rejected");
    inner.Size = VMX_LAUNCH_CONTEXT_SIZE;
    inner.Version = VMX_LAUNCH_ABI_VERSION + 1u;
    TEST_ASSERT_NULL(VmxRuntimeContextFromAssembly(&inner), "Bad Version rejected");
    inner.Version = VMX_LAUNCH_ABI_VERSION;
    inner.HostRsp = host_rsp + 8u;
    TEST_ASSERT_NULL(VmxRuntimeContextFromAssembly(&inner), "Bad HostRsp rejected");
    inner.HostRsp = host_rsp;
    inner.HostStackTop = VMX_DEDICATED_HOST_STACK_SIZE - 1u;
    TEST_ASSERT_NULL(VmxRuntimeContextFromAssembly(&inner), "HostStackTop underflow rejected");
    inner.HostStackTop = 0x0000800000000000ULL;
    TEST_ASSERT_NULL(VmxRuntimeContextFromAssembly(&inner), "Noncanonical HostStackTop rejected");
    return 0;
}

static int test_address_range_overflow_rejected(void)
{
    TEST_ASSERT_FALSE(VmxIsAddressInStack(UINT64_MAX - 1ULL,
                                          (const void*)(uintptr_t)(UINT64_MAX - 4ULL),
                                          16u),
                      "Stack range overflow rejected");
    TEST_ASSERT_FALSE(VmxIsAddressInStack(0, NULL, 16u), "Null stack rejected");
    return 0;
}

static int test_destroy_slot_requires_exact_layout(void)
{
    uint8_t stack[VMX_DEDICATED_HOST_STACK_SIZE];
    VMX_RUNTIME_LAUNCH_CONTEXT d = {0};
    uint64_t base = (uint64_t)(uintptr_t)stack;
    uint64_t top = base + VMX_DEDICATED_HOST_STACK_SIZE;
    uint64_t host_rsp = (top & ~15ULL) - VMX_HOST_STACK_SLOT_SIZE;
    VMX_RUNTIME_LAUNCH_CONTEXT* marker = &d;
    d.HostStack = stack;
    d.OwnedResources = VMX_RESOURCE_HOST_STACK_BIT;
    d.Assembly.HostRsp = host_rsp;
    *((VMX_RUNTIME_LAUNCH_CONTEXT**)(uintptr_t)host_rsp) = marker;
    VmxLaunchDestroyContextInternal(&g_noop_free, &d);
    TEST_ASSERT_NULL(*(VMX_RUNTIME_LAUNCH_CONTEXT**)(uintptr_t)host_rsp,
                     "Exact HostRsp slot cleared");
    TEST_ASSERT_NULL(d.HostStack, "Host stack released");

    (void)memset(&d, 0, sizeof(d));
    d.HostStack = stack;
    d.OwnedResources = VMX_RESOURCE_HOST_STACK_BIT;
    d.Assembly.HostRsp = host_rsp + 8ULL;
    *((VMX_RUNTIME_LAUNCH_CONTEXT**)(uintptr_t)host_rsp) = marker;
    VmxLaunchDestroyContextInternal(&g_noop_free, &d);
    TEST_ASSERT_TRUE(*(VMX_RUNTIME_LAUNCH_CONTEXT**)(uintptr_t)host_rsp == marker,
                     "Mismatched HostRsp slot preserved");
    TEST_ASSERT_NULL(d.HostStack, "Mismatched HostRsp still releases stack");
    return 0;
}

static int test_destroy_invalid_stack_layout_no_deref(void)
{
    VMX_RUNTIME_LAUNCH_CONTEXT d = {0};
    d.HostStack = (void*)(uintptr_t)(UINT64_MAX - 4ULL);
    d.OwnedResources = VMX_RESOURCE_HOST_STACK_BIT;
    d.Assembly.HostRsp = UINT64_MAX;
    VmxLaunchDestroyContextInternal(&g_noop_free, &d);
    TEST_ASSERT_NULL(d.HostStack, "Invalid stack released without dereference");
    return 0;
}
TEST_MAIN_BEGIN()
    TEST_RUN(test_assembly_offset_zero);
    TEST_RUN(test_align_down_16);
    TEST_RUN(test_canonical);
    TEST_RUN(test_in_stack);
    TEST_RUN(test_resource_bits);
    TEST_RUN(test_create_success);
    TEST_RUN(test_context_abi_fields_initialized);
    TEST_RUN(test_context_dispatcher_injection);
    TEST_RUN(test_rejects_invalid_basic);
    TEST_RUN(test_fails_context_alloc);
    TEST_RUN(test_fails_host_stack);
    TEST_RUN(test_fails_vmxon);
    TEST_RUN(test_fails_vmcs);
    TEST_RUN(test_destroy_null_context);
    TEST_RUN(test_destroy_null_allocator);
    TEST_RUN(test_inner_recovery_validation);
    TEST_RUN(test_address_range_overflow_rejected);
    TEST_RUN(test_destroy_slot_requires_exact_layout);
    TEST_RUN(test_destroy_invalid_stack_layout_no_deref);
    TEST_RUN(test_allocation_size_always_4096);
    TEST_RUN(test_allocation_propagates_32bit_limit);
    TEST_RUN(test_allocation_respects_advertised_size);
    TEST_RUN(test_unwind_invalid_vmxon_pa);
    TEST_RUN(test_unwind_invalid_vmcs_pa);
    TEST_RUN(test_boundary_vmxon_alloc_fails);
    TEST_RUN(test_boundary_vmcs_alloc_fails);
TEST_MAIN_END()
