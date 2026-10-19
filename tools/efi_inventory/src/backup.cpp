#include "backup.h"

#include <Windows.h>
#include <objbase.h>
#include <sddl.h>
#include <wincrypt.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <limits>
#include <vector>

namespace {

constexpr std::size_t kMaxValue = 1024u * 1024u;
constexpr std::size_t kMaxSnapshot = 16u * 1024u * 1024u;
constexpr DWORD kRuntimeAccess = 0x00000004u;

std::wstring Win32Error(const wchar_t* operation, DWORD code) {
    return std::wstring(operation) + L" failed (Win32 " +
           std::to_wstring(code) + L")";
}

bool EnableFirmwareReadPrivilege(std::wstring& error) {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY,
                          &token)) {
        error = Win32Error(L"OpenProcessToken", GetLastError());
        return false;
    }
    LUID luid{};
    if (!LookupPrivilegeValueW(nullptr, SE_SYSTEM_ENVIRONMENT_NAME, &luid)) {
        error = Win32Error(L"LookupPrivilegeValue", GetLastError());
        CloseHandle(token);
        return false;
    }
    TOKEN_PRIVILEGES privileges{};
    privileges.PrivilegeCount = 1;
    privileges.Privileges[0].Luid = luid;
    privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    SetLastError(ERROR_SUCCESS);
    const BOOL adjusted = AdjustTokenPrivileges(token, FALSE, &privileges, 0,
                                                nullptr, nullptr);
    const DWORD code = GetLastError();
    CloseHandle(token);
    if (!adjusted || code != ERROR_SUCCESS) {
        error = Win32Error(L"Enabling SeSystemEnvironmentPrivilege", code);
        return false;
    }
    return true;
}

std::wstring GuidString(const GUID& guid) {
    wchar_t text[40]{};
    if (StringFromGUID2(guid, text, static_cast<int>(std::size(text))) == 0) {
        return {};
    }
    return text;
}

bool ReadVariable(const EfiVariable& variable, std::vector<BYTE>& value,
                  DWORD& attributes) {
    const std::wstring guid = GuidString(variable.guid);
    if (guid.empty()) {
        return false;
    }
    for (std::size_t capacity = 256; capacity <= kMaxValue; capacity *= 2) {
        if (!value.empty()) {
            SecureZeroMemory(value.data(), value.size());
        }
        value.resize(capacity);
        attributes = 0;
        const DWORD size = GetFirmwareEnvironmentVariableExW(
            variable.name.c_str(), guid.c_str(), value.data(),
            static_cast<DWORD>(value.size()), &attributes);
        if (size != 0) {
            value.resize(size);
            return true;
        }
        const DWORD code = GetLastError();
        if (code != ERROR_INSUFFICIENT_BUFFER && code != ERROR_BUFFER_OVERFLOW) {
            SecureZeroMemory(value.data(), value.size());
            value.clear();
            return false;
        }
    }
    if (!value.empty()) {
        SecureZeroMemory(value.data(), value.size());
    }
    value.clear();
    return false;
}

void Append32(std::vector<BYTE>& out, std::uint32_t value) {
    for (unsigned shift = 0; shift != 32; shift += 8) {
        out.push_back(static_cast<BYTE>(value >> shift));
    }
}

void AppendBytes(std::vector<BYTE>& out, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const BYTE*>(data);
    out.insert(out.end(), bytes, bytes + size);
}

