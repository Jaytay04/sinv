/* Bounded boot-table publication followed by the unchanged inventory handoff. */
#define EfiMain InventoryReferenceMain
#include "inventory_reference.c"
#undef EfiMain

#include "efi_smbios.h"
#include "efi_smbios_diagnostic.h"
#ifdef HYPERSINV_SMBIOS_VALUES_HEADER
#include HYPERSINV_SMBIOS_VALUES_HEADER
#else
#include "smbios_values.h"
#endif
#include "build_identity.h"
#include "boot_report.h"

static EFI_STATUS boot_text(EFI_FILE_PROTOCOL *file, const char *text)
{
    UINTN length = 0;
    while (text[length] != 0) ++length;
    return write_all(file, (const UINT8 *)text, length);
}

static EFI_STATUS boot_number(EFI_FILE_PROTOCOL *file, UINTN number)
{
    char digits[24];
    UINTN length = 0, index;
    do {
        digits[length++] = (char)('0' + number % 10u);
        number /= 10u;
    } while (number != 0 && length < sizeof(digits));
    for (index = 0; index < length / 2u; ++index) {
        char temporary = digits[index];
        digits[index] = digits[length - index - 1u];
        digits[length - index - 1u] = temporary;
    }
    return write_all(file, (const UINT8 *)digits, length);
}

static EFI_STATUS boot_number64(EFI_FILE_PROTOCOL *file, UINT64 number)
{
    char digits[20];
    UINTN length = 0u, index;
    do {
        digits[length++] = (char)('0' + number % 10u);
        number /= 10u;
    } while (number != 0u && length < sizeof(digits));
    for (index = 0u; index < length / 2u; ++index) {
        char temporary = digits[index];
        digits[index] = digits[length - index - 1u];
        digits[length - index - 1u] = temporary;
    }
    return write_all(file, (const UINT8 *)digits, length);
}

static EFI_STATUS boot_hex64(EFI_FILE_PROTOCOL *file, UINT64 value)
{
    static const char digits[] = "0123456789ABCDEF";
    char output[16];
    UINTN index;
    for (index = 0u; index < sizeof(output); ++index) {
        const UINTN shift = (UINTN)((sizeof(output) - index - 1u) * 4u);
        output[index] = digits[(UINTN)((value >> shift) & UINT64_C(0xF))];
    }
    return write_all(file, (const UINT8 *)output, sizeof(output));
}

