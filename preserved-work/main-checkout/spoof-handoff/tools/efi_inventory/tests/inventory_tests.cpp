#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "inventory.h"

#include <algorithm>
#include <array>
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

void AppendStructure(std::vector<std::uint8_t>& table,
                     const std::vector<std::uint8_t>& formatted,
                     const std::string& strings = {})
{
    table.insert(table.end(), formatted.begin(), formatted.end());
    table.insert(table.end(), strings.begin(), strings.end());
    table.push_back(0u);
    if (strings.empty()) {
        table.push_back(0u);
    }
}

void AppendEndOfTable(std::vector<std::uint8_t>& table)
{
    AppendStructure(table, {127u, 4u, 0xFFu, 0xFFu});
}

std::vector<std::uint8_t> Type4Fixture(std::size_t formatted_length)
{
    std::vector<std::uint8_t> formatted(formatted_length, 0u);
    formatted[0] = 4u;
    formatted[1] = static_cast<std::uint8_t>(formatted_length);
    formatted[2] = 0x34u;
    formatted[3] = 0x12u;
    formatted[4] = 1u;
    formatted[7] = 2u;
    if (formatted_length > 0x1Fu) {
        formatted[0x1Fu] = 6u;
    }
    if (formatted_length > 0x20u) {
        formatted[0x20u] = 3u;
    }
    if (formatted_length > 0x21u) {
        formatted[0x21u] = 4u;
    }
    if (formatted_length > 0x22u) {
        formatted[0x22u] = 5u;
    }
    std::vector<std::uint8_t> table;
    AppendStructure(table, formatted,
                    std::string("CPU Socket\0Vendor\0Serial\0Asset\0Part\0Poison\0", 43u));
    AppendEndOfTable(table);
    return table;
}

std::string FindSmbiosField(
    const std::vector<std::pair<std::string, std::string>>& fields,
    const std::string& name)
{
    const auto found = std::find_if(fields.begin(), fields.end(),
                                    [&](const auto& field) {
                                        return field.first == name;
                                    });
    return found == fields.end() ? std::string{} : found->second;
}

bool ParseFixture(const std::vector<std::uint8_t>& table,
                  std::vector<std::pair<std::string, std::string>>& fields,
                  std::wstring& error)
{
    return ParseSmbiosTableSnapshot(table.data(), table.size(), 3u, 9u,
                                    fields, error);
}

