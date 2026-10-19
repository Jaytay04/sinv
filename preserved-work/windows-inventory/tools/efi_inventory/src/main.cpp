#include "backup.h"
#include "inventory.h"
#include "secure_pipe.h"
#include "runtime_variables.h"
#include "device_identifiers.h"
#include "read_only_inventory.h"

#include <Windows.h>
#include <sddl.h>

#include <algorithm>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

using hypersinv::efi_inventory::EnsurePipeKey;
using hypersinv::efi_inventory::RunPipeServer;
using hypersinv::efi_inventory::SendPipeRequest;

namespace {

void Usage() {
    std::wcout
        << L"Hypersinv EFI inventory (read-only firmware access)\n"
        << L"  efi_inventory capture-windows <new-variable-report> <new-identifier-report> [existing-smbios-report]\n"
        << L"  efi_inventory analyze-windows --same-host <existing-runtime-variable-report> <new-identifier-report> [current-smbios-report]\n"
        << L"  efi_inventory scan-windows <new-report-path>\n"
        << L"  efi_inventory scan-efi <EFI-collector-report> <new-report-path>\n"
        << L"  efi_inventory scan <EFI-collector-report> <new-report-path>\n"
        << L"  efi_inventory backup <EFI-collector-report> <new-backup-path>\n"
        << L"  efi_inventory serve\n"
        << L"  efi_inventory pipe-ping\n"
        << L"  efi_inventory pipe-smbios\n";
}

bool WriteNewRestrictedReport(const std::wstring& path,
                              const std::string& report,
                              std::wstring& error) {
    if (path.empty() || report.size() > 16u * 1024u * 1024u) {
        error = L"Report path is empty or report exceeds 16 MiB";
        return false;
    }
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        error = L"Could not inspect the current user token";
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
        error = L"Could not determine the current user SID";
        return false;
    }
    LPWSTR sid_text = nullptr;
    if (!ConvertSidToStringSidW(
            reinterpret_cast<TOKEN_USER*>(token_info.data())->User.Sid,
            &sid_text)) {
        error = L"Could not format the current user SID";
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
        error = L"Could not create report file security descriptor";
        return false;
    }
    SECURITY_ATTRIBUTES security{sizeof(security), descriptor, FALSE};
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, &security,
                              CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    const DWORD open_error = GetLastError();
    LocalFree(descriptor);
    if (file == INVALID_HANDLE_VALUE) {
        error = L"Could not create new report (Win32 " +
                std::to_wstring(open_error) + L"); existing files are not replaced";
        return false;
    }
    DWORD written = 0;
    const bool ok = WriteFile(file, report.data(),
                              static_cast<DWORD>(report.size()), &written,
                              nullptr) &&
                    written == report.size() && FlushFileBuffers(file);
    const DWORD write_error = ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(file);
    if (!ok) {
        DeleteFileW(path.c_str());  // removes only the new partial report
        error = L"Could not write report (Win32 " +
                std::to_wstring(write_error) + L")";
    }
    return ok;
}

bool BuildSmbiosReport(std::string& report, std::wstring& error) {
    std::vector<std::pair<std::string, std::string>> smbios;
    if (!CollectSmbios(smbios, error)) {
        return false;
    }
    report = FormatInventory({}, smbios);
    return true;
}

