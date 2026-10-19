#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <sysinfoapi.h>

#include "inventory.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr std::size_t kMaximumReportBytes = 16u * 1024u * 1024u;
constexpr std::size_t kMaximumVariables = 65536u;
constexpr std::size_t kMaximumNameCodeUnits = 1024u;
constexpr std::size_t kMaximumLineBytes = 8192u;
constexpr DWORD kMaximumSmbiosBytes = 16u * 1024u * 1024u;
constexpr std::size_t kRawSmbiosHeaderSize = 8u;
constexpr std::size_t kMaximumInventoryReportBytes = 16u * 1024u * 1024u;
constexpr std::size_t kMaximumSmbiosStructures = 262144u;
constexpr std::size_t kMaximumSmbiosStringsPerStructure = 255u;
constexpr std::size_t kMaximumSmbiosStringBytes = 4096u;
constexpr std::size_t kMaximumSmbiosExtractedFields = 65536u;
constexpr std::size_t kMaximumSmbiosExtractedTextBytes = 16u * 1024u * 1024u;
constexpr std::string_view kReportTruncationNotice =
    "\r\n[Inventory report truncated at the 16 MiB output limit.]\r\n";

class ScopedHandle {
public:
    explicit ScopedHandle(HANDLE handle) noexcept : handle_(handle) {}
    ~ScopedHandle()
    {
        if (handle_ != INVALID_HANDLE_VALUE && handle_ != nullptr) {
            (void)CloseHandle(handle_);
        }
    }

    ScopedHandle(const ScopedHandle&) = delete;
    ScopedHandle& operator=(const ScopedHandle&) = delete;

    HANDLE get() const noexcept { return handle_; }

private:
    HANDLE handle_;
};

