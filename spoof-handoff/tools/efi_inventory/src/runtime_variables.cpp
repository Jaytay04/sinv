#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "runtime_variables.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr std::size_t kMaximumSnapshotBytes = 16u * 1024u * 1024u;
constexpr std::size_t kMaximumVariables = 65536u;
constexpr std::size_t kVariableHeaderBytes = 32u;
constexpr std::size_t kMaximumNameCodeUnits = 1024u;
constexpr std::uint32_t kEfiVariableRuntimeAccess = 0x00000004u;
constexpr ULONG kSystemEnvironmentValueInformation = 2u;
constexpr std::size_t kInitialBufferBytes = 4096u;
constexpr std::size_t kMaximumQueryAttempts = 16u;
constexpr LONG kStatusInfoLengthMismatch = static_cast<LONG>(0xC0000004u);
constexpr LONG kStatusBufferOverflow = static_cast<LONG>(0x80000005u);
constexpr LONG kStatusBufferTooSmall = static_cast<LONG>(0xC0000023u);

using NtEnumerateSystemEnvironmentValuesExFn =
    LONG(NTAPI*)(ULONG informationClass, PVOID buffer, PULONG bufferLength);

std::uint16_t ReadU16(const std::uint8_t* data)
{
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(data[0]) |
        (static_cast<std::uint16_t>(data[1]) << 8u));
}

std::uint32_t ReadU32(const std::uint8_t* data)
{
    return static_cast<std::uint32_t>(data[0]) |
           (static_cast<std::uint32_t>(data[1]) << 8u) |
           (static_cast<std::uint32_t>(data[2]) << 16u) |
           (static_cast<std::uint32_t>(data[3]) << 24u);
}

std::uint32_t StatusBits(LONG status)
{
    return static_cast<std::uint32_t>(status);
}

std::wstring Win32Failure(const wchar_t* operation, DWORD code)
{
    return std::wstring(operation) + L" failed (Win32 error " +
           std::to_wstring(code) + L").";
}

std::wstring NtStatusFailure(LONG status)
{
    static constexpr wchar_t hex[] = L"0123456789ABCDEF";
    std::wstring result = L"NtEnumerateSystemEnvironmentValuesEx failed "
                          L"with NTSTATUS 0x";
    const std::uint32_t bits = StatusBits(status);
    for (int shift = 28; shift >= 0; shift -= 4) {
        result.push_back(hex[(bits >> static_cast<unsigned int>(shift)) & 0xFu]);
    }
    result.push_back(L'.');
    return result;
}

class ScopedEnvironmentPrivilege {
public:
    ScopedEnvironmentPrivilege() = default;
    ScopedEnvironmentPrivilege(const ScopedEnvironmentPrivilege&) = delete;
    ScopedEnvironmentPrivilege& operator=(const ScopedEnvironmentPrivilege&) = delete;

    ~ScopedEnvironmentPrivilege()
    {
        if (restoreRequired_ && !restored_) {
            std::wstring ignored;
            (void)Restore(ignored);
        }
        if (token_ != nullptr) {
            (void)CloseHandle(token_);
        }
    }

