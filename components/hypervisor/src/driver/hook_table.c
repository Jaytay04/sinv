/*
 * hook_table.c — Kernel-mode hook table with spinlock serialization
 *
 * WDK-dependent kernel module. Wraps the pure-logic hook table
 * (hook_manager.c in hypervisor_core) with WDF spinlock protection
 * for concurrent access from DPC routines and the IOCTL path.
 *
 * The hook table is a shared resource accessed by:
 *   - IOCTL_HOOK / IOCTL_UNHOOK (passive level)
 *   - EPT violation handler (VMX root, potentially elevated IRQL)
 *
 * Spinlock: WDFSPINLOCK acquired/released around each operation.
 */

#include <ntddk.h>
#include <wdf.h>

#include "ept.h"
#include "driver_interface.h"

/*===========================================================================
 * Global Hook Table
 *===========================================================================*/

static hook_table_t  g_hook_table;
static WDFSPINLOCK   g_hook_lock = NULL;

/*===========================================================================
 * Initialization
 *
 * Called once during driver initialization. Allocates the spinlock and
 * initializes the hook table. Must be called before any hook operations.
 *===========================================================================*/

NTSTATUS
DriverHookTableInit(
    _In_ WDFDEVICE Device)
{
    NTSTATUS status;

    /* Create spinlock for hook table serialization */
    status = WdfSpinLockCreate(
        WDF_NO_OBJECT_ATTRIBUTES,
        &g_hook_lock);

    if (!NT_SUCCESS(status)) {
        KdPrint(("hypersinv: DriverHookTableInit — spinlock creation failed: 0x%08X\n",
                 status));
        return status;
    }

    /* Initialize the pure-logic hook table */
    HookTableInit(&g_hook_table);

    KdPrint(("hypersinv: DriverHookTableInit — hook table ready (max %u hooks)\n",
             EPT_MAX_HOOKS));

    UNREFERENCED_PARAMETER(Device);
    return STATUS_SUCCESS;
}

/*===========================================================================
 * Hook Registration (Kernel Wrapper)
 *
 * Acquires spinlock, calls pure-logic HookRegister, releases lock.
 *
 * Returns:
 *   0 on success
 *  -1 on invalid parameters
 *  -2 on table full or duplicate GPA
 *===========================================================================*/

int
DriverHookRegister(
    uint64_t  gpa,
    uint8_t   permissions,
    uint32_t* out_hook_id)
{
    int      result;
    WDFREQUEST dummy = NULL;

    if (g_hook_lock == NULL) {
        KdPrint(("hypersinv: DriverHookRegister — spinlock not initialized\n"));
        return -1;
    }

    WdfSpinLockAcquire(g_hook_lock);

    result = HookRegister(&g_hook_table, gpa, permissions, out_hook_id);

    WdfSpinLockRelease(g_hook_lock);

    UNREFERENCED_PARAMETER(dummy);

    KdPrint(("hypersinv: DriverHookRegister — GPA=0x%llx, perms=0x%02x, "
             "result=%d, hook_id=%u\n",
             gpa, permissions, result, out_hook_id ? *out_hook_id : 0));

    return result;
}

/*===========================================================================
 * Hook Unregistration (Kernel Wrapper)
 *
 * Acquires spinlock, finds hook by GPA, calls pure-logic HookUnregister,
 * releases lock.
 *
 * Returns:
 *   0 on success
 *  -1 if GPA not found or table not initialized
 *===========================================================================*/

int
DriverHookUnregister(
    uint64_t gpa)
{
    int            result;
    uint32_t       hook_id;
    hook_entry_t*  entry;

    if (g_hook_lock == NULL) {
        KdPrint(("hypersinv: DriverHookUnregister — spinlock not initialized\n"));
        return -1;
    }

    WdfSpinLockAcquire(g_hook_lock);

    /* Find hook by GPA */
    result = HookFindByGpa(&g_hook_table, gpa, &hook_id, &entry);

    if (result == 0) {
        /* Hook found — unregister it */
        result = HookUnregister(&g_hook_table, hook_id);
    }

    WdfSpinLockRelease(g_hook_lock);

    KdPrint(("hypersinv: DriverHookUnregister — GPA=0x%llx, result=%d\n",
             gpa, result));

    return result;
}

/*===========================================================================
 * Hook Lookup by GPA (for EPT Violation Handler)
 *
 * Called from the EPT violation handler at VMX root (potentially elevated
 * IRQL). Uses the same pure-logic lookup but with spinlock protection.
 *
 * Returns:
 *   0 if found (hook_id and entry populated)
 *  -1 if not found or table not initialized
 *===========================================================================*/

int
DriverHookFindByGpa(
    uint64_t        gpa,
    uint32_t*       out_hook_id,
    hook_entry_t**  out_entry)
{
    int result;

    if (g_hook_lock == NULL) {
        return -1;
    }

    WdfSpinLockAcquire(g_hook_lock);
    result = HookFindByGpa(&g_hook_table, gpa, out_hook_id, out_entry);
    WdfSpinLockRelease(g_hook_lock);

    return result;
}

/*===========================================================================
 * Hook Count Query
 *
 * Returns the active hook count without acquiring the lock (atomic read
 * from a volatile field is sufficient for approximate count).
 *===========================================================================*/

uint32_t
DriverHookGetCount(void)
{
    return g_hook_table.active_count;
}
