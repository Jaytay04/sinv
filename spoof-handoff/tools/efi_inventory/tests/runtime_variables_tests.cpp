#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "inventory.h"
#include "runtime_variables.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

struct VariableInput {
    std::wstring name;
    GUID guid{};
    std::uint32_t attributes = 0u;
    std::vector<std::uint8_t> value;
};

bool Check(bool condition, const char* description)
{
    if (!condition) {
        std::cerr << "FAILED: " << description << std::endl;
    }
    return condition;
}

std::size_t Align4(std::size_t value)
{
    return (value + 3u) & ~static_cast<std::size_t>(3u);
}

void WriteU16(std::vector<std::uint8_t>& bytes, std::size_t offset,
              std::uint16_t value)
{
    bytes[offset] = static_cast<std::uint8_t>(value & 0xFFu);
    bytes[offset + 1u] = static_cast<std::uint8_t>(value >> 8u);
}

void WriteU32(std::vector<std::uint8_t>& bytes, std::size_t offset,
              std::uint32_t value)
{
    bytes[offset] = static_cast<std::uint8_t>(value & 0xFFu);
    bytes[offset + 1u] = static_cast<std::uint8_t>((value >> 8u) & 0xFFu);
    bytes[offset + 2u] = static_cast<std::uint8_t>((value >> 16u) & 0xFFu);
    bytes[offset + 3u] = static_cast<std::uint8_t>((value >> 24u) & 0xFFu);
}

std::vector<std::uint8_t> BuildSnapshot(
    const std::vector<VariableInput>& variables)
{
    if (variables.empty()) {
        return {};
    }

    std::vector<std::size_t> recordSizes;
    recordSizes.reserve(variables.size());
    std::size_t totalBytes = 0u;
    for (const VariableInput& variable : variables) {
        const std::size_t nameEnd = 32u + (variable.name.size() + 1u) * 2u;
        const std::size_t valueOffset = Align4(nameEnd);
        const std::size_t recordBytes = Align4(valueOffset + variable.value.size());
        recordSizes.push_back(recordBytes);
        totalBytes += recordBytes;
    }

    std::vector<std::uint8_t> bytes(totalBytes, 0u);
    std::size_t entryOffset = 0u;
    for (std::size_t index = 0u; index < variables.size(); ++index) {
        const VariableInput& variable = variables[index];
        const std::size_t recordBytes = recordSizes[index];
        const std::size_t nameEnd = 32u + (variable.name.size() + 1u) * 2u;
        const std::size_t valueOffset = Align4(nameEnd);
        WriteU32(bytes, entryOffset,
                 index + 1u < variables.size()
                     ? static_cast<std::uint32_t>(recordBytes)
                     : 0u);
        WriteU32(bytes, entryOffset + 4u,
                 static_cast<std::uint32_t>(valueOffset));
        WriteU32(bytes, entryOffset + 8u,
                 static_cast<std::uint32_t>(variable.value.size()));
        WriteU32(bytes, entryOffset + 12u, variable.attributes);
        WriteU32(bytes, entryOffset + 16u, variable.guid.Data1);
        WriteU16(bytes, entryOffset + 20u, variable.guid.Data2);
        WriteU16(bytes, entryOffset + 22u, variable.guid.Data3);
        for (std::size_t byteIndex = 0u; byteIndex < 8u; ++byteIndex) {
            bytes[entryOffset + 24u + byteIndex] = variable.guid.Data4[byteIndex];
        }
        for (std::size_t nameIndex = 0u; nameIndex < variable.name.size();
             ++nameIndex) {
            WriteU16(bytes, entryOffset + 32u + nameIndex * 2u,
                     static_cast<std::uint16_t>(variable.name[nameIndex]));
        }
        if (!variable.value.empty()) {
            std::copy(variable.value.begin(), variable.value.end(),
                      bytes.begin() + static_cast<std::ptrdiff_t>(entryOffset + valueOffset));
        }
        entryOffset += recordBytes;
    }
    return bytes;
}

GUID Guid(std::uint32_t data1)
{
    GUID guid{};
    guid.Data1 = data1;
    guid.Data2 = 0x1234u;
    guid.Data3 = 0xABCDu;
    guid.Data4[0] = 0x10u;
    guid.Data4[1] = 0x20u;
    guid.Data4[2] = 0x30u;
    guid.Data4[3] = 0x40u;
    guid.Data4[4] = 0x50u;
    guid.Data4[5] = 0x60u;
    guid.Data4[6] = 0x70u;
    guid.Data4[7] = 0x80u;
    return guid;
}

bool Parse(const std::vector<std::uint8_t>& bytes,
           std::vector<EfiVariable>& variables, std::wstring& error)
{
    return ParseRuntimeVariableSnapshot(bytes.empty() ? nullptr : bytes.data(),
                                        bytes.size(), variables, error);
}