    bool Enable(std::wstring& error)
    {
        if (!OpenProcessToken(GetCurrentProcess(),
                              TOKEN_QUERY | TOKEN_ADJUST_PRIVILEGES,
                              &token_)) {
            error = Win32Failure(L"OpenProcessToken", GetLastError());
            return false;
        }

        TOKEN_ELEVATION elevation{};
        DWORD returnedBytes = 0u;
        if (!GetTokenInformation(token_, TokenElevation, &elevation,
                                 static_cast<DWORD>(sizeof(elevation)),
                                 &returnedBytes)) {
            error = Win32Failure(L"GetTokenInformation(TokenElevation)",
                                 GetLastError());
            return false;
        }
        if (returnedBytes < sizeof(elevation) || elevation.TokenIsElevated == 0u) {
            error = L"Runtime EFI variable capture requires an elevated process.";
            return false;
        }

        LUID privilege{};
        if (!LookupPrivilegeValueW(nullptr, SE_SYSTEM_ENVIRONMENT_NAME,
                                   &privilege)) {
            error = Win32Failure(L"LookupPrivilegeValue(SeSystemEnvironmentPrivilege)",
                                 GetLastError());
            return false;
        }

        TOKEN_PRIVILEGES requested{};
        requested.PrivilegeCount = 1u;
        requested.Privileges[0].Luid = privilege;
        requested.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

        DWORD previousBytes = 0u;
        SetLastError(ERROR_SUCCESS);
        const BOOL adjusted = AdjustTokenPrivileges(
            token_, FALSE, &requested, static_cast<DWORD>(sizeof(previous_)),
            &previous_, &previousBytes);
        const DWORD adjustError = GetLastError();
        if (!adjusted) {
            error = Win32Failure(L"AdjustTokenPrivileges(enable)", adjustError);
            return false;
        }
        if (adjustError == ERROR_NOT_ALL_ASSIGNED) {
            error = L"The elevated process token does not contain "
                    L"SeSystemEnvironmentPrivilege.";
            return false;
        }
        if (adjustError != ERROR_SUCCESS) {
            error = Win32Failure(L"AdjustTokenPrivileges(enable)", adjustError);
            return false;
        }

        const std::size_t minimumPreviousBytes =
            offsetof(TOKEN_PRIVILEGES, Privileges) + sizeof(LUID_AND_ATTRIBUTES);
        if (previousBytes < sizeof(previous_.PrivilegeCount) ||
            static_cast<std::size_t>(previousBytes) > sizeof(previous_)) {
            error = L"AdjustTokenPrivileges returned invalid previous privilege bounds.";
            return false;
        }
        if (previous_.PrivilegeCount == 0u) {
            // Windows can report only the count field when enabling an already
            // enabled privilege. No state changed, so there is nothing to undo.
            restoreRequired_ = false;
            return true;
        }
        if (previous_.PrivilegeCount != 1u ||
            static_cast<std::size_t>(previousBytes) < minimumPreviousBytes) {
            error = L"AdjustTokenPrivileges returned invalid previous privilege count.";
            return false;
        }
        if (previous_.Privileges[0].Luid.LowPart != privilege.LowPart ||
            previous_.Privileges[0].Luid.HighPart != privilege.HighPart) {
            error = L"AdjustTokenPrivileges returned a mismatched previous privilege.";
            return false;
        }

        restoreRequired_ =
            (previous_.Privileges[0].Attributes & SE_PRIVILEGE_ENABLED) == 0u;
        return true;
    }

    bool Restore(std::wstring& error)
    {
        if (!restoreRequired_ || restored_) {
            return true;
        }
        SetLastError(ERROR_SUCCESS);
        const BOOL adjusted = AdjustTokenPrivileges(
            token_, FALSE, &previous_, 0u, nullptr, nullptr);
        const DWORD adjustError = GetLastError();
        if (!adjusted || adjustError != ERROR_SUCCESS) {
            error = Win32Failure(L"AdjustTokenPrivileges(restore)", adjustError);
            return false;
        }
        restored_ = true;
        return true;
    }

private:
    HANDLE token_ = nullptr;
    TOKEN_PRIVILEGES previous_{};
    bool restoreRequired_ = false;
    bool restored_ = false;
};
bool IsResizeStatus(LONG status)
{
    return status == kStatusBufferTooSmall ||
           status == kStatusInfoLengthMismatch ||
           status == kStatusBufferOverflow;
}

void AppendHex(std::string& target, std::uint64_t value, unsigned int digits)
{
    static constexpr char hex[] = "0123456789ABCDEF";
    for (unsigned int index = digits; index > 0u; --index) {
        const unsigned int shift = (index - 1u) * 4u;
        target.push_back(hex[(value >> shift) & 0xFu]);
    }
}

