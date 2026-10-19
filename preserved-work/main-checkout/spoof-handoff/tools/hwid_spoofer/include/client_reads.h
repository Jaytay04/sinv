#pragma once

#include "hwid_spoofer.h"

#include <cstdint>
#include <string>
#include <vector>

namespace hwid_spoofer::client {

struct Identifier {
    HWID_SPOOF_FIELD field;
    std::wstring device;
    std::wstring value;
};

// Reads only the nine confirmed device-fingerprint paths. Every invocation
// observes Windows anew; fixtures cannot replace this host reader.
bool ReadNativeIdentifiers(std::vector<Identifier>& values, std::wstring& error,
                           DWORD fieldMask = HWID_ALL_CONFIRMED_FIELDS);

// Client applications use this wrapper to observe process-local replacements.
bool ReadClientIdentifiers(std::vector<Identifier>& values, std::wstring& error,
                           DWORD fieldMask = HWID_ALL_CONFIRMED_FIELDS);

namespace detail {
// Parses a GetSystemFirmwareTable('RSMB') result; exposed for bounded fixtures.
bool ParseConfirmedSmbios(const std::vector<std::uint8_t>& bytes,
                          std::vector<Identifier>& values,
                          std::wstring& error);
}

}  // namespace hwid_spoofer::client
