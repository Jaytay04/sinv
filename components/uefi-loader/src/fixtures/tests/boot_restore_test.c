/* boot_restore_test.c - Host tests for the fail-closed restore transaction. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "boot_restore.h"
#include "efi_file.h"

#define CHECK(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, \
                #expression); \
        return 1; \
    } \
} while (0)

#define IMAGE_HANDLE ((EFI_HANDLE)(uintptr_t)1u)
#define SLOT_CAPACITY 128u

typedef struct {
    int present;
    uint8_t bytes[SLOT_CAPACITY];
    size_t size;
} fake_slot_t;

typedef struct {
    fake_slot_t target;
    fake_slot_t backup;
    fake_slot_t temp;
    fake_slot_t displaced;
    fake_slot_t hash_record;
} fake_volume_t;

typedef enum {
    RENAME_FAIL_NONE = 0,
    RENAME_FAIL_TEMP_TO_TARGET,
    RENAME_FAIL_DISPLACED_TO_TARGET
} rename_failure_t;

typedef struct {
    rename_failure_t rename_failure;
    int corrupt_final_target_read;
    const CHAR16 *delete_failure_path;
    int write_calls;
    int rename_calls;
    int delete_calls;
} fake_faults_t;

static const CHAR16 g_target_path[] = {
    '\\', 'E', 'F', 'I', '\\', 'M', 'i', 'c', 'r', 'o', 's', 'o', 'f', 't',
    '\\', 'B', 'o', 'o', 't', '\\', 'b', 'o', 'o', 't', 'm', 'g', 'f', 'w',
    '.', 'e', 'f', 'i', 0
};
static const CHAR16 g_backup_path[] = {
    '\\', 'E', 'F', 'I', '\\', 'M', 'i', 'c', 'r', 'o', 's', 'o', 'f', 't',
    '\\', 'B', 'o', 'o', 't', '\\', 'b', 'o', 'o', 't', 'm', 'g', 'f', 'w',
    '.', 'e', 'f', 'i', '.', 'b', 'a', 'k', 0
};
static const CHAR16 g_temp_path[] = {
    '\\', 'E', 'F', 'I', '\\', 'M', 'i', 'c', 'r', 'o', 's', 'o', 'f', 't',
    '\\', 'B', 'o', 'o', 't', '\\', 'b', 'o', 'o', 't', 'm', 'g', 'f', 'w',
    '.', 'e', 'f', 'i', '.', 'i', 'b', 'n', 'e', 'w', 0
};
static const CHAR16 g_displaced_path[] = {
    '\\', 'E', 'F', 'I', '\\', 'M', 'i', 'c', 'r', 'o', 's', 'o', 'f', 't',
    '\\', 'B', 'o', 'o', 't', '\\', 'b', 'o', 'o', 't', 'm', 'g', 'f', 'w',
    '.', 'e', 'f', 'i', '.', 'i', 'b', 'o', 'l', 'd', 0
};
static const CHAR16 g_hash_record_path[] = {
    '\\', 'E', 'F', 'I', '\\', 'M', 'i', 'c', 'r', 'o', 's', 'o', 'f', 't',
    '\\', 'B', 'o', 'o', 't', '\\', 'b', 'o', 'o', 't', 'm', 'g', 'f', 'w',
    '.', 's', 'h', 'a', '2', '5', '6', 0
};

static const boot_restore_paths_t g_paths = {
    g_target_path, g_backup_path, g_temp_path, g_displaced_path,
    g_hash_record_path
};

static const uint8_t g_original[] = "original boot manager";
static const uint8_t g_replacement[] = "verified replacement boot manager";

/* These globals provide the UEFI symbols required by boot_restore.c.  The
 * test deliberately supplies no service implementations: the file helpers
 * below are the seam under test and boot_restore_run only requires gBS to be
 * non-NULL. */
EFI_SYSTEM_TABLE *gST;
EFI_BOOT_SERVICES *gBS;
static EFI_SYSTEM_TABLE g_fake_system_table;
static EFI_BOOT_SERVICES g_fake_boot_services;
static fake_volume_t g_volume;
static fake_faults_t g_faults;

static int path_equal(const CHAR16 *left, const CHAR16 *right)
{
    size_t i = 0;

    if (left == NULL || right == NULL) {
        return left == right;
    }
    while (left[i] != 0 && right[i] != 0) {
        if (left[i] != right[i]) {
            return 0;
        }
        i++;
    }
    return left[i] == right[i];
}

