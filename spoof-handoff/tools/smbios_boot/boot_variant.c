/* Bounded boot-table publication followed by the unchanged inventory handoff. */
#define EfiMain InventoryReferenceMain
#include "inventory_reference.c"
#undef EfiMain

#include "efi_smbios.h"
#include "smbios_values.h"
#include "build_identity.h"

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

EFI_STATUS EFIAPI EfiMain(EFI_HANDLE image, EFI_SYSTEM_TABLE *system)
{
    static CHAR16 report_path[] = L"\\HypersinvSmbiosPatch.txt";
    EFI_SMBIOS_CONTEXT context = {0};
    EFI_FILE_PROTOCOL *root = NULL, *existing = NULL, *file = NULL;
    EFI_STATUS status, cleanup_status;
    int applied = 0;

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
    if (!EFI_ERROR(status)) {
        status = EfiSmbiosPrepare(system, SM_ACTIVE_BOOT_MASK,
                                  &sm_host_originals, &sm_host_replacements, &context);
    }
    if (!EFI_ERROR(status)) status = EfiSmbiosPublish(&context);
    if (!EFI_ERROR(status)) status = EfiSmbiosReadback(&context);
    if (!EFI_ERROR(status)) status = boot_values(file, &context);
    if (!EFI_ERROR(status)) status = file->Flush(file);
    cleanup_status = file->Close(file);
    if (!EFI_ERROR(status) && EFI_ERROR(cleanup_status)) status = cleanup_status;
    cleanup_status = root->Close(root);
    if (!EFI_ERROR(status) && EFI_ERROR(cleanup_status)) status = cleanup_status;
    applied = !EFI_ERROR(status);
    if (!applied && context.State != EfiSmbiosStateEmpty) {
        cleanup_status = EfiSmbiosRollback(&context);
        if (EFI_ERROR(cleanup_status)) status = cleanup_status;
    }
    cleanup_status = EfiSmbiosFree(&context);
    if (!EFI_ERROR(status) && EFI_ERROR(cleanup_status)) {
        status = cleanup_status;
        cleanup_status = EfiSmbiosRollback(&context);
        if (EFI_ERROR(cleanup_status)) status = cleanup_status;
        (void)EfiSmbiosFree(&context);
    }
    return status;
}