void AppendGuid(std::string& target, const GUID& guid)
{
    target.push_back('{');
    AppendHex(target, guid.Data1, 8u);
    target.push_back('-');
    AppendHex(target, guid.Data2, 4u);
    target.push_back('-');
    AppendHex(target, guid.Data3, 4u);
    target.push_back('-');
    AppendHex(target, guid.Data4[0], 2u);
    AppendHex(target, guid.Data4[1], 2u);
    target.push_back('-');
    for (std::size_t index = 2u; index < 8u; ++index) {
        AppendHex(target, guid.Data4[index], 2u);
    }
    target.push_back('}');
}

bool HasValidSurrogatePairs(const std::wstring& value)
{
    static_assert(sizeof(wchar_t) == 2u,
                  "The Windows EFI inventory format stores UTF-16 code units.");
    for (std::size_t index = 0u; index < value.size(); ++index) {
        const std::uint16_t codeUnit = static_cast<std::uint16_t>(value[index]);
        if (codeUnit >= 0xD800u && codeUnit <= 0xDBFFu) {
            if (index + 1u >= value.size()) {
                return false;
            }
            const std::uint16_t low =
                static_cast<std::uint16_t>(value[index + 1u]);
            if (low < 0xDC00u || low > 0xDFFFu) {
                return false;
            }
            ++index;
        } else if (codeUnit >= 0xDC00u && codeUnit <= 0xDFFFu) {
            return false;
        }
    }
    return true;
}

}  // namespace

