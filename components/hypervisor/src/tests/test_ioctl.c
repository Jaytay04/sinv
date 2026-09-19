/*
 * test_ioctl.c — Unit tests for IOCTL dispatch, hook table, and EPT violation
 *
 * Tests hook table CRUD, EPT violation qualification decode, and IOCTL
 * action mapping. Pure logic — no kernel dependencies.
 * Links against hypervisor_core.
 */

#include "test_common.h"
#include "vmx.h"
#include "ept.h"
#include "driver_interface.h"
#include "ioctl_launch_probe.h"
#include "vmx_diagnostic.h"
#include "vmx_lifecycle.h"
#include "vmx_kernel_adapters.h"

#include <stddef.h>
#include <string.h>

/*===========================================================================
 * Helper: initialize a clean vm_exit_context_t
 *===========================================================================*/

static void init_context(vm_exit_context_t* ctx, uint32_t exit_reason,
                         uint64_t exit_qual) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->exit_reason = exit_reason;
    ctx->exit_qualification = exit_qual;
    ctx->guest_rip = 0xFFFF800000010000ULL;
    ctx->instruction_length = 3;
}

/*===========================================================================
 * Task 4.2 — Hook Table CRUD (hook_manager.c)
 *===========================================================================*/

static int test_hook_table_init(void) {
    hook_table_t table;
    HookTableInit(&table);

    TEST_ASSERT_UINT_EQ(table.active_count, 0, "init: active_count zero");
    /* Verify all slots are free */
    for (uint32_t i = 0; i < EPT_MAX_HOOKS; i++) {
        char msg[64];
        snprintf(msg, sizeof(msg), "init: slot %u in_use=0", (unsigned)i);
        TEST_ASSERT_UINT_EQ(table.entries[i].in_use, 0, msg);
    }
    return 0;
}

static int test_hook_register_single(void) {
    hook_table_t table;
    HookTableInit(&table);

    uint32_t hook_id = 0xFFFFFFFF;
    int rc = HookRegister(&table, 0x5000, HOOK_PERM_WRITE, &hook_id);

    TEST_ASSERT_INT_EQ(rc, 0, "register single: returns success");
    TEST_ASSERT_UINT_EQ(hook_id, 0, "register single: hook_id=0");
    TEST_ASSERT_UINT_EQ(table.active_count, 1, "register single: active_count=1");
    TEST_ASSERT_UINT_EQ(table.entries[0].in_use, 1, "register single: slot in_use");
    TEST_ASSERT_UINT_EQ(table.entries[0].gpa, 0x5000, "register single: GPA stored");
    TEST_ASSERT_UINT_EQ(table.entries[0].permissions, HOOK_PERM_WRITE,
                        "register single: permissions stored");
    return 0;
}

static int test_hook_register_multiple(void) {
    hook_table_t table;
    HookTableInit(&table);

    uint32_t id_a, id_b, id_c;
    int rc;

    rc = HookRegister(&table, 0x1000, HOOK_PERM_READ, &id_a);
    TEST_ASSERT_INT_EQ(rc, 0, "register multi A: ok");

    rc = HookRegister(&table, 0x2000, HOOK_PERM_WRITE, &id_b);
    TEST_ASSERT_INT_EQ(rc, 0, "register multi B: ok");

    rc = HookRegister(&table, 0x3000, HOOK_PERM_EXECUTE, &id_c);
    TEST_ASSERT_INT_EQ(rc, 0, "register multi C: ok");

    TEST_ASSERT_UINT_EQ(table.active_count, 3, "register multi: count=3");
    TEST_ASSERT_UINT_EQ(id_a, 0, "register multi: id_a=0");
    TEST_ASSERT_UINT_EQ(id_b, 1, "register multi: id_b=1");
    TEST_ASSERT_UINT_EQ(id_c, 2, "register multi: id_c=2");

    /* Verify GPAs are correct */
    TEST_ASSERT_UINT_EQ(table.entries[0].gpa, 0x1000, "register multi: slot 0 GPA");
    TEST_ASSERT_UINT_EQ(table.entries[1].gpa, 0x2000, "register multi: slot 1 GPA");
    TEST_ASSERT_UINT_EQ(table.entries[2].gpa, 0x3000, "register multi: slot 2 GPA");
    return 0;
}

static int test_hook_register_different_permissions(void) {
    hook_table_t table;
    HookTableInit(&table);

    uint32_t id;

    HookRegister(&table, 0x5000, HOOK_PERM_NONE, &id);
    TEST_ASSERT_UINT_EQ(table.entries[0].permissions, HOOK_PERM_NONE,
                        "perms: none = 0x00");

    HookRegister(&table, 0x6000, HOOK_PERM_READ, &id);
    TEST_ASSERT_UINT_EQ(table.entries[1].permissions, HOOK_PERM_READ,
                        "perms: read = 0x01");

    HookRegister(&table, 0x7000, HOOK_PERM_WRITE, &id);
    TEST_ASSERT_UINT_EQ(table.entries[2].permissions, HOOK_PERM_WRITE,
                        "perms: write = 0x02");

    HookRegister(&table, 0x8000, HOOK_PERM_EXECUTE, &id);
    TEST_ASSERT_UINT_EQ(table.entries[3].permissions, HOOK_PERM_EXECUTE,
                        "perms: execute = 0x04");

    HookRegister(&table, 0x9000, HOOK_PERM_READ | HOOK_PERM_WRITE, &id);
    TEST_ASSERT_UINT_EQ(table.entries[4].permissions, HOOK_PERM_READ | HOOK_PERM_WRITE,
                        "perms: R/W = 0x03");

    HookRegister(&table, 0xA000, HOOK_PERM_READ | HOOK_PERM_WRITE | HOOK_PERM_EXECUTE, &id);
    TEST_ASSERT_UINT_EQ(table.entries[5].permissions,
                        HOOK_PERM_READ | HOOK_PERM_WRITE | HOOK_PERM_EXECUTE,
                        "perms: R/W/X = 0x07");
    return 0;
}

static int test_hook_unregister_by_id(void) {
    hook_table_t table;
    HookTableInit(&table);

    uint32_t id;
    HookRegister(&table, 0x1000, HOOK_PERM_READ, &id);
    HookRegister(&table, 0x2000, HOOK_PERM_WRITE, &id);
    HookRegister(&table, 0x3000, HOOK_PERM_EXECUTE, &id);

    /* Unregister middle slot (ID 1) */
    int rc = HookUnregister(&table, 1);
    TEST_ASSERT_INT_EQ(rc, 0, "unregister id=1: returns success");
    TEST_ASSERT_UINT_EQ(table.active_count, 2, "unregister id=1: count drops to 2");
    TEST_ASSERT_UINT_EQ(table.entries[1].in_use, 0, "unregister id=1: slot freed");
    TEST_ASSERT_UINT_EQ(table.entries[0].in_use, 1, "unregister id=1: slot 0 still active");
    TEST_ASSERT_UINT_EQ(table.entries[2].in_use, 1, "unregister id=1: slot 2 still active");

    /* Re-register — should reuse freed slot */
    uint32_t new_id;
    rc = HookRegister(&table, 0x4000, HOOK_PERM_READ | HOOK_PERM_WRITE, &new_id);
    TEST_ASSERT_INT_EQ(rc, 0, "unregister: re-register ok");
    TEST_ASSERT_UINT_EQ(new_id, 1, "unregister: reuses freed slot 1");
    TEST_ASSERT_UINT_EQ(table.active_count, 3, "unregister: count back to 3");
    return 0;
}

static int test_hook_unregister_invalid(void) {
    hook_table_t table;
    HookTableInit(&table);

    uint32_t id;
    HookRegister(&table, 0x1000, HOOK_PERM_READ, &id);

    /* Unregister non-existent slot */
    int rc = HookUnregister(&table, 5);
    TEST_ASSERT_INT_EQ(rc, -1, "unregister invalid: returns error");
    TEST_ASSERT_UINT_EQ(table.active_count, 1, "unregister invalid: count unchanged");

    /* Unregister already-freed slot */
    rc = HookUnregister(&table, 0);
    TEST_ASSERT_INT_EQ(rc, 0, "unregister valid: ok");
    rc = HookUnregister(&table, 0);  /* Double-free */
    TEST_ASSERT_INT_EQ(rc, -1, "unregister double: returns error");
    return 0;
}

static int test_hook_find_by_gpa(void) {
    hook_table_t table;
    HookTableInit(&table);

    uint32_t id;
    HookRegister(&table, 0x1000, HOOK_PERM_READ, &id);
    HookRegister(&table, 0x5000, HOOK_PERM_WRITE, &id);
    HookRegister(&table, 0xA000, HOOK_PERM_EXECUTE, &id);

    uint32_t found_id;
    hook_entry_t* found_entry;

    /* Find existing hooks */
    int rc = HookFindByGpa(&table, 0x1000, &found_id, &found_entry);
    TEST_ASSERT_INT_EQ(rc, 0, "find 0x1000: found");
    TEST_ASSERT_UINT_EQ(found_id, 0, "find 0x1000: correct ID");
    TEST_ASSERT_NOT_NULL(found_entry, "find 0x1000: entry non-NULL");
    TEST_ASSERT_UINT_EQ(found_entry->gpa, 0x1000, "find 0x1000: correct GPA");

    rc = HookFindByGpa(&table, 0x5000, &found_id, &found_entry);
    TEST_ASSERT_INT_EQ(rc, 0, "find 0x5000: found");
    TEST_ASSERT_UINT_EQ(found_id, 1, "find 0x5000: correct ID");

    rc = HookFindByGpa(&table, 0xA000, &found_id, &found_entry);
    TEST_ASSERT_INT_EQ(rc, 0, "find 0xA000: found");
    TEST_ASSERT_UINT_EQ(found_id, 2, "find 0xA000: correct ID");

    /* Find non-existing GPA */
    rc = HookFindByGpa(&table, 0xDEAD, &found_id, &found_entry);
    TEST_ASSERT_INT_EQ(rc, -1, "find 0xDEAD: not found");

    /* Page-aligned boundary test */
    rc = HookFindByGpa(&table, 0x1001, &found_id, &found_entry);
    TEST_ASSERT_INT_EQ(rc, -1, "find 0x1001: not found (exact match only)");
    return 0;
}

static int test_hook_register_duplicate_gpa(void) {
    hook_table_t table;
    HookTableInit(&table);

    uint32_t id1, id2;
    int rc;

    rc = HookRegister(&table, 0x5000, HOOK_PERM_READ, &id1);
    TEST_ASSERT_INT_EQ(rc, 0, "dup: first register ok");

    /* Try to register same GPA again */
    rc = HookRegister(&table, 0x5000, HOOK_PERM_WRITE, &id2);
    TEST_ASSERT_INT_EQ(rc, -2, "dup: second register returns error (already hooked)");
    TEST_ASSERT_UINT_EQ(table.active_count, 1, "dup: count still 1");
    return 0;
}

static int test_hook_table_full(void) {
    hook_table_t table;
    HookTableInit(&table);

    uint32_t id;
    int rc;

    /* Fill all slots */
    for (uint32_t i = 0; i < EPT_MAX_HOOKS; i++) {
        rc = HookRegister(&table, (uint64_t)i * 0x1000, HOOK_PERM_READ, &id);
        TEST_ASSERT_INT_EQ(rc, 0, "fill: register ok");
        TEST_ASSERT_UINT_EQ(id, i, "fill: sequential IDs");
    }

    TEST_ASSERT_UINT_EQ(table.active_count, EPT_MAX_HOOKS, "fill: count = MAX_HOOKS");

    /* Try to register one more — should fail */
    rc = HookRegister(&table, EPT_MAX_HOOKS * 0x1000, HOOK_PERM_READ, &id);
    TEST_ASSERT_INT_EQ(rc, -2, "fill: overflow returns error");
    TEST_ASSERT_UINT_EQ(table.active_count, EPT_MAX_HOOKS,
                        "fill: count unchanged on overflow");

    /* Verify first and last slots */
    TEST_ASSERT_UINT_EQ(table.entries[0].gpa, 0, "fill: first GPA = 0");
    TEST_ASSERT_UINT_EQ(table.entries[EPT_MAX_HOOKS - 1].gpa,
                        (EPT_MAX_HOOKS - 1) * 0x1000, "fill: last GPA correct");
    return 0;
}

static int test_hook_unregister_and_refill(void) {
    hook_table_t table;
    HookTableInit(&table);

    /* Fill half the table */
    uint32_t id;
    for (uint32_t i = 0; i < 128; i++) {
        HookRegister(&table, (uint64_t)i * 0x1000, HOOK_PERM_READ, &id);
    }

    /* Unregister a few slots */
    HookUnregister(&table, 0);
    HookUnregister(&table, 5);
    HookUnregister(&table, 127);

    TEST_ASSERT_UINT_EQ(table.active_count, 125, "refill: count after unregister");

    /* Register new hooks — should fill the gaps */
    uint32_t new_id;
    int rc;

    rc = HookRegister(&table, 0x100000, HOOK_PERM_EXECUTE, &new_id);
    TEST_ASSERT_INT_EQ(rc, 0, "refill: register in gap 0");
    TEST_ASSERT_UINT_EQ(new_id, 0, "refill: reused slot 0");

    rc = HookRegister(&table, 0x100001, HOOK_PERM_WRITE, &new_id);
    TEST_ASSERT_INT_EQ(rc, 0, "refill: register in gap 5");
    TEST_ASSERT_UINT_EQ(new_id, 5, "refill: reused slot 5");

    rc = HookRegister(&table, 0x100002, HOOK_PERM_READ, &new_id);
    TEST_ASSERT_INT_EQ(rc, 0, "refill: register in gap 127");
    TEST_ASSERT_UINT_EQ(new_id, 127, "refill: reused slot 127");

    TEST_ASSERT_UINT_EQ(table.active_count, 128, "refill: count restored to 128");
    return 0;
}

static int test_hook_find_on_empty_table(void) {
    hook_table_t table;
    HookTableInit(&table);

    uint32_t found_id;
    hook_entry_t* found_entry;

    int rc = HookFindByGpa(&table, 0, &found_id, &found_entry);
    TEST_ASSERT_INT_EQ(rc, -1, "empty find: returns not found");
    TEST_ASSERT_UINT_EQ(table.active_count, 0, "empty: count still zero");
    return 0;
}

static int test_hook_find_after_unregister(void) {
    hook_table_t table;
    HookTableInit(&table);

    uint32_t id;
    HookRegister(&table, 0x1000, HOOK_PERM_READ, &id);
    HookRegister(&table, 0x2000, HOOK_PERM_WRITE, &id);

    /* Unregister first */
    HookUnregister(&table, 0);

    uint32_t found_id;
    hook_entry_t* found_entry;

    /* First GPA should no longer be found */
    int rc = HookFindByGpa(&table, 0x1000, &found_id, &found_entry);
    TEST_ASSERT_INT_EQ(rc, -1, "find after unregister: old GPA not found");

    /* Second GPA should still be found */
    rc = HookFindByGpa(&table, 0x2000, &found_id, &found_entry);
    TEST_ASSERT_INT_EQ(rc, 0, "find after unregister: remaining GPA found");
    return 0;
}

static int test_hook_null_safety(void) {
    hook_table_t table;
    HookTableInit(&table);

    uint32_t id;

    /* NULL hook_id out parameter */
    int rc = HookRegister(&table, 0x1000, HOOK_PERM_READ, NULL);
    TEST_ASSERT_INT_EQ(rc, -1, "null safety: NULL hook_id rejected");
    TEST_ASSERT_UINT_EQ(table.active_count, 0, "null safety: no registration");

    /* NULL find outputs */
    rc = HookFindByGpa(&table, 0x1000, NULL, NULL);
    TEST_ASSERT_INT_EQ(rc, -1, "null safety: NULL find outputs rejected");

    /* Valid registration with non-NULL output */
    rc = HookRegister(&table, 0x1000, HOOK_PERM_READ, &id);
    TEST_ASSERT_INT_EQ(rc, 0, "null safety: valid registration ok");
    return 0;
}

/*===========================================================================
 * Task 4.3 — EPT Violation Qualification Decode (ept_violation.c)
 *===========================================================================*/

static int test_ept_qual_data_read(void) {
    /* Read-only violation: bit 0 = 1 */
    uint64_t qual = EPT_QUAL_DATA_READ;

    uint8_t access = EptViolationGetAccessType(qual);
    TEST_ASSERT_UINT_EQ(access, EPT_QUAL_DATA_READ, "qual: data read detected");
    TEST_ASSERT_UINT_EQ(access & EPT_QUAL_DATA_WRITE, 0, "qual: write not set");
    TEST_ASSERT_UINT_EQ(access & EPT_QUAL_DATA_EXECUTE, 0, "qual: exec not set");
    return 0;
}

static int test_ept_qual_data_write(void) {
    uint64_t qual = EPT_QUAL_DATA_WRITE;

    uint8_t access = EptViolationGetAccessType(qual);
    TEST_ASSERT_UINT_EQ(access, EPT_QUAL_DATA_WRITE, "qual: data write detected");
    TEST_ASSERT_UINT_EQ(access & EPT_QUAL_DATA_READ, 0, "qual: read not set");
    return 0;
}

static int test_ept_qual_instruction_fetch(void) {
    uint64_t qual = EPT_QUAL_DATA_EXECUTE;

    uint8_t access = EptViolationGetAccessType(qual);
    TEST_ASSERT_UINT_EQ(access, EPT_QUAL_DATA_EXECUTE, "qual: instruction fetch detected");
    return 0;
}

static int test_ept_qual_read_write(void) {
    uint64_t qual = EPT_QUAL_DATA_READ | EPT_QUAL_DATA_WRITE;

    uint8_t access = EptViolationGetAccessType(qual);
    TEST_ASSERT_UINT_EQ(access, (EPT_QUAL_DATA_READ | EPT_QUAL_DATA_WRITE),
                        "qual: R/W detected");
    return 0;
}

static int test_ept_qual_all_permissions(void) {
    uint64_t qual = EPT_QUAL_DATA_READ | EPT_QUAL_DATA_WRITE | EPT_QUAL_DATA_EXECUTE;

    uint8_t access = EptViolationGetAccessType(qual);
    TEST_ASSERT_UINT_EQ(access, (EPT_QUAL_DATA_READ | EPT_QUAL_DATA_WRITE | EPT_QUAL_DATA_EXECUTE),
                        "qual: R/W/X detected");
    return 0;
}