int HexValue(char ch) noexcept
{
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

bool ParseHex(std::string_view text, std::uint64_t& value) noexcept
{
    std::uint64_t parsed = 0;

    if (text.empty()) {
        return false;
    }
    for (const char ch : text) {
        const int digit = HexValue(ch);
        if (digit < 0 || parsed > (std::numeric_limits<std::uint64_t>::max() -
                                   static_cast<std::uint64_t>(digit)) / 16u) {
            return false;
        }
        parsed = parsed * 16u + static_cast<std::uint64_t>(digit);
    }
    value = parsed;
    return true;
}

bool ParseGuid(std::string_view text, GUID& guid) noexcept
{
    constexpr std::array<std::size_t, 4> hyphen_positions = {9u, 14u, 19u, 24u};
    std::uint64_t value = 0;
    std::array<unsigned char, 8> trailing{};
    std::size_t byte_index = 0;
    bool have_high_nibble = false;
    unsigned char high_nibble = 0;

    if (text.size() != 38u || text.front() != '{' || text.back() != '}') {
        return false;
    }
    for (const std::size_t position : hyphen_positions) {
        if (text[position] != '-') {
            return false;
        }
    }
    if (!ParseHex(text.substr(1u, 8u), value) || value > UINT32_MAX) {
        return false;
    }
    guid.Data1 = static_cast<unsigned long>(value);
    if (!ParseHex(text.substr(10u, 4u), value) || value > UINT16_MAX) {
        return false;
    }
    guid.Data2 = static_cast<unsigned short>(value);
    if (!ParseHex(text.substr(15u, 4u), value) || value > UINT16_MAX) {
        return false;
    }
    guid.Data3 = static_cast<unsigned short>(value);

    for (std::size_t i = 20u; i < 24u; ++i) {
        if (text[i] == '-') {
            continue;
        }
        const int digit = HexValue(text[i]);
        if (digit < 0) {
            return false;
        }
        if (!have_high_nibble) {
            high_nibble = static_cast<unsigned char>(digit << 4);
            have_high_nibble = true;
        } else {
            trailing[byte_index++] = static_cast<unsigned char>(high_nibble | digit);
            have_high_nibble = false;
        }
    }
    for (std::size_t i = 25u; i < 37u; ++i) {
        const int digit = HexValue(text[i]);
        if (digit < 0) {
            return false;
        }
        if (!have_high_nibble) {
            high_nibble = static_cast<unsigned char>(digit << 4);
            have_high_nibble = true;
        } else {
            trailing[byte_index++] = static_cast<unsigned char>(high_nibble | digit);
            have_high_nibble = false;
        }
    }
    if (have_high_nibble || byte_index != trailing.size()) {
        return false;
    }
    for (std::size_t i = 0u; i < trailing.size(); ++i) {
        guid.Data4[i] = trailing[i];
    }
    return true;
}

bool IsValidUtf16Name(const std::wstring& name) noexcept
{
    if (name.empty() || name.size() > kMaximumNameCodeUnits) {
        return false;
    }
    for (std::size_t i = 0u; i < name.size(); ++i) {
        const auto unit = static_cast<std::uint16_t>(name[i]);
        if (unit == 0u) {
            return false;
        }
        if (unit >= 0xD800u && unit <= 0xDBFFu) {
            if (i + 1u >= name.size()) {
                return false;
            }
            const auto low = static_cast<std::uint16_t>(name[i + 1u]);
            if (low < 0xDC00u || low > 0xDFFFu) {
                return false;
            }
            ++i;
        } else if (unit >= 0xDC00u && unit <= 0xDFFFu) {
            return false;
        }
    }
    return true;
}

bool ParseName(std::string_view encoded, std::wstring& name)
{
    if (encoded.empty() || (encoded.size() % 4u) != 0u ||
        encoded.size() / 4u > kMaximumNameCodeUnits) {
        return false;
    }
    name.clear();
    name.reserve(encoded.size() / 4u);
    for (std::size_t i = 0u; i < encoded.size(); i += 4u) {
        std::uint64_t unit = 0;
        if (!ParseHex(encoded.substr(i, 4u), unit)) {
            return false;
        }
        name.push_back(static_cast<wchar_t>(static_cast<std::uint16_t>(unit)));
    }
    return IsValidUtf16Name(name);
}

std::vector<std::string_view> SplitTabs(std::string_view line)
{
    std::vector<std::string_view> fields;
    std::size_t start = 0u;
    while (true) {
        const std::size_t delimiter = line.find('\t', start);
        if (delimiter == std::string_view::npos) {
            fields.emplace_back(line.substr(start));
            return fields;
        }
        fields.emplace_back(line.substr(start, delimiter - start));
        start = delimiter + 1u;
    }
}

bool ParseVariableLine(std::string_view line, EfiVariable& variable)
{
    const std::vector<std::string_view> fields = SplitTabs(line);
    std::uint64_t value = 0;

    if (fields.size() != 6u || fields[0] != "VAR" ||
        !ParseGuid(fields[1], variable.guid) || fields[2].size() != 8u ||
        fields[3].size() != 16u || fields[4].size() != 16u ||
        !ParseHex(fields[2], value) || value > UINT32_MAX) {
        return false;
    }
    variable.attributes = static_cast<std::uint32_t>(value);
    if (!ParseHex(fields[3], variable.size) ||
        !ParseHex(fields[4], variable.status) || !ParseName(fields[5], variable.name)) {
        return false;
    }
    return true;
}

bool ReadBoundedFile(const std::wstring& path, std::string& bytes,
                     std::wstring& error)
{
    ScopedHandle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                  nullptr, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                                  nullptr));
    LARGE_INTEGER initial_size{};
    LARGE_INTEGER final_size{};
    if (file.get() == INVALID_HANDLE_VALUE) {
        error = L"Could not open the EFI inventory report (Windows error " +
                std::to_wstring(GetLastError()) + L").";
        return false;
    }
    if (!GetFileSizeEx(file.get(), &initial_size) || initial_size.QuadPart < 0 ||
        static_cast<unsigned long long>(initial_size.QuadPart) >
            static_cast<unsigned long long>(kMaximumReportBytes)) {
        error = L"EFI inventory report is unreadable or exceeds the 16 MiB limit.";
        return false;
    }
    const std::size_t expected_size = static_cast<std::size_t>(initial_size.QuadPart);
    bytes.resize(expected_size);
    std::size_t offset = 0u;
    while (offset < expected_size) {
        const std::size_t chunk_size = (std::min)(expected_size - offset,
                                                  static_cast<std::size_t>(1u << 20u));
        DWORD received = 0u;
        if (!ReadFile(file.get(), bytes.data() + offset,
                      static_cast<DWORD>(chunk_size), &received, nullptr) ||
            received == 0u) {
            error = L"EFI inventory report ended while being read.";
            return false;
        }
        offset += received;
    }
    if (!GetFileSizeEx(file.get(), &final_size) ||
        final_size.QuadPart != initial_size.QuadPart) {
        error = L"EFI inventory report changed while being read.";
        return false;
    }
    return true;
}

bool NextCrlfLine(std::string_view input, std::size_t& offset,
                  std::string_view& line) noexcept
{
    const std::size_t end = input.find('\n', offset);
    if (end == std::string_view::npos || end == offset || input[end - 1u] != '\r') {
        return false;
    }
    const std::size_t length = end - offset - 1u;
    if (length > kMaximumLineBytes) {
        return false;
    }
    line = input.substr(offset, length);
    offset = end + 1u;
    return true;
}

