#ifndef SMBIOS_VALUES_H
#define SMBIOS_VALUES_H

#include "smbios_table.h"

/* Exact captured values and replacements from the 2026-10-12 client proof. */
static const SM_VALUES sm_host_originals = {{
    "3080-26", "MAINGEAR", "VYBE", "07D3710_LC1E186916",
    "Micro-Star International Co., Ltd.",
    "PRO B660M-A CEC WIFI DDR4 (MS-7D37)",
    "C88B4631-C88C-871B-A6D7-D8BBC1D85720"
}, 0, 0, 0};

static const SM_VALUES sm_host_replacements = {{
    "4091-37", "NORTHLAB", "NODE", "08E4821_MD2F297027",
    "Example Devices Corporation", "Client Reference Board (CB-4200)",
    "A17F24D0-68E2-4A09-9C31-2B7D8450E6F9"
}, 0, 0, 0};

static const char *const sm_field_names[SM_FIELD_COUNT] = {
    "system_serial", "system_vendor", "system_product", "board_serial",
    "board_vendor", "board_product", "system_uuid"
};

/* Device and unavailable identifiers have no enabled path in this variant. */
#define SM_ACTIVE_BOOT_MASK SM_MASK_ALL
#define SM_DISK_SERIAL_ENABLED 0
#define SM_ADAPTER_MAC_ENABLED 0
#define SM_CPU_SERIAL_ENABLED 0
#define SM_GPU_SERIAL_ENABLED 0

#endif