static EFI_STATUS boot_prepare_diagnostic(EFI_FILE_PROTOCOL *file)
{
    EFI_SMBIOS_PREPARE_DIAGNOSTIC diagnostic;
    EFI_STATUS status;
    const char *check_name;
#define BOOT_DIAG_WRITE(call) do { status = (call); if (EFI_ERROR(status)) return status; } while (0)
    status = EfiSmbiosGetLastPrepareDiagnostic(&diagnostic);
    if (EFI_ERROR(status)) {
        return status;
    }
    check_name = EfiSmbiosPrepareCheckName(diagnostic.check);
    if (check_name == NULL) {
        check_name = "unknown";
    }
    BOOT_DIAG_WRITE(boot_text(file, "prepare_check="));
    BOOT_DIAG_WRITE(boot_text(file, check_name));
    BOOT_DIAG_WRITE(boot_text(file, "\r\nprepare_status_scope=internal_first_failure"));
    BOOT_DIAG_WRITE(boot_text(file, "\r\nprepare_entry_index="));
    if (diagnostic.entry_index == EFI_SMBIOS_DIAGNOSTIC_ENTRY_NONE) {
        BOOT_DIAG_WRITE(boot_text(file, "none"));
    } else {
        BOOT_DIAG_WRITE(boot_number64(file, diagnostic.entry_index));
    }
    BOOT_DIAG_WRITE(boot_text(file, "\r\nprepare_cause_status=0x"));
    BOOT_DIAG_WRITE(boot_hex64(file, (UINT64)diagnostic.cause_status));
    BOOT_DIAG_WRITE(boot_text(file, "\r\nprepare_cleanup_status=0x"));
    BOOT_DIAG_WRITE(boot_hex64(file,
                               (UINT64)diagnostic.prepared_pages_cleanup_status));
    BOOT_DIAG_WRITE(boot_text(file, "\r\nprepare_aux_cleanup_status=0x"));
    BOOT_DIAG_WRITE(boot_hex64(file,
                               (UINT64)diagnostic.auxiliary_cleanup_status));
    BOOT_DIAG_WRITE(boot_text(file, "\r\nprepare_entry_metadata="));
    BOOT_DIAG_WRITE(boot_text(file,
                              diagnostic.entry_metadata_available ? "1" : "0"));
    BOOT_DIAG_WRITE(boot_text(file, "\r\n"));
    if (diagnostic.entry_metadata_available) {
        BOOT_DIAG_WRITE(boot_text(file, "prepare_ep_kind="));
        if (diagnostic.entry_kind == (UINT8)SM_EP_SMBIOS2) {
            BOOT_DIAG_WRITE(boot_text(file, "smbios2"));
        } else if (diagnostic.entry_kind == (UINT8)SM_EP_SMBIOS3) {
            BOOT_DIAG_WRITE(boot_text(file, "smbios3"));
        } else {
            BOOT_DIAG_WRITE(boot_text(file, "unknown"));
        }
        BOOT_DIAG_WRITE(boot_text(file, "\r\nprepare_ep_version="));
        BOOT_DIAG_WRITE(boot_number64(file, diagnostic.entry_major));
        BOOT_DIAG_WRITE(boot_text(file, "."));
        BOOT_DIAG_WRITE(boot_number64(file, diagnostic.entry_minor));
        BOOT_DIAG_WRITE(boot_text(file, "\r\nprepare_ep_table_bytes="));
        BOOT_DIAG_WRITE(boot_number64(file, diagnostic.declared_table_bytes));
        BOOT_DIAG_WRITE(boot_text(file, "\r\nprepare_ep_table_bytes_semantics="));
        BOOT_DIAG_WRITE(boot_text(
            file, diagnostic.entry_kind == (UINT8)SM_EP_SMBIOS2
                      ? "exact_length" : "maximum_size"));
        if (diagnostic.entry_kind == (UINT8)SM_EP_SMBIOS2) {
            BOOT_DIAG_WRITE(boot_text(file, "\r\nprepare_ep_structure_count="));
            BOOT_DIAG_WRITE(
                boot_number64(file, diagnostic.declared_structure_count));
            BOOT_DIAG_WRITE(boot_text(file, "\r\nprepare_ep_max_structure_size="));
            BOOT_DIAG_WRITE(boot_number64(
                file, diagnostic.declared_maximum_structure_size));
        }
        BOOT_DIAG_WRITE(boot_text(file, "\r\n"));
    }
    BOOT_DIAG_WRITE(boot_text(file, "prepare_table_metadata="));
    BOOT_DIAG_WRITE(boot_text(file,
                              diagnostic.table_metadata_available ? "1" : "0"));
    BOOT_DIAG_WRITE(boot_text(file, "\r\nprepare_table_parse_complete="));
    BOOT_DIAG_WRITE(boot_text(file,
                              diagnostic.table_parse_complete ? "1" : "0"));
    BOOT_DIAG_WRITE(boot_text(file, "\r\n"));
    if (diagnostic.table_metadata_available) {
        BOOT_DIAG_WRITE(boot_text(file, "prepare_actual_version="));
        BOOT_DIAG_WRITE(boot_number64(file, diagnostic.entry_major));
        BOOT_DIAG_WRITE(boot_text(file, "."));
        BOOT_DIAG_WRITE(boot_number64(file, diagnostic.entry_minor));
        BOOT_DIAG_WRITE(boot_text(file, "\r\nprepare_actual_table_bytes="));
        BOOT_DIAG_WRITE(boot_number64(file, diagnostic.actual_table_bytes));
        BOOT_DIAG_WRITE(boot_text(file, "\r\nprepare_actual_structure_count="));
        BOOT_DIAG_WRITE(boot_number64(file, diagnostic.actual_structure_count));
        BOOT_DIAG_WRITE(boot_text(file, "\r\nprepare_actual_max_structure_size="));
        BOOT_DIAG_WRITE(boot_number64(
            file, diagnostic.actual_maximum_structure_size));
        BOOT_DIAG_WRITE(boot_text(file, "\r\n"));
    }
    BOOT_DIAG_WRITE(boot_text(file, "prepare_replacement_metadata="));
    BOOT_DIAG_WRITE(boot_text(file,
                              diagnostic.replacement_metadata_available ? "1" : "0"));
    BOOT_DIAG_WRITE(boot_text(file, "\r\n"));
    if (diagnostic.replacement_metadata_available) {
        BOOT_DIAG_WRITE(boot_text(file, "prepare_replacement_table_bytes="));
        BOOT_DIAG_WRITE(boot_number64(file, diagnostic.replacement_table_bytes));
        BOOT_DIAG_WRITE(boot_text(file, "\r\nprepare_replacement_structure_count="));
        BOOT_DIAG_WRITE(boot_number64(file, diagnostic.replacement_structure_count));
        BOOT_DIAG_WRITE(boot_text(file, "\r\nprepare_replacement_max_structure_size="));
        BOOT_DIAG_WRITE(boot_number64(
            file, diagnostic.replacement_maximum_structure_size));
        BOOT_DIAG_WRITE(boot_text(file, "\r\n"));
    }
#undef BOOT_DIAG_WRITE
    return EFI_SUCCESS;
}