HANDLE CreateRestrictedCaptureFile(const std::wstring& path, std::wstring& error) {
    if (path.empty()) {
        error = L"Capture output path is empty";
        return INVALID_HANDLE_VALUE;
    }
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        error = L"Could not inspect the current user token";
        return INVALID_HANDLE_VALUE;
    }
    DWORD token_bytes = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &token_bytes);
    std::vector<BYTE> token_info(token_bytes);
    const bool have_user = token_bytes != 0 &&
        GetTokenInformation(token, TokenUser, token_info.data(), token_bytes, &token_bytes);
    CloseHandle(token);
    if (!have_user) {
        error = L"Could not determine the current user SID";
        return INVALID_HANDLE_VALUE;
    }
    LPWSTR sid_text = nullptr;
    if (!ConvertSidToStringSidW(
            reinterpret_cast<TOKEN_USER*>(token_info.data())->User.Sid, &sid_text)) {
        error = L"Could not format the current user SID";
        return INVALID_HANDLE_VALUE;
    }
    const std::wstring sddl = L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;" +
        std::wstring(sid_text) + L")";
    LocalFree(sid_text);
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr)) {
        error = L"Could not create capture security descriptor";
        return INVALID_HANDLE_VALUE;
    }
    SECURITY_ATTRIBUTES security{sizeof(security), descriptor, FALSE};
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, &security,
        CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    const DWORD code = GetLastError();
    LocalFree(descriptor);
    if (file == INVALID_HANDLE_VALUE) {
        error = L"Could not create new capture (Win32 " + std::to_wstring(code) +
            L"); existing files are never replaced: " + path;
    }
    return file;
}

class CaptureFile {
public:
    explicit CaptureFile(const std::wstring& path) : path_(path) {}
    CaptureFile(const CaptureFile&) = delete;
    CaptureFile& operator=(const CaptureFile&) = delete;
    ~CaptureFile() {
        if (file_ != INVALID_HANDLE_VALUE) {
            CloseHandle(file_);
            if (!keep_) DeleteFileW(path_.c_str()); // only our newly created partial file
        }
    }
    bool Open(std::wstring& error) {
        file_ = CreateRestrictedCaptureFile(path_, error);
        return file_ != INVALID_HANDLE_VALUE;
    }
    bool Write(const std::string& data, std::wstring& error) {
        if (data.size() > 16u * 1024u * 1024u) {
            error = L"Capture report exceeds 16 MiB";
            return false;
        }
        DWORD written = 0;
        if (!WriteFile(file_, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) ||
            written != data.size() || !FlushFileBuffers(file_)) {
            error = L"Could not write complete capture (Win32 " +
                std::to_wstring(GetLastError()) + L")";
            return false;
        }
        return true;
    }
    void Keep() { keep_ = true; }
private:
    std::wstring path_;
    HANDLE file_ = INVALID_HANDLE_VALUE;
    bool keep_ = false;
};

bool CheckSuppliedSmbiosReport(const std::wstring& path,
        const std::vector<std::pair<std::string, std::string>>& smbios,
        std::wstring& error) {
    if (path.empty()) return true;
    std::ifstream source(std::filesystem::path(path), std::ios::binary | std::ios::ate);
    if (!source) {
        error = L"Could not read supplied SMBIOS capture: " + path;
        return false;
    }
    const auto length = source.tellg();
    if (length < 0 || length > 16 * 1024 * 1024) {
        error = L"Supplied SMBIOS capture size is invalid";
        return false;
    }
    std::string input(static_cast<std::size_t>(length), '\0');
    source.seekg(0);
    if (!source.read(input.data(), static_cast<std::streamsize>(input.size()))) {
        error = L"Could not read complete supplied SMBIOS capture";
        return false;
    }
    if (input != FormatInventory({}, smbios)) {
        error = L"Supplied SMBIOS capture differs from the current Windows raw table; "
            L"refusing to combine stale captures or different machines";
        return false;
    }
    return true;
}

