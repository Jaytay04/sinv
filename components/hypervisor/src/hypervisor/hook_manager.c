/*
 * hook_manager.c — Pure-logic hook table CRUD
 *
 * Fixed-size hook table with register, unregister, and find-by-GPA.
 * No WDK dependency — fully unit-testable. Spinlock serialization
 * is handled by kernel-mode wrappers in src/driver/hook_table.c.
 */

#include "ept.h"
#include <stdint.h>
#include <string.h>

/*===========================================================================
 * Hook Table Initialization
 *
 * Zeroes all entries and resets active_count.
 *===========================================================================*/

void HookTableInit(hook_table_t* table) {
    if (table == NULL) {
        return;
    }
    memset(table, 0, sizeof(*table));
}

/*===========================================================================
 * Hook Registration
 *
 * Finds the first free slot, stores GPA + permissions, increments
 * active_count. Returns the slot index as out_hook_id.
 *
 * Returns:
 *   0 on success
 *  -1 if table or out_hook_id is NULL
 *  -2 if table is full
 *  -2 if GPA is already registered (duplicate check)
 *===========================================================================*/

int HookRegister(hook_table_t* table, uint64_t gpa, uint8_t permissions,
                 uint32_t* out_hook_id) {
    uint32_t i;

    if (table == NULL || out_hook_id == NULL) {
        return -1;
    }

    /* Check for duplicate GPA */
    for (i = 0; i < EPT_MAX_HOOKS; i++) {
        if (table->entries[i].in_use && table->entries[i].gpa == gpa) {
            return -2;  /* Duplicate GPA */
        }
    }

    /* Find first free slot */
    for (i = 0; i < EPT_MAX_HOOKS; i++) {
        if (!table->entries[i].in_use) {
            /* Initialize slot */
            memset(&table->entries[i], 0, sizeof(table->entries[i]));
            table->entries[i].gpa = gpa;
            table->entries[i].in_use = 1;
            table->entries[i].permissions = permissions;
            /* Store permissions in the bit-field */
            table->entries[i].new_read  = (permissions & HOOK_PERM_READ)  ? 1 : 0;
            table->entries[i].new_write = (permissions & HOOK_PERM_WRITE) ? 1 : 0;
            table->entries[i].new_exec  = (permissions & HOOK_PERM_EXEC)  ? 1 : 0;

            *out_hook_id = i;
            table->active_count++;
            return 0;
        }
    }

    /* Table is full */
    return -2;
}

/*===========================================================================
 * Hook Unregistration
 *
 * Frees a slot by hook_id (array index). Decrements active_count.
 *
 * Returns:
 *   0 on success
 *  -1 if table is NULL, hook_id out of range, or slot already free
 *===========================================================================*/

int HookUnregister(hook_table_t* table, uint32_t hook_id) {
    if (table == NULL || hook_id >= EPT_MAX_HOOKS) {
        return -1;
    }

    if (!table->entries[hook_id].in_use) {
        return -1;  /* Already free */
    }

    /* Clear the slot */
    memset(&table->entries[hook_id], 0, sizeof(table->entries[hook_id]));
    table->active_count--;
    return 0;
}

/*===========================================================================
 * Hook Lookup by GPA
 *
 * Linear scan for a matching GPA. Returns the hook_id (slot index) and
 * a pointer to the hook_entry_t.
 *
 * Returns:
 *   0 on found
 *  -1 if table or outputs are NULL, or GPA not found
 *===========================================================================*/

int HookFindByGpa(hook_table_t* table, uint64_t gpa,
                  uint32_t* out_hook_id, hook_entry_t** out_entry) {
    uint32_t i;

    if (table == NULL || out_hook_id == NULL || out_entry == NULL) {
        return -1;
    }

    for (i = 0; i < EPT_MAX_HOOKS; i++) {
        if (table->entries[i].in_use && table->entries[i].gpa == gpa) {
            *out_hook_id = i;
            *out_entry  = &table->entries[i];
            return 0;
        }
    }

    /* Not found */
    return -1;
}