static int test_ept_qual_no_access(void) {
    uint64_t qual = 0;  /* Should never happen, but handle gracefully */

    uint8_t access = EptViolationGetAccessType(qual);
    TEST_ASSERT_UINT_EQ(access, 0, "qual: no access = 0");
    return 0;
}

static int test_ept_qual_extract_gpa(void) {
    /* GPA at bits 63:12, shifted right 12 */
    uint64_t qual = (uint64_t)0x5000 << EPT_QUAL_GPA_SHIFT;
    qual |= EPT_QUAL_DATA_WRITE;

    uint64_t gpa = EptViolationGetGpa(qual);
    TEST_ASSERT_UINT_EQ(gpa, 0x5000, "qual: GPA extracted from shifted bits");
    return 0;
}

static int test_ept_qual_extract_gpa_boundary(void) {
    /* GPA = 0 (all zero in bits 63:12) */
    uint64_t qual = EPT_QUAL_DATA_READ;
    uint64_t gpa = EptViolationGetGpa(qual);
    TEST_ASSERT_UINT_EQ(gpa, 0, "qual: GPA=0 extracted");

    /* Max GPA (all ones in bits 63:12 within mask) */
    qual = EPT_QUAL_GPA_MASK | EPT_QUAL_DATA_WRITE;
    gpa = EptViolationGetGpa(qual);
    TEST_ASSERT_UINT_EQ(gpa, EPT_QUAL_GPA_MASK >> EPT_QUAL_GPA_SHIFT,
                        "qual: max GPA extracted");
    return 0;
}

static int test_ept_qual_extract_gla(void) {
    /* GLA is from bits 63:0 of the guest-linear address field.
     * For a mock test, the GLA is also embedded in the exit qualification
     * in a separate VMCS field. For unit tests, we verify the function
     * extracts correctly from whatever value we pass. */
    uint64_t qual = 0xFFFF800000010000ULL;  /* Typical kernel GLA */
    uint64_t gla = EptViolationGetGla(qual);
    TEST_ASSERT_UINT_EQ(gla, 0xFFFF800000010000ULL, "qual: GLA extracted");
    return 0;
}

static int test_ept_qual_constants_alignment(void) {
    /* Verify EPT qual constants match Intel SDM */
    TEST_ASSERT_UINT_EQ(EPT_QUAL_DATA_READ, 1ULL, "qual: READ = bit 0");
    TEST_ASSERT_UINT_EQ(EPT_QUAL_DATA_WRITE, 2ULL, "qual: WRITE = bit 1");
    TEST_ASSERT_UINT_EQ(EPT_QUAL_DATA_EXECUTE, 4ULL, "qual: EXEC = bit 2");
    TEST_ASSERT_UINT_EQ(EPT_QUAL_GPA_MASK, 0xFFFFFFFFFFFFF000ULL,
                        "qual: GPA_MASK = bits 63:12");
    TEST_ASSERT_UINT_EQ(EPT_QUAL_GPA_SHIFT, 12, "qual: GPA_SHIFT = 12");
    return 0;
}

static int test_ept_handle_violation_hooked(void) {
    /* Create a context for an EPT violation on a hooked GPA */
    vm_exit_context_t ctx;
    uint64_t qual = ((uint64_t)0x5000 << EPT_QUAL_GPA_SHIFT) | EPT_QUAL_DATA_WRITE;
    init_context(&ctx, EXIT_REASON_EPT_VIOLATION, qual);

    hook_table_t table;
    HookTableInit(&table);

    uint32_t id;
    HookRegister(&table, 0x5000, HOOK_PERM_WRITE, &id);

    int rc = EptHandleViolation(&ctx, &table);
    TEST_ASSERT_INT_EQ(rc, 0, "handle violation: hooked GPA returns success");
    TEST_ASSERT_FALSE(ctx.advance_rip, "handle violation: does NOT advance RIP (fault-class)");
    return 0;
}

static int test_ept_handle_violation_not_hooked(void) {
    vm_exit_context_t ctx;
    uint64_t qual = ((uint64_t)0xDEAD << EPT_QUAL_GPA_SHIFT) | EPT_QUAL_DATA_READ;
    init_context(&ctx, EXIT_REASON_EPT_VIOLATION, qual);

    hook_table_t table;
    HookTableInit(&table);

    int rc = EptHandleViolation(&ctx, &table);
    TEST_ASSERT_INT_EQ(rc, -1, "handle violation: unhooked GPA returns error");
    TEST_ASSERT_FALSE(ctx.advance_rip, "handle violation: no RIP advance");
    return 0;
}

static int test_ept_handle_violation_null_safety(void) {
    vm_exit_context_t ctx;
    uint64_t qual = ((uint64_t)0x5000 << EPT_QUAL_GPA_SHIFT) | EPT_QUAL_DATA_WRITE;
    init_context(&ctx, EXIT_REASON_EPT_VIOLATION, qual);

    /* NULL hook table */
    int rc = EptHandleViolation(&ctx, NULL);
    TEST_ASSERT_INT_EQ(rc, -1, "handle violation: NULL table rejected");

    /* NULL context */
    hook_table_t table;
    HookTableInit(&table);
    rc = EptHandleViolation(NULL, &table);
    TEST_ASSERT_INT_EQ(rc, -1, "handle violation: NULL ctx rejected");
    return 0;
}

static int test_ept_handle_misconfig(void) {
    vm_exit_context_t ctx;
    init_context(&ctx, EXIT_REASON_EPT_MISCONFIG, 0);

    /* EPT misconfiguration always returns error */
    int rc = EptHandleMisconfig(&ctx);
    TEST_ASSERT_INT_EQ(rc, -1, "misconfig: returns error");
    TEST_ASSERT_FALSE(ctx.advance_rip, "misconfig: no RIP advance");
    return 0;
}

/*===========================================================================
 * Task 4.1 — IOCTL Action Mapping (pure logic)
 *===========================================================================*/

static int test_ioctl_action_load(void) {
    ioctl_action_t action = IoctlGetAction(IOCTL_HYPERVISOR_LOAD);
    TEST_ASSERT_UINT_EQ((unsigned)action, (unsigned)IOCTL_ACTION_LOAD, "ioctl action: LOAD");
    return 0;
}

static int test_ioctl_action_unload(void) {
    ioctl_action_t action = IoctlGetAction(IOCTL_HYPERVISOR_UNLOAD);
    TEST_ASSERT_UINT_EQ((unsigned)action, (unsigned)IOCTL_ACTION_UNLOAD, "ioctl action: UNLOAD");
    return 0;
}

static int test_ioctl_action_hook(void) {
    ioctl_action_t action = IoctlGetAction(IOCTL_HYPERVISOR_HOOK);
    TEST_ASSERT_UINT_EQ((unsigned)action, (unsigned)IOCTL_ACTION_HOOK, "ioctl action: HOOK");
    return 0;
}

static int test_ioctl_action_unhook(void) {
    ioctl_action_t action = IoctlGetAction(IOCTL_HYPERVISOR_UNHOOK);
    TEST_ASSERT_UINT_EQ((unsigned)action, (unsigned)IOCTL_ACTION_UNHOOK, "ioctl action: UNHOOK");
    return 0;
}

static int test_ioctl_action_status(void) {
    ioctl_action_t action = IoctlGetAction(IOCTL_HYPERVISOR_STATUS);
    TEST_ASSERT_UINT_EQ((unsigned)action, (unsigned)IOCTL_ACTION_STATUS, "ioctl action: STATUS");
    return 0;
}

static int test_ioctl_action_probe(void) {
    ioctl_action_t action = IoctlGetAction(IOCTL_HYPERVISOR_PROBE);
    TEST_ASSERT_UINT_EQ((unsigned)action, (unsigned)IOCTL_ACTION_PROBE, "ioctl action: PROBE");
    return 0;
}

static int test_probe_response_layout(void) {
    TEST_ASSERT_UINT_EQ(sizeof(hypervisor_probe_response_t),
                        HYPERVISOR_PROBE_RESPONSE_SIZE,
                        "probe response: fixed size");
    TEST_ASSERT_UINT_EQ(offsetof(hypervisor_probe_response_t, status), 0,
                        "probe response: status offset");
    TEST_ASSERT_UINT_EQ(offsetof(hypervisor_probe_response_t, nt_status), 4,
                        "probe response: NTSTATUS offset");
    TEST_ASSERT_UINT_EQ(offsetof(hypervisor_probe_response_t, runtime_stage), 8,
                        "probe response: stage offset");
    TEST_ASSERT_UINT_EQ(offsetof(hypervisor_probe_response_t, processor_group), 16,
                        "probe response: processor offset");
    TEST_ASSERT_UINT_EQ(offsetof(hypervisor_probe_response_t, vmx_revision_id), 20,
                        "probe response: revision offset");
    TEST_ASSERT_UINT_EQ(offsetof(hypervisor_probe_response_t, feature_control), 32,
                        "probe response: feature-control offset");
    TEST_ASSERT_UINT_EQ(offsetof(hypervisor_probe_response_t, adjusted_cr4), 80,
                        "probe response: final field offset");
    return 0;
}

static int test_ioctl_action_unknown(void) {
    ioctl_action_t action = IoctlGetAction(0xDEADBEEF);
    TEST_ASSERT_UINT_EQ((unsigned)action, (unsigned)IOCTL_ACTION_UNKNOWN, "ioctl action: unknown code");
    return 0;
}

static int test_ioctl_action_zero(void) {
    ioctl_action_t action = IoctlGetAction(0);
    TEST_ASSERT_UINT_EQ((unsigned)action, (unsigned)IOCTL_ACTION_UNKNOWN, "ioctl action: zero code");
    return 0;
}

/*===========================================================================
 * Task 4.1 — Buffer Validation (pure logic)
 *===========================================================================*/

static int test_validate_hook_buffer_valid(void) {
    hypervisor_hook_request_t req = { 0 };
    req.gpa = 0x5000;
    req.permissions = HOOK_PERM_READ;

    ioctl_validation_result_t result = IoctlValidateHookRequest(&req, sizeof(req));
    TEST_ASSERT_UINT_EQ(result, IOCTL_VALID_OK, "validate hook: valid buffer");
    return 0;
}

static int test_validate_hook_buffer_null(void) {
    ioctl_validation_result_t result = IoctlValidateHookRequest(NULL, sizeof(hypervisor_hook_request_t));
    TEST_ASSERT_UINT_EQ(result, IOCTL_VALID_NULL_BUFFER, "validate hook: NULL buffer");
    return 0;
}

static int test_validate_hook_buffer_short(void) {
    hypervisor_hook_request_t req = { 0 };
    req.gpa = 0x5000;

    ioctl_validation_result_t result = IoctlValidateHookRequest(&req, 4);  /* Too short */
    TEST_ASSERT_UINT_EQ(result, IOCTL_VALID_SHORT_BUFFER, "validate hook: short buffer");
    result = IoctlValidateHookRequest(&req, sizeof(req) + 1U);
    TEST_ASSERT_UINT_EQ(result, IOCTL_VALID_SHORT_BUFFER,
        "validate hook: overlong buffer");
    return 0;
}

static int test_validate_hook_buffer_unaligned_gpa(void) {
    hypervisor_hook_request_t req = { 0 };
    req.gpa = 0x5001;  /* Not page-aligned */
    req.permissions = HOOK_PERM_READ;

    ioctl_validation_result_t result = IoctlValidateHookRequest(&req, sizeof(req));
    TEST_ASSERT_UINT_EQ(result, IOCTL_VALID_UNALIGNED_GPA, "validate hook: unaligned GPA");
    return 0;
}

static int test_validate_hook_permissions_and_reserved(void) {
    hypervisor_hook_request_t req = { 0 };
    req.gpa = 0x5000;
    TEST_ASSERT_UINT_EQ(IoctlValidateHookRequest(&req, sizeof(req)),
        IOCTL_VALID_INVALID_PERMISSIONS, "validate hook: empty permissions");
    req.permissions = 0x80;
    TEST_ASSERT_UINT_EQ(IoctlValidateHookRequest(&req, sizeof(req)),
        IOCTL_VALID_INVALID_PERMISSIONS, "validate hook: unknown permissions");
    req.permissions = HOOK_PERM_WRITE;
    req.reserved[6] = 1;
    TEST_ASSERT_UINT_EQ(IoctlValidateHookRequest(&req, sizeof(req)),
        IOCTL_VALID_INVALID_PERMISSIONS, "validate hook: reserved");
    return 0;
}
static int test_validate_unhook_buffer_valid(void) {
    hypervisor_unhook_request_t req = { 0 };
    req.gpa = 0x5000;

    ioctl_validation_result_t result = IoctlValidateUnhookRequest(&req, sizeof(req));
    TEST_ASSERT_UINT_EQ(result, IOCTL_VALID_OK, "validate unhook: valid");
    return 0;
}

static int test_validate_unhook_buffer_null(void) {
    ioctl_validation_result_t result = IoctlValidateUnhookRequest(NULL, sizeof(hypervisor_unhook_request_t));
    TEST_ASSERT_UINT_EQ(result, IOCTL_VALID_NULL_BUFFER, "validate unhook: NULL");
    return 0;
}

static int test_validate_unhook_buffer_short(void) {
    hypervisor_unhook_request_t req = { 0 };
    ioctl_validation_result_t result = IoctlValidateUnhookRequest(&req, 4);
    TEST_ASSERT_UINT_EQ(result, IOCTL_VALID_SHORT_BUFFER, "validate unhook: short");
    result = IoctlValidateUnhookRequest(&req, sizeof(req) + 1U);
    TEST_ASSERT_UINT_EQ(result, IOCTL_VALID_SHORT_BUFFER,
        "validate unhook: overlong");
    return 0;
}

/*===========================================================================
 * Task 4.2 — Empty/NULL GPT (boundary cases)
 *===========================================================================*/

static int test_validate_unhook_alignment_and_reserved(void) {
    hypervisor_unhook_request_t req = { 0 };
    req.gpa = 0x5001;
    TEST_ASSERT_UINT_EQ(IoctlValidateUnhookRequest(&req, sizeof(req)),
        IOCTL_VALID_UNALIGNED_GPA, "validate unhook: unaligned");
    req.gpa = 0x5000;
    req.reserved[7] = 1;
    TEST_ASSERT_UINT_EQ(IoctlValidateUnhookRequest(&req, sizeof(req)),
        IOCTL_VALID_INVALID_PERMISSIONS, "validate unhook: reserved");
    return 0;
}
static int test_hook_gpa_zero(void) {
    hook_table_t table;
    HookTableInit(&table);

    uint32_t id;
    int rc = HookRegister(&table, 0, HOOK_PERM_READ, &id);
    TEST_ASSERT_INT_EQ(rc, 0, "zero GPA: register ok");
    TEST_ASSERT_UINT_EQ(table.entries[0].gpa, 0, "zero GPA: stored correctly");

    uint32_t found_id;
    hook_entry_t* found_entry;
    rc = HookFindByGpa(&table, 0, &found_id, &found_entry);
    TEST_ASSERT_INT_EQ(rc, 0, "zero GPA: found");
    return 0;
}

static int test_hook_gpa_max(void) {
    hook_table_t table;
    HookTableInit(&table);

    uint64_t max_gpa = 0xFFFFFFFFFF000ULL;  /* 52-bit address, page-aligned */

    uint32_t id;
    int rc = HookRegister(&table, max_gpa, HOOK_PERM_WRITE, &id);
    TEST_ASSERT_INT_EQ(rc, 0, "max GPA: register ok");

    uint32_t found_id;
    hook_entry_t* found_entry;
    rc = HookFindByGpa(&table, max_gpa, &found_id, &found_entry);
    TEST_ASSERT_INT_EQ(rc, 0, "max GPA: found");
    TEST_ASSERT_UINT_EQ(found_entry->gpa, max_gpa, "max GPA: correct value");
    return 0;
}

/*===========================================================================
 * G1: Launch-Probe Wiring Tests
 *===========================================================================*/

/* === G1: Launch-Probe Response Layout === */
static int test_launch_probe_response_size(void){
 TEST_ASSERT_UINT_EQ(sizeof(VMX_LAUNCH_PROBE_RESPONSE_V2),280,"size");
 TEST_ASSERT_UINT_EQ(VMX_LAUNCH_PROBE_RESPONSE_V2_SIZE,280,"macro");
 return 0;}

static int test_launch_probe_response_init(void){
 VMX_LAUNCH_PROBE_RESPONSE_V2 r={0};
 TEST_ASSERT_UINT_EQ(r.Version,0,"v0");TEST_ASSERT_UINT_EQ(r.DomainStatus,0,"d0");
 r.Size=280;r.Version=VMX_LAUNCH_PROBE_VERSION_V2;
 TEST_ASSERT_UINT_EQ(r.Version,2,"v2");TEST_ASSERT_UINT_EQ(r.Size,280,"sz");
 return 0;}

/* === G1: Preflight-to-Domain Mapping === */
static int test_map_preflight_stage_all(void){
    static const struct { VMX_PREFLIGHT_RESULT result; VMX_LIFECYCLE_STAGE stage; } cases[] = {
        { VMX_PREFLIGHT_SUCCESS, VMX_STAGE_COOKIE_GENERATED },
        { VMX_PREFLIGHT_NOT_PASSIVE_LEVEL, VMX_STAGE_PASSIVE_LEVEL },
        { VMX_PREFLIGHT_SERIALIZATION_FAILED, VMX_STAGE_SERIALIZATION },
        { VMX_PREFLIGHT_PIN_FAILED, VMX_STAGE_CPU0_PINNED },
        { VMX_PREFLIGHT_CET_ACTIVE, VMX_STAGE_CET_PREFLIGHT },
        { VMX_PREFLIGHT_NO_VMX_CPUID, VMX_STAGE_CPUID_VALIDATED },
        { VMX_PREFLIGHT_FEATURE_CONTROL, VMX_STAGE_FEATURE_CONTROL },
        { VMX_PREFLIGHT_BAD_VMX_BASIC, VMX_STAGE_VMX_BASIC },
        { VMX_PREFLIGHT_CR0_CHANGE_REQUIRED, VMX_STAGE_FIXED_CR },
        { VMX_PREFLIGHT_CR4_CHANGE_REQUIRED, VMX_STAGE_FIXED_CR },
        { VMX_PREFLIGHT_CONTEXT_FAILED, VMX_STAGE_CONTEXT_ALLOCATED },
        { VMX_PREFLIGHT_COOKIE_FAILED, VMX_STAGE_COOKIE_GENERATED },
        { VMX_PREFLIGHT_INVALID_PLATFORM, VMX_STAGE_NOT_STARTED }
    };
    size_t i;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        TEST_ASSERT_UINT_EQ((unsigned)IoctlMapPreflightToStage(cases[i].result),
                            (unsigned)cases[i].stage, "preflight stage");
    }
    return 0;
}