std::string GuidToString(const GUID& guid)
{
    std::ostringstream text;
    text << '{' << std::uppercase << std::hex << std::setfill('0')
         << std::setw(8) << static_cast<std::uint32_t>(guid.Data1) << '-'
         << std::setw(4) << static_cast<std::uint16_t>(guid.Data2) << '-'
         << std::setw(4) << static_cast<std::uint16_t>(guid.Data3) << '-'
         << std::setw(2) << static_cast<unsigned int>(guid.Data4[0])
         << std::setw(2) << static_cast<unsigned int>(guid.Data4[1]) << '-';
    for (std::size_t i = 2u; i < 8u; ++i) {
        text << std::setw(2) << static_cast<unsigned int>(guid.Data4[i]);
    }
    text << '}';
    return text.str();
}

std::string WideToUtf8(const std::wstring& value)
{
    if (value.empty()) {
        return {};
    }
    if (value.size() > kMaximumNameCodeUnits ||
        value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return "<name exceeds formatting limit>";
    }
    const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                              value.data(),
                                              static_cast<int>(value.size()),
                                              nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        return "<invalid UTF-16 name>";
    }
    std::string result(static_cast<std::size_t>(required), '\0');
    const int written = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                             value.data(),
                                             static_cast<int>(value.size()),
                                             result.data(), required,
                                             nullptr, nullptr);
    if (written != required) {
        return "<invalid UTF-16 name>";
    }
    return result;
}

bool IsStandardGuid(const GUID& guid) noexcept
{
    static constexpr GUID global_variable_guid = {
        0x8BE4DF61u, 0x93CAu, 0x11D2u,
        {0xAAu, 0x0Du, 0x00u, 0xE0u, 0x98u, 0x03u, 0x2Bu, 0x8Cu}};
    static constexpr GUID image_security_database_guid = {
        0xD719B2CBu, 0x3D3Au, 0x4596u,
        {0xA3u, 0xBCu, 0xDAu, 0xD0u, 0x0Eu, 0x67u, 0x65u, 0x6Fu}};
    const auto equals = [&](const GUID& known) {
        return guid.Data1 == known.Data1 && guid.Data2 == known.Data2 &&
               guid.Data3 == known.Data3 && guid.Data4[0] == known.Data4[0] &&
               guid.Data4[1] == known.Data4[1] && guid.Data4[2] == known.Data4[2] &&
               guid.Data4[3] == known.Data4[3] && guid.Data4[4] == known.Data4[4] &&
               guid.Data4[5] == known.Data4[5] && guid.Data4[6] == known.Data4[6] &&
               guid.Data4[7] == known.Data4[7];
    };
    return equals(global_variable_guid) || equals(image_security_database_guid);
}

bool HasIdentityHint(const std::wstring& name)
{
    static constexpr std::array<std::wstring_view, 10> hints = {
        L"UUID", L"SERIAL", L"ASSET", L"BOARD", L"SYSTEM",
        L"PRODUCT", L"MACHINE", L"HARDWARE", L"PLATFORM", L"CHASSIS"};
    std::wstring uppercase(name);
    for (wchar_t& ch : uppercase) {
        if (ch >= L'a' && ch <= L'z') {
            ch = static_cast<wchar_t>(ch - L'a' + L'A');
        }
    }
    return std::any_of(hints.begin(), hints.end(), [&](std::wstring_view hint) {
        return uppercase.find(hint) != std::wstring::npos;
    });
}

bool IsReviewCandidate(const EfiVariable& variable)
{
    return !IsStandardGuid(variable.guid) || HasIdentityHint(variable.name);
}

std::string SmbiosText(const std::uint8_t* bytes, std::size_t length)
{
    std::string value;
    for (std::size_t i = 0u; i < length; ++i) {
        const std::uint8_t ch = bytes[i];
        if (ch >= 0x20u && ch <= 0x7Eu && ch != '\\') {
            value.push_back(static_cast<char>(ch));
        } else if (ch == '\\') {
            value.append("\\\\");
        } else {
            std::ostringstream escaped;
            escaped << "\\x" << std::uppercase << std::hex << std::setfill('0')
                    << std::setw(2) << static_cast<unsigned int>(ch);
            value.append(escaped.str());
        }
    }
    return value;
}

std::string HexBytes(const std::uint8_t* bytes, std::size_t length)
{
    std::ostringstream value;
    value << std::uppercase << std::hex << std::setfill('0');
    for (std::size_t i = 0u; i < length; ++i) {
        value << std::setw(2) << static_cast<unsigned int>(bytes[i]);
    }
    return value.str();
}

std::uint16_t ReadU16(const std::uint8_t* bytes) noexcept
{
    return static_cast<std::uint16_t>(bytes[0]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[1]) << 8u);
}

std::uint32_t ReadU32(const std::uint8_t* bytes) noexcept
{
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8u) |
           (static_cast<std::uint32_t>(bytes[2]) << 16u) |
           (static_cast<std::uint32_t>(bytes[3]) << 24u);
}

