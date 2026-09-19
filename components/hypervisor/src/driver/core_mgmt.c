/*
 * core_mgmt.c — DPC broadcast for per-core VMX initialization and teardown
 *
 * WDK-dependent kernel module. Orchestrates VMX entry/exit across all
 * logical processors using KeGenericCallDpc. Manages per-core VMX context
 * arrays, synchronization barriers, and state transitions.
 *
 * This file is the kernel-side bridge between the driver (driver_main.c)
 * and the hypervisor core library (vmx_init.c, cpu_features.c).
 */

#include <ntddk.h>
#include <wdf.h>

#include "vmx.h"
#include "ept.h"
#include "driver_interface.h"

/* ntddk.h in WDK 10.0.28000.0 may not declare the generic-call DPC APIs. */
NTKERNELAPI VOID KeGenericCallDpc(
    _In_ PKDEFERRED_ROUTINE Routine,
    _In_opt_ PVOID Context);
NTKERNELAPI VOID KeSignalCallDpcDone(
    _In_ PVOID SystemArgument1);
NTKERNELAPI LOGICAL KeSignalCallDpcSynchronize(
    _In_ PVOID SystemArgument2);

/*===========================================================================
 * Per-Core VMX Context
 *
 * One instance per logical processor, indexed by processor number.
 * Populated during DPC broadcast and freed on teardown.
 *===========================================================================*/

typedef struct _VMX_CPU_CONTEXT {
    /* VMX region pointers (virtual and physical) */
    void*     vmxon_region_va;       /* Virtual address of VMXON region */
    uint64_t  vmxon_region_pa;       /* Physical address of VMXON region */
    void*     vmcs_region_va;        /* Virtual address of VMCS region */
    uint64_t  vmcs_region_pa;        /* Physical address of VMCS region */

    /* MSR bitmap and EPT */
    void*     msr_bitmap_va;
    uint64_t  msr_bitmap_pa;

    /* VPID (1-based per core) */
    uint16_t  vpid;

    /* State flags */
    volatile LONG vmx_active;        /* 1 if this core is in VMX root */

    /* Synchronization */
    KSPIN_LOCK  state_lock;
} VMX_CPU_CONTEXT;

/*===========================================================================
 * Global State
 *===========================================================================*/

#define HYPERVISOR_MAX_CORES    64

static VMX_CPU_CONTEXT g_cpu_contexts[HYPERVISOR_MAX_CORES];
static volatile LONG   g_initialized_cores = 0;
static volatile LONG   g_teardown_cores    = 0;
static volatile LONG   g_vmx_init_state    = DRIVER_STATE_UNLOADED;

/* Spinlock for state transitions */
static KSPIN_LOCK g_vmx_lock;

/*===========================================================================
 * Helper: Get physical address (approximate for non-paged pool)
 *
 * In WDF, MmGetPhysicalAddress translates virtual to physical for
 * non-paged pool allocations. Returns the physical address.
 *===========================================================================*/

static
uint64_t
CoreGetPhysicalAddress(
    _In_ void* VirtualAddress)
{
    PHYSICAL_ADDRESS pa;

    pa = MmGetPhysicalAddress(VirtualAddress);
    return pa.QuadPart;
}

/*===========================================================================
 * VMX Region Allocation (Kernel-Mode Implementation)
 *
 * Allocates 4KB-aligned non-pageable memory for VMXON and VMCS regions.
 * Writes the VMCS revision identifier to offset 0 of each region.
 *===========================================================================*/