static int test_map_success(void){
 TEST_ASSERT_UINT_EQ((unsigned)IoctlMapPreflightToDomain(VMX_PREFLIGHT_SUCCESS),(unsigned)VMX_DOMAIN_SUCCESS,"ok");
 TEST_ASSERT_INT_EQ(IoctlMapPreflightToNtStatus(VMX_PREFLIGHT_SUCCESS),0,"ntok");
 return 0;}

static int test_map_invalid_platform(void){
 TEST_ASSERT_UINT_EQ((unsigned)IoctlMapPreflightToDomain(VMX_PREFLIGHT_INVALID_PLATFORM),(unsigned)VMX_DOMAIN_FAILED,"ip");
 return 0;}

static int test_map_not_passive(void){
 TEST_ASSERT_UINT_EQ((unsigned)IoctlMapPreflightToDomain(VMX_PREFLIGHT_NOT_PASSIVE_LEVEL),(unsigned)VMX_DOMAIN_REJECTED,"np");
 TEST_ASSERT_INT_EQ(IoctlMapPreflightToNtStatus(VMX_PREFLIGHT_NOT_PASSIVE_LEVEL),(int32_t)0xC00000BB,"nt");
 return 0;}

static int test_map_serialization_failed(void){
 TEST_ASSERT_UINT_EQ((unsigned)IoctlMapPreflightToDomain(VMX_PREFLIGHT_SERIALIZATION_FAILED),(unsigned)VMX_DOMAIN_FAILED,"sf");
 return 0;}

static int test_map_pin_failed(void){
 TEST_ASSERT_UINT_EQ((unsigned)IoctlMapPreflightToDomain(VMX_PREFLIGHT_PIN_FAILED),(unsigned)VMX_DOMAIN_FAILED,"pf");
 return 0;}

static int test_map_cet_active(void){
 VMX_LAUNCH_PROBE_RESPONSE_V2 response;
 VMX_INSTRUCTION_REPORT* reports[] = {
     &response.Vmxon, &response.Vmclear, &response.Vmptrld,
     &response.VmwritePlan, &response.GuestRsp, &response.GuestRflags,
     &response.Vmlaunch, &response.Vmresume, &response.Vmxoff
 };
 size_t i;
 memset(&response, 0xA5, sizeof(response));
 IoctlInitializePreflightFailureResponse(&response, VMX_PREFLIGHT_CET_ACTIVE);
 TEST_ASSERT_UINT_EQ(response.Version, VMX_LAUNCH_PROBE_VERSION_V2, "cet version");
 TEST_ASSERT_UINT_EQ(response.Size, VMX_LAUNCH_PROBE_RESPONSE_V2_SIZE, "cet size");
 TEST_ASSERT_UINT_EQ(response.DomainStatus, VMX_DOMAIN_REJECTED, "cet domain");
 TEST_ASSERT_INT_EQ(response.NtStatus, (int32_t)0xC00000BB, "cet status");
 TEST_ASSERT_UINT_EQ(response.RuntimeStage, VMX_STAGE_CET_PREFLIGHT, "cet stage");
 TEST_ASSERT_UINT_EQ(response.Flags, VMX_FLAG_CET_REJECTED, "cet flag");
 TEST_ASSERT_UINT_EQ(response.CleanupRequirement, VMX_CLEANUP_NONE, "cet cleanup none");
 TEST_ASSERT_UINT_EQ(response.CleanupOwner, VMX_CLEANUP_OWNER_NONE, "cet cleanup owner");
 for (i = 0; i < sizeof(reports) / sizeof(reports[0]); ++i) {
     TEST_ASSERT_UINT_EQ(reports[i]->Outcome, VMX_INSTR_NOT_ATTEMPTED, "cet not attempted");
     TEST_ASSERT_UINT_EQ(reports[i]->ErrorValid, 0, "cet error invalid");
     TEST_ASSERT_UINT_EQ(reports[i]->Reserved, 0, "cet reserved zero");
     TEST_ASSERT_UINT_EQ(reports[i]->Error, 0, "cet error zero");
 }
 return 0;}
static int test_map_no_vmx_cpuid(void){
 TEST_ASSERT_UINT_EQ((unsigned)IoctlMapPreflightToDomain(VMX_PREFLIGHT_NO_VMX_CPUID),(unsigned)VMX_DOMAIN_REJECTED,"cpuid");
 return 0;}

static int test_map_feature_control(void){
 TEST_ASSERT_UINT_EQ((unsigned)IoctlMapPreflightToDomain(VMX_PREFLIGHT_FEATURE_CONTROL),(unsigned)VMX_DOMAIN_REJECTED,"feat");
 return 0;}

static int test_map_bad_vmx_basic(void){
 TEST_ASSERT_UINT_EQ((unsigned)IoctlMapPreflightToDomain(VMX_PREFLIGHT_BAD_VMX_BASIC),(unsigned)VMX_DOMAIN_FAILED,"basic");
 return 0;}

static int test_map_cr0_change(void){
 TEST_ASSERT_UINT_EQ((unsigned)IoctlMapPreflightToDomain(VMX_PREFLIGHT_CR0_CHANGE_REQUIRED),(unsigned)VMX_DOMAIN_REJECTED,"cr0");
 return 0;}

static int test_map_cr4_change(void){
 TEST_ASSERT_UINT_EQ((unsigned)IoctlMapPreflightToDomain(VMX_PREFLIGHT_CR4_CHANGE_REQUIRED),(unsigned)VMX_DOMAIN_REJECTED,"cr4");
 return 0;}

static int test_map_context_failed(void){
 TEST_ASSERT_UINT_EQ((unsigned)IoctlMapPreflightToDomain(VMX_PREFLIGHT_CONTEXT_FAILED),(unsigned)VMX_DOMAIN_FAILED,"ctx");
 TEST_ASSERT_INT_EQ(IoctlMapPreflightToNtStatus(VMX_PREFLIGHT_CONTEXT_FAILED),(int32_t)0xC000009A,"ntctx");
 return 0;}

static int test_map_cookie_failed(void){
 TEST_ASSERT_UINT_EQ((unsigned)IoctlMapPreflightToDomain(VMX_PREFLIGHT_COOKIE_FAILED),(unsigned)VMX_DOMAIN_FAILED,"cook");
 return 0;}

/*===========================================================================
 * G1b: Transient Result Mapping
 *===========================================================================*/

static void SetTransientSuccess(VMX_INSTRUCTION_REPORTS* r){
 memset(r,0,sizeof(*r));
 r->VmxonOutcome=VMX_INSTR_SUCCESS;r->VmclearOutcome=VMX_INSTR_SUCCESS;
 r->VmptrldOutcome=VMX_INSTR_SUCCESS;r->VmwriteOutcome=VMX_VMWRITE_SUCCESS;
 r->GuestRspOutcome=VMX_INSTR_SUCCESS;r->GuestRflagsOutcome=VMX_INSTR_SUCCESS;
 r->VmlaunchOutcome=VMX_INSTR_SUCCESS;r->VmresumeOutcome=VMX_INSTR_SUCCESS;
 r->VmxoffOutcome=VMX_INSTR_SUCCESS;
}

static int test_transient_map_preflight_rejected(void){
 VMX_INSTRUCTION_REPORTS r={0};
 VMX_DOMAIN_STATUS s=IoctlMapTransientResult(VMX_PREFLIGHT_NOT_PASSIVE_LEVEL,VMX_STAGE_INSTRUCTION_INTERVAL,&r);
 TEST_ASSERT_UINT_EQ((unsigned)s,(unsigned)VMX_DOMAIN_REJECTED,"preflight");
 return 0;}

static int test_transient_map_vmxon_fail(void){
 VMX_INSTRUCTION_REPORTS r={0};r.VmxonOutcome=VMX_INSTR_FAIL_VALID;
 VMX_DOMAIN_STATUS s=IoctlMapTransientResult(VMX_PREFLIGHT_SUCCESS,VMX_STAGE_VMXON,&r);
 TEST_ASSERT_UINT_EQ((unsigned)s,(unsigned)VMX_DOMAIN_FAILED,"vmxon");
 return 0;}

static int test_transient_map_vmwrite_fail(void){
 VMX_INSTRUCTION_REPORTS r;SetTransientSuccess(&r);
 r.VmwriteOutcome=VMX_VMWRITE_FAIL_VALID;
 VMX_DOMAIN_STATUS s=IoctlMapTransientResult(VMX_PREFLIGHT_SUCCESS,VMX_STAGE_HOST_PLAN,&r);
 TEST_ASSERT_UINT_EQ((unsigned)s,(unsigned)VMX_DOMAIN_FAILED,"vmwrite");
 return 0;}

static int test_transient_map_vmlaunch_fail(void){
 VMX_INSTRUCTION_REPORTS r;SetTransientSuccess(&r);
 r.VmlaunchOutcome=VMX_INSTR_FAIL_VALID;r.VmresumeOutcome=VMX_INSTR_NOT_ATTEMPTED;
 VMX_DOMAIN_STATUS s=IoctlMapTransientResult(VMX_PREFLIGHT_SUCCESS,VMX_STAGE_HOST_PLAN,&r);
 TEST_ASSERT_UINT_EQ((unsigned)s,(unsigned)VMX_DOMAIN_FAILED,"vmlaunch");
 return 0;}

static int test_transient_map_vmresume_fail(void){
 VMX_INSTRUCTION_REPORTS r;SetTransientSuccess(&r);
 r.VmresumeOutcome=VMX_INSTR_FAIL_VALID;
 VMX_DOMAIN_STATUS s=IoctlMapTransientResult(VMX_PREFLIGHT_SUCCESS,VMX_STAGE_VMLAUNCH,&r);
 TEST_ASSERT_UINT_EQ((unsigned)s,(unsigned)VMX_DOMAIN_FAILED,"vmresume");
 return 0;}

static int test_transient_map_vmxoff_fail(void){
 VMX_INSTRUCTION_REPORTS r;SetTransientSuccess(&r);
 r.VmxoffOutcome=VMX_INSTR_FAIL_INVALID;
 VMX_DOMAIN_STATUS s=IoctlMapTransientResult(VMX_PREFLIGHT_SUCCESS,VMX_STAGE_VMRESUME,&r);
 TEST_ASSERT_UINT_EQ((unsigned)s,(unsigned)VMX_DOMAIN_FAILED,"vmxoff");
 return 0;}

static int test_transient_map_final_vmclear_fail(void){
 VMX_INSTRUCTION_REPORTS r;const int outcomes[2]={VMX_INSTR_FAIL_VALID,VMX_INSTR_FAIL_INVALID};size_t i;
 for(i=0;i<2;i++){SetTransientSuccess(&r);r.VmclearOutcome=outcomes[i];
  TEST_ASSERT_UINT_EQ((unsigned)IoctlMapTransientResult(VMX_PREFLIGHT_SUCCESS,VMX_STAGE_VMRESUME,&r),
   (unsigned)VMX_DOMAIN_FAILED,"final vmclear");}
 return 0;}

static int test_transient_map_full_success(void){
 VMX_INSTRUCTION_REPORTS r;SetTransientSuccess(&r);
 VMX_DOMAIN_STATUS s=IoctlMapTransientResult(VMX_PREFLIGHT_SUCCESS,VMX_STAGE_VMRESUME,&r);
 TEST_ASSERT_UINT_EQ((unsigned)s,(unsigned)VMX_DOMAIN_SUCCESS,"full");
 return 0;}

static int test_transient_map_rejects_malformed(void){
 VMX_INSTRUCTION_REPORTS r;
 int* fields[]={&r.VmxonOutcome,&r.VmclearOutcome,&r.VmptrldOutcome,&r.VmwriteOutcome,
  &r.GuestRspOutcome,&r.GuestRflagsOutcome,&r.VmlaunchOutcome,&r.VmresumeOutcome,&r.VmxoffOutcome};
 const int bad[]={VMX_INSTR_NOT_ATTEMPTED,VMX_INSTR_FAIL_VALID,VMX_INSTR_FAIL_INVALID};
 size_t i,j;
 for(i=0;i<sizeof(fields)/sizeof(fields[0]);++i){
  for(j=0;j<sizeof(bad)/sizeof(bad[0]);++j){
   SetTransientSuccess(&r);*fields[i]=bad[j];
   TEST_ASSERT_UINT_EQ((unsigned)IoctlMapTransientResult(VMX_PREFLIGHT_SUCCESS,VMX_STAGE_VMRESUME,&r),
    (unsigned)VMX_DOMAIN_FAILED,"malformed outcome");
  }
 }
 return 0;}
/* Instruction report layout test */
static int test_report_layout(void){
 TEST_ASSERT_UINT_EQ(offsetof(VMX_LAUNCH_PROBE_RESPONSE_V2,Vmxon),112,"off_vmxon");
 TEST_ASSERT_UINT_EQ(offsetof(VMX_LAUNCH_PROBE_RESPONSE_V2,Vmclear),120,"off_vmclear");
 TEST_ASSERT_UINT_EQ(offsetof(VMX_LAUNCH_PROBE_RESPONSE_V2,Vmptrld),128,"off_vmptrld");
 TEST_ASSERT_UINT_EQ(offsetof(VMX_LAUNCH_PROBE_RESPONSE_V2,VmwritePlan),136,"off_vmwrite");
 TEST_ASSERT_UINT_EQ(offsetof(VMX_LAUNCH_PROBE_RESPONSE_V2,GuestRsp),144,"off_rsp");
 TEST_ASSERT_UINT_EQ(offsetof(VMX_LAUNCH_PROBE_RESPONSE_V2,GuestRflags),152,"off_rflags");
 TEST_ASSERT_UINT_EQ(offsetof(VMX_LAUNCH_PROBE_RESPONSE_V2,Vmlaunch),160,"off_launch");
 TEST_ASSERT_UINT_EQ(offsetof(VMX_LAUNCH_PROBE_RESPONSE_V2,Vmresume),168,"off_resume");
 TEST_ASSERT_UINT_EQ(offsetof(VMX_LAUNCH_PROBE_RESPONSE_V2,Vmxoff),176,"off_vmxoff");
 return 0;}

/*===========================================================================
 * G1c-a: VMX Instruction Normalization
 *===========================================================================*/

static int test_normalize_vmx_success(void) {
    /* Pure logic: VMX_INSTR_SUCCESS == 1, not 0 */
    TEST_ASSERT_INT_EQ(VMX_INSTR_SUCCESS, 1, "succ");
    TEST_ASSERT_INT_EQ(VMX_INSTR_FAIL_VALID, 2, "fv");
    TEST_ASSERT_INT_EQ(VMX_INSTR_FAIL_INVALID, 3, "fi");
    return 0;
}

static int test_normalize_vmx_nonzero_is_failure(void) {
    TEST_ASSERT_TRUE(VMX_INSTR_FAIL_VALID != 0, "fv_nz");
    TEST_ASSERT_TRUE(VMX_INSTR_FAIL_INVALID != 0, "fi_nz");
    TEST_ASSERT_TRUE(VMX_INSTR_SUCCESS != 0, "succ_nz");
    return 0;
}

/*===========================================================================
 * G1c-b: Transient-to-NTSTATUS Mapping
 *===========================================================================*/

static int test_transient_ntstatus_preflight_fail(void) {
    VMX_INSTRUCTION_REPORTS r = {0};
    int32_t st = IoctlMapTransientToNtStatus(
        VMX_PREFLIGHT_NOT_PASSIVE_LEVEL, VMX_STAGE_NOT_STARTED, &r);
    TEST_ASSERT_INT_EQ(st, (int32_t)0xC00000BB, "nt_pre");
    return 0;
}

static int test_transient_ntstatus_success(void) {
    VMX_INSTRUCTION_REPORTS r; SetTransientSuccess(&r);
    int32_t st = IoctlMapTransientToNtStatus(
        VMX_PREFLIGHT_SUCCESS, VMX_STAGE_VMRESUME, &r);
    TEST_ASSERT_INT_EQ(st, 0, "nt_ok");
    return 0;
}

static int test_transient_ntstatus_vmxon_fail(void) {
    VMX_INSTRUCTION_REPORTS r = {0};
    r.VmxonOutcome = VMX_INSTR_FAIL_VALID;
    int32_t st = IoctlMapTransientToNtStatus(
        VMX_PREFLIGHT_SUCCESS, VMX_STAGE_VMXON, &r);
    TEST_ASSERT_INT_EQ(st, (int32_t)0xC00000BB, "nt_vmxon");
    return 0;
}

static int test_transient_ntstatus_vmlaunch_fail(void) {
    VMX_INSTRUCTION_REPORTS r; SetTransientSuccess(&r);
    r.VmlaunchOutcome = VMX_INSTR_FAIL_VALID;
    r.VmresumeOutcome = VMX_INSTR_NOT_ATTEMPTED;
    int32_t st = IoctlMapTransientToNtStatus(
        VMX_PREFLIGHT_SUCCESS, VMX_STAGE_VMLAUNCH, &r);
    TEST_ASSERT_INT_EQ(st, (int32_t)0xC0000001, "nt_launch");
    return 0;
}

static int test_transient_ntstatus_vmresume_fail(void) {
    VMX_INSTRUCTION_REPORTS r; SetTransientSuccess(&r);
    r.VmresumeOutcome = VMX_INSTR_FAIL_INVALID;
    int32_t st = IoctlMapTransientToNtStatus(
        VMX_PREFLIGHT_SUCCESS, VMX_STAGE_VMRESUME, &r);
    TEST_ASSERT_INT_EQ(st, (int32_t)0xC0000001, "nt_resume");
    return 0;
}

static int test_transient_ntstatus_vmclear_fail(void) {
    VMX_INSTRUCTION_REPORTS r = {0};
    r.VmxonOutcome = VMX_INSTR_SUCCESS;
    r.VmclearOutcome = VMX_INSTR_FAIL_INVALID;
    int32_t st = IoctlMapTransientToNtStatus(
        VMX_PREFLIGHT_SUCCESS, VMX_STAGE_VMCLEAR, &r);
    TEST_ASSERT_INT_EQ(st, (int32_t)0xC0000001, "nt_vmclear");
    return 0;
}