bool GetSmbiosStrings(const std::uint8_t* table, std::size_t table_size,
                      std::size_t strings_offset,
                      std::vector<std::string>& strings,
                      std::size_t& structure_end) {
    std::size_t cursor = strings_offset;
    strings.clear();
    if (cursor >= table_size) {
        return false;
    }
    if (table[cursor] == 0u) {
        if (cursor + 1u >= table_size || table[cursor + 1u] != 0u) {
            return false;
        }
        structure_end = cursor + 2u;
        return true;
    }
    while (cursor < table_size) {
        const std::size_t begin = cursor;
        while (cursor < table_size && table[cursor] != 0u) {
            ++cursor;
        }
        if (cursor >= table_size || cursor - begin > kMaximumSmbiosStringBytes ||
            strings.size() >= kMaximumSmbiosStringsPerStructure) {
            return false;
        }
        strings.emplace_back(SmbiosText(table + begin, cursor - begin));
        ++cursor;
        if (cursor >= table_size) {
            return false;
        }
        if (table[cursor] == 0u) {
            structure_end = cursor + 1u;
            return true;
        }
    }
    return false;
}

bool StringAt(const std::vector<std::string>& strings, std::uint8_t index,
              std::string& value) {
    if (index == 0u) {
        value = "Not provided";
        return true;
    }
    if (static_cast<std::size_t>(index) > strings.size()) {
        return false;
    }
    value = strings[static_cast<std::size_t>(index) - 1u];
    return true;
}

void AddField(std::vector<std::pair<std::string, std::string>>& fields,
              const char* name, std::string value)
{
    fields.emplace_back(name, std::move(value));
}

bool AddStringField(const std::uint8_t* formatted, std::size_t formatted_length,
                    std::size_t index_offset,
                    const std::vector<std::string>& strings,
                    const char* label,
                    std::vector<std::pair<std::string, std::string>>& fields)
{
    std::string value;
    if (index_offset >= formatted_length ||
        !StringAt(strings, formatted[index_offset], value)) {
        return false;
    }
    AddField(fields, label, std::move(value));
    return true;
}

std::string FormatSmbiosUuid(const std::uint8_t* bytes, bool smbios_26_or_later)
{
    std::array<std::uint8_t, 16> canonical{};
    const bool all_zero = std::all_of(bytes, bytes + canonical.size(),
                                      [](std::uint8_t value) { return value == 0u; });
    const bool all_ones = std::all_of(bytes, bytes + canonical.size(),
                                      [](std::uint8_t value) { return value == 0xFFu; });
    if (all_zero || all_ones) {
        return "Not specified";
    }
    if (smbios_26_or_later) {
        canonical[0] = bytes[3];
        canonical[1] = bytes[2];
        canonical[2] = bytes[1];
        canonical[3] = bytes[0];
        canonical[4] = bytes[5];
        canonical[5] = bytes[4];
        canonical[6] = bytes[7];
        canonical[7] = bytes[6];
        std::copy(bytes + 8u, bytes + 16u, canonical.begin() + 8u);
    } else {
        std::copy(bytes, bytes + canonical.size(), canonical.begin());
    }
    std::ostringstream value;
    value << std::uppercase << std::hex << std::setfill('0');
    for (std::size_t i = 0u; i < canonical.size(); ++i) {
        if (i == 4u || i == 6u || i == 8u || i == 10u) {
            value << '-';
        }
        value << std::setw(2) << static_cast<unsigned int>(canonical[i]);
    }
    return value.str();
}

std::string FormatMemorySize(const std::uint8_t* formatted,
                             std::size_t formatted_length)
{
    const std::uint16_t encoded = ReadU16(formatted + 12u);
    if (encoded == 0u) {
        return "Not installed";
    }
    if (encoded == 0xFFFFu) {
        return "Unknown";
    }
    if (encoded == 0x7FFFu && formatted_length >= 32u) {
        const std::uint32_t extended = ReadU32(formatted + 28u) & 0x7FFFFFFFu;
        if (extended != 0u) {
            return std::to_string(extended) + " MiB";
        }
        return "Unknown";
    }
    const std::uint16_t amount = static_cast<std::uint16_t>(encoded & 0x7FFFu);
    if ((encoded & 0x8000u) != 0u) {
        return std::to_string(amount) + " KiB";
    }
    return std::to_string(amount) + " MiB";
}

std::string FormatMemorySpeed(const std::uint8_t* formatted,
                              std::size_t formatted_length)
{
    if (formatted_length < 23u) {
        return "Unavailable in this SMBIOS record";
    }
    const std::uint16_t speed = ReadU16(formatted + 21u);
    if (speed == 0u || speed == 0xFFFFu) {
        return "Unknown";
    }
    return std::to_string(speed) + " MT/s";
}

