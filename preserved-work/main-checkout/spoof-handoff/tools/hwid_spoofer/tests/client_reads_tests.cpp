#include "client_reads.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using hwid_spoofer::client::Identifier;

void Require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void AppendStructure(
    std::vector<std::uint8_t>& table,
    std::vector<std::uint8_t> formatted,
    const std::vector<std::string>& strings) {
    Require(formatted.size() >= 4u && formatted.size() <= 255u,
            "fixture structure length must fit the SMBIOS header");
    formatted[1] = static_cast<std::uint8_t>(formatted.size());
    table.insert(table.end(), formatted.begin(), formatted.end());
    for (const std::string& value : strings) {
        table.insert(table.end(), value.begin(), value.end());
        table.push_back(0u);
    }
    table.push_back(0u);
    if (strings.empty()) {
        table.push_back(0u);
    }
}

void WriteTableLength(std::vector<std::uint8_t>& bytes) {
    const std::uint32_t length =
        static_cast<std::uint32_t>(bytes.size() - 8u);
    bytes[4] = static_cast<std::uint8_t>(length & 0xFFu);
    bytes[5] = static_cast<std::uint8_t>((length >> 8u) & 0xFFu);
    bytes[6] = static_cast<std::uint8_t>((length >> 16u) & 0xFFu);
    bytes[7] = static_cast<std::uint8_t>((length >> 24u) & 0xFFu);
}

std::vector<std::uint8_t> BuildRawSmbios(
    std::uint8_t major = 3u,
    std::uint8_t minor = 0u) {
    // RawSMBIOSData: Used20CallingMethod at byte 0, major at byte 1,
    // minor at byte 2, DmiRevision at byte 3, and Length at byte 4.
    std::vector<std::uint8_t> bytes(8u, 0u);
    bytes[0] = 1u;
    bytes[1] = major;
    bytes[2] = minor;
    bytes[3] = 0u;

    std::vector<std::uint8_t> system(25u, 0u);
    system[0] = 1u;
    system[4] = 1u;
    system[5] = 2u;
    system[6] = 0u;
    system[7] = 3u;
    const std::uint8_t uuid_wire[16] = {
        0x31, 0x46, 0x8B, 0xC8, 0x8C, 0xC8, 0x1B, 0x87,
        0xA6, 0xD7, 0xD8, 0xBB, 0xC1, 0xD8, 0x57, 0x20};
    std::copy(std::begin(uuid_wire), std::end(uuid_wire), system.begin() + 8);
    AppendStructure(bytes, std::move(system), {"System maker", "System model", "SYS-1001"});

    std::vector<std::uint8_t> board(8u, 0u);
    board[0] = 2u;
    board[4] = 1u;
    board[5] = 2u;
    board[6] = 0u;
    board[7] = 3u;
    AppendStructure(bytes, std::move(board), {"Board maker", "Board model", "BOARD-2002"});

    AppendStructure(bytes, {127u, 4u, 0u, 0u}, {});
    WriteTableLength(bytes);
    return bytes;
}

bool Parse(
    const std::vector<std::uint8_t>& bytes,
    std::vector<Identifier>& values,
    std::wstring& error) {
    return hwid_spoofer::client::detail::ParseConfirmedSmbios(bytes, values, error);
}

const Identifier& Find(
    const std::vector<Identifier>& values,
    HWID_SPOOF_FIELD field) {
    const auto found = std::find_if(values.begin(), values.end(),
        [field](const Identifier& value) { return value.field == field; });
    if (found == values.end()) {
        throw std::runtime_error("expected SMBIOS field was not parsed");
    }
    return *found;
}

void TestConfirmedTypeOneAndTwoFields() {
    std::vector<Identifier> values;
    std::wstring error;
    Require(Parse(BuildRawSmbios(), values, error),
            "valid SMBIOS Type 1 and Type 2 data must parse");
    Require(values.size() == 7u, "parser must expose exactly the seven confirmed SMBIOS fields");
    Require(Find(values, HwidFieldSystemSerial).value == L"SYS-1001",
            "Type 1 serial must use its validated string index");
    Require(Find(values, HwidFieldSystemManufacturer).value == L"System maker",
            "Type 1 manufacturer must be parsed");
    Require(Find(values, HwidFieldSystemProduct).value == L"System model",
            "Type 1 product must be parsed");
    Require(Find(values, HwidFieldBoardSerial).value == L"BOARD-2002",
            "Type 2 serial must use its validated string index");
    Require(Find(values, HwidFieldBoardManufacturer).value == L"Board maker",
            "Type 2 manufacturer must be parsed");
    Require(Find(values, HwidFieldBoardProduct).value == L"Board model",
            "Type 2 product must be parsed");
    Require(Find(values, HwidFieldSystemUuid).value ==
                L"C88B4631-C88C-871B-A6D7-D8BBC1D85720",
            "SMBIOS 2.6 and later UUID fields must use mixed-endian formatting");
}

