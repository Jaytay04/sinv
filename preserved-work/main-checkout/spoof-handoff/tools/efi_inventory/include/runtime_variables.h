#pragma once

#include "inventory.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

bool CollectRuntimeVariables(std::vector<EfiVariable>& out,
                             std::wstring& error);

// Parses the bounded class-2 VARIABLE_NAME_AND_VALUE snapshot returned by
// NtEnumerateSystemEnvironmentValuesEx. ValueOffset is relative to its record.
bool ParseRuntimeVariableSnapshot(const std::uint8_t* buffer,
                                  std::size_t length,
                                  std::vector<EfiVariable>& out,
                                  std::wstring& error);

std::string FormatRuntimeVariableReport(
    const std::vector<EfiVariable>& variables);