std::string FormatHexNumber(std::uint64_t value, std::size_t width);

std::string FormatProcessorArchitecture(std::uint8_t processor_type)
{
    static constexpr std::array<std::string_view, 11> names = {
        "Reserved", "IA32 (x86)", "x64 (x86-64)", "Intel Itanium",
        "32-bit ARM (AArch32)", "64-bit ARM (AArch64)", "32-bit RISC-V (RV32)",
        "64-bit RISC-V (RV64)", "128-bit RISC-V (RV128)",
        "32-bit LoongArch", "64-bit LoongArch"};
    if (static_cast<std::size_t>(processor_type) < names.size()) {
        return "0x" + FormatHexNumber(processor_type, 2u) + " (" +
               std::string(names[processor_type]) + ")";
    }
    return "0x" + FormatHexNumber(processor_type, 2u) + " (unknown)";
}

class BoundedReport {
public:
    void Append(std::string_view value)
    {
        if (truncated_ || value.empty()) {
            return;
        }
        const std::size_t prefix_limit =
            kMaximumInventoryReportBytes - kReportTruncationNotice.size();
        if (text_.size() >= prefix_limit) {
            truncated_ = true;
            return;
        }
        const std::size_t copied = (std::min)(prefix_limit - text_.size(), value.size());
        text_.append(value.data(), copied);
        if (copied != value.size()) {
            truncated_ = true;
        }
    }

    void AppendEscaped(std::string_view value)
    {
        static constexpr char hex[] = "0123456789ABCDEF";
        std::size_t run_start = 0u;
        for (std::size_t i = 0u; i < value.size(); ++i) {
            const auto ch = static_cast<unsigned char>(value[i]);
            if (ch >= 0x20u && ch != 0x7Fu) {
                continue;
            }
            if (truncated_) {
                return;
            }
            Append(value.substr(run_start, i - run_start));
            if (truncated_) {
                return;
            }
            const char escaped[] = {'\\', 'x', hex[ch >> 4u], hex[ch & 0x0Fu]};
            Append(std::string_view(escaped, sizeof(escaped)));
            run_start = i + 1u;
        }
        Append(value.substr(run_start));
    }

    bool IsTruncated() const noexcept { return truncated_; }

    std::string Finish()
    {
        if (truncated_) {
            text_.append(kReportTruncationNotice.data(),
                         kReportTruncationNotice.size());
        }
        return std::move(text_);
    }

private:
    std::string text_;
    bool truncated_ = false;
};

std::string FormatHexNumber(std::uint64_t value, std::size_t width)
{
    std::ostringstream text;
    text << std::uppercase << std::hex << std::setfill('0') << std::setw(
        static_cast<int>(width)) << value;
    return text.str();
}

