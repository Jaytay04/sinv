/* VM ONLY: QEMU debug ports and synthetic boot-manager child, never a payload. */
#include <intrin.h>
#include "ib_uefi.h"

#pragma intrinsic(__outbyte, __outdword)
static int ib_vm_anchor_data;
void *volatile ib_vm_relocation_anchor = &ib_vm_anchor_data;

static void trace(const char *text) {
    while (*text) __outbyte(0xe9, (unsigned char)*text++);
}
#if !defined(IB_VM_CHILD)
static void trace_hex(ib_u64 value) {
    static const char digits[] = "0123456789ABCDEF";
    int shift;
    for (shift = 60; shift >= 0; shift -= 4) __outbyte(0xe9, digits[(value >> shift) & 15]);
    trace("\n");
}
#endif

#if defined(IB_VM_CHILD)
ib_efi_status IB_EFIAPI EfiMain(ib_efi_handle image, ib_efi_system_table *table) {
    (void)image; (void)table;
    if (!ib_vm_relocation_anchor) return IB_EFI_LOAD_ERROR;
    trace("boot-only child: RETURN LOAD_ERROR\n");
    return IB_EFI_LOAD_ERROR;
}
#else
#include "ib_bootmgfw.h"
#include "ib_util.h"

/* Fixture-only error adapters keep unrelated TPM error renderers unlinked. */
void ib_loader_error_str(ib_loader_error_t *error, ib_loader_code code, const char *text) {
    ib_memset(error, 0, sizeof(*error)); error->code = code; error->text = text;
}
void ib_loader_error_uefi(ib_loader_error_t *error, ib_efi_status status) {
    ib_loader_error_str(error, IB_LOADER_ERR_UEFI, NULL); error->status = status;
}
void ib_loader_error_partial_read(ib_loader_error_t *error, const char *path, size_t got, size_t expected) {
    ib_loader_error_str(error, IB_LOADER_ERR_PARTIAL_READ, path); error->len = got; error->expected = expected;
}
void ib_loader_error_partial_write(ib_loader_error_t *error, const char *path, size_t got, size_t expected) {
    ib_loader_error_str(error, IB_LOADER_ERR_PARTIAL_WRITE, path); error->len = got; error->expected = expected;
}

static int equal(const ib_bytes_t *a, const ib_bytes_t *b) {
    return a->length == b->length && ib_memcmp(a->data, b->data, a->length) == 0;
}

ib_efi_status IB_EFIAPI EfiMain(ib_efi_handle image, ib_efi_system_table *table) {
    ib_fs_volume_t volume;
    ib_bytes_t mode = {0, 0}, before = {0, 0}, expected = {0, 0}, after = {0, 0}, stage = {0, 0};
    ib_loader_error_t error;
    ib_loader_code code;
    int failed = 1, present = 0, missing_backup, missing_parent;
    ib_efi_set_system_table(table); ib_efi_set_image_handle(image);
    if (!ib_vm_relocation_anchor) return IB_EFI_LOAD_ERROR;
    trace("boot-only fixture: ENTRY (no TPM, payload, or hypervisor)\n");
    if (ib_fs_open(&volume, &error) != IB_LOADER_OK) goto finish;
    if (ib_fs_read(&volume, "\\case.txt", &mode, &error) != IB_LOADER_OK) goto done;
    missing_backup = mode.length == 14 && ib_memcmp(mode.data, "missing-backup", 14) == 0;
    missing_parent = mode.length == 14 && ib_memcmp(mode.data, "missing-parent", 14) == 0;
    if (missing_backup) {
        if (ib_fs_read(&volume, IB_BOOTMGFW_PATH, &before, &error) != IB_LOADER_OK) goto done;
        code = ib_bootmgfw_restore(&volume, &error);
        if (code != IB_LOADER_ERR_MISSING_ARTIFACT) goto done;
        if (ib_fs_read(&volume, IB_BOOTMGFW_PATH, &after, &error) != IB_LOADER_OK || !equal(&before, &after)) goto done;
        trace("boot-only fixture: MISSING BACKUP FAILS CLOSED\n");
    } else if (missing_parent) {
        if (ib_fs_read(&volume, IB_BOOTMGFW_BACKUP, &expected, &error) != IB_LOADER_OK) goto done;
        if (ib_bootmgfw_restore(&volume, &error) != IB_LOADER_ERR_MISSING_ARTIFACT) goto done;
        if (ib_fs_read_optional(&volume, IB_BOOTMGFW_PATH, &present, &after, &error) != IB_LOADER_OK || present) goto done;
        if (ib_fs_read(&volume, IB_BOOTMGFW_BACKUP, &after, &error) != IB_LOADER_OK || !equal(&expected, &after)) goto done;
        if (ib_fs_read(&volume, IB_BOOTMGFW_STAGING, &stage, &error) != IB_LOADER_OK || !equal(&expected, &stage)) goto done;
        trace("boot-only fixture: MISSING PARENT FAILS CLOSED\n");
    } else {
        if (ib_fs_read(&volume, IB_BOOTMGFW_BACKUP, &expected, &error) != IB_LOADER_OK) goto done;
        if (ib_bootmgfw_restore(&volume, &error) != IB_LOADER_OK) goto done;
        if (ib_fs_read(&volume, IB_BOOTMGFW_PATH, &after, &error) != IB_LOADER_OK || !equal(&expected, &after)) goto done;
        trace("boot-only fixture: RESTORE READBACK PASS\n");
        code = ib_fs_read_optional(&volume, IB_BOOTMGFW_STAGING, &present, &stage, &error);
        trace("stage absence code: "); trace_hex(code);
        trace("stage present: "); trace_hex(present);
        if (code != IB_LOADER_OK || present) goto done;
        {
            ib_efi_device_path_protocol *start_path = NULL;

            code = ib_fs_file_device_path(IB_BOOTMGFW_PATH, &start_path, &error);
            if (code == IB_LOADER_OK) {
                code = ib_bootmgfw_start(after.data, after.length, start_path, &error);
            }
            ib_fs_device_path_free(start_path);
        }
        trace("firmware start code: "); trace_hex(code);
        trace("firmware start status: "); trace_hex(error.status);
        if (code != IB_LOADER_ERR_UEFI || error.status != IB_EFI_LOAD_ERROR) goto done;
        trace("boot-only fixture: FIRMWARE CHILD RETURN PASS\n");
    }
    failed = 0;
done:
    ib_fs_close_volume(&volume);
    ib_bytes_free(&mode); ib_bytes_free(&before); ib_bytes_free(&expected);
    ib_bytes_free(&after); ib_bytes_free(&stage);
finish:
    trace(failed ? "boot-only fixture: FAIL\n" : "boot-only fixture: PASS\n");
    __outdword(0xf4, failed ? 0x11 : 0x10);
    return failed ? IB_EFI_DEVICE_ERROR : IB_EFI_SUCCESS;
}
#endif
