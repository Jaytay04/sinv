#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "inventory.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

class TemporaryReport {
public:
    TemporaryReport()
    {
        wchar_t directory[MAX_PATH]{};
        wchar_t file[MAX_PATH]{};
        if (GetTempPathW(MAX_PATH, directory) == 0u ||
            GetTempFileNameW(directory, L"hvi", 0u, file) == 0u) {
            return;
        }
        path_ = file;
    }

    ~TemporaryReport()
    {
        if (!path_.empty()) {
            (void)DeleteFileW(path_.c_str());
        }
    }

    bool valid() const { return !path_.empty(); }
    const std::wstring& path() const { return path_; }

    bool Write(const std::string& contents) const
    {
        HANDLE file = CreateFileW(path_.c_str(), GENERIC_WRITE, 0u, nullptr,
                                  CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            return false;
        }
        std::size_t offset = 0u;
        bool success = true;
        while (offset < contents.size()) {
            const DWORD requested = static_cast<DWORD>((std::min)(
                contents.size() - offset, static_cast<std::size_t>(1u << 20u)));
            DWORD written = 0u;
            if (!WriteFile(file, contents.data() + offset, requested,
                           &written, nullptr) || written == 0u) {
                success = false;
                break;
            }
            offset += written;
        }
        if (!CloseHandle(file)) {
            success = false;
        }
        return success;
    }

private:
    std::wstring path_;
};

std::string ValidReport()
{
    return "HVEFI001\r\n"
           "VAR\t{8BE4DF61-93CA-11D2-AA0D-00E098032B8C}\t00000007\t"
           "0000000000000003\t0000000000000000\t0042006F006F0074004F0072006400650072\r\n"
           "END\t00000001\r\n";
}

bool Check(bool condition, const char* description)
{
    if (!condition) {
        std::cerr << "FAILED: " << description << '\n';
    }
    return condition;
}

bool TestValidReport()
{
    TemporaryReport fixture;
    std::vector<EfiVariable> variables;
    std::wstring error;
    if (!Check(fixture.valid(), "temporary file path is available") ||
        !Check(fixture.Write(ValidReport()), "valid fixture is written") ||
        !Check(ParseEfiReport(fixture.path(), variables, error),
               "valid EFI report parses")) {
        return false;
    }
    return Check(variables.size() == 1u, "one EFI variable parsed") &&
           Check(variables[0].name == L"BootOrder", "UTF-16 name decoded") &&
           Check(variables[0].guid.Data1 == 0x8BE4DF61u, "GUID decoded") &&
           Check(variables[0].attributes == 7u, "attributes decoded") &&
           Check(variables[0].size == 3u, "size decoded") &&
           Check(variables[0].status == 0u, "status decoded");
}

bool TestRejectsMalformedReports()
{
    const std::vector<std::pair<std::string, const char*>> invalid = {
        {"HVEFI001\nEND\t00000000\r\n", "LF-only line endings rejected"},
        {"HVEFI001\r\nEND\t00000001\r\n", "incorrect footer count rejected"},
        {"HVEFI001\r\nEND\t00000000\r\nTRAIL\r\n", "trailing records rejected"},
        {"HVEFI001\r\nVAR\t{8be4df61-93CA-11D2-AA0D-00E098032B8C}\t00000007\t0000000000000003\t0000000000000000\t0042\r\nEND\t00000001\r\n", "lowercase GUID rejected"},
        {"HVEFI001\r\nVAR\t{8BE4DF61-93CA-11D2-AA0D-00E098032B8C}\t00000007\t0000000000000003\t0000000000000000\tD800\r\nEND\t00000001\r\n", "unpaired UTF-16 surrogate rejected"},
        {"HVEFI001\r\nVAR\t{8BE4DF61-93CA-11D2-AA0D-00E098032B8C}\t00000007\t0000000000000003\t0000000000000000\t0042\r\nEND\t00000001", "truncated footer rejected"},
    };
    for (const auto& sample : invalid) {
        TemporaryReport fixture;
        std::vector<EfiVariable> variables;
        std::wstring error;
        if (!Check(fixture.valid(), "temporary file path is available") ||
            !Check(fixture.Write(sample.first), "invalid fixture is written")) {
            return false;
        }
        if (!Check(!ParseEfiReport(fixture.path(), variables, error), sample.second) ||
            !Check(variables.empty(), "failed parse leaves output empty")) {
            return false;
        }
    }
    return true;
}

bool TestFormattedInventoryIsReadOnly()
{
    EfiVariable variable{};
    variable.name = L"Vendor\r\nForged: yes\tSerialNumber";
    variable.guid.Data1 = 0x12345678u;
    variable.guid.Data2 = 0x1234u;
    variable.guid.Data3 = 0x5678u;
    const std::string report = FormatInventory(
        {variable}, {{"System UUID", "Not specified\r\nInjected field"}});
    return Check(report.find("read-only") != std::string::npos,
                 "formatted report states read-only access") &&
           Check(report.find("Candidate detection: heuristic only") != std::string::npos,
                 "formatted report qualifies candidate heuristics") &&
           Check(report.find("Review candidate (heuristic): yes") != std::string::npos,
                 "vendor namespace is flagged for review") &&
           Check(report.find("Vendor\\x0D\\x0AForged: yes\\x09SerialNumber") !=
                     std::string::npos,
                 "EFI names escape report control characters") &&
           Check(report.find("Not specified\\x0D\\x0AInjected field") !=
                     std::string::npos,
                 "SMBIOS values escape report control characters") &&
           Check(report.find("\r\nForged: yes") == std::string::npos &&
                     report.find("\r\nInjected field") == std::string::npos,
                 "untrusted report fields cannot forge new lines");
}

bool TestFormattedInventoryHasOutputBound()
{
    const std::string oversized(17u * 1024u * 1024u, 'X');
    const std::string report = FormatInventory({}, {{"Oversized field", oversized}});
    return Check(report.size() <= 16u * 1024u * 1024u,
                 "formatted report stays within its output bound") &&
           Check(report.find("Inventory report truncated") != std::string::npos,
                 "truncated report carries an explicit marker");
}

}  // namespace

int main()
{
    if (!TestValidReport() || !TestRejectsMalformedReports() ||
        !TestFormattedInventoryIsReadOnly() ||
        !TestFormattedInventoryHasOutputBound()) {
        return 1;
    }
    std::cout << "All EFI inventory tests passed.\n";
    return 0;
}