void TestNonemptyLastStringUsesDoubleNulTerminator() {
    std::vector<Identifier> values;
    std::wstring error;
    Require(Parse(BuildRawSmbios(), values, error),
            "a nonempty final string followed by the string-set double NUL must parse");
    Require(Find(values, HwidFieldSystemSerial).value == L"SYS-1001" &&
                Find(values, HwidFieldBoardSerial).value == L"BOARD-2002",
            "the final string in each string set must remain available");
}

void TestUuidWireOrderBeforeVersionTwoPointSix() {
    std::vector<Identifier> values;
    std::wstring error;
    Require(Parse(BuildRawSmbios(2u, 5u), values, error),
            "valid pre-2.6 SMBIOS UUID data must parse");
    Require(Find(values, HwidFieldSystemUuid).value ==
                L"31468BC8-8CC8-1B87-A6D7-D8BBC1D85720",
            "pre-2.6 UUID fields must retain wire byte order");
}

void TestInvalidStringIndexRejected() {
    std::vector<std::uint8_t> bytes = BuildRawSmbios();
    bytes[8u + 7u] = 4u;
    std::vector<Identifier> values;
    std::wstring error;
    Require(!Parse(bytes, values, error),
            "a string index beyond the structure string set must fail");
    Require(values.empty() && !error.empty(),
            "invalid string indices must produce no partial values");
}

void TestMissingDoubleNulRejected() {
    std::vector<std::uint8_t> bytes = BuildRawSmbios();
    bytes.pop_back();
    WriteTableLength(bytes);
    std::vector<Identifier> values;
    std::wstring error;
    Require(!Parse(bytes, values, error),
            "truncated Type 127 strings must fail double-NUL validation");
    Require(values.empty(), "truncated tables must not expose partial fields");
}

void TestLengthAndBoundsRejected() {
    std::vector<Identifier> values;
    std::wstring error;
    std::vector<std::uint8_t> bytes = BuildRawSmbios();
    ++bytes[4];
    Require(!Parse(bytes, values, error),
            "declared table length must match the returned buffer");

    bytes = BuildRawSmbios();
    bytes[8u + 1u] = 3u;
    WriteTableLength(bytes);
    Require(!Parse(bytes, values, error),
            "formatted structures shorter than their header must fail");

    bytes.assign(8u, 0u);
    bytes[4] = 1u;
    bytes[5] = 0u;
    bytes[6] = 0u;
    bytes[7] = 1u;
    Require(!Parse(bytes, values, error),
            "a table length beyond available bytes must fail bounds validation");
}

void TestExactTypeCountsAndEndMarker() {
    std::vector<Identifier> values;
    std::wstring error;
    std::vector<std::uint8_t> bytes = BuildRawSmbios();
    bytes.erase(bytes.begin() + 8u, bytes.begin() + 8u + 61u);
    WriteTableLength(bytes);
    Require(!Parse(bytes, values, error),
            "missing Type 1 must fail the exact structure count requirement");

    bytes = BuildRawSmbios();
    std::vector<std::uint8_t> duplicate(
        bytes.begin() + 8u, bytes.begin() + 8u + 61u);
    const auto marker = std::find(bytes.begin() + 8u, bytes.end(), 127u);
    Require(marker != bytes.end(), "fixture must contain an end marker");
    bytes.insert(marker, duplicate.begin(), duplicate.end());
    WriteTableLength(bytes);
    Require(!Parse(bytes, values, error),
            "duplicate Type 1 structures must fail rather than select one");

    bytes = BuildRawSmbios();
    bytes.erase(std::find(bytes.begin() + 8u, bytes.end(), 127u), bytes.end());
    WriteTableLength(bytes);
    Require(!Parse(bytes, values, error),
            "missing Type 127 must fail table termination validation");
}

}  // namespace

int main() {
    try {
        TestConfirmedTypeOneAndTwoFields();
        TestNonemptyLastStringUsesDoubleNulTerminator();
        TestUuidWireOrderBeforeVersionTwoPointSix();
        TestInvalidStringIndexRejected();
        TestMissingDoubleNulRejected();
        TestLengthAndBoundsRejected();
        TestExactTypeCountsAndEndMarker();
        std::cout << "PASS: native reader SMBIOS parser fixtures\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