static EFI_STATUS boot_values(EFI_FILE_PROTOCOL *file,
                              const EFI_SMBIOS_CONTEXT *context)
{
    UINTN entry;
    EFI_STATUS status;
#define BOOT_WRITE(call) do { status = (call); if (EFI_ERROR(status)) return status; } while (0)
    BOOT_WRITE(boot_text(file, "entries="));
    BOOT_WRITE(boot_number(file, context->EntryCount));
    BOOT_WRITE(boot_text(file, "\r\n"));
    for (entry = 0; entry < context->EntryCount; ++entry) {
        UINTN field;
        const EFI_SMBIOS_ENTRY_CONTEXT *item = &context->Entries[entry];
        BOOT_WRITE(boot_text(file, "entry\t"));
        BOOT_WRITE(boot_number(file, entry));
        BOOT_WRITE(boot_text(file, "\t"));
        BOOT_WRITE(boot_number(file, (UINTN)item->Kind));
        BOOT_WRITE(boot_text(file, "\tEfiRuntimeServicesData\r\n"));
        for (field = 0; field < SM_FIELD_COUNT; ++field) {
            BOOT_WRITE(boot_text(file, "field\t"));
            BOOT_WRITE(boot_number(file, entry));
            BOOT_WRITE(boot_text(file, "\t"));
            BOOT_WRITE(boot_text(file, sm_field_names[field]));
            BOOT_WRITE(boot_text(file, "\t"));
            BOOT_WRITE(boot_text(file, item->BeforeValues.field[field]));
            BOOT_WRITE(boot_text(file, "\t"));
            BOOT_WRITE(boot_text(file, item->AfterValues.field[field]));
            BOOT_WRITE(boot_text(file, "\r\n"));
        }
    }
    BOOT_WRITE(boot_text(file, "result=applied\r\nEND\r\n"));
#undef BOOT_WRITE
    return EFI_SUCCESS;
}

static EFI_STATUS boot_failure_record(EFI_FILE_PROTOCOL *file,
                                      const char *phase,
                                      EFI_STATUS failure_status,
                                      int rollback_attempted,
                                      EFI_STATUS rollback_status,
                                      int free_attempted,
                                      EFI_STATUS free_status,
                                      EFI_STATUS cleanup_free_status)
{
    char record[320];
    size_t length = 0u;
    if (!sm_boot_format_failure_record(record, sizeof(record), &length, phase,
                                       (uint64_t)failure_status,
                                       rollback_attempted,
                                       (uint64_t)rollback_status,
                                       free_attempted,
                                       (uint64_t)free_status,
                                       (uint64_t)cleanup_free_status)) {
        return EFI_INVALID_PARAMETER;
    }
    return write_all(file, (const UINT8 *)record, (UINTN)length);
}

