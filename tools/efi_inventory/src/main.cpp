#include "backup.h"
#include "inventory.h"
#include "secure_pipe.h"

#include <Windows.h>
#include <sddl.h>

#include <iostream>
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

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        Usage();
        return 2;
    }
    const std::wstring command = argv[1];
    std::wstring error;

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