static fake_slot_t *slot_for_path(const CHAR16 *path)
{
    if (path_equal(path, g_target_path)) {
        return &g_volume.target;
    }
    if (path_equal(path, g_backup_path)) {
        return &g_volume.backup;
    }
    if (path_equal(path, g_temp_path)) {
        return &g_volume.temp;
    }
    if (path_equal(path, g_displaced_path)) {
        return &g_volume.displaced;
    }
    if (path_equal(path, g_hash_record_path)) {
        return &g_volume.hash_record;
    }
    return NULL;
}

static void set_slot(fake_slot_t *slot, const uint8_t *bytes, size_t size)
{
    memset(slot, 0, sizeof(*slot));
    slot->present = 1;
    slot->size = size;
    memcpy(slot->bytes, bytes, size);
}

static void clear_slot(fake_slot_t *slot)
{
    memset(slot, 0, sizeof(*slot));
}

static void set_hash_record(const uint8_t digest[SHA256_DIGEST_SIZE])
{
    static const uint8_t digits[] = "0123456789ABCDEF";
    uint8_t record[66];
    size_t i;

    for (i = 0; i < SHA256_DIGEST_SIZE; i++) {
        record[i * 2u] = digits[digest[i] >> 4];
        record[i * 2u + 1u] = digits[digest[i] & 0x0fu];
    }
    record[64] = '\r';
    record[65] = '\n';
    set_slot(&g_volume.hash_record, record, sizeof(record));
}

static void reset_volume(void)
{
    uint8_t replacement_digest[SHA256_DIGEST_SIZE];

    memset(&g_volume, 0, sizeof(g_volume));
    memset(&g_faults, 0, sizeof(g_faults));
    set_slot(&g_volume.target, g_original, sizeof(g_original) - 1u);
    set_slot(&g_volume.backup, g_replacement, sizeof(g_replacement) - 1u);
    memset(&g_fake_system_table, 0, sizeof(g_fake_system_table));
    memset(&g_fake_boot_services, 0, sizeof(g_fake_boot_services));
    gBS = &g_fake_boot_services;
    gST = &g_fake_system_table;
    g_fake_system_table.BootServices = gBS;
    if (sha256_hash(g_replacement, sizeof(g_replacement) - 1u,
                    replacement_digest) == SHA256_OK) {
        set_hash_record(replacement_digest);
    }
}

static int slots_equal(const fake_volume_t *left, const fake_volume_t *right)
{
    return memcmp(left, right, sizeof(*left)) == 0;
}

static int slot_contains(const fake_slot_t *slot, const uint8_t *bytes,
                          size_t size)
{
    return slot->present && slot->size == size &&
           memcmp(slot->bytes, bytes, size) == 0;
}

/* ------------------------------------------------------------------------- */
/* Fake efi_file seam                                                        */
/* ------------------------------------------------------------------------- */