static EFI_STATUS boot_append_failure_after_close(EFI_FILE_PROTOCOL *root,
                                                   CHAR16 *report_path,
                                                   const char *phase,
                                                   EFI_STATUS failure_status,
                                                   int rollback_attempted,
                                                   EFI_STATUS rollback_status,
                                                   int free_attempted,
                                                   EFI_STATUS free_status,
                                                   EFI_STATUS cleanup_free_status)
{
    EFI_FILE_PROTOCOL *file = NULL;
    EFI_STATUS status, close_status;
    if (root == NULL || root->Open == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    status = root->Open(root, &file, report_path,
                        EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0);
    if (EFI_ERROR(status) || file == NULL) {
        return EFI_ERROR(status) ? status : EFI_DEVICE_ERROR;
    }
    if (file->SetPosition == NULL || file->Flush == NULL || file->Close == NULL) {
        if (file->Close != NULL) (void)file->Close(file);
        return EFI_UNSUPPORTED;
    }
    status = file->SetPosition(file, UINT64_MAX);
    if (!EFI_ERROR(status)) {
        status = boot_failure_record(file, phase, failure_status,
                                     rollback_attempted, rollback_status,
                                     free_attempted, free_status,
                                     cleanup_free_status);
    }
    if (!EFI_ERROR(status)) {
        status = file->Flush(file);
    }
    close_status = file->Close(file);
    if (!EFI_ERROR(status) && EFI_ERROR(close_status)) {
        status = close_status;
    }
    return status;
}

EFI_STATUS EFIAPI EfiMain(EFI_HANDLE image, EFI_SYSTEM_TABLE *system)
{
    static CHAR16 report_path[] = L"\\HypersinvSmbiosPatch.txt";
    EFI_SMBIOS_CONTEXT context = {0};
    EFI_FILE_PROTOCOL *root = NULL, *existing = NULL, *file = NULL;
    EFI_STATUS status, cleanup_status, failure_status = EFI_SUCCESS;
    EFI_STATUS rollback_status = EFI_SUCCESS, free_status = EFI_SUCCESS;
    EFI_STATUS cleanup_free_status = EFI_SUCCESS;
    int rollback_attempted = 0, free_attempted = 0;
    const char *failure_phase = "report_header";

    status = InventoryReferenceMain(image, system);
    if (EFI_ERROR(status)) return status;
    status = open_report_filesystem(image, system->BootServices, &root);
    if (EFI_ERROR(status)) return status;
    if (root->Open == NULL || root->Close == NULL) {
        if (root->Close != NULL) (void)root->Close(root);
        return EFI_UNSUPPORTED;
    }
    status = root->Open(root, &existing, report_path, EFI_FILE_MODE_READ, 0);
    if (!EFI_ERROR(status)) {
        if (existing != NULL && existing->Close != NULL) (void)existing->Close(existing);
        (void)root->Close(root);
        return EFI_ALREADY_STARTED;
    }
    if (status != EFI_NOT_FOUND) {
        if (existing != NULL && existing->Close != NULL) (void)existing->Close(existing);
        (void)root->Close(root);
        return status;
    }
    status = root->Open(root, &file, report_path,
                        EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0);
    if (EFI_ERROR(status) || file == NULL) {
        (void)root->Close(root);
        return EFI_ERROR(status) ? status : EFI_DEVICE_ERROR;
    }
    if (file->Close == NULL || file->Flush == NULL) {
        if (file->Close != NULL) (void)file->Close(file);
        (void)root->Close(root);
        return EFI_UNSUPPORTED;
    }
    status = check_new_empty_file(file);
    if (!EFI_ERROR(status)) status = boot_text(file, "HSMBIOS001\r\nmask=");
    if (!EFI_ERROR(status)) status = boot_number(file, SM_ACTIVE_BOOT_MASK);
    if (!EFI_ERROR(status)) status = boot_text(file, "\r\nbuild_id=" SM_BUILD_ID "\r\n");
    if (EFI_ERROR(status)) {
        failure_status = status;
        goto report_failure;
    }

    failure_phase = "prepare";
    status = EfiSmbiosPrepare(system, SM_ACTIVE_BOOT_MASK,
                              &sm_host_originals, &sm_host_replacements, &context);
    if (EFI_ERROR(status)) {
        failure_status = status;
        (VOID)boot_prepare_diagnostic(file);
        goto report_failure;
    }

    failure_phase = "publish";
    status = EfiSmbiosPublish(&context);
    if (EFI_ERROR(status)) {
        failure_status = status;
        goto report_failure;
    }

    failure_phase = "readback";
    status = EfiSmbiosReadback(&context);
    if (EFI_ERROR(status)) {
        failure_status = status;
        goto report_failure;
    }

    failure_phase = "free";
    free_attempted = 1;
    free_status = EfiSmbiosFree(&context);
    if (EFI_ERROR(free_status)) {
        failure_status = free_status;
        goto report_failure;
    }

    failure_phase = "report_write";
    status = boot_values(file, &context);
    if (EFI_ERROR(status)) {
        failure_status = status;
        goto report_failure;
    }

    failure_phase = "report_flush";
    status = file->Flush(file);
    if (EFI_ERROR(status)) {
        failure_status = status;
        goto report_failure;
    }

    cleanup_status = file->Close(file);
    file = NULL;
    if (EFI_ERROR(cleanup_status)) {
        failure_phase = "report_close";
        failure_status = cleanup_status;
        if (context.State != EfiSmbiosStateEmpty &&
            context.State != EfiSmbiosStateRolledBack) {
            rollback_attempted = 1;
            rollback_status = EfiSmbiosRollback(&context);
        }
        free_attempted = 1;
        cleanup_free_status = EfiSmbiosFree(&context);
        (void)boot_append_failure_after_close(root, report_path, failure_phase,
                                              failure_status, rollback_attempted,
                                              rollback_status, free_attempted,
                                              free_status, cleanup_free_status);
        cleanup_status = root->Close(root);
        root = NULL;
        return failure_status;
    }
    cleanup_status = root->Close(root);
    if (EFI_ERROR(cleanup_status)) {
        EFI_FILE_PROTOCOL *reopened_root = NULL;
        EFI_STATUS reopen_status, append_status, reopened_close_status;
        failure_phase = "report_root_close";
        failure_status = cleanup_status;
        if (context.State != EfiSmbiosStateEmpty &&
            context.State != EfiSmbiosStateRolledBack) {
            rollback_attempted = 1;
            rollback_status = EfiSmbiosRollback(&context);
        }
        cleanup_free_status = EfiSmbiosFree(&context);
        /* The original root handle may have been closed despite its error. */
        reopen_status = open_report_filesystem(image, system->BootServices,
                                               &reopened_root);
        if (!EFI_ERROR(reopen_status) && reopened_root != NULL) {
            append_status = boot_append_failure_after_close(
                reopened_root, report_path, failure_phase, failure_status,
                rollback_attempted, rollback_status, free_attempted,
                free_status, cleanup_free_status);
            reopened_close_status = reopened_root->Close(reopened_root);
            if (!EFI_ERROR(append_status) && EFI_ERROR(reopened_close_status)) {
                append_status = reopened_close_status;
            }
        } else {
            append_status = EFI_ERROR(reopen_status) ? reopen_status : EFI_DEVICE_ERROR;
        }
        (void)append_status;
        root = NULL;
        return failure_status;
    }
    root = NULL;
    return EFI_SUCCESS;

report_failure:
    if ((failure_phase[0] == 'p' || failure_phase[0] == 'r') &&
        (context.State == EfiSmbiosStateRolledBack ||
         (context.State == EfiSmbiosStatePublished && EFI_ERROR(context.LastStatus)))) {
        /* Publish and readback perform an internal rollback before returning. */
        rollback_attempted = 1;
        rollback_status = context.State == EfiSmbiosStateRolledBack
            ? EFI_SUCCESS : context.LastStatus;
    }
    if (context.State != EfiSmbiosStateEmpty &&
        context.State != EfiSmbiosStateRolledBack && !rollback_attempted) {
        rollback_attempted = 1;
        rollback_status = EfiSmbiosRollback(&context);
    }
    cleanup_free_status = EfiSmbiosFree(&context);

    if (file != NULL) {
        status = boot_failure_record(file, failure_phase, failure_status,
                                     rollback_attempted, rollback_status,
                                     free_attempted, free_status,
                                     cleanup_free_status);
        if (!EFI_ERROR(status)) {
            status = file->Flush(file);
        }
        cleanup_status = file->Close(file);
        file = NULL;
        if (!EFI_ERROR(status) && EFI_ERROR(cleanup_status)) {
            status = cleanup_status;
        }
    }
    if (root != NULL) {
        cleanup_status = root->Close(root);
        root = NULL;
        if (!EFI_ERROR(status) && EFI_ERROR(cleanup_status)) {
            status = cleanup_status;
        }
    }
    /* The returned error remains the first failing phase, never rollback/free. */
    return failure_status;
}
