/* Canary checks for the real formatter and ABI checks for the port header. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "ib_debug.h"

_Static_assert(sizeof(ib_efi_table_header) == 24, "table header ABI");
_Static_assert(offsetof(ib_efi_boot_services, handle_protocol) == 152, "HandleProtocol ABI");
_Static_assert(offsetof(ib_efi_boot_services, load_image) == 200, "LoadImage ABI");
_Static_assert(offsetof(ib_efi_boot_services, start_image) == 208, "StartImage ABI");
_Static_assert(offsetof(ib_efi_boot_services, unload_image) == 224, "UnloadImage ABI");
_Static_assert(offsetof(ib_efi_boot_services, open_protocol) == 280, "OpenProtocol ABI");
_Static_assert(offsetof(ib_efi_system_table, boot_services) == 96, "system table ABI");
_Static_assert(offsetof(ib_efi_loaded_image_protocol, image_base) == 64, "image base ABI");
_Static_assert(offsetof(ib_efi_file_info, file_name) == 80, "FileInfo name ABI");

int main(void) {
    struct { unsigned char before[32]; char text[512]; unsigned char after[32]; } guard;
    const uint32_t flags[] = {0, 7, 0x3f, 0x7f, 0xff, UINT32_MAX};
    const size_t sizes[] = {0, 1, 8, sizeof(guard.text)};
    size_t i, j, k;
    /* UEFI Device Path Utilities protocol, independent specification bytes. */
    const unsigned char utilities_tail[] = {0xb0, 0x37, 0xed, 0xb8, 0x2f, 0xb7, 0x72, 0xa4};
    if (ib_guid_device_path_utilities.data1 != 0x0379be4e ||
        ib_guid_device_path_utilities.data2 != 0xd706 ||
        ib_guid_device_path_utilities.data3 != 0x437d ||
        memcmp(ib_guid_device_path_utilities.data4, utilities_tail, 8) != 0) return 4;
    for (i = 0; i < sizeof(flags) / sizeof(flags[0]); i++) {
        for (j = 0; j < sizeof(sizes) / sizeof(sizes[0]); j++) {
            memset(&guard, 0xa5, sizeof(guard));
            ib_variable_attributes_debug(flags[i], guard.text, sizes[j]);
            for (k = 0; k < sizeof(guard.before); k++) {
                if (guard.before[k] != 0xa5 || guard.after[k] != 0xa5) return 1;
            }
            for (k = sizes[j]; k < sizeof(guard.text); k++) {
                if ((unsigned char)guard.text[k] != 0xa5) return 2;
            }
            if (sizes[j] && memchr(guard.text, 0, sizes[j]) == NULL) return 3;
        }
    }
    puts("ib_port_safety_test: PASS (24 formatter cases and port x64 ABI)");
    return 0;
}