bool ParseSmbiosTable(const std::uint8_t* table, std::size_t table_size,
                      BYTE major, BYTE minor,
                      std::vector<std::pair<std::string, std::string>>& fields,
                      std::wstring& error)
{
    struct ProcessorAdditionalInfo {
        std::uint16_t structure_handle;
        std::uint16_t referenced_type4_handle;
        std::uint8_t data_length;
        std::uint8_t processor_type;
    };
    std::size_t offset = 0u;
    std::size_t structure_count = 0u;
    std::size_t extracted_text_bytes = 0u;
    std::vector<std::uint16_t> type4_handles;
    std::vector<ProcessorAdditionalInfo> processor_additional_info;
    const bool uuid_26_or_later = major > 2u || (major == 2u && minor >= 6u);
    while (offset < table_size) {
        if (table_size - offset < 4u) {
            error = L"SMBIOS table ends with a truncated structure header.";
            return false;
        }
        const std::uint8_t type = table[offset];
        const std::size_t formatted_length = table[offset + 1u];
        if (formatted_length < 4u || formatted_length > table_size - offset) {
            error = L"SMBIOS structure has an invalid formatted length.";
            return false;
        }
        const std::uint8_t* formatted = table + offset;
        std::vector<std::string> strings;
        std::size_t structure_end = 0u;
        if (!GetSmbiosStrings(table, table_size, offset + formatted_length,
                              strings, structure_end) || structure_end <= offset) {
            error = L"SMBIOS structure has a missing or truncated string terminator.";
            return false;
        }
        ++structure_count;
        if (structure_count > kMaximumSmbiosStructures) {
            error = L"SMBIOS table exceeds the 262,144 structure limit.";
            return false;
        }
        const std::size_t first_added_field = fields.size();
        const auto string_field = [&](std::size_t index_offset, const char* label) {
            return AddStringField(formatted, formatted_length, index_offset,
                                  strings, label, fields);
        };
        switch (type) {
        case 0u:
            if (!string_field(4u, "BIOS vendor") ||
                !string_field(5u, "BIOS version") ||
                !string_field(8u, "BIOS release date")) {
                error = L"SMBIOS Type 0 contains an invalid string index.";
                return false;
            }
            break;
        case 1u:
            if (!string_field(4u, "System manufacturer") ||
                !string_field(5u, "System product") ||
                !string_field(6u, "System version") ||
                !string_field(7u, "System serial number")) {
                error = L"SMBIOS Type 1 contains an invalid string index.";
                return false;
            }
            if (formatted_length >= 24u) {
                AddField(fields, "System UUID",
                         FormatSmbiosUuid(formatted + 8u, uuid_26_or_later));
            } else {
                AddField(fields, "System UUID", "Unavailable in this SMBIOS record");
            }
            if (formatted_length > 25u &&
                !string_field(25u, "System SKU number")) {
                error = L"SMBIOS Type 1 contains an invalid SKU string index.";
                return false;
            }
            if (formatted_length > 26u &&
                !string_field(26u, "System family")) {
                error = L"SMBIOS Type 1 contains an invalid family string index.";
                return false;
            }
            break;
        case 2u:
            if (!string_field(4u, "Baseboard manufacturer") ||
                !string_field(5u, "Baseboard product") ||
                !string_field(6u, "Baseboard version") ||
                !string_field(7u, "Baseboard serial number") ||
                !string_field(8u, "Baseboard asset tag")) {
                error = L"SMBIOS Type 2 contains an invalid string index.";
                return false;
            }
            if (formatted_length > 10u && !string_field(10u, "Baseboard location")) {
                error = L"SMBIOS Type 2 contains an invalid location string index.";
                return false;
            }
            break;
        case 3u:
            if (!string_field(4u, "Chassis manufacturer") ||
                !string_field(6u, "Chassis version") ||
                !string_field(7u, "Chassis serial number") ||
                !string_field(8u, "Chassis asset tag")) {
                error = L"SMBIOS Type 3 contains an invalid string index.";
                return false;
            }
            if (formatted_length > 5u) {
                std::ostringstream chassis_type;
                chassis_type << "0x" << std::uppercase << std::hex
                             << std::setfill('0') << std::setw(2)
                             << static_cast<unsigned int>(formatted[5] & 0x7Fu);
                AddField(fields, "Chassis type", chassis_type.str());
            }
            break;
        case 4u:
            type4_handles.push_back(ReadU16(formatted + 2u));
            if (!string_field(4u, "Processor socket") ||
                !string_field(7u, "Processor manufacturer")) {
                error = L"SMBIOS Type 4 contains an invalid string index.";
                return false;
            }
            if (formatted_length >= 16u) {
                AddField(fields, "Processor ID", HexBytes(formatted + 8u, 8u));
            }
            if (formatted_length > 0x20u &&
                !string_field(0x20u, "Processor serial number")) {
                error = L"SMBIOS Type 4 contains an invalid serial string index.";
                return false;
            }
            if (formatted_length > 0x21u &&
                !string_field(0x21u, "Processor asset tag")) {
                error = L"SMBIOS Type 4 contains an invalid asset string index.";
                return false;
            }
            if (formatted_length > 0x22u &&
                !string_field(0x22u, "Processor part number")) {
                error = L"SMBIOS Type 4 contains an invalid part-number string index.";
                return false;
            }
            break;
        case 44u: {
            if (formatted_length < 8u ||
                static_cast<std::size_t>(formatted[6]) + 8u != formatted_length) {
                error = L"SMBIOS Type 44 has an invalid processor-specific block length.";
                return false;
            }
            processor_additional_info.push_back({
                ReadU16(formatted + 2u), ReadU16(formatted + 4u),
                formatted[6], formatted[7]});
            break;
        }
        case 17u:
            if (!string_field(16u, "Memory device locator") ||
                !string_field(17u, "Memory bank locator")) {
                error = L"SMBIOS Type 17 contains an invalid locator string index.";
                return false;
            }
            AddField(fields, "Memory device size", FormatMemorySize(formatted,
                                                                      formatted_length));
            if (formatted_length > 23u && !string_field(23u, "Memory manufacturer")) {
                error = L"SMBIOS Type 17 contains an invalid manufacturer string index.";
                return false;
            }
            if (formatted_length > 24u && !string_field(24u, "Memory serial number")) {
                error = L"SMBIOS Type 17 contains an invalid serial string index.";
                return false;
            }
            if (formatted_length > 25u && !string_field(25u, "Memory asset tag")) {
                error = L"SMBIOS Type 17 contains an invalid asset string index.";
                return false;
            }
            if (formatted_length > 26u && !string_field(26u, "Memory part number")) {
                error = L"SMBIOS Type 17 contains an invalid part-number string index.";
                return false;
            }
            AddField(fields, "Memory device speed", FormatMemorySpeed(formatted,
                                                                         formatted_length));
            break;
        default:
            break;
        }
        if (fields.size() > kMaximumSmbiosExtractedFields) {
            error = L"SMBIOS extraction exceeds the 65,536 field limit.";
            return false;
        }
        for (std::size_t i = first_added_field; i < fields.size(); ++i) {
            const std::size_t field_bytes = fields[i].first.size() + fields[i].second.size();
            if (field_bytes > kMaximumSmbiosExtractedTextBytes - extracted_text_bytes) {
                error = L"SMBIOS extraction exceeds the 16 MiB text limit.";
                return false;
            }
            extracted_text_bytes += field_bytes;
        }
        offset = structure_end;
        if (type == 127u) {
            if (offset != table_size) {
                error = L"SMBIOS table contains data after its End-of-Table structure.";
                return false;
            }
            break;
        }
    }
    if (structure_count == 0u) {
        error = L"SMBIOS table contains no structures.";
        return false;
    }

    const std::size_t audit_first_field = fields.size();
    AddField(fields, "Processor additional information (Type 44) count",
             std::to_string(processor_additional_info.size()));
    if (!processor_additional_info.empty()) {
        AddField(fields, "Processor additional information standardized serial mapping",
                 "None; Type 44 processor-specific data is not a standardized serial field");
    }
    for (std::size_t i = 0u; i < processor_additional_info.size(); ++i) {
        const ProcessorAdditionalInfo& info = processor_additional_info[i];
        const std::string prefix = "Processor additional information [" +
                                   std::to_string(i + 1u) + "] ";
        const bool linked = std::find(type4_handles.begin(), type4_handles.end(),
                                      info.referenced_type4_handle) != type4_handles.end();
        AddField(fields, (prefix + "structure handle").c_str(),
                 "0x" + FormatHexNumber(info.structure_handle, 4u));
        AddField(fields, (prefix + "referenced Type 4 handle").c_str(),
                 "0x" + FormatHexNumber(info.referenced_type4_handle, 4u) +
                     (linked ? " (matched Type 4)" : " (no Type 4 found)"));
        AddField(fields, (prefix + "processor-specific block data length").c_str(),
                 std::to_string(static_cast<unsigned int>(info.data_length)));
        AddField(fields, (prefix + "processor architecture/type").c_str(),
                 FormatProcessorArchitecture(info.processor_type));
    }
    if (fields.size() > kMaximumSmbiosExtractedFields) {
        error = L"SMBIOS extraction exceeds the 65,536 field limit.";
        return false;
    }
    for (std::size_t i = audit_first_field; i < fields.size(); ++i) {
        const std::size_t field_bytes = fields[i].first.size() + fields[i].second.size();
        if (field_bytes > kMaximumSmbiosExtractedTextBytes - extracted_text_bytes) {
            error = L"SMBIOS extraction exceeds the 16 MiB text limit.";
            return false;
        }
        extracted_text_bytes += field_bytes;
    }
    return true;
}

}  // namespace