static int test_transient_ntstatus_null_reports(void) {
    /* NULL reports with preflight success → domain=FAILED → STATUS_UNSUCCESSFUL */
    int32_t st = IoctlMapTransientToNtStatus(
        VMX_PREFLIGHT_SUCCESS, VMX_STAGE_VMXON, NULL);
    TEST_ASSERT_INT_EQ(st, (int32_t)0xC00000BB, "nt_null");
    return 0;
}

/*===========================================================================
 * G1c-b: VMLAUNCH MASM-to-instruction normalization
 *===========================================================================*/

static int test_vmlaunch_normalize_masm_success(void) {
    /*
     * MASM reports the authoritative four-state outcome directly.
     */
    VMX_INSTRUCTION_REPORTS r = {0};
    VMX_LAUNCH_PROBE_RESPONSE_V2 dst;
    memset(&dst, 0xCC, sizeof(dst)); /* poison to catch misses */

    r.VmlaunchOutcome = VMX_INSTR_SUCCESS;
    /* Other outcomes remain NOT_ATTEMPTED for this copy-only test. */

    IoctlCopyInstructionReports(&r, &dst);

    TEST_ASSERT_UINT_EQ(dst.Vmlaunch.Outcome, (uint8_t)VMX_INSTR_SUCCESS,
                        "n_masm_ok");
    TEST_ASSERT_UINT_EQ(dst.Vmlaunch.ErrorValid, 0, "n_masm_ev");
    TEST_ASSERT_UINT_EQ(dst.Vmlaunch.Error, 0, "n_masm_err");
    return 0;
}

static int test_vmlaunch_normalize_masm_failure(void) {
    VMX_INSTRUCTION_REPORTS r = {0};
    VMX_LAUNCH_PROBE_RESPONSE_V2 dst;
    memset(&dst, 0xCC, sizeof(dst));

    r.VmlaunchOutcome = VMX_INSTR_FAIL_VALID;

    IoctlCopyInstructionReports(&r, &dst);

    TEST_ASSERT_UINT_EQ(dst.Vmlaunch.Outcome, (uint8_t)VMX_INSTR_FAIL_VALID,
                        "n_masm_fail");
    return 0;
}

static int test_vmlaunch_normalize_other_reports_unchanged(void) {
    VMX_INSTRUCTION_REPORTS r = {0};
    VMX_LAUNCH_PROBE_RESPONSE_V2 dst;
    memset(&dst, 0xCC, sizeof(dst));

    r.VmxonOutcome = VMX_INSTR_SUCCESS;       /* 1 */
    r.VmclearOutcome = VMX_INSTR_FAIL_VALID;  /* 2 */
    r.VmptrldOutcome = VMX_INSTR_FAIL_INVALID;/* 3 */
    r.VmwriteOutcome = VMX_VMWRITE_FAIL_INVALID; /* 3 */
    r.VmwriteErrorValid = true;
    r.VmwriteError = 0x1234;
    r.VmresumeOutcome = VMX_INSTR_NOT_ATTEMPTED; /* 0 */
    r.VmlaunchOutcome = VMX_INSTR_SUCCESS; /* explicit four-state success */

    IoctlCopyInstructionReports(&r, &dst);

    TEST_ASSERT_UINT_EQ(dst.Vmxon.Outcome, (uint8_t)VMX_INSTR_SUCCESS, "n_other_vmxon");
    TEST_ASSERT_UINT_EQ(dst.Vmclear.Outcome, (uint8_t)VMX_INSTR_FAIL_VALID, "n_other_vmclear");
    TEST_ASSERT_UINT_EQ(dst.Vmptrld.Outcome, (uint8_t)VMX_INSTR_FAIL_INVALID, "n_other_vmptrld");
    TEST_ASSERT_INT_EQ((int)dst.VmwritePlan.Outcome, VMX_VMWRITE_FAIL_INVALID, "n_other_vmwrite");
    TEST_ASSERT_UINT_EQ(dst.VmwritePlan.ErrorValid, 1, "n_other_vmwrite_error_valid");
    TEST_ASSERT_UINT_EQ(dst.VmwritePlan.Error, 0x1234, "n_other_vmwrite_error");
    TEST_ASSERT_UINT_EQ(dst.Vmresume.Outcome, (uint8_t)VMX_INSTR_NOT_ATTEMPTED, "n_other_vmresume");
    /* VMLAUNCH preserves the four-state success outcome. */
    TEST_ASSERT_UINT_EQ(dst.Vmlaunch.Outcome, (uint8_t)VMX_INSTR_SUCCESS, "n_other_vmlaunch");
    return 0;
}

static int test_kernel_vmwrite_outcome_normalization(void) {
    TEST_ASSERT_INT_EQ(KernelNormalizeVmxResult(0),
                       VMX_VMWRITE_SUCCESS, "vmwrite 0 = success");
    TEST_ASSERT_INT_EQ(KernelNormalizeVmxResult(1),
                       VMX_VMWRITE_FAIL_VALID, "vmwrite 1 = fail-valid");
    TEST_ASSERT_INT_EQ(KernelNormalizeVmxResult(2),
                       VMX_VMWRITE_FAIL_INVALID, "vmwrite 2 = fail-invalid");
    TEST_ASSERT_INT_EQ(KernelNormalizeVmxResult(3),
                       VMX_VMWRITE_FAIL_INVALID, "vmwrite unexpected = fail-invalid");
    TEST_ASSERT_INT_EQ(KernelNormalizeVmxResult(0xFF),
                       VMX_VMWRITE_FAIL_INVALID, "vmwrite 0xFF = fail-invalid");
    return 0;
}

/* Response layout completeness */
static int test_response_size_still_280(void) {
    TEST_ASSERT_UINT_EQ(sizeof(VMX_LAUNCH_PROBE_RESPONSE_V2), 280, "280");
    return 0;
}

static int test_domain_enum_values(void) {
    TEST_ASSERT_UINT_EQ((unsigned)VMX_DOMAIN_UNKNOWN, 0, "u");
    TEST_ASSERT_UINT_EQ((unsigned)VMX_DOMAIN_SUCCESS, 1, "s");
    TEST_ASSERT_UINT_EQ((unsigned)VMX_DOMAIN_REJECTED, 2, "r");
    TEST_ASSERT_UINT_EQ((unsigned)VMX_DOMAIN_FAILED, 3, "f");
    return 0;
}

/* D2b2a: checked Runtime V2 serialization */
static void SetCheckedValid(
    VMX_RUNTIME_LAUNCH_CONTEXT* context,
    VMX_INSTRUCTION_REPORTS* reports)
{
    memset(context, 0, sizeof(*context));
    memset(reports, 0, sizeof(*reports));
    reports->VmxonOutcome = VMX_INSTR_SUCCESS;
    reports->VmclearOutcome = VMX_INSTR_SUCCESS;
    reports->VmptrldOutcome = VMX_INSTR_SUCCESS;
    reports->VmwriteOutcome = VMX_VMWRITE_FAIL_VALID;
    reports->VmwriteErrorValid = true;
    reports->VmwriteError = 0x1234;
    reports->GuestRspOutcome = VMX_INSTR_FAIL_VALID;
    reports->GuestRflagsOutcome = VMX_INSTR_FAIL_VALID;
    reports->VmlaunchOutcome = VMX_INSTR_FAIL_VALID;
    reports->VmresumeOutcome = VMX_INSTR_FAIL_VALID;
    reports->VmxoffOutcome = VMX_INSTR_FAIL_INVALID;
    context->ProcessorGroup = 7;
    context->ProcessorNumber = 3;
    context->RevisionId = 0x1234;
    context->VmcsSize = 0x1000;
    context->VmxBasic = 0x1122334455667788ULL;
    context->VmxonPa = 0x2000;
    context->VmcsPa = 0x3000;
    context->OriginalCr0 = 0x80050033;
    context->FinalCr0 = 0x80050032;
    context->OriginalCr4 = 0x2000;
    context->FinalCr4 = 0x2000;
    context->Assembly.Cookie = 0xABCDEF0123456789ULL;
    context->Assembly.LaunchOutcome = VMX_INSTR_FAIL_VALID;
    context->Assembly.ResumeOutcome = VMX_INSTR_FAIL_VALID;
    context->Assembly.VmxoffOutcome = VMX_INSTR_SUCCESS;
    context->Assembly.GuestRspWriteOutcome = VMX_INSTR_FAIL_VALID;
    context->Assembly.GuestRspWriteInstructionErrorValid = 1;
    context->Assembly.GuestRspWriteInstructionError = 0xAABBCCDD;
    context->Assembly.GuestRflagsWriteOutcome = VMX_INSTR_FAIL_VALID;
    context->Assembly.GuestRflagsWriteInstructionErrorValid = 1;
    context->Assembly.GuestRflagsWriteInstructionError = 0x11223344;
    context->Assembly.ResumeInstructionErrorValid = 1;
    context->Assembly.ResumeInstructionError = 0x55667788;
    context->ProbeExit.Observed = true;
    context->ProbeExit.ValidationMask = 0xA5;
    context->ProbeExit.Report.Reason = 0x10;
    context->ProbeExit.Report.InstructionLength = 3;
    context->ProbeExit.Report.Qualification = 0x1111;
    context->ProbeExit.Report.Rip = 0x2222;
    context->ProbeExit.Report.Rax = 0x3333;
    context->ProbeExit.Report.Rcx = 0x4444;
    context->ProbeExit.Report.Rdx = 0x5555;
    context->ShutdownExit.Observed = true;
    context->ShutdownExit.ValidationMask = 0x5A;
    context->ShutdownExit.Report.Reason = 0x20;
    context->ShutdownExit.Report.InstructionLength = 2;
    context->ShutdownExit.Report.Qualification = 0xAAAA;
    context->ShutdownExit.Report.Rip = 0xBBBB;
    context->ShutdownExit.Report.Rax = 0xCCCC;
    context->ShutdownExit.Report.Rcx = 0xDDDD;
    context->ShutdownExit.Report.Rdx = 0xEEEE;
    context->ProtocolStep = VMX_PROTOCOL_SHUTDOWN_COMPLETE;
}

static bool CheckedResponseIsZero(const VMX_LAUNCH_PROBE_RESPONSE_V2* response)
{
    VMX_LAUNCH_PROBE_RESPONSE_V2 zero = {0};
    return memcmp(response, &zero, sizeof(zero)) == 0;
}

static int test_checked_runtime_copy(void)
{
    VMX_RUNTIME_LAUNCH_CONTEXT context;
    VMX_INSTRUCTION_REPORTS reports;
    VMX_LAUNCH_PROBE_RESPONSE_V2 response;
    VMX_INSTRUCTION_REPORT* copied[] = {
        &response.Vmxon, &response.Vmclear, &response.Vmptrld,
        &response.VmwritePlan, &response.GuestRsp, &response.GuestRflags,
        &response.Vmlaunch, &response.Vmresume, &response.Vmxoff
    };
    const uint8_t expected[] = {1, 1, 1, 2, 2, 2, 2, 2, 3};
    size_t index;

    SetCheckedValid(&context, &reports);
    memset(&response, 0xA5, sizeof(response));
    TEST_ASSERT_TRUE(IoctlCopyRuntimeDiagnosticsChecked(
        &context, &reports, &response), "checked copy succeeds");
    TEST_ASSERT_UINT_EQ(response.Version, 2, "checked version");
    TEST_ASSERT_UINT_EQ(response.Size, 280, "checked size");
    TEST_ASSERT_UINT_EQ(response.ProcessorGroup, 7, "checked group");
    TEST_ASSERT_UINT_EQ(response.ProcessorNumber, 3, "checked processor");
    TEST_ASSERT_UINT_EQ(response.VmxRevisionId, 0x1234, "checked revision");
    TEST_ASSERT_UINT_EQ(response.VmxRegionSize, 0x1000, "checked region size");
    TEST_ASSERT_UINT_EQ(response.VmxBasic, context.VmxBasic, "checked basic");
    TEST_ASSERT_UINT_EQ(response.VmxonPa, 0x2000, "checked vmxon pa");
    TEST_ASSERT_UINT_EQ(response.VmcsPa, 0x3000, "checked vmcs pa");
    TEST_ASSERT_UINT_EQ(response.OriginalCr0, context.OriginalCr0, "checked original cr0");
    TEST_ASSERT_UINT_EQ(response.FinalCr0, context.FinalCr0, "checked final cr0");
    TEST_ASSERT_UINT_EQ(response.OriginalCr4, context.OriginalCr4, "checked original cr4");
    TEST_ASSERT_UINT_EQ(response.FinalCr4, context.FinalCr4, "checked final cr4");
    TEST_ASSERT_UINT_EQ(response.ExpectedCookie, context.Assembly.Cookie, "checked cookie");
    TEST_ASSERT_UINT_EQ(response.ProtocolStep, VMX_PROTOCOL_SHUTDOWN_COMPLETE,
                        "checked protocol step");
    TEST_ASSERT_UINT_EQ(response.ProcessorReserved, 0, "checked processor reserved");
    TEST_ASSERT_UINT_EQ(response.Reserved0, 0, "checked reserved0");
    TEST_ASSERT_UINT_EQ(response.Reserved1, 0, "checked reserved1");
    for (index = 0; index < sizeof(copied) / sizeof(copied[0]); ++index) {
        TEST_ASSERT_UINT_EQ(copied[index]->Outcome, expected[index],
                            "checked report outcome");
        TEST_ASSERT_UINT_EQ(copied[index]->Reserved, 0, "checked report reserved");
    }
    TEST_ASSERT_UINT_EQ(response.VmwritePlan.ErrorValid, 1, "checked vmwrite valid");
    TEST_ASSERT_UINT_EQ(response.VmwritePlan.Error, 0x1234, "checked vmwrite error");
    TEST_ASSERT_UINT_EQ(response.GuestRsp.ErrorValid, 1, "checked rsp valid");
    TEST_ASSERT_UINT_EQ(response.GuestRsp.Error, 0xAABBCCDD, "checked rsp error");
    TEST_ASSERT_UINT_EQ(response.GuestRflags.ErrorValid, 1, "checked rflags valid");
    TEST_ASSERT_UINT_EQ(response.GuestRflags.Error, 0x11223344, "checked rflags error");
    TEST_ASSERT_UINT_EQ(response.Vmresume.ErrorValid, 1, "checked resume valid");
    TEST_ASSERT_UINT_EQ(response.Vmresume.Error, 0x55667788, "checked resume error");
    TEST_ASSERT_UINT_EQ(response.Vmxon.ErrorValid, 0, "checked vmxon error zero");
    TEST_ASSERT_UINT_EQ(response.Vmxoff.Error, 0, "checked vmxoff error zero");
    TEST_ASSERT_UINT_EQ(response.ProbeExit.Reason, 0x10, "checked probe reason");
    TEST_ASSERT_UINT_EQ(response.ProbeExit.InstructionLength, 3, "checked probe length");
    TEST_ASSERT_UINT_EQ(response.ProbeExit.Qualification, 0x1111, "checked probe qualification");
    TEST_ASSERT_UINT_EQ(response.ProbeExit.Rip, 0x2222, "checked probe rip");
    TEST_ASSERT_UINT_EQ(response.ProbeExit.Rax, 0x3333, "checked probe rax");
    TEST_ASSERT_UINT_EQ(response.ProbeExit.Rcx, 0x4444, "checked probe rcx");
    TEST_ASSERT_UINT_EQ(response.ProbeExit.Rdx, 0x5555, "checked probe rdx");
    TEST_ASSERT_UINT_EQ(response.ShutdownExit.Reason, 0x20, "checked shutdown reason");
    TEST_ASSERT_UINT_EQ(response.ShutdownExit.InstructionLength, 2, "checked shutdown length");
    TEST_ASSERT_UINT_EQ(response.ShutdownExit.Qualification, 0xAAAA, "checked shutdown qualification");
    TEST_ASSERT_UINT_EQ(response.ShutdownExit.Rip, 0xBBBB, "checked shutdown rip");
    TEST_ASSERT_UINT_EQ(response.ShutdownExit.Rax, 0xCCCC, "checked shutdown rax");
    TEST_ASSERT_UINT_EQ(response.ShutdownExit.Rcx, 0xDDDD, "checked shutdown rcx");
    TEST_ASSERT_UINT_EQ(response.ShutdownExit.Rdx, 0xEEEE, "checked shutdown rdx");
    return 0;
}

static int test_checked_runtime_rejects_malformed(void)
{
    VMX_RUNTIME_LAUNCH_CONTEXT context;
    VMX_INSTRUCTION_REPORTS reports;
    VMX_LAUNCH_PROBE_RESPONSE_V2 response;
    int* outcomes[] = {
        &reports.VmxonOutcome, &reports.VmclearOutcome, &reports.VmptrldOutcome,
        &reports.VmwriteOutcome, &reports.GuestRspOutcome, &reports.GuestRflagsOutcome,
        &reports.VmlaunchOutcome, &reports.VmresumeOutcome, &reports.VmxoffOutcome
    };
    size_t index;

    SetCheckedValid(&context, &reports);
    TEST_ASSERT_TRUE(!IoctlCopyRuntimeDiagnosticsChecked(
        NULL, &reports, &response), "checked null context");
    TEST_ASSERT_TRUE(CheckedResponseIsZero(&response), "checked null context zeroes");
    TEST_ASSERT_TRUE(!IoctlCopyRuntimeDiagnosticsChecked(
        &context, NULL, &response), "checked null reports");
    TEST_ASSERT_TRUE(CheckedResponseIsZero(&response), "checked null reports zeroes");
    TEST_ASSERT_TRUE(!IoctlCopyRuntimeDiagnosticsChecked(
        &context, &reports, NULL), "checked null response");

    for (index = 0; index < sizeof(outcomes) / sizeof(outcomes[0]); ++index) {
        SetCheckedValid(&context, &reports);
        *outcomes[index] = -1;
        memset(&response, 0xA5, sizeof(response));
        TEST_ASSERT_TRUE(!IoctlCopyRuntimeDiagnosticsChecked(
            &context, &reports, &response), "checked negative outcome");
        TEST_ASSERT_TRUE(CheckedResponseIsZero(&response), "checked negative zeroes");
        SetCheckedValid(&context, &reports);
        *outcomes[index] = 4;
        TEST_ASSERT_TRUE(!IoctlCopyRuntimeDiagnosticsChecked(
            &context, &reports, &response), "checked large outcome");
    }
    SetCheckedValid(&context, &reports);
    reports.VmwriteError = (uint64_t)UINT32_MAX + 1ULL;
    TEST_ASSERT_TRUE(!IoctlCopyRuntimeDiagnosticsChecked(
        &context, &reports, &response), "checked vmwrite overflow");
    SetCheckedValid(&context, &reports);
    reports.VmwriteOutcome = VMX_VMWRITE_FAIL_INVALID;
    TEST_ASSERT_TRUE(!IoctlCopyRuntimeDiagnosticsChecked(
        &context, &reports, &response), "checked vmwrite valid contradiction");
    SetCheckedValid(&context, &reports);
    reports.VmwriteErrorValid = false;
    reports.VmwriteError = 1;
    TEST_ASSERT_TRUE(!IoctlCopyRuntimeDiagnosticsChecked(
        &context, &reports, &response), "checked vmwrite invalid error");
    return 0;
}