std::vector<std::uint8_t> Type44Fixture(std::uint16_t referenced_handle,
                                        std::uint8_t block_data_length,
                                        std::uint8_t architecture,
                                        bool include_type4)
{
    std::vector<std::uint8_t> table;
    if (include_type4) {
        std::vector<std::uint8_t> type4(8u, 0u);
        type4[0] = 4u;
        type4[1] = static_cast<std::uint8_t>(type4.size());
        type4[2] = 0x34u;
        type4[3] = 0x12u;
        type4[4] = 1u;
        type4[7] = 2u;
        AppendStructure(table, type4, std::string("CPU Socket\0Vendor\0", 18u));
    }
    const std::size_t type44_length = 8u + block_data_length;
    std::vector<std::uint8_t> type44(type44_length, 0u);
    type44[0] = 44u;
    type44[1] = static_cast<std::uint8_t>(type44_length);
    type44[2] = 0x78u;
    type44[3] = 0x56u;
    type44[4] = static_cast<std::uint8_t>(referenced_handle & 0xFFu);
    type44[5] = static_cast<std::uint8_t>(referenced_handle >> 8u);
    type44[6] = block_data_length;
    type44[7] = architecture;
    for (std::size_t i = 0u; i < block_data_length; ++i) {
        type44[8u + i] = static_cast<std::uint8_t>(0xA0u + i);
    }
    AppendStructure(table, type44);
    AppendEndOfTable(table);
    return table;
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

bool TestType4SerialFieldsUseSpecOffsetsAndLengthGuards()
{
    std::vector<std::pair<std::string, std::string>> fields;
    std::wstring error;
    if (!Check(ParseFixture(Type4Fixture(0x23u), fields, error),
               "full Type 4 fixture parses")) {
        return false;
    }
    if (!Check(FindSmbiosField(fields, "Processor serial number") == "Serial",
               "Type 4 serial uses offset 0x20") ||
        !Check(FindSmbiosField(fields, "Processor asset tag") == "Asset",
               "Type 4 asset tag uses offset 0x21") ||
        !Check(FindSmbiosField(fields, "Processor part number") == "Part",
               "Type 4 part number uses offset 0x22") ||
        !Check(FindSmbiosField(fields, "Processor serial number") != "Poison",
               "byte 0x1F is not misread as the processor serial")) {
        return false;
    }

    const std::array<std::size_t, 3> lengths = {0x20u, 0x21u, 0x22u};
    for (const std::size_t length : lengths) {
        fields.clear();
        if (!Check(ParseFixture(Type4Fixture(length), fields, error),
                   "truncated Type 4 fixture parses without out-of-bounds access")) {
            return false;
        }
        if (length == 0x20u &&
            !Check(FindSmbiosField(fields, "Processor serial number").empty(),
                   "length 0x20 omits the Type 4 serial field")) {
            return false;
        }
        if (length == 0x21u &&
            (!Check(FindSmbiosField(fields, "Processor serial number") == "Serial",
                    "length 0x21 includes only the Type 4 serial field") ||
             !Check(FindSmbiosField(fields, "Processor asset tag").empty(),
                    "length 0x21 omits the Type 4 asset field"))) {
            return false;
        }
        if (length == 0x22u &&
            (!Check(FindSmbiosField(fields, "Processor serial number") == "Serial",
                    "length 0x22 includes the Type 4 serial field") ||
             !Check(FindSmbiosField(fields, "Processor asset tag") == "Asset",
                    "length 0x22 includes the Type 4 asset field") ||
             !Check(FindSmbiosField(fields, "Processor part number").empty(),
                    "length 0x22 omits the Type 4 part-number field"))) {
            return false;
        }
    }
    return true;
}

bool TestType44AuditAndLengthValidation()
{
    std::vector<std::pair<std::string, std::string>> fields;
    std::wstring error;
    if (!Check(ParseFixture(Type44Fixture(0x1234u, 2u, 2u, true), fields, error),
               "valid Type 44 fixture parses")) {
        return false;
    }
    if (!Check(FindSmbiosField(fields,
                              "Processor additional information (Type 44) count") == "1",
               "Type 44 structure count is reported") ||
        !Check(FindSmbiosField(fields,
                              "Processor additional information [1] referenced Type 4 handle") ==
                   "0x1234 (matched Type 4)",
               "Type 44 reference is linked to its Type 4 handle") ||
        !Check(FindSmbiosField(fields,
                              "Processor additional information [1] processor-specific block data length") ==
                   "2",
               "Type 44 processor-specific data length is reported") ||
        !Check(FindSmbiosField(fields,
                              "Processor additional information [1] processor architecture/type") ==
                   "0x02 (x64 (x86-64))",
               "Type 44 architecture type is decoded") ||
        !Check(FindSmbiosField(fields,
                              "Processor additional information standardized serial mapping") ==
                   "None; Type 44 processor-specific data is not a standardized serial field",
               "Type 44 does not claim a standardized serial mapping")) {
        return false;
    }

    fields.clear();
    if (!Check(ParseFixture(Type44Fixture(0x9999u, 0u, 0xFEu, false), fields, error),
               "Type 44 without matching Type 4 still audits safely") ||
        !Check(FindSmbiosField(fields,
                              "Processor additional information [1] referenced Type 4 handle") ==
                   "0x9999 (no Type 4 found)",
               "unresolved Type 4 reference is explicit")) {
        return false;
    }

    fields.clear();
    std::vector<std::uint8_t> absent_table;
    AppendEndOfTable(absent_table);
    if (!Check(ParseFixture(absent_table, fields, error),
               "table with no Type 44 parses") ||
        !Check(FindSmbiosField(fields,
                              "Processor additional information (Type 44) count") == "0",
               "absent Type 44 structures report explicit count zero")) {
        return false;
    }

    for (const std::size_t malformed_length : {7u, 9u}) {
        std::vector<std::uint8_t> malformed;
        std::vector<std::uint8_t> type44(malformed_length, 0u);
        type44[0] = 44u;
        type44[1] = static_cast<std::uint8_t>(malformed_length);
        if (malformed_length >= 4u) {
            type44[2] = 0x78u;
            type44[3] = 0x56u;
        }
        if (malformed_length >= 6u) {
            type44[4] = 0x34u;
            type44[5] = 0x12u;
        }
        if (malformed_length >= 7u) {
            type44[6] = malformed_length == 7u ? 0u : 2u;
        }
        if (malformed_length >= 8u) {
            type44[7] = 2u;
        }
        AppendStructure(malformed, type44);
        AppendEndOfTable(malformed);
        fields.clear();
        if (!Check(!ParseFixture(malformed, fields, error),
                   "truncated or inconsistent Type 44 block length is rejected") ||
            !Check(fields.empty(), "failed Type 44 parse leaves output empty")) {
            return false;
        }
    }
    return true;
}

bool TestSmbiosParserRejectsInvalidInput()
{
    std::vector<std::pair<std::string, std::string>> fields;
    std::wstring error;
    return Check(!ParseSmbiosTableSnapshot(nullptr, 1u, 3u, 9u, fields, error),
                 "null SMBIOS table pointer is rejected") &&
           Check(fields.empty(), "invalid SMBIOS input leaves output empty");
}

}  // namespace

int main()
{
    if (!TestValidReport() || !TestRejectsMalformedReports() ||
        !TestFormattedInventoryIsReadOnly() ||
        !TestFormattedInventoryHasOutputBound() ||
        !TestType4SerialFieldsUseSpecOffsetsAndLengthGuards() ||
        !TestType44AuditAndLengthValidation() ||
        !TestSmbiosParserRejectsInvalidInput()) {
        return 1;
    }
    std::cout << "All EFI inventory tests passed.\n";
    return 0;
}