bool TestRuntimeFilteringAndRawMetadata()
{
    const std::vector<VariableInput> fixture = {
        {L"BootOrder", Guid(0x01020304u), 0x80000007u, {0x01u, 0x02u}},
        {L"BootCurrent", Guid(0x11121314u), 0x00000003u, {0x03u}},
        {L"BoardSerial", Guid(0x21222324u), 0xA0000004u,
         {0x41u, 0x42u, 0x43u}},
    };
    const std::vector<std::uint8_t> bytes = BuildSnapshot(fixture);
    std::vector<EfiVariable> variables;
    std::wstring error;
    if (!Check(Parse(bytes, variables, error),
               "valid runtime variable snapshot parses")) {
        return false;
    }
    return Check(variables.size() == 2u,
                 "entries without EFI_VARIABLE_RUNTIME_ACCESS are dropped") &&
           Check(variables[0].name == L"BootOrder" &&
                     variables[1].name == L"BoardSerial",
                 "runtime-visible names retain native order") &&
           Check(variables[0].attributes == 0x80000007u &&
                     variables[1].attributes == 0xA0000004u,
                 "raw EFI attribute bits are preserved") &&
           Check(variables[0].size == 2u && variables[1].size == 3u,
                 "value lengths are reported without copying payloads") &&
           Check(variables[0].status == 0u && variables[1].status == 0u,
                 "successful native snapshot records EFI success status") &&
           Check(variables[0].guid.Data1 == 0x01020304u,
                 "GUID fields are decoded at their fixed offsets");
}

bool Rejects(const std::vector<std::uint8_t>& bytes,
             const char* description)
{
    std::vector<EfiVariable> variables;
    variables.push_back(EfiVariable{});
    std::wstring error;
    return Check(!Parse(bytes, variables, error), description) &&
           Check(variables.empty(), "failed parse leaves output empty");
}

bool TestBoundsAndAlignmentValidation()
{
    const std::vector<VariableInput> one = {
        {L"X", Guid(1u), 0x00000004u, {0xAAu, 0xBBu, 0xCCu}},
    };
    const std::vector<std::uint8_t> valid = BuildSnapshot(one);
    std::vector<std::uint8_t> invalid = valid;

    WriteU32(invalid, 0u, 35u);
    if (!Rejects(invalid, "unaligned next-entry offset is rejected")) {
        return false;
    }
    invalid = valid;
    WriteU32(invalid, 0u, 0xFFFFFFFCu);
    if (!Rejects(invalid, "next-entry offset beyond the snapshot is rejected")) {
        return false;
    }
    invalid = valid;
    WriteU32(invalid, 4u, 35u);
    if (!Rejects(invalid, "unaligned value offset is rejected")) {
        return false;
    }
    invalid = valid;
    WriteU32(invalid, 4u, 0xFFFFFFFCu);
    if (!Rejects(invalid, "value offset beyond its record is rejected")) {
        return false;
    }
    invalid = valid;
    WriteU32(invalid, 4u, 32u);
    if (!Rejects(invalid, "value overlapping the header and name is rejected")) {
        return false;
    }
    invalid = valid;
    WriteU32(invalid, 8u, 0xFFFFFFFFu);
    return Rejects(invalid, "value length beyond its record is rejected");
}

bool TestNameTerminationAndUnicodeValidation()
{
    const std::vector<VariableInput> one = {
        {L"X", Guid(1u), 0x00000004u, {}},
    };
    std::vector<std::uint8_t> unterminated = BuildSnapshot(one);
    std::fill(unterminated.begin() + 34, unterminated.end(),
              static_cast<std::uint8_t>(0x41u));
    if (!Rejects(unterminated, "missing bounded UTF-16 NUL is rejected")) {
        return false;
    }

    const std::wstring loneSurrogate(1u, static_cast<wchar_t>(0xD800u));
    const std::vector<std::uint8_t> malformed = BuildSnapshot(
        {{loneSurrogate, Guid(2u), 0x00000004u, {}}});
    return Rejects(malformed, "unpaired UTF-16 surrogate is rejected");
}

bool TestDuplicateNamesRejectedAcrossFilteredEntries()
{
    const std::vector<VariableInput> duplicate = {
        {L"SerialNumber", Guid(7u), 0x00000003u, {0x01u}},
        {L"SerialNumber", Guid(7u), 0x00000004u, {0x02u}},
    };
    return Rejects(BuildSnapshot(duplicate),
                  "duplicate GUID and name is rejected before runtime filtering");
}