bool ParseRuntimeVariableSnapshot(const std::uint8_t* buffer,
                                  std::size_t length,
                                  std::vector<EfiVariable>& out,
                                  std::wstring& error)
{
    out.clear();
    error.clear();
    if (length > kMaximumSnapshotBytes) {
        error = L"EFI variable snapshot exceeds the 16 MiB limit.";
        return false;
    }
    if (length != 0u && buffer == nullptr) {
        error = L"EFI variable snapshot buffer is null.";
        return false;
    }
    if (length == 0u) {
        return true;
    }

    try {
        std::vector<EfiVariable> parsed;
        parsed.reserve((std::min)(length / kVariableHeaderBytes,
                                  kMaximumVariables));
        std::set<std::pair<std::array<std::uint8_t, 16>, std::wstring>> seen;

        std::size_t entryOffset = 0u;
        std::size_t entryCount = 0u;
        for (;;) {
            if (entryCount >= kMaximumVariables) {
                error = L"EFI variable snapshot exceeds the 65536 entry limit.";
                return false;
            }
            if ((entryOffset & 3u) != 0u || entryOffset > length ||
                length - entryOffset < kVariableHeaderBytes) {
                error = L"EFI variable entry header is out of bounds or misaligned.";
                return false;
            }

            const std::uint32_t nextEntryOffset = ReadU32(buffer + entryOffset);
            const std::uint32_t valueOffset = ReadU32(buffer + entryOffset + 4u);
            const std::uint32_t valueLength = ReadU32(buffer + entryOffset + 8u);
            const std::uint32_t attributes = ReadU32(buffer + entryOffset + 12u);
            const std::size_t remainingBytes = length - entryOffset;
            std::size_t recordBytes = remainingBytes;
            if (nextEntryOffset != 0u) {
                if ((nextEntryOffset & 3u) != 0u ||
                    static_cast<std::size_t>(nextEntryOffset) >= remainingBytes) {
                    error = L"EFI variable next-entry offset is invalid.";
                    return false;
                }
                recordBytes = static_cast<std::size_t>(nextEntryOffset);
            }
            if (recordBytes < kVariableHeaderBytes + sizeof(std::uint16_t)) {
                error = L"EFI variable entry is too short to contain a name.";
                return false;
            }

            const std::size_t recordEnd = entryOffset + recordBytes;
            std::wstring name;
            name.reserve(32u);
            std::size_t namePosition = entryOffset + kVariableHeaderBytes;
            bool terminated = false;
            std::size_t nameUnits = 0u;
            while (namePosition <= recordEnd &&
                   recordEnd - namePosition >= sizeof(std::uint16_t)) {
                const std::uint16_t codeUnit = ReadU16(buffer + namePosition);
                namePosition += sizeof(std::uint16_t);
                if (codeUnit == 0u) {
                    terminated = true;
                    break;
                }
                if (nameUnits >= kMaximumNameCodeUnits) {
                    error = L"EFI variable name exceeds the 1024 code-unit limit.";
                    return false;
                }
                name.push_back(static_cast<wchar_t>(codeUnit));
                ++nameUnits;
            }
            if (!terminated || name.empty()) {
                error = L"EFI variable name is empty or lacks a bounded UTF-16 NUL.";
                return false;
            }
            if (!HasValidSurrogatePairs(name)) {
                error = L"EFI variable name contains an invalid UTF-16 surrogate sequence.";
                return false;
            }

            const std::size_t metadataBytes = namePosition - entryOffset;
            if (nextEntryOffset != 0u &&
                static_cast<std::size_t>(nextEntryOffset) < metadataBytes) {
                error = L"EFI variable next-entry offset overlaps its name.";
                return false;
            }
            if ((valueOffset & 3u) != 0u ||
                static_cast<std::size_t>(valueOffset) > recordBytes) {
                error = L"EFI variable value offset is out of bounds or misaligned.";
                return false;
            }
            if (valueLength != 0u) {
                if (static_cast<std::size_t>(valueOffset) < metadataBytes ||
                    static_cast<std::size_t>(valueLength) >
                        recordBytes - static_cast<std::size_t>(valueOffset)) {
                    error = L"EFI variable value overlaps metadata or exceeds its record.";
                    return false;
                }
            }

            std::array<std::uint8_t, 16> guidBytes{};
            std::memcpy(guidBytes.data(), buffer + entryOffset + 16u,
                        guidBytes.size());
            GUID guid{};
            guid.Data1 = ReadU32(buffer + entryOffset + 16u);
            guid.Data2 = ReadU16(buffer + entryOffset + 20u);
            guid.Data3 = ReadU16(buffer + entryOffset + 22u);
            for (std::size_t index = 0u; index < 8u; ++index) {
                guid.Data4[index] = buffer[entryOffset + 24u + index];
            }
            if (!seen.emplace(guidBytes, name).second) {
                error = L"EFI variable snapshot contains a duplicate GUID and name.";
                return false;
            }

            if ((attributes & kEfiVariableRuntimeAccess) != 0u) {
                EfiVariable variable{};
                variable.name = std::move(name);
                variable.guid = guid;
                variable.attributes = attributes;
                variable.size = valueLength;
                variable.status = 0u;
                parsed.push_back(std::move(variable));
            }

            ++entryCount;
            if (nextEntryOffset == 0u) {
                break;
            }
            entryOffset += static_cast<std::size_t>(nextEntryOffset);
        }
        out.swap(parsed);
        return true;
    } catch (const std::bad_alloc&) {
        out.clear();
        error = L"Insufficient memory to parse EFI variable snapshot.";
        return false;
    }
}