static int test_checked_runtime_consistency(void)
{
    VMX_RUNTIME_LAUNCH_CONTEXT context;
    VMX_INSTRUCTION_REPORTS reports;
    VMX_LAUNCH_PROBE_RESPONSE_V2 response;

    SetCheckedValid(&context, &reports);
    context.Assembly.GuestRspWriteInstructionErrorValid = 2;
    TEST_ASSERT_TRUE(!IoctlCopyRuntimeDiagnosticsChecked(
        &context, &reports, &response), "checked error-valid range");
    SetCheckedValid(&context, &reports);
    context.Assembly.GuestRspWriteInstructionErrorValid = 0;
    context.Assembly.GuestRspWriteInstructionError = 1;
    TEST_ASSERT_TRUE(!IoctlCopyRuntimeDiagnosticsChecked(
        &context, &reports, &response), "checked stale guest rsp error");
    SetCheckedValid(&context, &reports);
    context.Assembly.GuestRspWriteOutcome = VMX_INSTR_SUCCESS;
    TEST_ASSERT_TRUE(!IoctlCopyRuntimeDiagnosticsChecked(
        &context, &reports, &response), "checked rsp outcome mismatch");
    SetCheckedValid(&context, &reports);
    context.Assembly.GuestRflagsWriteOutcome = VMX_INSTR_SUCCESS;
    TEST_ASSERT_TRUE(!IoctlCopyRuntimeDiagnosticsChecked(
        &context, &reports, &response), "checked rflags outcome mismatch");
    SetCheckedValid(&context, &reports);
    context.Assembly.LaunchOutcome = VMX_INSTR_SUCCESS;
    TEST_ASSERT_TRUE(!IoctlCopyRuntimeDiagnosticsChecked(
        &context, &reports, &response), "checked launch outcome mismatch");
    SetCheckedValid(&context, &reports);
    context.Assembly.ResumeOutcome = VMX_INSTR_SUCCESS;
    TEST_ASSERT_TRUE(!IoctlCopyRuntimeDiagnosticsChecked(
        &context, &reports, &response), "checked resume outcome mismatch");
    SetCheckedValid(&context, &reports);
    context.Assembly.VmxoffOutcome = 4;
    TEST_ASSERT_TRUE(!IoctlCopyRuntimeDiagnosticsChecked(
        &context, &reports, &response), "checked vmxoff assembly range");
    SetCheckedValid(&context, &reports);
    context.ProtocolStep = (VMX_PROTOCOL_STEP)-1;
    TEST_ASSERT_TRUE(!IoctlCopyRuntimeDiagnosticsChecked(
        &context, &reports, &response), "checked protocol below range");
    SetCheckedValid(&context, &reports);
    context.ProtocolStep = (VMX_PROTOCOL_STEP)3;
    TEST_ASSERT_TRUE(!IoctlCopyRuntimeDiagnosticsChecked(
        &context, &reports, &response), "checked protocol above range");
    return 0;
}
static void SetValidationOutcomes(
    VMX_RUNTIME_LAUNCH_CONTEXT* context,
    VMX_INSTRUCTION_REPORTS* reports,
    int vmxon, int vmclear, int vmptrld, int vmwrite,
    int guest_rsp, int guest_rflags, int launch, int resume, int vmxoff)
{
    reports->VmxonOutcome = vmxon;
    reports->VmclearOutcome = vmclear;
    reports->VmptrldOutcome = vmptrld;
    reports->VmwriteOutcome = vmwrite;
    reports->GuestRspOutcome = guest_rsp;
    reports->GuestRflagsOutcome = guest_rflags;
    reports->VmlaunchOutcome = launch;
    reports->VmresumeOutcome = resume;
    reports->VmxoffOutcome = vmxoff;
    context->Assembly.VmxoffOutcome = (uint32_t)vmxoff;
    context->Assembly.GuestRspWriteOutcome = (uint32_t)guest_rsp;
    context->Assembly.GuestRflagsWriteOutcome = (uint32_t)guest_rflags;
    context->Assembly.LaunchOutcome = (uint32_t)launch;
    context->Assembly.ResumeOutcome = (uint32_t)resume;
}
static void SetValidationStage(
    VMX_RUNTIME_LAUNCH_CONTEXT* context,
    VMX_INSTRUCTION_REPORTS* reports,
    VMX_LIFECYCLE_STAGE stage)
{
    (void)memset(context, 0, sizeof(*context));
    (void)memset(reports, 0, sizeof(*reports));
    switch (stage) {
    case VMX_STAGE_NOT_STARTED:
        SetValidationOutcomes(context, reports, 0, 0, 0, 0, 0, 0, 0, 0, 0); break;
    case VMX_STAGE_INSTRUCTION_INTERVAL:
        SetValidationOutcomes(context, reports, 2, 0, 0, 0, 0, 0, 0, 0, 0); break;
    case VMX_STAGE_VMXON:
        SetValidationOutcomes(context, reports, 1, 2, 0, 0, 0, 0, 0, 0, 1); break;
    case VMX_STAGE_VMCLEAR:
        SetValidationOutcomes(context, reports, 1, 1, 2, 0, 0, 0, 0, 0, 1); break;
    case VMX_STAGE_VMPTRLD:
    case VMX_STAGE_PROCESSOR_STATE:
    case VMX_STAGE_CONTROLS_PLANNED:
        SetValidationOutcomes(context, reports, 1, 1, 1, 0, 0, 0, 0, 0, 1); break;
    case VMX_STAGE_HOST_PLAN:
        SetValidationOutcomes(context, reports, 1, 1, 1, 2, 0, 0, 0, 0, 1); break;
    case VMX_STAGE_VMRESUME:
        SetValidationOutcomes(context, reports, 1, 1, 1, 1, 1, 1, 1, 0, 1);
        context->Assembly.VmxoffOutcome = VMX_INSTR_SUCCESS;
        context->ProbeExit.Observed = true;
        context->ProbeExit.ValidationMask = 1;
        break;
    }
}
static void InitValidationInput(
    VMX_LAUNCH_PROBE_MAP_INPUT* input,
    VMX_RUNTIME_LAUNCH_CONTEXT* context,
    VMX_INSTRUCTION_REPORTS* reports,
    VMX_LIFECYCLE_STAGE stage)
{
    (void)memset(input, 0, sizeof(*input));
    input->PreflightResult = VMX_PREFLIGHT_SUCCESS;
    input->TransientStage = stage;
    input->Context = context;
    input->Reports = reports;
    input->ResourcesReleased = true;
}
static int test_validate_stage_matrix(void)
{
    const VMX_LIFECYCLE_STAGE valid[] = {
        VMX_STAGE_NOT_STARTED, VMX_STAGE_INSTRUCTION_INTERVAL, VMX_STAGE_VMXON,
        VMX_STAGE_VMCLEAR, VMX_STAGE_VMPTRLD, VMX_STAGE_PROCESSOR_STATE,
        VMX_STAGE_CONTROLS_PLANNED, VMX_STAGE_HOST_PLAN, VMX_STAGE_VMRESUME
    };
    VMX_RUNTIME_LAUNCH_CONTEXT context;
    VMX_INSTRUCTION_REPORTS reports;
    VMX_LAUNCH_PROBE_MAP_INPUT input;
    size_t index;
    for (index = 0; index < sizeof(valid) / sizeof(valid[0]); ++index) {
        SetValidationStage(&context, &reports, valid[index]);
        InitValidationInput(&input, &context, &reports, valid[index]);
        TEST_ASSERT_TRUE(IoctlValidateLaunchProbeMapInput(&input), "valid stage matrix");
    }
    {
        const int host_cases[4][9] = {
            { 1, 1, 1, 2, 0, 0, 0, 0, 1 },
            { 1, 1, 1, 1, 2, 0, 0, 0, 1 },
            { 1, 1, 1, 1, 1, 2, 0, 0, 1 },
            { 1, 1, 1, 1, 1, 1, 2, 0, 1 }
        };
        for (index = 0; index < 4; ++index) {
            SetValidationStage(&context, &reports, VMX_STAGE_HOST_PLAN);
            SetValidationOutcomes(&context, &reports,
                                  host_cases[index][0], host_cases[index][1],
                                  host_cases[index][2], host_cases[index][3],
                                  host_cases[index][4], host_cases[index][5],
                                  host_cases[index][6], host_cases[index][7],
                                  host_cases[index][8]);
            InitValidationInput(&input, &context, &reports, VMX_STAGE_HOST_PLAN);
            TEST_ASSERT_TRUE(IoctlValidateLaunchProbeMapInput(&input), "host branch");
        }
    }
    SetValidationStage(&context, &reports, VMX_STAGE_VMXON);
    InitValidationInput(&input, &context, &reports, VMX_STAGE_VMXON);
    reports.VmclearOutcome = VMX_INSTR_SUCCESS;
    TEST_ASSERT_TRUE(!IoctlValidateLaunchProbeMapInput(&input), "vmxon dependency");
    SetValidationStage(&context, &reports, VMX_STAGE_VMRESUME);
    InitValidationInput(&input, &context, &reports, VMX_STAGE_VMRESUME);
    reports.VmxoffOutcome = VMX_INSTR_NOT_ATTEMPTED;
    context.Assembly.VmxoffOutcome = VMX_INSTR_NOT_ATTEMPTED;
    TEST_ASSERT_TRUE(!IoctlValidateLaunchProbeMapInput(&input), "resume teardown dependency");
    return 0;
}
static void InitMapperInput(VMX_LAUNCH_PROBE_MAP_INPUT*, VMX_RUNTIME_LAUNCH_CONTEXT*, VMX_INSTRUCTION_REPORTS*, VMX_LIFECYCLE_STAGE);
static void SetMapperFullSuccess(VMX_LAUNCH_PROBE_MAP_INPUT*, VMX_RUNTIME_LAUNCH_CONTEXT*, VMX_INSTRUCTION_REPORTS*);
static int test_validate_final_vmclear_failures(void)
{
    const int failures[] = { VMX_INSTR_FAIL_VALID, VMX_INSTR_FAIL_INVALID };
    VMX_LAUNCH_PROBE_MAP_INPUT input;
    VMX_RUNTIME_LAUNCH_CONTEXT context;
    VMX_INSTRUCTION_REPORTS reports;
    VMX_LAUNCH_PROBE_RESPONSE_V2 response;
    size_t index;

    for (index = 0; index < sizeof(failures) / sizeof(failures[0]); ++index) {
        SetMapperFullSuccess(&input, &context, &reports);
        reports.VmclearOutcome = failures[index];
        TEST_ASSERT_TRUE(IoctlValidateLaunchProbeMapInput(&input), "late vmclear validator");
        TEST_ASSERT_TRUE(IoctlMapLaunchProbeResponse(&input, &response), "late vmclear mapper");
        TEST_ASSERT_UINT_EQ(response.DomainStatus, VMX_DOMAIN_FAILED, "late vmclear domain");
        TEST_ASSERT_INT_EQ(response.NtStatus, (int32_t)0xC0000001, "late vmclear status");
        TEST_ASSERT_UINT_EQ(response.Vmclear.Outcome, (unsigned)failures[index], "late vmclear preserved");
    }
    InitMapperInput(&input, &context, &reports, VMX_STAGE_HOST_PLAN);
    reports.VmclearOutcome = VMX_INSTR_FAIL_VALID;
    TEST_ASSERT_TRUE(IoctlValidateLaunchProbeMapInput(&input), "host final vmclear validator");
    InitMapperInput(&input, &context, &reports, VMX_STAGE_VMPTRLD);
    reports.VmclearOutcome = VMX_INSTR_FAIL_INVALID;
    TEST_ASSERT_TRUE(IoctlValidateLaunchProbeMapInput(&input), "vmptrld final vmclear validator");
    return 0;
}
static int test_validate_cleanup_and_input_contract(void)
{
    const struct {
        bool released;
        VMX_CLEANUP_REQUIREMENT requirement;
        VMX_CLEANUP_OWNER owner;
        uint32_t resources;
        bool valid;
    } cases[] = {
        { true, VMX_CLEANUP_NONE, VMX_CLEANUP_OWNER_NONE, 0x3Fu, true },
        { false, VMX_CLEANUP_NONE, VMX_CLEANUP_OWNER_NONE, 0, true },
        { false, VMX_CLEANUP_REQUIRED, VMX_CLEANUP_OWNER_C, 1u, true },
        { false, VMX_CLEANUP_REQUIRED, VMX_CLEANUP_OWNER_MASM, 2u, true },
        { true, VMX_CLEANUP_REQUIRED, VMX_CLEANUP_OWNER_C, 1u, false },
        { false, VMX_CLEANUP_REQUIRED, VMX_CLEANUP_OWNER_NONE, 1u, false },
        { false, (VMX_CLEANUP_REQUIREMENT)-1, VMX_CLEANUP_OWNER_C, 1u, false },
        { false, (VMX_CLEANUP_REQUIREMENT)2, VMX_CLEANUP_OWNER_C, 1u, false },
        { false, VMX_CLEANUP_REQUIRED, (VMX_CLEANUP_OWNER)-1, 1u, false },
    };
    VMX_RUNTIME_LAUNCH_CONTEXT context;
    VMX_INSTRUCTION_REPORTS reports;
    VMX_LAUNCH_PROBE_MAP_INPUT input;
    size_t index;
    SetValidationStage(&context, &reports, VMX_STAGE_VMRESUME);
    for (index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        InitValidationInput(&input, &context, &reports, VMX_STAGE_VMRESUME);
        input.ResourcesReleased = cases[index].released;
        input.CleanupRequirement = cases[index].requirement;
        input.CleanupOwner = cases[index].owner;
        input.ResourceMask = cases[index].resources;
        TEST_ASSERT_TRUE(IoctlValidateLaunchProbeMapInput(&input) == cases[index].valid,
                         "cleanup contract");
    }
    InitValidationInput(&input, &context, &reports, VMX_STAGE_VMRESUME);
    input.ResourceMask = 1u << 6;
    TEST_ASSERT_TRUE(!IoctlValidateLaunchProbeMapInput(&input), "resource bit contract");
    input.ResourceMask = 0;
    input.PreflightResult = (VMX_PREFLIGHT_RESULT)-1;
    TEST_ASSERT_TRUE(!IoctlValidateLaunchProbeMapInput(&input), "preflight below range");
    input.PreflightResult = VMX_PREFLIGHT_SUCCESS;
    input.TransientStage = (VMX_LIFECYCLE_STAGE)-1;
    TEST_ASSERT_TRUE(!IoctlValidateLaunchProbeMapInput(&input), "stage below range");
    TEST_ASSERT_TRUE(!IoctlValidateLaunchProbeMapInput(NULL), "null input");
    input.TransientStage = VMX_STAGE_VMRESUME;
    input.Context = NULL;
    TEST_ASSERT_TRUE(!IoctlValidateLaunchProbeMapInput(&input), "null context");
    input.Context = &context;
    input.Reports = NULL;
    TEST_ASSERT_TRUE(!IoctlValidateLaunchProbeMapInput(&input), "null reports");
    (void)memset(&input, 0, sizeof(input));
    input.PreflightResult = VMX_PREFLIGHT_NOT_PASSIVE_LEVEL;
    input.TransientStage = VMX_STAGE_NOT_STARTED;
    TEST_ASSERT_TRUE(IoctlValidateLaunchProbeMapInput(&input), "preflight failure allows null runtime");
    input.TransientStage = VMX_STAGE_VMXON;
    TEST_ASSERT_TRUE(!IoctlValidateLaunchProbeMapInput(&input), "preflight failure stage");
    return 0;
}
static int test_validate_protocol_contract(void)
{
    const struct {
        VMX_LIFECYCLE_STAGE stage; VMX_PROTOCOL_STEP step;
        uint16_t probe_mask; uint16_t shutdown_mask; int resume;
        bool probe_observed; bool shutdown_observed; bool valid;
    } cases[] = {
        { VMX_STAGE_VMRESUME, VMX_PROTOCOL_NONE, 0, 0, 0, true, false, true },
        { VMX_STAGE_VMRESUME, VMX_PROTOCOL_PROBE_COMPLETE, 0x03FF, 0, 1, true, false, true },
        { VMX_STAGE_VMRESUME, VMX_PROTOCOL_PROBE_COMPLETE, 0x03FF, 0, 2, true, false, true },
        { VMX_STAGE_VMRESUME, VMX_PROTOCOL_PROBE_COMPLETE, 0x03FF, 0, 1, true, true, true },
        { VMX_STAGE_VMRESUME, VMX_PROTOCOL_SHUTDOWN_COMPLETE, 0x03FF, 0x01FF, 1, true, true, true },
        { VMX_STAGE_HOST_PLAN, VMX_PROTOCOL_NONE, 0, 0, 0, false, false, true },
        { VMX_STAGE_VMRESUME, VMX_PROTOCOL_NONE, 0x0400, 0, 0, true, false, false },
        { VMX_STAGE_VMRESUME, VMX_PROTOCOL_PROBE_COMPLETE, 0x03FF, 0x0200, 1, true, true, false },
        { VMX_STAGE_VMRESUME, VMX_PROTOCOL_NONE, 1, 0, 0, false, false, false },
        { VMX_STAGE_VMRESUME, VMX_PROTOCOL_NONE, 0x03FF, 0, 0, true, false, false },
        { VMX_STAGE_VMRESUME, VMX_PROTOCOL_PROBE_COMPLETE, 1, 0, 1, true, false, false },
        { VMX_STAGE_VMRESUME, VMX_PROTOCOL_PROBE_COMPLETE, 0x03FF, 0, 0, true, false, false },
        { VMX_STAGE_VMRESUME, VMX_PROTOCOL_PROBE_COMPLETE, 0x03FF, 0x01FF, 1, true, true, false },
        { VMX_STAGE_VMRESUME, VMX_PROTOCOL_SHUTDOWN_COMPLETE, 0x03FF, 1, 1, true, true, false },
        { VMX_STAGE_VMRESUME, VMX_PROTOCOL_SHUTDOWN_COMPLETE, 0x03FF, 0x01FF, 2, true, true, false },
        { VMX_STAGE_VMRESUME, VMX_PROTOCOL_SHUTDOWN_COMPLETE, 0x03FF, 0x01FF, 1, false, true, false }
    };
    VMX_RUNTIME_LAUNCH_CONTEXT context;
    VMX_INSTRUCTION_REPORTS reports;
    VMX_LAUNCH_PROBE_MAP_INPUT input;
    size_t index;
    for (index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        SetValidationStage(&context, &reports, cases[index].stage);
        InitValidationInput(&input, &context, &reports, cases[index].stage);
        context.ProtocolStep = cases[index].step;
        context.ProbeExit.Observed = cases[index].probe_observed;
        context.ProbeExit.ValidationMask = cases[index].probe_mask;
        context.ShutdownExit.Observed = cases[index].shutdown_observed;
        context.ShutdownExit.ValidationMask = cases[index].shutdown_mask;
        reports.VmresumeOutcome = cases[index].resume;
        context.Assembly.ResumeOutcome = (uint32_t)cases[index].resume;
        TEST_ASSERT_TRUE(IoctlValidateLaunchProbeMapInput(&input) == cases[index].valid,
                         "protocol matrix");
    }
    return 0;
}