bool ParseSmbiosTableSnapshot(
    const std::uint8_t* table, std::size_t table_size,
    std::uint8_t major, std::uint8_t minor,
    std::vector<std::pair<std::string, std::string>>& fields,
    std::wstring& error)
{
    fields.clear();
    error.clear();
    if (table == nullptr || table_size == 0u ||
        table_size > static_cast<std::size_t>(kMaximumSmbiosBytes)) {
        error = L"SMBIOS table size is outside the supported bounds.";
        return false;
    }
    std::vector<std::pair<std::string, std::string>> parsed;
    if (!ParseSmbiosTable(table, table_size, major, minor, parsed, error)) {
        return false;
    }
    fields = std::move(parsed);
    return true;
}

bool ParseEfiReport(const std::wstring& path,
                    std::vector<EfiVariable>& out,
                    std::wstring& error)
{
    std::string bytes;
    std::size_t offset = 0u;
    std::size_t count = 0u;
    std::string_view line;
    std::vector<EfiVariable> parsed;

    out.clear();
    error.clear();
    if (!ReadBoundedFile(path, bytes, error)) {
        return false;
    }
    if (!NextCrlfLine(bytes, offset, line) || line != "HVEFI001") {
        error = L"EFI report header is invalid or truncated.";
        return false;
    }
    while (offset < bytes.size()) {
        if (!NextCrlfLine(bytes, offset, line)) {
            error = L"EFI report contains a malformed or truncated CRLF line.";
            return false;
        }
        if (line.size() >= 4u && line.substr(0u, 4u) == "END\t") {
            const std::string_view count_text = line.substr(4u);
            std::uint64_t declared_count = 0u;
            if (count_text.size() != 8u || !ParseHex(count_text, declared_count) ||
                declared_count != count || offset != bytes.size()) {
                error = L"EFI report footer count is invalid or trailing data follows it.";
                return false;
            }
            out = std::move(parsed);
            return true;
        }
        if (count >= kMaximumVariables) {
            error = L"EFI report exceeds the 65,536 variable limit.";
            return false;
        }
        if (line.size() < 4u || line.size() > kMaximumLineBytes ||
            line.substr(0u, 4u) != "VAR\t") {
            error = L"EFI report contains an unknown record or malformed line.";
            return false;
        }
        EfiVariable variable{};
        if (!ParseVariableLine(line, variable)) {
            error = L"EFI report contains a malformed variable record.";
            return false;
        }
        parsed.push_back(std::move(variable));
        ++count;
    }
    error = L"EFI report is missing its END footer.";
    return false;
}

