#pragma once

#include <string>
#include <utility>
#include <vector>

#include "device_identifiers.h"
#include "inventory.h"

namespace hwid_spoofer {

// Formats a sidecar report that maps runtime EFI name leads, Windows device
// observations, and the supplied Windows raw SMBIOS capture to HWID config
// members. Name-only EFI leads are
// explicitly dropped; EFI variable payloads are never included.
std::string FormatRuntimeIdentifierInventory(
    const std::vector<EfiVariable>& variables,
    const std::vector<std::pair<std::string, std::string>>& smbios,
    const std::vector<DeviceIdentifier>& devices,
    const std::wstring& supplied_smbios_path,
    const std::wstring& runtime_metadata_report_path = {});

}  // namespace hwid_spoofer