static void InitMapperInput(
    VMX_LAUNCH_PROBE_MAP_INPUT* input,
    VMX_RUNTIME_LAUNCH_CONTEXT* context,
    VMX_INSTRUCTION_REPORTS* reports,
    VMX_LIFECYCLE_STAGE stage)
{
    SetValidationStage(context, reports, stage);
    InitValidationInput(input, context, reports, stage);
}

static void SetMapperFullSuccess(
    VMX_LAUNCH_PROBE_MAP_INPUT* input,
    VMX_RUNTIME_LAUNCH_CONTEXT* context,
    VMX_INSTRUCTION_REPORTS* reports)
{
    InitMapperInput(input, context, reports, VMX_STAGE_VMRESUME);
    SetValidationOutcomes(context, reports,
                          VMX_INSTR_SUCCESS, VMX_INSTR_SUCCESS,
                          VMX_INSTR_SUCCESS, VMX_VMWRITE_SUCCESS,
                          VMX_INSTR_SUCCESS, VMX_INSTR_SUCCESS,
                          VMX_INSTR_SUCCESS, VMX_INSTR_SUCCESS,
                          VMX_INSTR_SUCCESS);
    context->Assembly.VmxoffOutcome = VMX_INSTR_SUCCESS;
    context->ProbeExit.Observed = true;
    context->ProbeExit.ValidationMask = 0x03FFu;
    context->ShutdownExit.Observed = true;
    context->ShutdownExit.ValidationMask = 0x01FFu;
    context->ProtocolStep = VMX_PROTOCOL_SHUTDOWN_COMPLETE;
    context->OriginalCr0 = 0x80050033;
    context->FinalCr0 = 0x80050033;
    context->OriginalCr4 = 0x2000;
    context->FinalCr4 = 0x2000;
}

static int test_map_launch_probe_response_success(void)
{
    VMX_LAUNCH_PROBE_MAP_INPUT input;
    VMX_RUNTIME_LAUNCH_CONTEXT context;
    VMX_INSTRUCTION_REPORTS reports;
    VMX_LAUNCH_PROBE_RESPONSE_V2 response;

    SetMapperFullSuccess(&input, &context, &reports);
    input.ResourceMask = 0x3Fu;
    TEST_ASSERT_TRUE(IoctlMapLaunchProbeResponse(&input, &response),
                     "mapper full success");
    TEST_ASSERT_UINT_EQ(response.Version, VMX_LAUNCH_PROBE_VERSION_V2, "mapper version");
    TEST_ASSERT_UINT_EQ(response.Size, VMX_LAUNCH_PROBE_RESPONSE_V2_SIZE, "mapper size");
    TEST_ASSERT_UINT_EQ(response.DomainStatus, VMX_DOMAIN_SUCCESS, "mapper domain");
    TEST_ASSERT_INT_EQ(response.NtStatus, 0, "mapper status");
    TEST_ASSERT_UINT_EQ(response.RuntimeStage, VMX_STAGE_VMRESUME, "mapper stage");
    TEST_ASSERT_UINT_EQ(response.ResourceMask, 0x3Fu, "mapper resources");
    TEST_ASSERT_UINT_EQ(response.CleanupRequirement, VMX_CLEANUP_NONE, "mapper cleanup");
    TEST_ASSERT_UINT_EQ(response.CleanupOwner, VMX_CLEANUP_OWNER_NONE, "mapper owner");
    TEST_ASSERT_UINT_EQ(response.Flags,
                        VMX_FLAG_PROBE_OBSERVED | VMX_FLAG_SHUTDOWN_OBSERVED |
                        VMX_FLAG_CR0_RESTORED | VMX_FLAG_CR4_RESTORED |
                        VMX_FLAG_RESOURCES_RELEASED | VMX_FLAG_PROTOCOL_VALID,
                        "mapper flags");
    TEST_ASSERT_UINT_EQ(response.Vmxon.Outcome, VMX_INSTR_SUCCESS, "mapper outcome");
    TEST_ASSERT_UINT_EQ(response.ProcessorReserved, 0, "mapper processor reserved");
    TEST_ASSERT_UINT_EQ(response.Vmxon.Reserved, 0, "mapper report reserved");
    TEST_ASSERT_UINT_EQ(sizeof(response), VMX_LAUNCH_PROBE_RESPONSE_V2_SIZE, "mapper response size");
    return 0;
}

static int test_map_launch_probe_response_preflight(void)
{
    VMX_LAUNCH_PROBE_MAP_INPUT input = {0};
    VMX_LAUNCH_PROBE_RESPONSE_V2 response;
    input.PreflightResult = VMX_PREFLIGHT_CET_ACTIVE;
    input.TransientStage = VMX_STAGE_NOT_STARTED;
    input.ResourceMask = 0x3Fu;
    input.CleanupRequirement = VMX_CLEANUP_REQUIRED;
    input.CleanupOwner = VMX_CLEANUP_OWNER_C;
    TEST_ASSERT_TRUE(IoctlMapLaunchProbeResponse(&input, &response),
                     "mapper CET preflight");
    TEST_ASSERT_UINT_EQ(response.Version, 2, "mapper CET version");
    TEST_ASSERT_UINT_EQ(response.Size, 280, "mapper CET size");
    TEST_ASSERT_UINT_EQ(response.DomainStatus, VMX_DOMAIN_REJECTED, "mapper CET domain");
    TEST_ASSERT_INT_EQ(response.NtStatus, (int32_t)0xC00000BB, "mapper CET status");
    TEST_ASSERT_UINT_EQ(response.RuntimeStage, VMX_STAGE_CET_PREFLIGHT, "mapper CET stage");
    TEST_ASSERT_UINT_EQ(response.ResourceMask, 0x3Fu, "mapper CET resources");
    TEST_ASSERT_UINT_EQ(response.CleanupRequirement, VMX_CLEANUP_REQUIRED, "mapper CET cleanup");
    TEST_ASSERT_UINT_EQ(response.CleanupOwner, VMX_CLEANUP_OWNER_C, "mapper CET owner");
    TEST_ASSERT_UINT_EQ(response.Flags, VMX_FLAG_CET_REJECTED, "mapper CET flags");
    (void)memset(&input, 0, sizeof(input));
    input.PreflightResult = VMX_PREFLIGHT_NOT_PASSIVE_LEVEL;
    input.TransientStage = VMX_STAGE_NOT_STARTED;
    input.ResourcesReleased = true;
    TEST_ASSERT_TRUE(IoctlMapLaunchProbeResponse(&input, &response),
                     "mapper rejected preflight");
    TEST_ASSERT_UINT_EQ(response.DomainStatus, VMX_DOMAIN_REJECTED, "mapper reject domain");
    TEST_ASSERT_INT_EQ(response.NtStatus, (int32_t)0xC00000BB, "mapper reject status");
    TEST_ASSERT_UINT_EQ(response.RuntimeStage, VMX_STAGE_PASSIVE_LEVEL, "mapper reject stage");
    TEST_ASSERT_UINT_EQ(response.Flags, VMX_FLAG_RESOURCES_RELEASED, "mapper release flag");
    return 0;
}

static int test_map_launch_probe_response_failures(void)
{
    const struct {
        VMX_LIFECYCLE_STAGE stage;
        int kind;
        int32_t status;
    } cases[] = {
        { VMX_STAGE_INSTRUCTION_INTERVAL, 0, (int32_t)0xC00000BB },
        { VMX_STAGE_VMXON, 1, (int32_t)0xC0000001 },
        { VMX_STAGE_HOST_PLAN, 2, (int32_t)0xC0000001 },
        { VMX_STAGE_VMRESUME, 3, (int32_t)0xC0000001 },
        { VMX_STAGE_VMRESUME, 4, (int32_t)0xC0000001 }
    };
    VMX_LAUNCH_PROBE_MAP_INPUT input;
    VMX_RUNTIME_LAUNCH_CONTEXT context;
    VMX_INSTRUCTION_REPORTS reports;
    VMX_LAUNCH_PROBE_RESPONSE_V2 response;
    size_t index;
    for (index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        InitMapperInput(&input, &context, &reports, cases[index].stage);
        if (cases[index].kind == 3) {
            context.ProtocolStep = VMX_PROTOCOL_PROBE_COMPLETE;
            context.ProbeExit.ValidationMask = 0x03FFu;
            reports.VmresumeOutcome = VMX_INSTR_FAIL_VALID;
            context.Assembly.ResumeOutcome = VMX_INSTR_FAIL_VALID;
        } else if (cases[index].kind == 4) {
            context.ProtocolStep = VMX_PROTOCOL_PROBE_COMPLETE;
            context.ProbeExit.ValidationMask = 0x03FFu;
            reports.VmresumeOutcome = VMX_INSTR_SUCCESS;
            context.Assembly.ResumeOutcome = VMX_INSTR_SUCCESS;
            reports.VmxoffOutcome = VMX_INSTR_FAIL_INVALID;
            context.Assembly.VmxoffOutcome = VMX_INSTR_FAIL_INVALID;
        }
        TEST_ASSERT_TRUE(IoctlMapLaunchProbeResponse(&input, &response),
                         "mapper valid failure");
        TEST_ASSERT_UINT_EQ(response.DomainStatus, VMX_DOMAIN_FAILED, "mapper failure domain");
        TEST_ASSERT_INT_EQ(response.NtStatus, cases[index].status, "mapper failure status");
        TEST_ASSERT_UINT_EQ(response.RuntimeStage, cases[index].stage, "mapper failure stage");
    }
    return 0;
}

static int test_map_launch_probe_response_partial(void)
{
    VMX_LAUNCH_PROBE_MAP_INPUT input;
    VMX_RUNTIME_LAUNCH_CONTEXT context;
    VMX_INSTRUCTION_REPORTS reports;
    VMX_LAUNCH_PROBE_RESPONSE_V2 response;

    InitMapperInput(&input, &context, &reports, VMX_STAGE_VMRESUME);
    context.ProtocolStep = VMX_PROTOCOL_NONE;
    context.ProbeExit.Observed = true;
    context.ProbeExit.ValidationMask = 0;
    TEST_ASSERT_TRUE(IoctlMapLaunchProbeResponse(&input, &response),
                     "mapper invalid probe");
    TEST_ASSERT_UINT_EQ(response.DomainStatus, VMX_DOMAIN_FAILED, "mapper probe domain");
    TEST_ASSERT_UINT_EQ(response.Flags,
                        VMX_FLAG_PROBE_OBSERVED | VMX_FLAG_CR0_RESTORED |
                        VMX_FLAG_CR4_RESTORED | VMX_FLAG_RESOURCES_RELEASED,
                        "mapper probe flags");
    TEST_ASSERT_TRUE((response.Flags & VMX_FLAG_PROTOCOL_VALID) == 0,
                     "mapper probe protocol flag");

    SetMapperFullSuccess(&input, &context, &reports);
    context.ProtocolStep = VMX_PROTOCOL_PROBE_COMPLETE;
    context.ShutdownExit.ValidationMask = 1;
    TEST_ASSERT_TRUE(IoctlMapLaunchProbeResponse(&input, &response),
                     "mapper invalid shutdown");
    TEST_ASSERT_UINT_EQ(response.DomainStatus, VMX_DOMAIN_FAILED, "mapper shutdown domain");
    TEST_ASSERT_TRUE((response.Flags & VMX_FLAG_PROBE_OBSERVED) != 0,
                     "mapper shutdown probe flag");
    TEST_ASSERT_TRUE((response.Flags & VMX_FLAG_SHUTDOWN_OBSERVED) != 0,
                     "mapper shutdown observed flag");
    TEST_ASSERT_TRUE((response.Flags & VMX_FLAG_PROTOCOL_VALID) == 0,
                     "mapper shutdown protocol flag");
    return 0;
}

static int test_map_launch_probe_response_structural_failure(void)
{
    VMX_LAUNCH_PROBE_MAP_INPUT input;
    VMX_RUNTIME_LAUNCH_CONTEXT context;
    VMX_INSTRUCTION_REPORTS reports;
    VMX_LAUNCH_PROBE_RESPONSE_V2 response;

    SetMapperFullSuccess(&input, &context, &reports);
    context.OriginalCr0++;
    TEST_ASSERT_TRUE(IoctlMapLaunchProbeResponse(&input, &response),
                     "mapper CR mismatch");
    TEST_ASSERT_UINT_EQ(response.DomainStatus, VMX_DOMAIN_FAILED, "mapper CR domain");
    TEST_ASSERT_TRUE((response.Flags & VMX_FLAG_CR0_RESTORED) == 0,
                     "mapper CR0 flag");
    TEST_ASSERT_TRUE((response.Flags & VMX_FLAG_CR4_RESTORED) != 0,
                     "mapper CR4 flag");

    SetMapperFullSuccess(&input, &context, &reports);
    input.ResourcesReleased = false;
    input.CleanupRequirement = VMX_CLEANUP_REQUIRED;
    input.CleanupOwner = VMX_CLEANUP_OWNER_C;
    input.ResourceMask = VMX_RESOURCE_CONTEXT;
    TEST_ASSERT_TRUE(IoctlMapLaunchProbeResponse(&input, &response),
                     "mapper resources outstanding");
    TEST_ASSERT_UINT_EQ(response.DomainStatus, VMX_DOMAIN_FAILED, "mapper resource domain");
    TEST_ASSERT_UINT_EQ(response.CleanupRequirement, VMX_CLEANUP_REQUIRED,
                        "mapper resource cleanup");
    TEST_ASSERT_UINT_EQ(response.CleanupOwner, VMX_CLEANUP_OWNER_C, "mapper resource owner");
    TEST_ASSERT_TRUE((response.Flags & VMX_FLAG_RESOURCES_RELEASED) == 0,
                     "mapper resource release flag");
    return 0;
}

static int test_map_launch_probe_response_rejects_invalid(void)
{
    VMX_LAUNCH_PROBE_MAP_INPUT input;
    VMX_RUNTIME_LAUNCH_CONTEXT context;
    VMX_INSTRUCTION_REPORTS reports;
    VMX_LAUNCH_PROBE_RESPONSE_V2 response;

    InitMapperInput(&input, &context, &reports, VMX_STAGE_VMRESUME);
    input.ResourceMask = 1u << 6;
    (void)memset(&response, 0xA5, sizeof(response));
    TEST_ASSERT_TRUE(!IoctlMapLaunchProbeResponse(&input, &response),
                     "mapper resource reject");
    TEST_ASSERT_TRUE(CheckedResponseIsZero(&response), "mapper resource zero");

    {
        const uint32_t invalid_vmxoff[] = {
            VMX_INSTR_NOT_ATTEMPTED, VMX_INSTR_FAIL_INVALID
        };
        size_t index;
        for (index = 0; index < sizeof(invalid_vmxoff) / sizeof(invalid_vmxoff[0]); ++index) {
            SetMapperFullSuccess(&input, &context, &reports);
            reports.VmxoffOutcome = VMX_INSTR_SUCCESS;
            context.Assembly.VmxoffOutcome = invalid_vmxoff[index];
            (void)memset(&response, 0xA5, sizeof(response));
            TEST_ASSERT_TRUE(!IoctlValidateLaunchProbeMapInput(&input),
                             "mapper vmxoff mismatch validator");
            TEST_ASSERT_TRUE(!IoctlMapLaunchProbeResponse(&input, &response),
                             "mapper vmxoff mismatch");
            TEST_ASSERT_TRUE(CheckedResponseIsZero(&response),
                             "mapper vmxoff mismatch zero");
        }
    }

    (void)memset(&response, 0xA5, sizeof(response));
    TEST_ASSERT_TRUE(!IoctlMapLaunchProbeResponse(NULL, &response),
                     "mapper null input");
    TEST_ASSERT_TRUE(CheckedResponseIsZero(&response), "mapper null input zero");
    TEST_ASSERT_TRUE(!IoctlMapLaunchProbeResponse(NULL, NULL), "mapper null response");

    TEST_ASSERT_UINT_EQ(sizeof(response), 280, "mapper fixed size");
    return 0;
}

/*===========================================================================
 * D2b3b-ii lifecycle seams
 *===========================================================================*/
enum { FAKE_CREATE = 1, FAKE_EXECUTE, FAKE_DESTROY, FAKE_MAP };
typedef struct _FAKE_LAUNCH_HARNESS {
    int Events[8];
    size_t EventCount;
    unsigned CreateCount, ExecuteCount, DestroyCount, MapCount;
    VMX_PREFLIGHT_RESULT CreateResult;
    VMX_LIFECYCLE_STAGE ExecuteStage;
    bool CreateContext, CreatePinned, CreateSerialized;
    bool ExecuteFailure, DestroyIncomplete, MapResult, CheckSnapshot;
    bool DestroyPoisoned, MapSawSnapshot;
    uint32_t ExpectedResourceMask, MappedResourceMask;
    bool MappedResourcesReleased;
    VMX_CLEANUP_REQUIREMENT MappedCleanupRequirement;
    VMX_CLEANUP_OWNER MappedCleanupOwner;
    VMX_PREFLIGHT_RESULT MappedPreflightResult;
    VMX_RUNTIME_LAUNCH_CONTEXT Context;
} FAKE_LAUNCH_HARNESS;

