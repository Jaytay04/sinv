#include "ib_uefi.h"

static ib_efi_system_table *ib_efi_system_table_value;
static ib_efi_handle ib_efi_image_handle_value;

void ib_efi_set_system_table(ib_efi_system_table *table) {
    ib_efi_system_table_value = table;
}

ib_efi_system_table *ib_efi_get_system_table(void) {
    return ib_efi_system_table_value;
}

void ib_efi_set_image_handle(ib_efi_handle handle) {
    ib_efi_image_handle_value = handle;
}

ib_efi_handle ib_efi_image_handle(void) {
    return ib_efi_image_handle_value;
}
