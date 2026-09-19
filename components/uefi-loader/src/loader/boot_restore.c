/* boot_restore.c - bounded, same-directory one-shot restore transaction. */
#include "boot_restore.h"

#include "efi_file.h"

static UINTN path_length(const CHAR16 *Path)
{
    UINTN length = 0;

    if (Path == NULL) {
        return 0;
    }
    while (Path[length] != 0) {
        if (length >= 32767u) {
            return 0;
        }
        length++;
    }
    return length;
}

static int is_separator(CHAR16 c)
{
    return c == (CHAR16)'\\' || c == (CHAR16)'/';
}

static CHAR16 ascii_lower(CHAR16 c)
{
    if (c >= (CHAR16)'A' && c <= (CHAR16)'Z') {
        return (CHAR16)(c + ((CHAR16)'a' - (CHAR16)'A'));
    }
    return c;
}

static int path_char_equal(CHAR16 Left, CHAR16 Right)
{
    if (is_separator(Left) && is_separator(Right)) {
        return 1;
    }
    return ascii_lower(Left) == ascii_lower(Right);
}

static UINTN last_separator(const CHAR16 *Path, UINTN Length)
{
    UINTN i;
    UINTN last = (UINTN)(~(UINTN)0);

    for (i = 0; i < Length; i++) {
        if (is_separator(Path[i])) {
            last = i;
        }
    }
    return last;
}

static int paths_equal(const CHAR16 *Left, const CHAR16 *Right)
{
    UINTN left_length = path_length(Left);
    UINTN right_length = path_length(Right);
    UINTN i;

    if (left_length == 0 || left_length != right_length) {
        return 0;
    }
    for (i = 0; i < left_length; i++) {
        if (!path_char_equal(Left[i], Right[i])) {
            return 0;
        }
    }
    return 1;
}

static int same_directory(const CHAR16 *Left, const CHAR16 *Right)
{
    UINTN left_length = path_length(Left);
    UINTN right_length = path_length(Right);
    UINTN left_sep;
    UINTN right_sep;
    UINTN left_parent;
    UINTN right_parent;
    UINTN i;

    if (left_length == 0 || right_length == 0) {
        return 0;
    }
    left_sep = last_separator(Left, left_length);
    right_sep = last_separator(Right, right_length);
    left_parent = left_sep == (UINTN)(~(UINTN)0) ? 0 : left_sep + 1u;
    right_parent = right_sep == (UINTN)(~(UINTN)0) ? 0 : right_sep + 1u;
    if (left_parent != right_parent || left_parent == left_length ||
        right_parent == right_length) {
        return 0;
    }
    for (i = 0; i < left_parent; i++) {
        if (!path_char_equal(Left[i], Right[i])) {
            return 0;
        }
    }
    return 1;
}

static int valid_path(const CHAR16 *Path)
{
    UINTN length = path_length(Path);
    UINTN i;
    UINTN sep;
    UINTN leaf;

    if (length == 0) {
        return 0;
    }
    sep = last_separator(Path, length);
    leaf = sep == (UINTN)(~(UINTN)0) ? 0 : sep + 1u;
    if (leaf >= length) {
        return 0;
    }
    if ((length - leaf == 1u && Path[leaf] == (CHAR16)'.') ||
        (length - leaf == 2u && Path[leaf] == (CHAR16)'.' &&
         Path[leaf + 1u] == (CHAR16)'.')) {
        return 0;
    }
    for (i = leaf; i < length; i++) {
        if (is_separator(Path[i])) {
            return 0;
        }
    }
    return 1;
}

static void report_init(boot_restore_report_t *Report)
{
    UINTN i;

    if (Report == NULL) {
        return;
    }
    Report->efi_status = EFI_SUCCESS;
    Report->rollback_attempted = FALSE;
    Report->rollback_succeeded = FALSE;
    for (i = 0; i < SHA256_DIGEST_SIZE; i++) {
        Report->backup_sha256[i] = 0;
    }
}

static int digest_equal(const UINT8 *Left, const UINT8 *Right)
{
    UINTN i;
    UINT8 difference = 0;

    for (i = 0; i < SHA256_DIGEST_SIZE; i++) {
        difference |= (UINT8)(Left[i] ^ Right[i]);
    }
    return difference == 0;
}