static FAKE_LAUNCH_HARNESS g_fake_launch;
static VMX_LAUNCH_PROBE_LIFECYCLE_OPERATIONS* g_mutable_lifecycle_operations;
static unsigned g_lifecycle_poison_count;
static const VMX_PREFLIGHT_PLATFORM g_fake_preflight = { 0 };
static const VMX_ALLOCATION_INTERFACE g_fake_allocation = { 0 };
static const VMX_TRANSIENT_PLATFORM g_fake_transient = { 0 };

static VMX_LIFECYCLE_STAGE PoisonExecuteTransient(
    const VMX_TRANSIENT_PLATFORM* platform,
    VMX_RUNTIME_LAUNCH_CONTEXT* context,
    VMX_INSTRUCTION_REPORTS* reports);
static void PoisonDestroyContext(
    const VMX_PREFLIGHT_PLATFORM* platform,
    const VMX_ALLOCATION_INTERFACE* allocation,
    VMX_RUNTIME_LAUNCH_CONTEXT** context,
    VMX_PREFLIGHT_OWNERSHIP* ownership);
static bool PoisonMapResponse(const VMX_LAUNCH_PROBE_MAP_INPUT* input,
    VMX_LAUNCH_PROBE_RESPONSE_V2* response);

static void FakeRecordEvent(int event)
{
    if (g_fake_launch.EventCount < sizeof(g_fake_launch.Events) / sizeof(g_fake_launch.Events[0])) {
        g_fake_launch.Events[g_fake_launch.EventCount++] = event;
    }
}

static void FakeLaunchReset(void)
{
    (void)memset(&g_fake_launch, 0, sizeof(g_fake_launch));
    g_mutable_lifecycle_operations = NULL;
    g_lifecycle_poison_count = 0;
    g_fake_launch.CreateResult = VMX_PREFLIGHT_SUCCESS;
    g_fake_launch.CreateContext = true;
    g_fake_launch.ExecuteStage = VMX_STAGE_VMRESUME;
    g_fake_launch.MapResult = true;
}

static uint32_t FakeExitDispatcher(struct VMX_LAUNCH_CONTEXT* context, VMX_GUEST_GPRS* gprs)
{
    (void)context;
    (void)gprs;
    return VMX_EXIT_ACTION_OFFLINE;
}

static void FakePublishDiagnostics(VMX_RUNTIME_LAUNCH_CONTEXT* context,
                                   VMX_INSTRUCTION_REPORTS* reports)
{
    VMX_EXIT_DISPATCH_ROUTINE dispatcher = context->Assembly.ExitDispatcher;
    (void)memset(context, 0, sizeof(*context));
    context->Assembly.ExitDispatcher = dispatcher;
    context->ProcessorGroup = 0x1234;
    context->ProcessorNumber = 7;
    context->OriginalCr0 = 0x1111222233334444ULL;
    context->FinalCr0 = context->OriginalCr0;
    context->OriginalCr4 = 0x5555666677778888ULL;
    context->FinalCr4 = context->OriginalCr4;
    context->VmxonPa = 0x100000ULL;
    context->VmcsPa = 0x200000ULL;
    context->VmxBasic = 0xABCDEF0123456789ULL;
    context->RevisionId = 0x10203040U;
    context->VmcsSize = 0x1000U;
    context->OwnedResources = VMX_RESOURCE_CONTEXT_BIT |
                              VMX_RESOURCE_HOST_STACK_BIT |
                              VMX_RESOURCE_VMXON_REGION_BIT |
                              VMX_RESOURCE_VMCS_REGION_BIT;
    context->Assembly.Cookie = 0xCAFEBABE11223344ULL;
    context->Assembly.LaunchOutcome = VMX_INSTR_SUCCESS;
    context->Assembly.ResumeOutcome = VMX_INSTR_SUCCESS;
    context->Assembly.VmxoffOutcome = VMX_INSTR_SUCCESS;
    context->Assembly.GuestRspWriteOutcome = VMX_INSTR_SUCCESS;
    context->Assembly.GuestRflagsWriteOutcome = VMX_INSTR_SUCCESS;
    context->ProbeExit.Observed = true;
    context->ProbeExit.ValidationMask = 0x03FFU;
    context->ProbeExit.Report.Reason = 0x41U;
    context->ProbeExit.Report.InstructionLength = 3U;
    context->ProbeExit.Report.Rip = 0x700000ULL;
    context->ShutdownExit.Observed = true;
    context->ShutdownExit.ValidationMask = 0x01FFU;
    context->ShutdownExit.Report.Reason = 0x42U;
    context->ShutdownExit.Report.InstructionLength = 4U;
    context->ShutdownExit.Report.Rip = 0x700100ULL;
    context->ProtocolStep = VMX_PROTOCOL_SHUTDOWN_COMPLETE;
    (void)memset(reports, 0, sizeof(*reports));
    reports->VmxonOutcome = VMX_INSTR_SUCCESS;
    reports->VmclearOutcome = VMX_INSTR_SUCCESS;
    reports->VmptrldOutcome = VMX_INSTR_SUCCESS;
    reports->VmwriteOutcome = VMX_VMWRITE_SUCCESS;
    reports->GuestRspOutcome = VMX_INSTR_SUCCESS;
    reports->GuestRflagsOutcome = VMX_INSTR_SUCCESS;
    reports->VmlaunchOutcome = VMX_INSTR_SUCCESS;
    reports->VmresumeOutcome = VMX_INSTR_SUCCESS;
    reports->VmxoffOutcome = VMX_INSTR_SUCCESS;
    g_fake_launch.ExpectedResourceMask = VMX_RESOURCE_CONTEXT | VMX_RESOURCE_STACK |
                                          VMX_RESOURCE_VMXON_PAGE | VMX_RESOURCE_VMCS_PAGE |
                                          VMX_RESOURCE_SERIALIZED | VMX_RESOURCE_AFFINITY;
}

static VMX_PREFLIGHT_RESULT FakeCreateContext(
    const VMX_PREFLIGHT_PLATFORM* platform,
    const VMX_ALLOCATION_INTERFACE* allocation,
    VMX_RUNTIME_LAUNCH_CONTEXT** context,
    VMX_PREFLIGHT_OWNERSHIP* ownership)
{
    (void)platform;
    (void)allocation;
    ++g_fake_launch.CreateCount;
    FakeRecordEvent(FAKE_CREATE);
    if (g_mutable_lifecycle_operations != NULL) {
        g_mutable_lifecycle_operations->ExecuteTransient =
            PoisonExecuteTransient;
        g_mutable_lifecycle_operations->DestroyContext =
            PoisonDestroyContext;
        g_mutable_lifecycle_operations->MapResponse = PoisonMapResponse;
    }
    *context = NULL;
    (void)memset(ownership, 0, sizeof(*ownership));
    if (g_fake_launch.CreateContext) {
        (void)memset(&g_fake_launch.Context, 0, sizeof(g_fake_launch.Context));
        *context = &g_fake_launch.Context;
    }
    ownership->CpuPinned = g_fake_launch.CreatePinned;
    ownership->SerializationHeld = g_fake_launch.CreateSerialized;
    return g_fake_launch.CreateResult;
}

static VMX_LIFECYCLE_STAGE FakeExecuteTransient(
    const VMX_TRANSIENT_PLATFORM* platform,
    VMX_RUNTIME_LAUNCH_CONTEXT* context,
    VMX_INSTRUCTION_REPORTS* reports)
{
    (void)platform;
    ++g_fake_launch.ExecuteCount;
    FakeRecordEvent(FAKE_EXECUTE);
    FakePublishDiagnostics(context, reports);
    if (g_fake_launch.ExecuteFailure) {
        context->Assembly.ResumeOutcome = VMX_INSTR_FAIL_VALID;
        reports->VmresumeOutcome = VMX_INSTR_FAIL_VALID;
        context->ProtocolStep = VMX_PROTOCOL_PROBE_COMPLETE;
        context->ShutdownExit.Observed = false;
        context->ShutdownExit.ValidationMask = 0;
    }
    return g_fake_launch.ExecuteStage;
}

static void FakeDestroyContext(
    const VMX_PREFLIGHT_PLATFORM* platform,
    const VMX_ALLOCATION_INTERFACE* allocation,
    VMX_RUNTIME_LAUNCH_CONTEXT** context,
    VMX_PREFLIGHT_OWNERSHIP* ownership)
{
    (void)platform;
    (void)allocation;
    ++g_fake_launch.DestroyCount;
    FakeRecordEvent(FAKE_DESTROY);
    if (*context != NULL) {
        (void)memset(*context, 0xA5, sizeof(**context));
        g_fake_launch.DestroyPoisoned = true;
        if (!g_fake_launch.DestroyIncomplete) {
            *context = NULL;
        }
    }
    if (!g_fake_launch.DestroyIncomplete) {
        ownership->CpuPinned = false;
        ownership->SerializationHeld = false;
    }
}

static bool FakeMapResponse(const VMX_LAUNCH_PROBE_MAP_INPUT* input,
                            VMX_LAUNCH_PROBE_RESPONSE_V2* response)
{
    ++g_fake_launch.MapCount;
    FakeRecordEvent(FAKE_MAP);
    g_fake_launch.MappedResourceMask = input->ResourceMask;
    g_fake_launch.MappedResourcesReleased = input->ResourcesReleased;
    g_fake_launch.MappedCleanupRequirement = input->CleanupRequirement;
    g_fake_launch.MappedCleanupOwner = input->CleanupOwner;
    g_fake_launch.MappedPreflightResult = input->PreflightResult;
    if (g_fake_launch.CheckSnapshot) {
        g_fake_launch.MapSawSnapshot = true;
        TEST_ASSERT_UINT_EQ(input->Context->ProcessorGroup, 0x1234, "snapshot processor group");
        TEST_ASSERT_UINT_EQ(input->Context->ProcessorNumber, 7, "snapshot processor number");
        TEST_ASSERT_UINT_EQ(input->Context->VmxonPa, 0x100000, "snapshot VMXON PA");
        TEST_ASSERT_UINT_EQ(input->Context->VmxBasic, 0xABCDEF0123456789ULL, "snapshot VMX basic");
        TEST_ASSERT_UINT_EQ(input->Context->Assembly.Cookie, 0xCAFEBABE11223344ULL, "snapshot cookie");
        TEST_ASSERT_UINT_EQ(input->Context->ProtocolStep,
                            g_fake_launch.ExecuteFailure ? VMX_PROTOCOL_PROBE_COMPLETE :
                            VMX_PROTOCOL_SHUTDOWN_COMPLETE, "snapshot protocol");
        TEST_ASSERT_NULL(input->Context->Assembly.ExitDispatcher, "snapshot dispatcher cleared");
        TEST_ASSERT_UINT_EQ(input->Context->OwnedResources, 0, "snapshot ownership omitted");
        TEST_ASSERT_TRUE(g_fake_launch.DestroyPoisoned, "mapper runs after poison");
    }
    if (g_fake_launch.ExecuteFailure) {
        TEST_ASSERT_UINT_EQ(input->TransientStage, g_fake_launch.ExecuteStage, "failure stage mapped");
        TEST_ASSERT_INT_EQ(input->Reports->VmresumeOutcome, VMX_INSTR_FAIL_VALID, "failure report mapped");
    }
    if (g_fake_launch.MapResult) {
        response->Version = VMX_LAUNCH_PROBE_VERSION_V2;
        response->Size = VMX_LAUNCH_PROBE_RESPONSE_V2_SIZE;
        response->DomainStatus = g_fake_launch.ExecuteFailure ||
                                input->PreflightResult != VMX_PREFLIGHT_SUCCESS ||
                                !input->ResourcesReleased ? VMX_DOMAIN_FAILED : VMX_DOMAIN_SUCCESS;
    }
    return g_fake_launch.MapResult;
}

static VMX_LIFECYCLE_STAGE PoisonExecuteTransient(
    const VMX_TRANSIENT_PLATFORM* platform,
    VMX_RUNTIME_LAUNCH_CONTEXT* context,
    VMX_INSTRUCTION_REPORTS* reports)
{
    (void)platform;
    (void)context;
    (void)reports;
    ++g_lifecycle_poison_count;
    return VMX_STAGE_NOT_STARTED;
}

static void PoisonDestroyContext(
    const VMX_PREFLIGHT_PLATFORM* platform,
    const VMX_ALLOCATION_INTERFACE* allocation,
    VMX_RUNTIME_LAUNCH_CONTEXT** context,
    VMX_PREFLIGHT_OWNERSHIP* ownership)
{
    (void)platform;
    (void)allocation;
    (void)context;
    (void)ownership;
    ++g_lifecycle_poison_count;
}

static bool PoisonMapResponse(const VMX_LAUNCH_PROBE_MAP_INPUT* input,
    VMX_LAUNCH_PROBE_RESPONSE_V2* response)
{
    (void)input;
    (void)response;
    ++g_lifecycle_poison_count;
    return false;
}

static VMX_LAUNCH_PROBE_LIFECYCLE_OPERATIONS FakeOperations(void)
{
    VMX_LAUNCH_PROBE_LIFECYCLE_OPERATIONS operations = {
        FakeCreateContext, FakeExecuteTransient, FakeDestroyContext, FakeMapResponse,
        &g_fake_preflight, &g_fake_allocation, &g_fake_transient, FakeExitDispatcher
    };
    return operations;
}

static int test_launch_probe_lifecycle_input_spans(void)
{
    union LIFECYCLE_ALIAS_STORAGE {
        VMX_LAUNCH_PROBE_LIFECYCLE_OPERATIONS Operations;
        VMX_LAUNCH_PROBE_RESPONSE_V2 Response;
        unsigned char Bytes[sizeof(VMX_LAUNCH_PROBE_LIFECYCLE_OPERATIONS) +
            sizeof(VMX_LAUNCH_PROBE_RESPONSE_V2) + 16];
    } storage, before;
    VMX_LAUNCH_PROBE_LIFECYCLE_OPERATIONS operations = FakeOperations();
    VMX_LAUNCH_PROBE_RESPONSE_V2 response;

    FakeLaunchReset();
    (void)memset(&storage, 0xA5, sizeof(storage));
    before = storage;
    TEST_ASSERT_FALSE(IoctlRunLaunchProbeLifecycle(
        (const VMX_LAUNCH_PROBE_LIFECYCLE_OPERATIONS*)storage.Bytes,
        (VMX_LAUNCH_PROBE_RESPONSE_V2*)storage.Bytes),
        "exact lifecycle overlap rejected");
    TEST_ASSERT_TRUE(memcmp(&storage, &before, sizeof(storage)) == 0,
        "exact lifecycle overlap preserves");

    (void)memset(&storage, 0x5A, sizeof(storage));
    before = storage;
    TEST_ASSERT_FALSE(IoctlRunLaunchProbeLifecycle(
        (const VMX_LAUNCH_PROBE_LIFECYCLE_OPERATIONS*)storage.Bytes,
        (VMX_LAUNCH_PROBE_RESPONSE_V2*)(storage.Bytes + 8)),
        "partial lifecycle overlap rejected");
    TEST_ASSERT_TRUE(memcmp(&storage, &before, sizeof(storage)) == 0,
        "partial lifecycle overlap preserves");

    (void)memset(&response, 0x3C, sizeof(response));
    TEST_ASSERT_FALSE(IoctlRunLaunchProbeLifecycle(
        (const VMX_LAUNCH_PROBE_LIFECYCLE_OPERATIONS*)
            (UINTPTR_MAX - sizeof(VMX_LAUNCH_PROBE_LIFECYCLE_OPERATIONS) + 2u),
        &response), "wrapping operations rejected");
    TEST_ASSERT_UINT_EQ(response.Version, 0x3C3C3C3Cu,
        "wrapping operations preserves response");
    TEST_ASSERT_FALSE(IoctlRunLaunchProbeLifecycle(&operations,
        (VMX_LAUNCH_PROBE_RESPONSE_V2*)
            (UINTPTR_MAX - sizeof(VMX_LAUNCH_PROBE_RESPONSE_V2) + 2u)),
        "wrapping response rejected");
    TEST_ASSERT_UINT_EQ(g_fake_launch.EventCount, 0,
        "span rejection no lifecycle callback");
    return 0;
}

static int test_launch_probe_lifecycle_snapshot(void)
{
    VMX_LAUNCH_PROBE_LIFECYCLE_OPERATIONS operations;
    VMX_LAUNCH_PROBE_RESPONSE_V2 response;

    FakeLaunchReset();
    operations = FakeOperations();
    g_fake_launch.CreatePinned = true;
    g_fake_launch.CreateSerialized = true;
    g_mutable_lifecycle_operations = &operations;
    TEST_ASSERT_TRUE(IoctlRunLaunchProbeLifecycle(&operations, &response),
        "frozen lifecycle completes");
    TEST_ASSERT_TRUE(operations.ExecuteTransient == PoisonExecuteTransient &&
        operations.DestroyContext == PoisonDestroyContext &&
        operations.MapResponse == PoisonMapResponse,
        "caller lifecycle table was mutated");
    TEST_ASSERT_UINT_EQ(g_lifecycle_poison_count, 0,
        "lifecycle snapshot blocks poison");
    TEST_ASSERT_UINT_EQ(g_fake_launch.EventCount, 4,
        "snapshot retains lifecycle order");
    TEST_ASSERT_TRUE(g_fake_launch.MappedResourcesReleased,
        "snapshot retains cleanup");
    return 0;
}