bool TestEmptySnapshotAndLimits()
{
    std::vector<EfiVariable> variables{EfiVariable{}};
    std::wstring error;
    if (!Check(ParseRuntimeVariableSnapshot(nullptr, 0u, variables, error),
               "empty snapshot parses") ||
        !Check(variables.empty(), "empty snapshot returns no variables")) {
        return false;
    }
    if (!Check(!ParseRuntimeVariableSnapshot(nullptr,
                                              16u * 1024u * 1024u + 1u,
                                              variables, error),
               "snapshot above the 16 MiB bound is rejected")) {
        return false;
    }

    constexpr std::size_t overLimit = 65537u;
    std::vector<VariableInput> many;
    many.reserve(overLimit);
    for (std::size_t index = 0u; index < overLimit; ++index) {
        many.push_back({L"A", Guid(static_cast<std::uint32_t>(index)), 0u, {}});
    }
    return Rejects(BuildSnapshot(many),
                   "snapshot above the 65536 entry bound is rejected");
}

class TemporaryReport {
public:
    TemporaryReport()
    {
        wchar_t directory[MAX_PATH]{};
        wchar_t file[MAX_PATH]{};
        if (GetTempPathW(MAX_PATH, directory) == 0u ||
            GetTempFileNameW(directory, L"hvr", 0u, file) == 0u) {
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
        DWORD written = 0u;
        const bool success =
            WriteFile(file, contents.data(), static_cast<DWORD>(contents.size()),
                      &written, nullptr) != FALSE &&
            written == contents.size();
        const bool closed = CloseHandle(file) != FALSE;
        return success && closed;
    }

private:
    std::wstring path_;
};

bool TestExactAsciiFormatAndExistingParserRoundTrip()
{
    EfiVariable variable{};
    variable.name = L"Serial\u00E9";
    variable.name.push_back(static_cast<wchar_t>(0xD83Du));
    variable.name.push_back(static_cast<wchar_t>(0xDE00u));
    variable.guid.Data1 = 0x12345678u;
    variable.guid.Data2 = 0x9ABCu;
    variable.guid.Data3 = 0xDEF0u;
    variable.guid.Data4[0] = 0x11u;
    variable.guid.Data4[1] = 0x22u;
    variable.guid.Data4[2] = 0x33u;
    variable.guid.Data4[3] = 0x44u;
    variable.guid.Data4[4] = 0x55u;
    variable.guid.Data4[5] = 0x66u;
    variable.guid.Data4[6] = 0x77u;
    variable.guid.Data4[7] = 0x88u;
    variable.attributes = 0xA1B2C3D4u;
    variable.size = 3u;
    variable.status = 0u;
    const std::string expected =
        "HVEFI001\r\n"
        "VAR\t{12345678-9ABC-DEF0-1122-334455667788}\tA1B2C3D4\t"
        "0000000000000003\t0000000000000000\t"
        "00530065007200690061006C00E9D83DDE00\r\n"
        "END\t00000001\r\n";
    EfiVariable preBootOnly{};
    preBootOnly.name = L"PreBootOnly";
    preBootOnly.attributes = 0x00000003u;
    preBootOnly.size = 17u;
    const std::string report =
        FormatRuntimeVariableReport({preBootOnly, variable});
    if (!Check(report == expected, "report matches exact ASCII CRLF contract") ||
        !Check(report.find("PreBootOnly") == std::string::npos,
               "formatter drops entries without runtime access") ||
        !Check(std::all_of(report.begin(), report.end(),
                           [](unsigned char byte) { return byte <= 0x7Fu; }),
               "report contains ASCII bytes only")) {
        return false;
    }

    TemporaryReport scratch;
    std::vector<EfiVariable> parsed;
    std::wstring error;
    if (!Check(scratch.valid(), "temporary parser scratch path is available") ||
        !Check(scratch.Write(report), "formatted report is written to scratch") ||
        !Check(ParseEfiReport(scratch.path(), parsed, error),
               "existing report parser accepts formatted output")) {
        return false;
    }
    return Check(parsed.size() == 1u && parsed[0].name == variable.name,
                 "UTF-16 surrogate pair survives report round-trip") &&
           Check(parsed[0].guid.Data1 == variable.guid.Data1 &&
                     parsed[0].attributes == variable.attributes &&
                     parsed[0].size == variable.size && parsed[0].status == 0u,
                 "report round-trip preserves EFI metadata");
}

}  // namespace

int main()
{
    if (!TestRuntimeFilteringAndRawMetadata() ||
        !TestBoundsAndAlignmentValidation() ||
        !TestNameTerminationAndUnicodeValidation() ||
        !TestDuplicateNamesRejectedAcrossFilteredEntries() ||
        !TestEmptySnapshotAndLimits() ||
        !TestExactAsciiFormatAndExistingParserRoundTrip()) {
        return 1;
    }
    std::cout << "All runtime EFI variable tests passed.\n";
    return 0;
}