bool CollectRuntimeVariables(std::vector<EfiVariable>& out,
                             std::wstring& error)
{
    out.clear();
    error.clear();

    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll == nullptr) {
        error = Win32Failure(L"GetModuleHandleW(ntdll.dll)", GetLastError());
        return false;
    }
    FARPROC symbol = GetProcAddress(ntdll, "NtEnumerateSystemEnvironmentValuesEx");
    if (symbol == nullptr) {
        error = Win32Failure(L"GetProcAddress(NtEnumerateSystemEnvironmentValuesEx)",
                             GetLastError());
        return false;
    }
    NtEnumerateSystemEnvironmentValuesExFn enumerate = nullptr;
    static_assert(sizeof(enumerate) == sizeof(symbol),
                  "Windows function pointers must have equal sizes.");
    std::memcpy(&enumerate, &symbol, sizeof(enumerate));

    try {
        std::vector<std::uint8_t> buffer(kInitialBufferBytes);
        LONG queryStatus = 0;
        ULONG snapshotLength = 0u;
        std::wstring queryError;
        bool querySucceeded = false;
        {
            ScopedEnvironmentPrivilege privilege;
            if (!privilege.Enable(error)) {
                return false;
            }

            for (std::size_t attempt = 0u;
                 attempt < kMaximumQueryAttempts; ++attempt) {
                if (buffer.empty() || buffer.size() > kMaximumSnapshotBytes) {
                    queryError = L"EFI variable snapshot buffer exceeded the 16 MiB limit.";
                    break;
                }
                snapshotLength = static_cast<ULONG>(buffer.size());
                queryStatus = enumerate(kSystemEnvironmentValueInformation,
                                        buffer.data(), &snapshotLength);
                if (queryStatus >= 0) {
                    if (snapshotLength > buffer.size()) {
                        queryError = L"NtEnumerateSystemEnvironmentValuesEx returned "
                                     L"a length larger than its buffer.";
                    } else {
                        querySucceeded = true;
                    }
                    break;
                }
                if (!IsResizeStatus(queryStatus)) {
                    queryError = NtStatusFailure(queryStatus);
                    break;
                }
                if (attempt + 1u >= kMaximumQueryAttempts) {
                    queryError = L"EFI variable enumeration exceeded its retry limit.";
                    break;
                }

                const std::size_t reported =
                    static_cast<std::size_t>(snapshotLength);
                std::size_t nextSize = reported;
                if (nextSize <= buffer.size()) {
                    nextSize = buffer.size() * 2u;
                }
                if (nextSize <= buffer.size() ||
                    nextSize > kMaximumSnapshotBytes) {
                    queryError = L"EFI variable snapshot requires more than 16 MiB.";
                    break;
                }
                buffer.resize(nextSize);
            }

            std::wstring restoreError;
            if (!privilege.Restore(restoreError)) {
                error = restoreError;
                if (!queryError.empty()) {
                    error += L" Collection also failed: " + queryError;
                }
                return false;
            }
        }

        if (!querySucceeded) {
            error = queryError.empty() ? NtStatusFailure(queryStatus) : queryError;
            return false;
        }
        return ParseRuntimeVariableSnapshot(buffer.data(),
                                            static_cast<std::size_t>(snapshotLength),
                                            out, error);
    } catch (const std::bad_alloc&) {
        out.clear();
        error = L"Insufficient memory to collect runtime EFI variables.";
        return false;
    }
}

std::string FormatRuntimeVariableReport(
    const std::vector<EfiVariable>& variables)
{
    static_assert(sizeof(wchar_t) == 2u,
                  "The EFI inventory report stores UTF-16 code units.");
    std::string report = "HVEFI001\r\n";
    std::uint32_t runtimeCount = 0u;
    for (const EfiVariable& variable : variables) {
        if ((variable.attributes & kEfiVariableRuntimeAccess) == 0u) {
            continue;
        }
        report.append("VAR\t");
        AppendGuid(report, variable.guid);
        report.push_back('\t');
        AppendHex(report, variable.attributes, 8u);
        report.push_back('\t');
        AppendHex(report, variable.size, 16u);
        report.push_back('\t');
        AppendHex(report, variable.status, 16u);
        report.push_back('\t');
        for (wchar_t codeUnit : variable.name) {
            AppendHex(report, static_cast<std::uint16_t>(codeUnit), 4u);
        }
        report.append("\r\n");
        ++runtimeCount;
    }
    report.append("END\t");
    AppendHex(report, runtimeCount, 8u);
    report.append("\r\n");
    return report;
}