bool CollectSmbios(
    std::vector<std::pair<std::string, std::string>>& fields,
    std::wstring& error)
{
    constexpr DWORD provider_rsmB =
        (static_cast<DWORD>('R') << 24u) |
        (static_cast<DWORD>('S') << 16u) |
        (static_cast<DWORD>('M') << 8u) |
        static_cast<DWORD>('B');
    fields.clear();
    error.clear();

    const DWORD required = GetSystemFirmwareTable(provider_rsmB, 0u, nullptr, 0u);
    if (required == 0u) {
        error = L"Windows could not query the raw SMBIOS table (error " +
                std::to_wstring(GetLastError()) + L").";
        return false;
    }
    if (required > kMaximumSmbiosBytes || required < kRawSmbiosHeaderSize) {
        error = L"Raw SMBIOS table size is outside the supported bounds.";
        return false;
    }
    std::vector<std::uint8_t> raw(required);
    const DWORD returned = GetSystemFirmwareTable(provider_rsmB, 0u, raw.data(), required);
    if (returned < kRawSmbiosHeaderSize || returned > required) {
        error = L"Windows returned a truncated or oversized raw SMBIOS table.";
        return false;
    }
    const std::uint32_t table_length = ReadU32(raw.data() + 4u);
    if (table_length > returned - kRawSmbiosHeaderSize) {
        error = L"Raw SMBIOS header declares a table larger than the returned buffer.";
        return false;
    }
    if (table_length == 0u) {
        error = L"Raw SMBIOS table is empty.";
        return false;
    }
    if (!ParseSmbiosTableSnapshot(raw.data() + kRawSmbiosHeaderSize, table_length,
                                  raw[1], raw[2], fields, error)) {
        fields.clear();
        return false;
    }
    AddField(fields, "SMBIOS version",
             std::to_string(static_cast<unsigned int>(raw[1])) + "." +
             std::to_string(static_cast<unsigned int>(raw[2])));
    AddField(fields, "SMBIOS table bytes", std::to_string(table_length));
    return true;
}

std::string FormatInventory(
    const std::vector<EfiVariable>& efi,
    const std::vector<std::pair<std::string, std::string>>& smbios)
{
    BoundedReport report;
    report.Append("Hardware Firmware Inventory\r\n"
                  "Access: read-only; no firmware variables were changed.\r\n"
                  "EFI variables: ");
    report.Append(std::to_string(efi.size()));
    report.Append("\r\nCandidate detection: heuristic only; a candidate label does not prove that a variable identifies hardware.\r\n\r\n");
    for (const EfiVariable& variable : efi) {
        if (report.IsTruncated()) {
            break;
        }
        report.Append("EFI variable\r\n  Name: ");
        report.AppendEscaped(WideToUtf8(variable.name));
        report.Append("\r\n  Vendor GUID: ");
        report.Append(GuidToString(variable.guid));
        report.Append("\r\n  Attributes: 0x");
        report.Append(FormatHexNumber(variable.attributes, 8u));
        report.Append("\r\n  Data size: ");
        report.Append(std::to_string(variable.size));
        report.Append(" bytes\r\n  Collection status: 0x");
        report.Append(FormatHexNumber(variable.status, 16u));
        report.Append("\r\n  Review candidate (heuristic): ");
        report.Append(IsReviewCandidate(variable) ? "yes" : "no");
        report.Append("\r\n\r\n");
    }
    report.Append("SMBIOS fields: ");
    report.Append(std::to_string(smbios.size()));
    report.Append("\r\nThese fields are reported from the Windows raw SMBIOS table.\r\n");
    for (const auto& field : smbios) {
        if (report.IsTruncated()) {
            break;
        }
        report.Append("  ");
        report.AppendEscaped(field.first);
        report.Append(": ");
        report.AppendEscaped(field.second);
        report.Append("\r\n");
    }
    return report.Finish();
}
