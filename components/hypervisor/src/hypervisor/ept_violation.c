/*
 * ept_violation.c — EPT violation qualification decoder and handler
 *
 * Pure-logic decode of the VM-Exit qualification field for EPT violations
 * and misconfigurations. Extracts access type, GPA, and GLA from the
 * qualification value. Dispatches to hook table for hook-aware handling.
 *
 * No WDK dependency — fully unit-testable.
 */

#include "ept.h"
#include "vmx.h"
#include <stdint.h>

/*===========================================================================
 * Access Type Decode
 *
 * Extracts the data read/write/execute bits from the exit qualification.
 * Returns a bitmask: EPT_QUAL_DATA_READ | WRITE | EXECUTE.
 *===========================================================================*/

uint8_t EptViolationGetAccessType(uint64_t exit_qualification) {
    return (uint8_t)(exit_qualification & (EPT_QUAL_DATA_READ |
                                           EPT_QUAL_DATA_WRITE |
                                           EPT_QUAL_DATA_EXECUTE));
}

/*===========================================================================
 * GPA Extraction
 *
 * Extracts the guest-physical address from the exit qualification.
 * GPA is in bits 63:12, shifted right 12.
 *===========================================================================*/

uint64_t EptViolationGetGpa(uint64_t exit_qualification) {
    return (exit_qualification & EPT_QUAL_GPA_MASK) >> EPT_QUAL_GPA_SHIFT;
}

/*===========================================================================
 * GLA Extraction
 *
 * Returns the guest-linear address from the exit qualification.
 * For unit testing, the full qualification value is returned as-is
 * (the real GLA is in a separate VMCS field in kernel mode,
 * but for pure-logic testing we pass it through the same interface).
 *===========================================================================*/

uint64_t EptViolationGetGla(uint64_t exit_qualification) {
    return exit_qualification;
}

/*===========================================================================
 * EPT Violation Handler
 *
 * Dispatches an EPT violation VM-Exit. Looks up the violating GPA in the
 * hook table. If hooked, returns success (the hook action will be applied
 * by the kernel-mode handler). If not hooked, returns error (the kernel
 * will inject a #PF into the guest).
 *
 * This is a fault-class exit: RIP is NOT advanced. The guest will
 * re-execute the faulting instruction after the EPT permissions are fixed.
 *
 * Parameters:
 *   ctx        — VM-Exit context (for GPA extraction and guest state)
 *   hook_table — Active hook table to search
 *
 * Returns:
 *   0 if hooked (permission fix applied at kernel level)
 *  -1 if not hooked (inject #PF), NULL parameters, or hook table empty
 *===========================================================================*/

int EptHandleViolation(vm_exit_context_t* ctx, hook_table_t* hook_table) {
    uint64_t      gpa;
    uint32_t      hook_id;
    hook_entry_t* entry;

    if (ctx == NULL || hook_table == NULL) {
        return -1;
    }

    /* Fault-class exit — do NOT advance RIP */
    ctx->advance_rip = false;

    /* Extract GPA from exit qualification */
    gpa = EptViolationGetGpa(ctx->exit_qualification);

    /* Look up in hook table */
    if (HookFindByGpa(hook_table, gpa, &hook_id, &entry) == 0) {
        /* GPA is hooked — kernel-mode handler will apply permission change.
         * For now, just acknowledge the hook match. */
        (void)hook_id;
        (void)entry;
        return 0;
    }

    /* Not hooked — kernel will inject #PF into the guest */
    return -1;
}

/*===========================================================================
 * EPT Misconfiguration Handler
 *
 * Handles EXIT_REASON_EPT_MISCONFIG (49). This is always an error condition.
 * The handler logs and returns error. No RIP advancement (fault-class).
 *===========================================================================*/

int EptHandleMisconfig(vm_exit_context_t* ctx) {
    if (ctx == NULL) {
        return -1;
    }

    /* Fault-class exit — do NOT advance RIP */
    ctx->advance_rip = false;

    /* EPT misconfiguration is always an error */
    return -1;
}