int CaptureWindowsUnchecked(const std::wstring& variable_path,
        const std::wstring& identifier_path, const std::wstring& smbios_path) {
    std::wstring error;
    CaptureFile variable_file(variable_path);
    CaptureFile identifier_file(identifier_path);
    // Reserve both new outputs before privileged reads. CREATE_NEW enforces no overwrite.
    if (!variable_file.Open(error) || !identifier_file.Open(error)) {
        std::wcerr << error << L'\n';
        return 1;
    }
    std::vector<EfiVariable> variables;
    std::vector<std::pair<std::string, std::string>> smbios;
    std::vector<hwid_spoofer::DeviceIdentifier> devices;
    if (!CollectRuntimeVariables(variables, error) ||
        !CollectSmbios(smbios, error) || !CheckSuppliedSmbiosReport(smbios_path, smbios, error) ||
        !hwid_spoofer::CollectDeviceIdentifiers(devices, error)) {
        std::wcerr << error << L'\n';
        return 1;
    }
    const auto variable_report = FormatRuntimeVariableReport(variables);
    const auto identifier_report = hwid_spoofer::FormatRuntimeIdentifierInventory(
        variables, smbios, devices, smbios_path);
    if (!variable_file.Write(variable_report, error) ||
        !identifier_file.Write(identifier_report, error)) {
        std::wcerr << error << L'\n';
        return 1;
    }
    variable_file.Keep();
    identifier_file.Keep();
    std::wcout << L"Captured " << variables.size() << L" runtime-visible EFI variables and "
        << smbios.size() << L" SMBIOS fields.\n"
        << L"Variable metadata: " << variable_path << L'\n'
        << L"Identifier coverage: " << identifier_path << L'\n'
        << L"Read-only capture complete; no firmware or hardware identifiers were changed.\n";
    return 0;
}

int CaptureWindows(const std::wstring& variable_path,
        const std::wstring& identifier_path, const std::wstring& smbios_path) {
    try {
        return CaptureWindowsUnchecked(variable_path, identifier_path, smbios_path);
    } catch (const std::exception& exception) {
        std::cerr << "Capture failed: " << exception.what()
                  << "; new partial reports were discarded.\n";
        return 1;
    }
}

int AnalyzeWindowsUnchecked(const std::wstring& variable_path,
        const std::wstring& identifier_path, const std::wstring& smbios_path) {
    std::wstring error;
    CaptureFile identifier_file(identifier_path);
    if (!identifier_file.Open(error)) {
        std::wcerr << error << L'\n';
        return 1;
    }
    std::vector<EfiVariable> variables;
    std::vector<std::pair<std::string, std::string>> smbios;
    std::vector<hwid_spoofer::DeviceIdentifier> devices;
    if (!ParseEfiReport(variable_path, variables, error) ||
        !CollectSmbios(smbios, error) ||
        !CheckSuppliedSmbiosReport(smbios_path, smbios, error) ||
        !hwid_spoofer::CollectDeviceIdentifiers(devices, error)) {
        std::wcerr << error << L'\n';
        return 1;
    }
    variables.erase(std::remove_if(variables.begin(), variables.end(),
        [](const EfiVariable& variable) {
            return (variable.attributes & 0x00000004u) == 0u;
        }), variables.end());
    const auto report = hwid_spoofer::FormatRuntimeIdentifierInventory(
        variables, smbios, devices, smbios_path, variable_path);
    if (!identifier_file.Write(report, error)) {
        std::wcerr << error << L'\n';
        return 1;
    }
    identifier_file.Keep();
    std::wcout << L"Analyzed " << variables.size() << L" runtime-access records from an existing same-host snapshot and "
        << smbios.size() << L" freshly read SMBIOS fields.\n"
        << L"Identifier coverage: " << identifier_path << L'\n'
        << L"Native runtime enumeration was not repeated; same-host provenance was asserted by the caller.\n";
    return 0;
}