static boot_restore_status_t hash_buffer(const efi_file_buffer_t *Buffer,
                                          UINT8 Digest[SHA256_DIGEST_SIZE])
{
    if (Buffer == NULL || Buffer->bytes == NULL || Buffer->size == 0u ||
        Digest == NULL) {
        return BOOT_RESTORE_ERR_ARGUMENT;
    }
    return sha256_hash(Buffer->bytes, (size_t)Buffer->size, Digest) ==
                   SHA256_OK
               ? BOOT_RESTORE_OK
               : BOOT_RESTORE_ERR_BACKUP_HASH;
}

static int hex_value(UINT8 Character)
{
    if (Character >= (UINT8)'0' && Character <= (UINT8)'9') {
        return (int)(Character - (UINT8)'0');
    }
    if (Character >= (UINT8)'A' && Character <= (UINT8)'F') {
        return 10 + (int)(Character - (UINT8)'A');
    }
    return -1;
}

/* The installer writes exactly 64 uppercase hexadecimal characters followed
 * by CRLF.  Do not accept whitespace, a NUL terminator, lowercase hex, or a
 * trailing byte: the record is an authenticated handoff contract, not a
 * human-readable hint. */
static int parse_hash_record(const efi_file_buffer_t *Record,
                             UINT8 Digest[SHA256_DIGEST_SIZE])
{
    UINTN i;

    if (Record == NULL || Record->bytes == NULL || Digest == NULL ||
        Record->size != 66u || Record->bytes[64] != (UINT8)'\r' ||
        Record->bytes[65] != (UINT8)'\n') {
        return 0;
    }
    for (i = 0; i < SHA256_DIGEST_SIZE; i++) {
        int high = hex_value(Record->bytes[i * 2u]);
        int low = hex_value(Record->bytes[i * 2u + 1u]);
        if (high < 0 || low < 0) {
            return 0;
        }
        Digest[i] = (UINT8)((high << 4) | low);
    }
    return 1;
}

static void set_efi_status(boot_restore_report_t *Report, EFI_STATUS Status)
{
    if (Report != NULL) {
        Report->efi_status = Status;
    }
}

static boot_restore_status_t rollback_after_displacement(
    EFI_HANDLE ImageHandle, const boot_restore_paths_t *Paths,
    boot_restore_report_t *Report, int TargetContainsReplacement)
{
    EFI_STATUS st;

    if (Report != NULL) {
        Report->rollback_attempted = TRUE;
    }
    if (TargetContainsReplacement) {
        /* The replacement is known-bad. Move it out of the way first. */
        st = efi_file_rename(ImageHandle, Paths->TargetPath,
                             Paths->TempPath);
        if (EFI_ERROR(st)) {
            set_efi_status(Report, st);
            return BOOT_RESTORE_ERR_ROLLBACK;
        }
    }
    st = efi_file_rename(ImageHandle, Paths->DisplacedPath,
                         Paths->TargetPath);
    if (EFI_ERROR(st)) {
        set_efi_status(Report, st);
        /* If moving the bad replacement failed, leave the original target in
         * displaced and do not destroy it.  The verified backup remains. */
        return BOOT_RESTORE_ERR_ROLLBACK;
    }
    if (Report != NULL) {
        Report->rollback_succeeded = TRUE;
    }
    st = efi_file_delete(ImageHandle, Paths->TempPath);
    if (EFI_ERROR(st) && st != EFI_NOT_FOUND) {
        set_efi_status(Report, st);
        return BOOT_RESTORE_ERR_TEMP_DELETE;
    }
    return BOOT_RESTORE_OK;
}

static boot_restore_status_t verify_file_hash(
    EFI_HANDLE ImageHandle, const CHAR16 *Path,
    const UINT8 Expected[SHA256_DIGEST_SIZE],
    boot_restore_report_t *Report, boot_restore_status_t ReadFailure,
    boot_restore_status_t HashFailure)
{
    efi_file_buffer_t file;
    UINT8 digest[SHA256_DIGEST_SIZE];
    EFI_STATUS st;
    boot_restore_status_t result;

    file.bytes = NULL;
    file.size = 0;
    st = efi_file_read(ImageHandle, Path, &file);
    if (EFI_ERROR(st)) {
        set_efi_status(Report, st);
        return ReadFailure;
    }
    result = hash_buffer(&file, digest);
    if (result != BOOT_RESTORE_OK || !digest_equal(digest, Expected)) {
        if (result == BOOT_RESTORE_OK) {
            set_efi_status(Report, EFI_VOLUME_CORRUPTED);
        } else {
            set_efi_status(Report, EFI_DEVICE_ERROR);
        }
        efi_file_release(&file);
        return HashFailure;
    }
    efi_file_release(&file);
    return BOOT_RESTORE_OK;
}