static int test_launch_probe_lifecycle_contract(void)
{
    VMX_LAUNCH_PROBE_LIFECYCLE_OPERATIONS operations;
    VMX_LAUNCH_PROBE_RESPONSE_V2 response;
    size_t index;

    FakeLaunchReset();
    (void)memset(&response, 0xA5, sizeof(response));
    TEST_ASSERT_FALSE(IoctlRunLaunchProbeLifecycle(NULL, &response), "null operations rejected");
    TEST_ASSERT_TRUE(CheckedResponseIsZero(&response), "null operations zero response");
    TEST_ASSERT_UINT_EQ(g_fake_launch.EventCount, 0, "null operations callbacks");
    FakeLaunchReset();
    TEST_ASSERT_FALSE(IoctlRunLaunchProbeLifecycle(&operations, NULL), "null response rejected");
    TEST_ASSERT_UINT_EQ(g_fake_launch.EventCount, 0, "null response callbacks");

    for (index = 0; index < 8; ++index) {
        FakeLaunchReset();
        operations = FakeOperations();
        switch (index) {
        case 0: operations.CreateContext = NULL; break;
        case 1: operations.ExecuteTransient = NULL; break;
        case 2: operations.DestroyContext = NULL; break;
        case 3: operations.MapResponse = NULL; break;
        case 4: operations.PreflightPlatform = NULL; break;
        case 5: operations.AllocationInterface = NULL; break;
        case 6: operations.TransientPlatform = NULL; break;
        default: operations.ExitDispatcher = NULL; break;
        }
        (void)memset(&response, 0xA5, sizeof(response));
        TEST_ASSERT_FALSE(IoctlRunLaunchProbeLifecycle(&operations, &response), "invalid operation rejected");
        TEST_ASSERT_TRUE(CheckedResponseIsZero(&response), "invalid operation zero response");
        TEST_ASSERT_UINT_EQ(g_fake_launch.EventCount, 0, "invalid operation callbacks");
    }

    FakeLaunchReset();
    operations = FakeOperations();
    g_fake_launch.CreatePinned = true;
    g_fake_launch.CreateSerialized = true;
    g_fake_launch.CheckSnapshot = true;
    TEST_ASSERT_TRUE(IoctlRunLaunchProbeLifecycle(&operations, &response), "full lifecycle");
    TEST_ASSERT_UINT_EQ(g_fake_launch.EventCount, 4, "full lifecycle event count");
    TEST_ASSERT_UINT_EQ(g_fake_launch.Events[0], FAKE_CREATE, "create first");
    TEST_ASSERT_UINT_EQ(g_fake_launch.Events[1], FAKE_EXECUTE, "execute second");
    TEST_ASSERT_UINT_EQ(g_fake_launch.Events[2], FAKE_DESTROY, "destroy third");
    TEST_ASSERT_UINT_EQ(g_fake_launch.Events[3], FAKE_MAP, "map last");
    TEST_ASSERT_UINT_EQ(g_fake_launch.CreateCount, 1, "create once");
    TEST_ASSERT_UINT_EQ(g_fake_launch.ExecuteCount, 1, "execute once");
    TEST_ASSERT_UINT_EQ(g_fake_launch.DestroyCount, 1, "destroy once");
    TEST_ASSERT_UINT_EQ(g_fake_launch.MapCount, 1, "map once");
    TEST_ASSERT_TRUE(g_fake_launch.MapSawSnapshot, "snapshot observed");
    TEST_ASSERT_UINT_EQ(g_fake_launch.MappedResourceMask, g_fake_launch.ExpectedResourceMask, "resource high-water mask");
    TEST_ASSERT_TRUE(g_fake_launch.MappedResourcesReleased, "complete cleanup released");
    TEST_ASSERT_UINT_EQ(g_fake_launch.MappedCleanupRequirement, VMX_CLEANUP_NONE, "complete cleanup requirement");
    TEST_ASSERT_UINT_EQ(g_fake_launch.MappedCleanupOwner, VMX_CLEANUP_OWNER_NONE, "complete cleanup owner");

    FakeLaunchReset();
    operations = FakeOperations();
    g_fake_launch.CreateResult = VMX_PREFLIGHT_CONTEXT_FAILED;
    g_fake_launch.CreatePinned = true;
    g_fake_launch.CreateSerialized = true;
    TEST_ASSERT_TRUE(IoctlRunLaunchProbeLifecycle(&operations, &response), "preflight residual maps");
    TEST_ASSERT_UINT_EQ(g_fake_launch.ExecuteCount, 0, "preflight skips execute");
    TEST_ASSERT_UINT_EQ(g_fake_launch.DestroyCount, 1, "preflight residual destroys");
    TEST_ASSERT_UINT_EQ(g_fake_launch.MapCount, 1, "preflight residual maps once");
    TEST_ASSERT_UINT_EQ(g_fake_launch.EventCount, 3, "preflight residual event count");
    TEST_ASSERT_UINT_EQ(g_fake_launch.Events[0], FAKE_CREATE, "preflight residual create first");
    TEST_ASSERT_UINT_EQ(g_fake_launch.Events[1], FAKE_DESTROY, "preflight destroy before map");
    TEST_ASSERT_UINT_EQ(g_fake_launch.Events[2], FAKE_MAP, "preflight map after destroy");
    TEST_ASSERT_FALSE(response.DomainStatus == VMX_DOMAIN_SUCCESS, "preflight not success");

    FakeLaunchReset();
    operations = FakeOperations();
    g_fake_launch.CreateResult = VMX_PREFLIGHT_PIN_FAILED;
    g_fake_launch.CreateContext = false;
    TEST_ASSERT_TRUE(IoctlRunLaunchProbeLifecycle(&operations, &response), "preflight clean maps");
    TEST_ASSERT_UINT_EQ(g_fake_launch.ExecuteCount, 0, "clean preflight skips execute");
    TEST_ASSERT_UINT_EQ(g_fake_launch.DestroyCount, 0, "clean preflight skips destroy");
    TEST_ASSERT_UINT_EQ(g_fake_launch.MapCount, 1, "clean preflight maps once");
    TEST_ASSERT_UINT_EQ(g_fake_launch.EventCount, 2, "clean preflight event count");
    TEST_ASSERT_UINT_EQ(g_fake_launch.Events[0], FAKE_CREATE, "clean preflight create first");
    TEST_ASSERT_UINT_EQ(g_fake_launch.Events[1], FAKE_MAP, "clean preflight maps after create");

    FakeLaunchReset();
    operations = FakeOperations();
    g_fake_launch.CreateContext = false;
    TEST_ASSERT_TRUE(IoctlRunLaunchProbeLifecycle(&operations, &response), "success null normalized");
    TEST_ASSERT_UINT_EQ(g_fake_launch.CreateCount, 1, "null context create once");
    TEST_ASSERT_UINT_EQ(g_fake_launch.ExecuteCount, 0, "null context skips execute");
    TEST_ASSERT_UINT_EQ(g_fake_launch.DestroyCount, 0, "null context skips destroy");
    TEST_ASSERT_UINT_EQ(g_fake_launch.MapCount, 1, "null context maps once");
    TEST_ASSERT_UINT_EQ(g_fake_launch.EventCount, 2, "null context event count");
    TEST_ASSERT_UINT_EQ(g_fake_launch.Events[0], FAKE_CREATE, "null context create first");
    TEST_ASSERT_UINT_EQ(g_fake_launch.Events[1], FAKE_MAP, "null context maps after create");
    TEST_ASSERT_UINT_EQ(g_fake_launch.MappedPreflightResult, VMX_PREFLIGHT_CONTEXT_FAILED, "null context normalized result");

    FakeLaunchReset();
    operations = FakeOperations();
    g_fake_launch.ExecuteFailure = true;
    g_fake_launch.CheckSnapshot = true;
    TEST_ASSERT_TRUE(IoctlRunLaunchProbeLifecycle(&operations, &response), "transient failure maps");
    TEST_ASSERT_UINT_EQ(g_fake_launch.ExecuteCount, 1, "failure executes once");
    TEST_ASSERT_UINT_EQ(g_fake_launch.DestroyCount, 1, "failure destroys");
    TEST_ASSERT_UINT_EQ(g_fake_launch.MapCount, 1, "failure maps once");
    TEST_ASSERT_UINT_EQ(g_fake_launch.EventCount, 4, "failure event count");
    TEST_ASSERT_UINT_EQ(g_fake_launch.Events[0], FAKE_CREATE, "failure create first");
    TEST_ASSERT_UINT_EQ(g_fake_launch.Events[1], FAKE_EXECUTE, "failure execute second");
    TEST_ASSERT_UINT_EQ(g_fake_launch.Events[2], FAKE_DESTROY, "failure destroy third");
    TEST_ASSERT_UINT_EQ(g_fake_launch.Events[3], FAKE_MAP, "failure map last");
    TEST_ASSERT_UINT_EQ(response.DomainStatus, VMX_DOMAIN_FAILED, "failure cannot fake success");

    FakeLaunchReset();
    operations = FakeOperations();
    g_fake_launch.DestroyIncomplete = true;
    g_fake_launch.CreateSerialized = true;
    g_fake_launch.CheckSnapshot = true;
    TEST_ASSERT_TRUE(IoctlRunLaunchProbeLifecycle(&operations, &response), "incomplete cleanup maps");
    TEST_ASSERT_UINT_EQ(g_fake_launch.DestroyCount, 1, "incomplete destroys once");
    TEST_ASSERT_UINT_EQ(g_fake_launch.MapCount, 1, "incomplete maps once");
    TEST_ASSERT_UINT_EQ(g_fake_launch.EventCount, 4, "incomplete event count");
    TEST_ASSERT_UINT_EQ(g_fake_launch.Events[0], FAKE_CREATE, "incomplete create first");
    TEST_ASSERT_UINT_EQ(g_fake_launch.Events[1], FAKE_EXECUTE, "incomplete execute second");
    TEST_ASSERT_UINT_EQ(g_fake_launch.Events[2], FAKE_DESTROY, "incomplete destroy third");
    TEST_ASSERT_UINT_EQ(g_fake_launch.Events[3], FAKE_MAP, "incomplete map last");
    TEST_ASSERT_UINT_EQ(g_fake_launch.MappedResourceMask,
                        VMX_RESOURCE_CONTEXT | VMX_RESOURCE_STACK | VMX_RESOURCE_VMXON_PAGE |
                        VMX_RESOURCE_VMCS_PAGE | VMX_RESOURCE_SERIALIZED, "incomplete high-water mask");
    TEST_ASSERT_FALSE(g_fake_launch.MappedResourcesReleased, "incomplete release false");
    TEST_ASSERT_UINT_EQ(g_fake_launch.MappedCleanupRequirement, VMX_CLEANUP_REQUIRED, "incomplete cleanup required");
    TEST_ASSERT_UINT_EQ(g_fake_launch.MappedCleanupOwner, VMX_CLEANUP_OWNER_C, "incomplete cleanup owner");
    TEST_ASSERT_FALSE(response.DomainStatus == VMX_DOMAIN_SUCCESS, "incomplete not success");

    FakeLaunchReset();
    operations = FakeOperations();
    g_fake_launch.MapResult = false;
    (void)memset(&response, 0xA5, sizeof(response));
    TEST_ASSERT_FALSE(IoctlRunLaunchProbeLifecycle(&operations, &response), "mapper failure propagates");
    TEST_ASSERT_TRUE(CheckedResponseIsZero(&response), "mapper failure zero response");
    TEST_ASSERT_UINT_EQ(g_fake_launch.MapCount, 1, "mapper failure maps once");
    return 0;
}

static int test_launch_probe_response_buffer_contract(void)
{
    TEST_ASSERT_FALSE(IoctlLaunchProbeResponseBufferIsSufficient(0), "zero buffer rejected");
    TEST_ASSERT_FALSE(IoctlLaunchProbeResponseBufferIsSufficient(VMX_LAUNCH_PROBE_RESPONSE_V2_SIZE - 1u), "short buffer rejected");
    TEST_ASSERT_TRUE(IoctlLaunchProbeResponseBufferIsSufficient(VMX_LAUNCH_PROBE_RESPONSE_V2_SIZE), "exact buffer accepted");
    TEST_ASSERT_TRUE(IoctlLaunchProbeResponseBufferIsSufficient(VMX_LAUNCH_PROBE_RESPONSE_V2_SIZE + 1u), "larger buffer accepted");
    return 0;
}
/*===========================================================================
 * Main
 *===========================================================================*/
TEST_MAIN_BEGIN()

    fprintf(stdout, "\n=== Hook Table CRUD ===\n");
    fflush(stdout);
    TEST_RUN(test_hook_table_init);
    TEST_RUN(test_hook_register_single);
    TEST_RUN(test_hook_register_multiple);
    TEST_RUN(test_hook_register_different_permissions);
    TEST_RUN(test_hook_unregister_by_id);
    TEST_RUN(test_hook_unregister_invalid);
    TEST_RUN(test_hook_find_by_gpa);
    TEST_RUN(test_hook_register_duplicate_gpa);
    TEST_RUN(test_hook_table_full);
    TEST_RUN(test_hook_unregister_and_refill);
    TEST_RUN(test_hook_find_on_empty_table);
    TEST_RUN(test_hook_find_after_unregister);
    TEST_RUN(test_hook_null_safety);
    TEST_RUN(test_hook_gpa_zero);
    TEST_RUN(test_hook_gpa_max);

    fprintf(stdout, "\n=== EPT Violation Qualification ===\n");
    fflush(stdout);
    TEST_RUN(test_ept_qual_data_read);
    TEST_RUN(test_ept_qual_data_write);
    TEST_RUN(test_ept_qual_instruction_fetch);
    TEST_RUN(test_ept_qual_read_write);
    TEST_RUN(test_ept_qual_all_permissions);
    TEST_RUN(test_ept_qual_no_access);
    TEST_RUN(test_ept_qual_extract_gpa);
    TEST_RUN(test_ept_qual_extract_gpa_boundary);
    TEST_RUN(test_ept_qual_extract_gla);
    TEST_RUN(test_ept_qual_constants_alignment);
    TEST_RUN(test_ept_handle_violation_hooked);
    TEST_RUN(test_ept_handle_violation_not_hooked);
    TEST_RUN(test_ept_handle_violation_null_safety);
    TEST_RUN(test_ept_handle_misconfig);

    fprintf(stdout, "\n=== IOCTL Dispatch Logic ===\n");
    fflush(stdout);
    TEST_RUN(test_ioctl_action_load);
    TEST_RUN(test_ioctl_action_unload);
    TEST_RUN(test_ioctl_action_hook);
    TEST_RUN(test_ioctl_action_unhook);
    TEST_RUN(test_ioctl_action_status);
    TEST_RUN(test_ioctl_action_probe);
    TEST_RUN(test_probe_response_layout);
    TEST_RUN(test_ioctl_action_unknown);
    TEST_RUN(test_ioctl_action_zero);

    fprintf(stdout, "\n=== Buffer Validation ===\n");
    fflush(stdout);
    TEST_RUN(test_validate_hook_buffer_valid);
    TEST_RUN(test_validate_hook_buffer_null);
    TEST_RUN(test_validate_hook_buffer_short);
    TEST_RUN(test_validate_hook_buffer_unaligned_gpa);
    TEST_RUN(test_validate_hook_permissions_and_reserved);
    TEST_RUN(test_validate_unhook_buffer_valid);
    TEST_RUN(test_validate_unhook_buffer_null);
    TEST_RUN(test_validate_unhook_buffer_short);
    TEST_RUN(test_validate_unhook_alignment_and_reserved);

    fprintf(stdout, "\n=== G1 Launch-Probe Wiring ===\n");
    fflush(stdout);
    TEST_RUN(test_launch_probe_response_size);
    TEST_RUN(test_launch_probe_response_init);
    TEST_RUN(test_map_preflight_stage_all);
    TEST_RUN(test_map_success);
    TEST_RUN(test_map_invalid_platform);
    TEST_RUN(test_map_not_passive);
    TEST_RUN(test_map_serialization_failed);
    TEST_RUN(test_map_pin_failed);
    TEST_RUN(test_map_cet_active);
    TEST_RUN(test_map_no_vmx_cpuid);
    TEST_RUN(test_map_feature_control);
    TEST_RUN(test_map_bad_vmx_basic);
    TEST_RUN(test_map_cr0_change);
    TEST_RUN(test_map_cr4_change);
    TEST_RUN(test_map_context_failed);
    TEST_RUN(test_map_cookie_failed);

    fprintf(stdout, "\n=== G1b Transient Mapping ===\n");
    fflush(stdout);
    TEST_RUN(test_transient_map_preflight_rejected);
    TEST_RUN(test_transient_map_vmxon_fail);
    TEST_RUN(test_transient_map_vmwrite_fail);
    TEST_RUN(test_transient_map_vmlaunch_fail);
    TEST_RUN(test_transient_map_vmresume_fail);
    TEST_RUN(test_transient_map_vmxoff_fail);
    TEST_RUN(test_transient_map_final_vmclear_fail);
    TEST_RUN(test_transient_map_full_success);
    TEST_RUN(test_transient_map_rejects_malformed);
    TEST_RUN(test_report_layout);

    fprintf(stdout, "\n=== G1c-a Normalization ===\n");
    fflush(stdout);
    TEST_RUN(test_normalize_vmx_success);
    TEST_RUN(test_normalize_vmx_nonzero_is_failure);
    TEST_RUN(test_kernel_vmwrite_outcome_normalization);
    TEST_RUN(test_response_size_still_280);
    TEST_RUN(test_domain_enum_values);

    fprintf(stdout, "\n=== G1c-b NTSTATUS + VMLAUNCH Normalization ===\n");
    fflush(stdout);
    TEST_RUN(test_transient_ntstatus_preflight_fail);
    TEST_RUN(test_transient_ntstatus_success);
    TEST_RUN(test_transient_ntstatus_vmxon_fail);
    TEST_RUN(test_transient_ntstatus_vmlaunch_fail);
    TEST_RUN(test_transient_ntstatus_vmresume_fail);
    TEST_RUN(test_transient_ntstatus_vmclear_fail);
    TEST_RUN(test_transient_ntstatus_null_reports);
    TEST_RUN(test_vmlaunch_normalize_masm_success);
    TEST_RUN(test_vmlaunch_normalize_masm_failure);
    TEST_RUN(test_vmlaunch_normalize_other_reports_unchanged);
    TEST_RUN(test_checked_runtime_copy);
    TEST_RUN(test_checked_runtime_rejects_malformed);
    TEST_RUN(test_checked_runtime_consistency);
    TEST_RUN(test_validate_stage_matrix);
    TEST_RUN(test_validate_final_vmclear_failures);
    TEST_RUN(test_validate_cleanup_and_input_contract);
    TEST_RUN(test_validate_protocol_contract);
    TEST_RUN(test_map_launch_probe_response_success);
    TEST_RUN(test_map_launch_probe_response_preflight);
    TEST_RUN(test_map_launch_probe_response_failures);
    TEST_RUN(test_map_launch_probe_response_partial);
    TEST_RUN(test_map_launch_probe_response_structural_failure);
    TEST_RUN(test_map_launch_probe_response_rejects_invalid);
    TEST_RUN(test_launch_probe_lifecycle_input_spans);
    TEST_RUN(test_launch_probe_lifecycle_snapshot);
    TEST_RUN(test_launch_probe_lifecycle_contract);
    TEST_RUN(test_launch_probe_response_buffer_contract);
    TEST_MAIN_END()