EFI_STATUS efi_file_read(EFI_HANDLE image_handle, const CHAR16 *path,
                         efi_file_buffer_t *out)
{
    fake_slot_t *slot;
    uint8_t *copy;

    (void)image_handle;
    if (out == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    out->bytes = NULL;
    out->size = 0;
    slot = slot_for_path(path);
    if (slot == NULL || !slot->present) {
        return EFI_NOT_FOUND;
    }
    if (g_faults.corrupt_final_target_read && slot == &g_volume.target) {
        /* Corrupt the on-disk replacement just before its final verification
         * read.  Rollback must still recover the displaced original. */
        if (slot->size != 0u) {
            slot->bytes[0] ^= 0xffu;
        }
        g_faults.corrupt_final_target_read = 0;
    }
    copy = (uint8_t *)malloc(slot->size == 0u ? 1u : slot->size);
    if (copy == NULL) {
        return EFI_OUT_OF_RESOURCES;
    }
    memcpy(copy, slot->bytes, slot->size);
    out->bytes = copy;
    out->size = (UINTN)slot->size;
    return EFI_SUCCESS;
}

void efi_file_release(efi_file_buffer_t *buffer)
{
    if (buffer == NULL) {
        return;
    }
    free(buffer->bytes);
    buffer->bytes = NULL;
    buffer->size = 0;
}

EFI_STATUS efi_file_write(EFI_HANDLE image_handle, const CHAR16 *path,
                          const UINT8 *bytes, UINTN size)
{
    fake_slot_t *slot;

    (void)image_handle;
    g_faults.write_calls++;
    slot = slot_for_path(path);
    if (slot == NULL || bytes == NULL || size > SLOT_CAPACITY) {
        return EFI_INVALID_PARAMETER;
    }
    memset(slot, 0, sizeof(*slot));
    slot->present = 1;
    slot->size = (size_t)size;
    memcpy(slot->bytes, bytes, (size_t)size);
    return EFI_SUCCESS;
}

EFI_STATUS efi_file_exists(EFI_HANDLE image_handle, const CHAR16 *path)
{
    fake_slot_t *slot;

    (void)image_handle;
    slot = slot_for_path(path);
    return slot != NULL && slot->present ? EFI_SUCCESS : EFI_NOT_FOUND;
}

EFI_STATUS efi_file_delete(EFI_HANDLE image_handle, const CHAR16 *path)
{
    fake_slot_t *slot;

    (void)image_handle;
    g_faults.delete_calls++;
    if (g_faults.delete_failure_path != NULL &&
        path_equal(path, g_faults.delete_failure_path)) {
        return EFI_DEVICE_ERROR;
    }
    slot = slot_for_path(path);
    if (slot == NULL || !slot->present) {
        return EFI_NOT_FOUND;
    }
    clear_slot(slot);
    return EFI_SUCCESS;
}

EFI_STATUS efi_file_rename(EFI_HANDLE image_handle, const CHAR16 *source,
                           const CHAR16 *destination)
{
    fake_slot_t *source_slot;
    fake_slot_t *destination_slot;

    (void)image_handle;
    g_faults.rename_calls++;
    if (g_faults.rename_failure == RENAME_FAIL_TEMP_TO_TARGET &&
        path_equal(source, g_temp_path) && path_equal(destination, g_target_path)) {
        return EFI_DEVICE_ERROR;
    }
    if (g_faults.rename_failure == RENAME_FAIL_DISPLACED_TO_TARGET &&
        path_equal(source, g_displaced_path) &&
        path_equal(destination, g_target_path)) {
        return EFI_DEVICE_ERROR;
    }
    source_slot = slot_for_path(source);
    destination_slot = slot_for_path(destination);
    if (source_slot == NULL || destination_slot == NULL ||
        !source_slot->present) {
        return EFI_NOT_FOUND;
    }
    if (destination_slot->present) {
        return EFI_ALREADY_STARTED;
    }
    *destination_slot = *source_slot;
    clear_slot(source_slot);
    return EFI_SUCCESS;
}

static boot_restore_status_t run_restore(boot_restore_report_t *report)
{
    return boot_restore_run(IMAGE_HANDLE, &g_paths, report);
}

static int test_success_hash_report_and_cleanup(void)
{
    boot_restore_report_t report;
    uint8_t expected_digest[SHA256_DIGEST_SIZE];

    reset_volume();
    CHECK(sha256_hash(g_replacement, sizeof(g_replacement) - 1u,
                      expected_digest) == SHA256_OK);
    CHECK(run_restore(&report) == BOOT_RESTORE_OK);
    CHECK(report.efi_status == EFI_SUCCESS);
    CHECK(report.rollback_attempted == FALSE);
    CHECK(report.rollback_succeeded == FALSE);
    CHECK(memcmp(report.backup_sha256, expected_digest,
                 sizeof(expected_digest)) == 0);
    CHECK(slot_contains(&g_volume.target, g_replacement,
                        sizeof(g_replacement) - 1u));
    CHECK(!g_volume.backup.present);
    CHECK(g_volume.hash_record.present);
    CHECK(!g_volume.temp.present);
    CHECK(!g_volume.displaced.present);
    CHECK(g_faults.write_calls == 1);
    CHECK(g_faults.rename_calls == 2);
    CHECK(g_faults.delete_calls == 2);
    return 0;
}

static int test_invalid_and_colliding_paths_are_non_mutating(void)
{
    boot_restore_report_t report;
    fake_volume_t before;
    boot_restore_paths_t invalid_paths = g_paths;
    static const CHAR16 invalid_leaf[] = {
        '\\', 'E', 'F', 'I', '\\', 'M', 'i', 'c', 'r', 'o', 's', 'o', 'f', 't',
        '\\', 'B', 'o', 'o', 't', '\\', 'b', 'a', 'd', '\\', 'f', 'i', 'l', 'e',
        '.', 'e', 'f', 'i', 0
    };

    reset_volume();
    invalid_paths.TargetPath = invalid_leaf;
    before = g_volume;
    CHECK(boot_restore_run(IMAGE_HANDLE, &invalid_paths, &report) ==
          BOOT_RESTORE_ERR_PATHS);
    CHECK(report.efi_status == EFI_INVALID_PARAMETER);
    CHECK(slots_equal(&g_volume, &before));
    CHECK(g_faults.write_calls == 0 && g_faults.rename_calls == 0 &&
          g_faults.delete_calls == 0);

    reset_volume();
    invalid_paths = g_paths;
    invalid_paths.TempPath = g_target_path;
    before = g_volume;
    CHECK(boot_restore_run(IMAGE_HANDLE, &invalid_paths, &report) ==
          BOOT_RESTORE_ERR_PATHS);
    CHECK(slots_equal(&g_volume, &before));
    CHECK(g_faults.write_calls == 0 && g_faults.rename_calls == 0 &&
          g_faults.delete_calls == 0);

    reset_volume();
    invalid_paths = g_paths;
    invalid_paths.HashRecordPath = g_backup_path;
    before = g_volume;
    CHECK(boot_restore_run(IMAGE_HANDLE, &invalid_paths, &report) ==
          BOOT_RESTORE_ERR_PATHS);
    CHECK(report.efi_status == EFI_INVALID_PARAMETER);
    CHECK(slots_equal(&g_volume, &before));
    CHECK(g_faults.write_calls == 0 && g_faults.rename_calls == 0 &&
          g_faults.delete_calls == 0);

    reset_volume();
    set_slot(&g_volume.temp, (const uint8_t *)"stale temp", 10u);
    before = g_volume;
    CHECK(run_restore(&report) == BOOT_RESTORE_ERR_TEMP_PRESENT);
    CHECK(report.efi_status == EFI_ALREADY_STARTED);
    CHECK(slots_equal(&g_volume, &before));
    CHECK(g_faults.write_calls == 0 && g_faults.rename_calls == 0 &&
          g_faults.delete_calls == 0);

    reset_volume();
    set_slot(&g_volume.displaced, (const uint8_t *)"stale displaced", 15u);
    before = g_volume;
    CHECK(run_restore(&report) == BOOT_RESTORE_ERR_DISPLACED_PRESENT);
    CHECK(report.efi_status == EFI_ALREADY_STARTED);
    CHECK(slots_equal(&g_volume, &before));
    CHECK(g_faults.write_calls == 0 && g_faults.rename_calls == 0 &&
          g_faults.delete_calls == 0);
    return 0;
}

static int test_missing_target_and_backup(void)
{
    boot_restore_report_t report;
    fake_volume_t before;

    reset_volume();
    clear_slot(&g_volume.target);
    before = g_volume;
    CHECK(run_restore(&report) == BOOT_RESTORE_ERR_TARGET_MISSING);
    CHECK(report.efi_status == EFI_NOT_FOUND);
    CHECK(slots_equal(&g_volume, &before));

    reset_volume();
    clear_slot(&g_volume.backup);
    before = g_volume;
    CHECK(run_restore(&report) == BOOT_RESTORE_ERR_BACKUP_MISSING);
    CHECK(report.efi_status == EFI_NOT_FOUND);
    CHECK(slots_equal(&g_volume, &before));
    return 0;
}

static int test_hash_record_failures_are_non_mutating(void)
{
    static const uint8_t malformed_record[] = "not a sha256 record";
    boot_restore_report_t report;
    fake_volume_t before;

    reset_volume();
    clear_slot(&g_volume.hash_record);
    before = g_volume;
    CHECK(run_restore(&report) == BOOT_RESTORE_ERR_HASH_RECORD_MISSING);
    CHECK(report.efi_status == EFI_NOT_FOUND);
    CHECK(slots_equal(&g_volume, &before));
    CHECK(g_faults.write_calls == 0 && g_faults.rename_calls == 0 &&
          g_faults.delete_calls == 0);

    reset_volume();
    set_slot(&g_volume.hash_record, malformed_record,
             sizeof(malformed_record) - 1u);
    before = g_volume;
    CHECK(run_restore(&report) == BOOT_RESTORE_ERR_HASH_RECORD_MALFORMED);
    CHECK(report.efi_status == EFI_VOLUME_CORRUPTED);
    CHECK(slots_equal(&g_volume, &before));
    CHECK(g_faults.write_calls == 0 && g_faults.rename_calls == 0 &&
          g_faults.delete_calls == 0);

    reset_volume();
    g_volume.hash_record.bytes[0] = 'a';
    before = g_volume;
    CHECK(run_restore(&report) == BOOT_RESTORE_ERR_HASH_RECORD_MALFORMED);
    CHECK(report.efi_status == EFI_VOLUME_CORRUPTED);
    CHECK(slots_equal(&g_volume, &before));
    CHECK(g_faults.write_calls == 0 && g_faults.rename_calls == 0 &&
          g_faults.delete_calls == 0);

    reset_volume();
    g_volume.backup.bytes[0] ^= 0xffu;
    before = g_volume;
    CHECK(run_restore(&report) == BOOT_RESTORE_ERR_HASH_RECORD_MISMATCH);
    CHECK(report.efi_status == EFI_VOLUME_CORRUPTED);
    CHECK(slots_equal(&g_volume, &before));
    CHECK(g_faults.write_calls == 0 && g_faults.rename_calls == 0 &&
          g_faults.delete_calls == 0);
    return 0;
}

static int test_temp_to_target_failure_rolls_back(void)
{
    boot_restore_report_t report;

    reset_volume();
    g_faults.rename_failure = RENAME_FAIL_TEMP_TO_TARGET;
    CHECK(run_restore(&report) == BOOT_RESTORE_ERR_TEMP_TO_TARGET);
    CHECK(report.efi_status == EFI_DEVICE_ERROR);
    CHECK(report.rollback_attempted == TRUE);
    CHECK(report.rollback_succeeded == TRUE);
    CHECK(slot_contains(&g_volume.target, g_original,
                        sizeof(g_original) - 1u));
    CHECK(g_volume.backup.present);
    CHECK(!g_volume.temp.present);
    CHECK(!g_volume.displaced.present);
    return 0;
}

static int test_final_verification_corruption_rolls_back_and_cleans(void)
{
    boot_restore_report_t report;

    reset_volume();
    g_faults.corrupt_final_target_read = 1;
    CHECK(run_restore(&report) == BOOT_RESTORE_ERR_FINAL_HASH);
    CHECK(report.efi_status == EFI_VOLUME_CORRUPTED);
    CHECK(report.rollback_attempted == TRUE);
    CHECK(report.rollback_succeeded == TRUE);
    CHECK(slot_contains(&g_volume.target, g_original,
                        sizeof(g_original) - 1u));
    CHECK(g_volume.backup.present);
    CHECK(!g_volume.temp.present);
    CHECK(!g_volume.displaced.present);
    return 0;
}

static int test_rollback_failure_retains_recovery_files(void)
{
    boot_restore_report_t report;

    reset_volume();
    g_faults.corrupt_final_target_read = 1;
    g_faults.rename_failure = RENAME_FAIL_DISPLACED_TO_TARGET;
    CHECK(run_restore(&report) == BOOT_RESTORE_ERR_ROLLBACK);
    CHECK(report.efi_status == EFI_DEVICE_ERROR);
    CHECK(report.rollback_attempted == TRUE);
    CHECK(report.rollback_succeeded == FALSE);
    CHECK(!g_volume.target.present);
    CHECK(g_volume.backup.present);
    CHECK(g_volume.temp.present);
    CHECK(g_volume.displaced.present);
    CHECK(slot_contains(&g_volume.displaced, g_original,
                        sizeof(g_original) - 1u));
    return 0;
}

static int test_cleanup_delete_failures(void)
{
    boot_restore_report_t report;

    reset_volume();
    g_faults.delete_failure_path = g_displaced_path;
    CHECK(run_restore(&report) == BOOT_RESTORE_ERR_DISPLACED_DELETE);
    CHECK(report.efi_status == EFI_DEVICE_ERROR);
    CHECK(report.rollback_attempted == FALSE);
    CHECK(slot_contains(&g_volume.target, g_replacement,
                        sizeof(g_replacement) - 1u));
    CHECK(g_volume.backup.present);
    CHECK(g_volume.displaced.present);
    CHECK(!g_volume.temp.present);

    reset_volume();
    g_faults.delete_failure_path = g_backup_path;
    CHECK(run_restore(&report) == BOOT_RESTORE_ERR_BACKUP_DELETE);
    CHECK(report.efi_status == EFI_DEVICE_ERROR);
    CHECK(report.rollback_attempted == FALSE);
    CHECK(slot_contains(&g_volume.target, g_replacement,
                        sizeof(g_replacement) - 1u));
    CHECK(g_volume.backup.present);
    CHECK(!g_volume.displaced.present);
    CHECK(!g_volume.temp.present);
    return 0;
}

int main(void)
{
    if (test_success_hash_report_and_cleanup() != 0 ||
        test_invalid_and_colliding_paths_are_non_mutating() != 0 ||
        test_missing_target_and_backup() != 0 ||
        test_hash_record_failures_are_non_mutating() != 0 ||
        test_temp_to_target_failure_rolls_back() != 0 ||
        test_final_verification_corruption_rolls_back_and_cleans() != 0 ||
        test_rollback_failure_retains_recovery_files() != 0 ||
        test_cleanup_delete_failures() != 0) {
        return 1;
    }
    puts("PASS: boot restore transaction is fail-closed and recoverable");
    return 0;
}