boot_restore_status_t boot_restore_run(
    EFI_HANDLE ImageHandle, const boot_restore_paths_t *Paths,
    boot_restore_report_t *Report)
{
    efi_file_buffer_t backup;
    efi_file_buffer_t hash_record;
    UINT8 backup_digest[SHA256_DIGEST_SIZE];
    UINT8 recorded_digest[SHA256_DIGEST_SIZE];
    EFI_STATUS st;
    boot_restore_status_t result;
    int displaced = 0;
    int replacement_at_target = 0;

    report_init(Report);
    backup.bytes = NULL;
    backup.size = 0;
    hash_record.bytes = NULL;
    hash_record.size = 0;
    if (ImageHandle == NULL || Paths == NULL || gBS == NULL ||
        !valid_path(Paths->TargetPath) || !valid_path(Paths->BackupPath) ||
        !valid_path(Paths->TempPath) || !valid_path(Paths->DisplacedPath) ||
        !valid_path(Paths->HashRecordPath) ||
        !same_directory(Paths->TargetPath, Paths->BackupPath) ||
        !same_directory(Paths->TargetPath, Paths->TempPath) ||
        !same_directory(Paths->TargetPath, Paths->DisplacedPath) ||
        !same_directory(Paths->TargetPath, Paths->HashRecordPath) ||
        paths_equal(Paths->TargetPath, Paths->BackupPath) ||
        paths_equal(Paths->TargetPath, Paths->TempPath) ||
        paths_equal(Paths->TargetPath, Paths->DisplacedPath) ||
        paths_equal(Paths->TargetPath, Paths->HashRecordPath) ||
        paths_equal(Paths->BackupPath, Paths->TempPath) ||
        paths_equal(Paths->BackupPath, Paths->DisplacedPath) ||
        paths_equal(Paths->BackupPath, Paths->HashRecordPath) ||
        paths_equal(Paths->TempPath, Paths->DisplacedPath) ||
        paths_equal(Paths->TempPath, Paths->HashRecordPath) ||
        paths_equal(Paths->DisplacedPath, Paths->HashRecordPath)) {
        set_efi_status(Report, EFI_INVALID_PARAMETER);
        return BOOT_RESTORE_ERR_PATHS;
    }

    st = efi_file_exists(ImageHandle, Paths->TargetPath);
    if (st == EFI_NOT_FOUND) {
        set_efi_status(Report, st);
        return BOOT_RESTORE_ERR_TARGET_MISSING;
    }
    if (EFI_ERROR(st)) {
        set_efi_status(Report, st);
        return BOOT_RESTORE_ERR_TARGET_MISSING;
    }
    st = efi_file_exists(ImageHandle, Paths->BackupPath);
    if (st == EFI_NOT_FOUND) {
        set_efi_status(Report, st);
        return BOOT_RESTORE_ERR_BACKUP_MISSING;
    }
    if (EFI_ERROR(st)) {
        set_efi_status(Report, st);
        return BOOT_RESTORE_ERR_BACKUP_MISSING;
    }
    st = efi_file_exists(ImageHandle, Paths->HashRecordPath);
    if (st == EFI_NOT_FOUND) {
        set_efi_status(Report, st);
        return BOOT_RESTORE_ERR_HASH_RECORD_MISSING;
    }
    if (EFI_ERROR(st)) {
        set_efi_status(Report, st);
        return BOOT_RESTORE_ERR_HASH_RECORD_MALFORMED;
    }
    st = efi_file_exists(ImageHandle, Paths->TempPath);
    if (st == EFI_SUCCESS) {
        set_efi_status(Report, EFI_ALREADY_STARTED);
        return BOOT_RESTORE_ERR_TEMP_PRESENT;
    }
    if (st != EFI_NOT_FOUND) {
        set_efi_status(Report, st);
        return BOOT_RESTORE_ERR_TEMP_PRESENT;
    }
    st = efi_file_exists(ImageHandle, Paths->DisplacedPath);
    if (st == EFI_SUCCESS) {
        set_efi_status(Report, EFI_ALREADY_STARTED);
        return BOOT_RESTORE_ERR_DISPLACED_PRESENT;
    }
    if (st != EFI_NOT_FOUND) {
        set_efi_status(Report, st);
        return BOOT_RESTORE_ERR_DISPLACED_PRESENT;
    }

    st = efi_file_read(ImageHandle, Paths->HashRecordPath, &hash_record);
    if (EFI_ERROR(st)) {
        set_efi_status(Report, st);
        return st == EFI_NOT_FOUND ? BOOT_RESTORE_ERR_HASH_RECORD_MISSING
                                   : BOOT_RESTORE_ERR_HASH_RECORD_MALFORMED;
    }
    if (!parse_hash_record(&hash_record, recorded_digest)) {
        set_efi_status(Report, EFI_VOLUME_CORRUPTED);
        efi_file_release(&hash_record);
        return BOOT_RESTORE_ERR_HASH_RECORD_MALFORMED;
    }
    efi_file_release(&hash_record);

    st = efi_file_read(ImageHandle, Paths->BackupPath, &backup);
    if (EFI_ERROR(st)) {
        set_efi_status(Report, st);
        return BOOT_RESTORE_ERR_BACKUP_READ;
    }
    result = hash_buffer(&backup, backup_digest);
    if (result != BOOT_RESTORE_OK) {
        set_efi_status(Report, EFI_DEVICE_ERROR);
        efi_file_release(&backup);
        return BOOT_RESTORE_ERR_BACKUP_HASH;
    }
    if (!digest_equal(backup_digest, recorded_digest)) {
        set_efi_status(Report, EFI_VOLUME_CORRUPTED);
        efi_file_release(&backup);
        return BOOT_RESTORE_ERR_HASH_RECORD_MISMATCH;
    }
    if (Report != NULL) {
        UINTN i;
        for (i = 0; i < SHA256_DIGEST_SIZE; i++) {
            Report->backup_sha256[i] = backup_digest[i];
        }
    }

    st = efi_file_write(ImageHandle, Paths->TempPath,
                        backup.bytes, backup.size);
    efi_file_release(&backup);
    if (EFI_ERROR(st)) {
        set_efi_status(Report, st);
        return BOOT_RESTORE_ERR_TEMP_WRITE;
    }
    result = verify_file_hash(ImageHandle, Paths->TempPath, backup_digest,
                              Report, BOOT_RESTORE_ERR_TEMP_VERIFY,
                              BOOT_RESTORE_ERR_TEMP_VERIFY);
    if (result != BOOT_RESTORE_OK) {
        (void)efi_file_delete(ImageHandle, Paths->TempPath);
        return result;
    }

    st = efi_file_rename(ImageHandle, Paths->TargetPath,
                         Paths->DisplacedPath);
    if (EFI_ERROR(st)) {
        set_efi_status(Report, st);
        (void)efi_file_delete(ImageHandle, Paths->TempPath);
        return BOOT_RESTORE_ERR_TARGET_TO_DISPLACED;
    }
    displaced = 1;

    st = efi_file_rename(ImageHandle, Paths->TempPath, Paths->TargetPath);
    if (EFI_ERROR(st)) {
        set_efi_status(Report, st);
        result = rollback_after_displacement(ImageHandle, Paths, Report, 0);
        return result == BOOT_RESTORE_OK
                   ? BOOT_RESTORE_ERR_TEMP_TO_TARGET
                   : result;
    }
    replacement_at_target = 1;

    result = verify_file_hash(ImageHandle, Paths->TargetPath,
                              backup_digest, Report,
                              BOOT_RESTORE_ERR_FINAL_READ,
                              BOOT_RESTORE_ERR_FINAL_HASH);
    if (result != BOOT_RESTORE_OK) {
        boot_restore_status_t rollback_result =
            rollback_after_displacement(ImageHandle, Paths,
                                        Report, replacement_at_target);
        return rollback_result == BOOT_RESTORE_OK ? result : rollback_result;
    }

    /* Cleanup is intentionally last.  If either delete fails, report the
     * residue while retaining the verified target and any still-present
     * backup for recovery. */
    if (displaced) {
        st = efi_file_delete(ImageHandle, Paths->DisplacedPath);
        if (EFI_ERROR(st)) {
            set_efi_status(Report, st);
            return BOOT_RESTORE_ERR_DISPLACED_DELETE;
        }
    }
    st = efi_file_delete(ImageHandle, Paths->BackupPath);
    if (EFI_ERROR(st)) {
        set_efi_status(Report, st);
        return BOOT_RESTORE_ERR_BACKUP_DELETE;
    }
    return BOOT_RESTORE_OK;
}