bool WriteNewRestrictedFile(const std::wstring& path, const DATA_BLOB& encrypted,
                            std::wstring& error) {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        error = Win32Error(L"OpenProcessToken", GetLastError());
        return false;
    }
    DWORD token_bytes = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &token_bytes);
    std::vector<BYTE> token_info(token_bytes);
    const bool have_user = token_bytes != 0 &&
        GetTokenInformation(token, TokenUser, token_info.data(), token_bytes,
                            &token_bytes) != FALSE;
    CloseHandle(token);
    if (!have_user) {
        error = L"Could not determine the current user SID for backup ACL";
        return false;
    }
    LPWSTR sid_text = nullptr;
    if (!ConvertSidToStringSidW(
            reinterpret_cast<TOKEN_USER*>(token_info.data())->User.Sid,
            &sid_text)) {
        error = Win32Error(L"ConvertSidToStringSidW", GetLastError());
        return false;
    }
    const std::wstring sddl =
        L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;" +
        std::wstring(sid_text) + L")";
    LocalFree(sid_text);
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            sddl.c_str(), SDDL_REVISION_1,
            &descriptor, nullptr)) {
        error = Win32Error(L"Creating backup security descriptor", GetLastError());
        return false;
    }
    SECURITY_ATTRIBUTES security{sizeof(security), descriptor, FALSE};
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, &security,
                              CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    const DWORD open_error = GetLastError();
    LocalFree(descriptor);
    if (file == INVALID_HANDLE_VALUE) {
        error = Win32Error(L"Creating backup (existing files are never replaced)",
                           open_error);
        return false;
    }
    DWORD written = 0;
    const bool ok = WriteFile(file, encrypted.pbData, encrypted.cbData,
                              &written, nullptr) &&
                    written == encrypted.cbData && FlushFileBuffers(file);
    const DWORD write_error = ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(file);
    if (!ok) {
        // Only the exact file created above is removed; no prior file existed.
        DeleteFileW(path.c_str());
        error = Win32Error(L"Writing backup", write_error);
    }
    return ok;
}

}  // namespace

bool CreateEncryptedBackup(const std::vector<EfiVariable>& variables,
                           const std::wstring& destination,
                           BackupSummary& summary,
                           std::wstring& error) {
    summary = {};
    if (destination.empty() || variables.size() > 8192) {
        error = L"Invalid backup destination or EFI record count";
        return false;
    }
    if (!EnableFirmwareReadPrivilege(error)) {
        return false;
    }

    std::vector<BYTE> plain;
    constexpr char magic[] = "HVBK001\0";
    AppendBytes(plain, magic, sizeof(magic) - 1);
    Append32(plain, 1);  // format version
    Append32(plain, 0);  // record count, filled in after collection

    for (const EfiVariable& variable : variables) {
        if (variable.status != 0 ||
            (variable.attributes & kRuntimeAccess) == 0) {
            ++summary.skipped_not_runtime;
            continue;
        }
        std::vector<BYTE> value;
        DWORD attributes = 0;
        if (!ReadVariable(variable, value, attributes)) {
            ++summary.skipped_unreadable;
            continue;
        }
        if (attributes != variable.attributes || value.size() != variable.size) {
            ++summary.changed_since_collection;
        }
        const std::size_t name_bytes = variable.name.size() * sizeof(wchar_t);
        if (name_bytes == 0 || name_bytes > 65534 ||
            plain.size() + 4 + 16 + 4 + 4 + name_bytes + value.size() >
                kMaxSnapshot) {
            error = L"Backup exceeds the 16 MiB safety limit";
            SecureZeroMemory(value.data(), value.size());
            SecureZeroMemory(plain.data(), plain.size());
            return false;
        }
        Append32(plain, static_cast<std::uint32_t>(name_bytes));
        AppendBytes(plain, &variable.guid, sizeof(variable.guid));
        Append32(plain, attributes);
        Append32(plain, static_cast<std::uint32_t>(value.size()));
        AppendBytes(plain, variable.name.data(), name_bytes);
        AppendBytes(plain, value.data(), value.size());
        ++summary.saved;
        SecureZeroMemory(value.data(), value.size());
    }
    const auto count = static_cast<std::uint32_t>(summary.saved);
    if (count == 0) {
        error = L"No runtime EFI variables could be read; no backup was created";
        SecureZeroMemory(plain.data(), plain.size());
        return false;
    }
    for (unsigned i = 0; i < 4; ++i) {
        plain[12 + i] = static_cast<BYTE>(count >> (8 * i));
    }

    DATA_BLOB input{static_cast<DWORD>(plain.size()), plain.data()};
    DATA_BLOB encrypted{};
    if (!CryptProtectData(&input, L"Hypersinv read-only EFI inventory backup",
                          nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN,
                          &encrypted)) {
        error = Win32Error(L"CryptProtectData", GetLastError());
        SecureZeroMemory(plain.data(), plain.size());
        return false;
    }
    SecureZeroMemory(plain.data(), plain.size());
    const bool ok = WriteNewRestrictedFile(destination, encrypted, error);
    LocalFree(encrypted.pbData);
    return ok;
}
