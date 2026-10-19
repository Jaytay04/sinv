#pragma once

#include "inventory.h"

#include <cstddef>
#include <string>
#include <vector>

struct BackupSummary {
    std::size_t saved = 0;
    std::size_t skipped_not_runtime = 0;
    std::size_t skipped_unreadable = 0;
    std::size_t changed_since_collection = 0;
};

// Stores readable runtime variables in a DPAPI user-scoped, authenticated
// snapshot. This does not modify firmware and is not a restore operation.
bool CreateEncryptedBackup(const std::vector<EfiVariable>& variables,
                           const std::wstring& destination,
                           BackupSummary& summary,
                           std::wstring& error);
