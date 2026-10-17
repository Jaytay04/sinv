#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <guiddef.h>

struct EfiVariable {
    std::wstring name;
    GUID guid{};
    std::uint32_t attributes = 0;
    std::uint64_t size = 0;
    std::uint64_t status = 0;
};

bool ParseEfiReport(const std::wstring& path,
                    std::vector<EfiVariable>& out,
                    std::wstring& error);

bool CollectSmbios(
    std::vector<std::pair<std::string, std::string>>& fields,
    std::wstring& error);

std::string FormatInventory(
    const std::vector<EfiVariable>& efi,
    const std::vector<std::pair<std::string, std::string>>& smbios);