int AnalyzeWindows(const std::wstring& variable_path,
        const std::wstring& identifier_path, const std::wstring& smbios_path) {
    try {
        return AnalyzeWindowsUnchecked(variable_path, identifier_path, smbios_path);
    } catch (const std::exception& exception) {
        std::cerr << "Analysis failed: " << exception.what()
                  << "; new partial report was discarded.\n";
        return 1;
    }
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        Usage();
        return 2;
    }
    const std::wstring command = argv[1];
    std::wstring error;

    if (command == L"capture-windows" && (argc == 4 || argc == 5)) {
        return CaptureWindows(argv[2], argv[3], argc == 5 ? argv[4] : L"");
    }
    if (command == L"analyze-windows" && (argc == 5 || argc == 6) &&
        std::wstring(argv[2]) == L"--same-host") {
        return AnalyzeWindows(argv[3], argv[4], argc == 6 ? argv[5] : L"");
    }
    if (command == L"scan-windows" && argc == 3) {
        std::string report;
        if (!BuildSmbiosReport(report, error) ||
            !WriteNewRestrictedReport(argv[2], report, error)) {
            std::wcerr << error << L'\n';
            return 1;
        }
        std::wcout << L"Wrote SMBIOS inventory to " << argv[2] << L'\n';
        return 0;
    }
    if (command == L"scan" && argc == 4) {
        std::vector<EfiVariable> variables;
        std::vector<std::pair<std::string, std::string>> smbios;
        if (!ParseEfiReport(argv[2], variables, error) ||
            !CollectSmbios(smbios, error) ||
            !WriteNewRestrictedReport(argv[3],
                                      FormatInventory(variables, smbios),
                                      error)) {
            std::wcerr << error << L'\n';
            return 1;
        }
        std::wcout << L"Wrote inventory of " << variables.size()
                   << L" EFI variables and SMBIOS fields to " << argv[3]
                   << L'\n';
        return 0;
    }
    if (command == L"scan-efi" && argc == 4) {
        std::vector<EfiVariable> variables;
        if (!ParseEfiReport(argv[2], variables, error) ||
            !WriteNewRestrictedReport(argv[3],
                                      FormatInventory(variables, {}), error)) {
            std::wcerr << error << L'\n';
            return 1;
        }
        std::wcout << L"Wrote inventory of " << variables.size()
                   << L" EFI variables to " << argv[3] << L'\n';
        return 0;
    }
    if (command == L"backup" && argc == 4) {
        std::vector<EfiVariable> variables;
        BackupSummary summary;
        if (!ParseEfiReport(argv[2], variables, error) ||
            !CreateEncryptedBackup(variables, argv[3], summary, error)) {
            std::wcerr << error << L'\n';
            return 1;
        }
        std::wcout << L"Saved " << summary.saved
                   << L" runtime EFI variables in a DPAPI-protected snapshot. "
                   << L"Skipped " << summary.skipped_not_runtime
                   << L" non-runtime/failed preboot records and "
                   << summary.skipped_unreadable << L" unreadable runtime records; "
                   << summary.changed_since_collection
                   << L" changed size or attributes since collection.\n"
                   << L"This snapshot is not a guaranteed restorable firmware image.\n";
        return 0;
    }
    if (command == L"serve" && argc == 2) {
        if (!EnsurePipeKey(error)) {
            std::wcerr << error << L'\n';
            return 1;
        }
        const auto handler = [](const std::string& request) -> std::string {
            if (request == "PING") {
                return "HVEFI-READ-ONLY\n";
            }
            if (request == "SMBIOS") {
                std::string report;
                std::wstring ignored;
                if (!BuildSmbiosReport(report, ignored)) {
                    return "ERROR: SMBIOS read failed\n";
                }
                return report;
            }
            return "ERROR: unsupported read-only request\n";
        };
        std::wcout << L"Serving read-only inventory requests on "
                      L"\\\\.\\pipe\\HypersinvEfiRuntimeCompanion\n";
        if (!RunPipeServer(handler, error)) {
            std::wcerr << error << L'\n';
            return 1;
        }
        return 0;
    }
    if ((command == L"pipe-ping" || command == L"pipe-smbios") && argc == 2) {
        std::string response;
        if (!SendPipeRequest(command == L"pipe-ping" ? "PING" : "SMBIOS",
                             response, error)) {
            std::wcerr << error << L'\n';
            return 1;
        }
        std::cout << response;
        return 0;
    }
    Usage();
    return 2;
}
