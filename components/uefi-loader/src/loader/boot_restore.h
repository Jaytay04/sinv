/* boot_restore.h - fail-closed one-shot boot-manager restore transaction. */
#ifndef BOOT_RESTORE_H
#define BOOT_RESTORE_H

#include "Uefi.h"
#include "sha256.h"

/* All five paths must name files in one directory.  The caller owns the
 * strings for the duration of boot_restore_run(). */
typedef struct {
    const CHAR16 *TargetPath;
    const CHAR16 *BackupPath;
    const CHAR16 *TempPath;
    const CHAR16 *DisplacedPath;
    const CHAR16 *HashRecordPath;
} boot_restore_paths_t;

typedef enum {
    BOOT_RESTORE_OK = 0,
    BOOT_RESTORE_ERR_ARGUMENT,
    BOOT_RESTORE_ERR_PATHS,
    BOOT_RESTORE_ERR_TARGET_MISSING,
    BOOT_RESTORE_ERR_BACKUP_MISSING,
    BOOT_RESTORE_ERR_HASH_RECORD_MISSING,
    BOOT_RESTORE_ERR_HASH_RECORD_MALFORMED,
    BOOT_RESTORE_ERR_HASH_RECORD_MISMATCH,
    BOOT_RESTORE_ERR_TEMP_PRESENT,
    BOOT_RESTORE_ERR_DISPLACED_PRESENT,
    BOOT_RESTORE_ERR_BACKUP_READ,
    BOOT_RESTORE_ERR_BACKUP_HASH,
    BOOT_RESTORE_ERR_TEMP_WRITE,
    BOOT_RESTORE_ERR_TEMP_VERIFY,
    BOOT_RESTORE_ERR_TARGET_TO_DISPLACED,
    BOOT_RESTORE_ERR_TEMP_TO_TARGET,
    BOOT_RESTORE_ERR_FINAL_READ,
    BOOT_RESTORE_ERR_FINAL_HASH,
    BOOT_RESTORE_ERR_ROLLBACK,
    BOOT_RESTORE_ERR_TEMP_DELETE,
    BOOT_RESTORE_ERR_DISPLACED_DELETE,
    BOOT_RESTORE_ERR_BACKUP_DELETE
} boot_restore_status_t;

typedef struct {
    EFI_STATUS efi_status;
    UINT8 backup_sha256[SHA256_DIGEST_SIZE];
    BOOLEAN rollback_attempted;
    BOOLEAN rollback_succeeded;
} boot_restore_report_t;

/*
 * Restore BackupPath into TargetPath transactionally within one ESP directory.
 *
 * The transaction first validates HashRecordPath against the SHA-256 digest
 * of BackupPath, then writes and verifies TempPath,
 * moves the old target to DisplacedPath, then moves the verified temp file to
 * the target and verifies it again.  BackupPath and DisplacedPath are removed
 * only after the final hash check succeeds.  HashRecordPath is retained after
 * a successful restore for post-boot verification.  Every failure before
 * cleanup leaves BackupPath and HashRecordPath intact; rename/final-
 * verification failures make a best effort rollback to the original target
 * and report whether it succeeded.
 * UEFI FAT rename semantics do not guarantee an atomic replace across power
 * loss; verification and retained recovery copies provide the safety model.
 */
boot_restore_status_t boot_restore_run(
    EFI_HANDLE ImageHandle, const boot_restore_paths_t *Paths,
    boot_restore_report_t *Report);

#endif /* BOOT_RESTORE_H */