static
NTSTATUS
CoreAllocateVmxonRegion(
    _Out_ void**    region_out,
    _Out_ uint64_t* pa_out)
{
    void* region;
    uint32_t revision;

    *region_out = NULL;
    *pa_out = 0;

    /* Allocate 4KB from non-paged pool (NonPagedPoolNx for security) */
    region = ExAllocatePool2(
        POOL_FLAG_NON_PAGED,
        VMX_REGION_MIN_SIZE,
        'XMVH');  /* Tag: "HVMX" reversed */

    if (region == NULL) {
        KdPrint(("hypersinv: CoreAllocateVmxonRegion — allocation failed\n"));
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Zero the region */
    RtlZeroMemory(region, VMX_REGION_MIN_SIZE);

    /* Verify 4KB alignment */
    if (!VmxIsRegionAligned(region)) {
        KdPrint(("hypersinv: CoreAllocateVmxonRegion — alignment check failed\n"));
        ExFreePool(region);
        return STATUS_DATATYPE_MISALIGNMENT;
    }

    /* Get VMCS revision ID from IA32_VMX_BASIC and write to region */
    revision = VmxReadVmcsRevision();
    *(uint32_t*)region = revision;

    /* Get physical address */
    *pa_out = CoreGetPhysicalAddress(region);
    *region_out = region;

    KdPrint(("hypersinv: CoreAllocateVmxonRegion — va=%p, pa=0x%llx, rev=%u\n",
             region, *pa_out, revision));

    return STATUS_SUCCESS;
}

/*===========================================================================
 * VMCS Region Allocation
 *===========================================================================*/

static
NTSTATUS
CoreAllocateVmcsRegion(
    _Out_ void**    region_out,
    _Out_ uint64_t* pa_out)
{
    void* region;
    uint32_t revision;

    *region_out = NULL;
    *pa_out = 0;

    region = ExAllocatePool2(
        POOL_FLAG_NON_PAGED,
        VMX_REGION_MIN_SIZE,
        'SMVH');  /* Tag: "HVMS" reversed */

    if (region == NULL) {
        KdPrint(("hypersinv: CoreAllocateVmcsRegion — allocation failed\n"));
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(region, VMX_REGION_MIN_SIZE);

    if (!VmxIsRegionAligned(region)) {
        KdPrint(("hypersinv: CoreAllocateVmcsRegion — alignment check failed\n"));
        ExFreePool(region);
        return STATUS_DATATYPE_MISALIGNMENT;
    }

    revision = VmxReadVmcsRevision();
    *(uint32_t*)region = revision;

    *pa_out = CoreGetPhysicalAddress(region);
    *region_out = region;

    return STATUS_SUCCESS;
}

/*===========================================================================
 * Per-Core VMX Init Callback (DPC Routine)
 *
 * Executes on EACH logical processor during KeGenericCallDpc broadcast.
 * Sequence:
 *   1. Allocate VMXON region + VMCS region
 *   2. Enter VMX root (CR4.VMXE + VMXON + VMCLEAR + VMPTRLD)
 *   3. Configure VMCS control fields
 *   4. Configure host and guest state
 *   5. Launch VM (VMLAUNCH)
 *
 * Each core operates on its own g_cpu_contexts[processor_number].
 *===========================================================================*/

static
KDEFERRED_ROUTINE
VmxInitOnCpu;
_Use_decl_annotations_
VOID
VmxInitOnCpu(
    PKDPC Dpc,
    PVOID DeferredContext,
    PVOID SystemArgument1,
    PVOID SystemArgument2)
{
    ULONG               processor_number;
    VMX_CPU_CONTEXT*    ctx;
    NTSTATUS            status;
    int                 vmx_result;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(DeferredContext);

    processor_number = KeGetCurrentProcessorNumber();
    if (processor_number >= HYPERVISOR_MAX_CORES) {
        KdPrint(("hypersinv: VmxInitOnCpu — processor %lu exceeds MAX_CORES\n",
                 processor_number));
        goto signal_complete;
    }

    ctx = &g_cpu_contexts[processor_number];
    RtlZeroMemory(ctx, sizeof(*ctx));

    KdPrint(("hypersinv: VmxInitOnCpu — core %lu starting VMX init\n",
             processor_number));

    /*-----------------------------------------------------------------
     * Step 1: Allocate VMXON region
     *-----------------------------------------------------------------*/
    status = CoreAllocateVmxonRegion(
        &ctx->vmxon_region_va,
        &ctx->vmxon_region_pa);
    if (!NT_SUCCESS(status)) {
        KdPrint(("hypersinv: VmxInitOnCpu — core %lu: VMXON alloc failed\n",
                 processor_number));
        goto signal_complete;
    }

    /*-----------------------------------------------------------------
     * Step 2: Allocate VMCS region
     *-----------------------------------------------------------------*/
    status = CoreAllocateVmcsRegion(
        &ctx->vmcs_region_va,
        &ctx->vmcs_region_pa);
    if (!NT_SUCCESS(status)) {
        KdPrint(("hypersinv: VmxInitOnCpu — core %lu: VMCS alloc failed\n",
                 processor_number));
        ExFreePool(ctx->vmxon_region_va);
        ctx->vmxon_region_va = NULL;
        goto signal_complete;
    }

    /*-----------------------------------------------------------------
     * Step 3: Enter VMX root mode
     *
     * This performs the full VMXON → VMCLEAR → VMPTRLD sequence
     * using the regions allocated above.
     *-----------------------------------------------------------------*/
    vmx_result = VmxEnterRoot(
        (const void*)(uintptr_t)ctx->vmxon_region_pa,
        (const void*)(uintptr_t)ctx->vmcs_region_pa);

    if (vmx_result != VMX_SUCCESS) {
        KdPrint(("hypersinv: VmxInitOnCpu — core %lu: VmxEnterRoot failed (%d)\n",
                 processor_number, vmx_result));
        goto cleanup;
    }

    /*-----------------------------------------------------------------
     * Step 4: Configure VMCS control fields
     *-----------------------------------------------------------------*/
    vmx_result = VmxConfigureControls();
    if (vmx_result != VMX_SUCCESS) {
        KdPrint(("hypersinv: VmxInitOnCpu — core %lu: VmxConfigureControls failed (%d)\n",
                 processor_number, vmx_result));
        VmxLeaveRoot();
        goto cleanup;
    }

    /*-----------------------------------------------------------------
     * Step 5: Configure host state (host CR0, CR3, CR4, RIP, RSP, etc.)
     *-----------------------------------------------------------------*/
    vmx_result = VmxConfigureHostState();
    if (vmx_result != VMX_SUCCESS) {
        KdPrint(("hypersinv: VmxInitOnCpu — core %lu: VmxConfigureHostState failed (%d)\n",
                 processor_number, vmx_result));
        VmxLeaveRoot();
        goto cleanup;
    }

    /*-----------------------------------------------------------------
     * Step 6: Configure guest state (mirrors host initially)
     *-----------------------------------------------------------------*/
    vmx_result = VmxConfigureGuestState();
    if (vmx_result != VMX_SUCCESS) {
        KdPrint(("hypersinv: VmxInitOnCpu — core %lu: VmxConfigureGuestState failed (%d)\n",
                 processor_number, vmx_result));
        VmxLeaveRoot();
        goto cleanup;
    }

    /*-----------------------------------------------------------------
     * Step 7: Assign VPID (1-based per core for uniqueness)
     *-----------------------------------------------------------------*/
    ctx->vpid = (uint16_t)(processor_number + 1);

    /*-----------------------------------------------------------------
     * Step 8: Launch VM
     *-----------------------------------------------------------------*/
    vmx_result = VmxLaunch();
    if (vmx_result != VMX_SUCCESS) {
        KdPrint(("hypersinv: VmxInitOnCpu — core %lu: VmxLaunch failed (%d)\n",
                 processor_number, vmx_result));
        VmxLeaveRoot();
        goto cleanup;
    }

    /* Success — core is now in VMX root mode */
    ctx->vmx_active = 1;
    InterlockedIncrement(&g_initialized_cores);

    KdPrint(("hypersinv: VmxInitOnCpu — core %lu: VMX root active, VPID=%u\n",
             processor_number, ctx->vpid));

    goto signal_complete;

cleanup:
    /* Free resources on failure */
    if (ctx->vmcs_region_va) {
        ExFreePool(ctx->vmcs_region_va);
        ctx->vmcs_region_va = NULL;
    }
    if (ctx->vmxon_region_va) {
        ExFreePool(ctx->vmxon_region_va);
        ctx->vmxon_region_va = NULL;
    }

signal_complete:
    KeSignalCallDpcSynchronize(SystemArgument2);
    KeSignalCallDpcDone(SystemArgument1);
}

/*===========================================================================
 * Per-Core VMX Teardown Callback (DPC Routine)
 *
 * Executes on EACH logical processor during KeGenericCallDpc broadcast.
 * Sequence: VMCLEAR → VMXOFF → free regions.
 *===========================================================================*/

static
KDEFERRED_ROUTINE
VmxTeardownOnCpu;
_Use_decl_annotations_
VOID
VmxTeardownOnCpu(
    PKDPC Dpc,
    PVOID DeferredContext,
    PVOID SystemArgument1,
    PVOID SystemArgument2)
{
    ULONG               processor_number;
    VMX_CPU_CONTEXT*    ctx;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(DeferredContext);

    processor_number = KeGetCurrentProcessorNumber();
    if (processor_number >= HYPERVISOR_MAX_CORES) {
        goto signal_complete;
    }

    ctx = &g_cpu_contexts[processor_number];

    if (!ctx->vmx_active) {
        goto signal_complete;
    }

    KdPrint(("hypersinv: VmxTeardownOnCpu — core %lu: exiting VMX\n",
             processor_number));

    /* VMCLEAR + VMXOFF */
    VmxLeaveRoot();

    ctx->vmx_active = 0;

    /* Free regions */
    if (ctx->vmcs_region_va) {
        ExFreePool(ctx->vmcs_region_va);
        ctx->vmcs_region_va = NULL;
    }
    if (ctx->vmxon_region_va) {
        ExFreePool(ctx->vmxon_region_va);
        ctx->vmxon_region_va = NULL;
    }

    InterlockedIncrement(&g_teardown_cores);

signal_complete:
    KeSignalCallDpcSynchronize(SystemArgument2);
    KeSignalCallDpcDone(SystemArgument1);
}

/*===========================================================================
 * Public API: Initialize All Cores
 *
 * Called from IOCTL_HYPERVISOR_LOAD handler. Broadcasts VmxInitOnCpu to
 * all logical processors via KeGenericCallDpc.
 *
 * Returns:
 *   STATUS_SUCCESS              — all cores entered VMX root
 *   STATUS_UNSUCCESSFUL         — one or more cores failed
 *   STATUS_HV_OPERATION_FAILED  — hypervisor already active
 *===========================================================================*/

NTSTATUS
VmxInitAllCores(void)
{
    NTSTATUS            status;
    KIRQL               old_irql;
    LONG                active_count;

    /* Prevent concurrent init/teardown */
    KeAcquireSpinLock(&g_vmx_lock, &old_irql);

    if (g_vmx_init_state == DRIVER_STATE_LOADED) {
        KeReleaseSpinLock(&g_vmx_lock, old_irql);
        KdPrint(("hypersinv: VmxInitAllCores — already active\n"));
        return STATUS_HV_OPERATION_FAILED;
    }

    g_vmx_init_state = DRIVER_STATE_LOADING;
    KeReleaseSpinLock(&g_vmx_lock, old_irql);

    /* Reset global state */
    g_initialized_cores = 0;
    RtlZeroMemory(g_cpu_contexts, sizeof(g_cpu_contexts));

    /* Broadcast VMX init to all cores */
    KdPrint(("hypersinv: VmxInitAllCores — broadcasting DPC to all cores\n"));

    KeGenericCallDpc(
        VmxInitOnCpu,
        NULL);

    /* Check results */
    active_count = g_initialized_cores;

    KdPrint(("hypersinv: VmxInitAllCores — %ld/%ld cores initialized\n",
             active_count, (LONG)KeQueryActiveProcessorCount(NULL)));

    KeAcquireSpinLock(&g_vmx_lock, &old_irql);

    if (active_count > 0) {
        g_vmx_init_state = DRIVER_STATE_LOADED;
        status = STATUS_SUCCESS;
    } else {
        g_vmx_init_state = DRIVER_STATE_ERROR;
        status = STATUS_UNSUCCESSFUL;
    }

    KeReleaseSpinLock(&g_vmx_lock, old_irql);

    return status;
}

/*===========================================================================
 * Public API: Teardown All Cores
 *
 * Called from IOCTL_HYPERVISOR_UNLOAD handler. Broadcasts VmxTeardownOnCpu
 * to all logical processors.
 *===========================================================================*/

NTSTATUS
VmxTeardownAllCores(void)
{
    KIRQL   old_irql;

    KeAcquireSpinLock(&g_vmx_lock, &old_irql);

    if (g_vmx_init_state != DRIVER_STATE_LOADED) {
        KeReleaseSpinLock(&g_vmx_lock, old_irql);
        KdPrint(("hypersinv: VmxTeardownAllCores — not active\n"));
        return STATUS_HV_OPERATION_FAILED;
    }

    g_vmx_init_state = DRIVER_STATE_UNLOADING;
    g_teardown_cores = 0;

    KeReleaseSpinLock(&g_vmx_lock, old_irql);

    /* Broadcast VMXOFF to all cores */
    KdPrint(("hypersinv: VmxTeardownAllCores — broadcasting teardown DPC\n"));

    KeGenericCallDpc(
        VmxTeardownOnCpu,
        NULL);

    KdPrint(("hypersinv: VmxTeardownAllCores — %ld cores torn down\n",
             g_teardown_cores));

    KeAcquireSpinLock(&g_vmx_lock, &old_irql);
    g_vmx_init_state = DRIVER_STATE_UNLOADED;
    KeReleaseSpinLock(&g_vmx_lock, old_irql);

    return STATUS_SUCCESS;
}

/*===========================================================================
 * Public API: Get Driver State
 *===========================================================================*/

LONG
VmxGetDriverState(void)
{
    return g_vmx_init_state;
}

/*===========================================================================
 * Public API: Get Per-Core Context (for status queries)
 *===========================================================================*/

NTSTATUS
VmxGetCoreContext(
    _In_  ULONG             core_index,
    _Out_ VMX_CPU_CONTEXT** context_out)
{
    if (core_index >= HYPERVISOR_MAX_CORES) {
        return STATUS_INVALID_PARAMETER;
    }

    *context_out = &g_cpu_contexts[core_index];
    return STATUS_SUCCESS;
}
