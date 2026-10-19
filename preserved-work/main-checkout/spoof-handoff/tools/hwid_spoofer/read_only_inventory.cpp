#include "read_only_inventory.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iomanip>
#include <locale>
#include <map>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

namespace hwid_spoofer {
namespace {

constexpr std::uint32_t kEfiVariableRuntimeAccess = 0x00000004u;

struct Target {
    const char* field;
};

constexpr std::array<Target, 11> kTargets = {{
    {"FakeDiskSerial"},
    {"FakeMacAddress"},
    {"FakeSMBIOSSerial"},
    {"FakeSMBIOSManufacturer"},
    {"FakeSMBIOSProduct"},
    {"FakeMotherboardSerial"},
    {"FakeMotherboardManufacturer"},
    {"FakeMotherboardProduct"},
    {"FakeCPUSerial"},
    {"FakeGPUSerial"},
    {"FakeSMBIOSUUID"},
}};

std::string LowerAscii(std::string value)
{
    for (char& ch : value) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return value;
}

std::wstring UpperAscii(std::wstring value)
{
    for (wchar_t& ch : value) {
        if (ch >= L'a' && ch <= L'z') {
            ch = static_cast<wchar_t>(ch - L'a' + L'A');
        }
    }
    return value;
}

std::string EscapeBytes(std::string_view input)
{
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string output;
    output.reserve(input.size());
    for (const char raw_ch : input) {
        const auto ch = static_cast<unsigned char>(raw_ch);
        if (ch == static_cast<unsigned char>('\\')) {
            output.append("\\\\");
        } else if (ch == static_cast<unsigned char>('"')) {
            output.append("\\\"");
        } else if (ch >= 0x20u && ch <= 0x7Eu) {
            output.push_back(static_cast<char>(ch));
        } else {
            output.append("\\x");
            output.push_back(kHex[ch >> 4u]);
            output.push_back(kHex[ch & 0x0Fu]);
        }
    }
    return output;
}

std::string EscapeWide(const std::wstring& input)
{
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string output;
    output.reserve(input.size());
    for (const wchar_t wide_ch : input) {
        const std::uint32_t ch = static_cast<std::uint32_t>(wide_ch);
        if (ch == static_cast<std::uint32_t>('\\')) {
            output.append("\\\\");
        } else if (ch == static_cast<std::uint32_t>('"')) {
            output.append("\\\"");
        } else if (ch >= 0x20u && ch <= 0x7Eu) {
            output.push_back(static_cast<char>(ch));
        } else if (ch <= 0xFFFFu) {
            output.append("\\u");
            for (int shift = 12; shift >= 0; shift -= 4) {
                output.push_back(kHex[(ch >> static_cast<unsigned int>(shift)) & 0x0Fu]);
            }
        } else {
            output.append("\\U");
            for (int shift = 28; shift >= 0; shift -= 4) {
                output.push_back(kHex[(ch >> static_cast<unsigned int>(shift)) & 0x0Fu]);
            }
        }
    }
    return output;
}

std::string Hex(std::uint64_t value, int width)
{
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::uppercase << std::hex << std::setfill('0') << std::setw(width)
           << value;
    return stream.str();
}

std::string GuidText(const GUID& guid)
{
    std::string text;
    text.reserve(38u);
    text.push_back('{');
    text.append(Hex(static_cast<std::uint32_t>(guid.Data1), 8));
    text.push_back('-');
    text.append(Hex(static_cast<std::uint16_t>(guid.Data2), 4));
    text.push_back('-');
    text.append(Hex(static_cast<std::uint16_t>(guid.Data3), 4));
    text.push_back('-');
    text.append(Hex(static_cast<std::uint8_t>(guid.Data4[0]), 2));
    text.append(Hex(static_cast<std::uint8_t>(guid.Data4[1]), 2));
    text.push_back('-');
    for (std::size_t i = 2u; i < 8u; ++i) {
        text.append(Hex(static_cast<std::uint8_t>(guid.Data4[i]), 2));
    }
    text.push_back('}');
    return text;
}

std::string TrimAscii(std::string value)
{
    const auto is_space = [](unsigned char ch) {
        return ch == static_cast<unsigned char>(' ') ||
               ch == static_cast<unsigned char>('\t') ||
               ch == static_cast<unsigned char>('\r') ||
               ch == static_cast<unsigned char>('\n') ||
               ch == static_cast<unsigned char>('\v') ||
               ch == static_cast<unsigned char>('\f');
    };
    while (!value.empty() && is_space(static_cast<unsigned char>(value.front()))) {
        value.erase(value.begin());
    }
    while (!value.empty() && is_space(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    return value;
}

bool IsPlaceholderValue(const std::string& raw_value)
{
    const std::string value = LowerAscii(TrimAscii(raw_value));
    if (value.empty() || value == "not provided" || value == "not specified" ||
        value == "default string" || value == "unknown" || value == "none" ||
        value == "n/a" || value == "not applicable") {
        return true;
    }

    std::string alphanumeric;
    alphanumeric.reserve(value.size());
    bool all_zero_digits = true;
    std::size_t zero_digit_count = 0u;
    for (const char raw_ch : value) {
        const auto ch = static_cast<unsigned char>(raw_ch);
        if ((ch >= static_cast<unsigned char>('a') &&
             ch <= static_cast<unsigned char>('z')) ||
            (ch >= static_cast<unsigned char>('0') &&
             ch <= static_cast<unsigned char>('9'))) {
            alphanumeric.push_back(static_cast<char>(ch));
        }
        if (ch >= static_cast<unsigned char>('0') &&
            ch <= static_cast<unsigned char>('9')) {
            ++zero_digit_count;
            if (ch != static_cast<unsigned char>('0')) {
                all_zero_digits = false;
            }
        } else if (ch != static_cast<unsigned char>('-') &&
                   ch != static_cast<unsigned char>('{') &&
                   ch != static_cast<unsigned char>('}') &&
                   ch != static_cast<unsigned char>(' ') &&
                   ch != static_cast<unsigned char>('\t') &&
                   ch != static_cast<unsigned char>('\r') &&
                   ch != static_cast<unsigned char>('\n')) {
            all_zero_digits = false;
        }
    }
    if (alphanumeric == "tobefilledbyoem" || alphanumeric == "oemfill" ||
        alphanumeric == "default") {
        return true;
    }
    return all_zero_digits && zero_digit_count != 0u;
}

bool IsCanonicalValidUuid(const std::string& value)
{
    if (value.size() != 36u) {
        return false;
    }

    bool all_zero = true;
    bool all_ones = true;
    for (std::size_t index = 0u; index < value.size(); ++index) {
        const char ch = value[index];
        if (index == 8u || index == 13u || index == 18u || index == 23u) {
            if (ch != '-') {
                return false;
            }
            continue;
        }

        const bool is_digit = ch >= '0' && ch <= '9';
        const bool is_lower_hex = ch >= 'a' && ch <= 'f';
        const bool is_upper_hex = ch >= 'A' && ch <= 'F';
        if (!is_digit && !is_lower_hex && !is_upper_hex) {
            return false;
        }
        const char normalized =
            (ch >= 'A' && ch <= 'F') ? static_cast<char>(ch - 'A' + 'a') : ch;
        all_zero = all_zero && normalized == '0';
        all_ones = all_ones && normalized == 'f';
    }
    return !all_zero && !all_ones;
}

bool Contains(const std::wstring& value, std::wstring_view part)
{
    return value.find(part) != std::wstring::npos;
}

bool ContainsMacMarker(const std::wstring& name)
{
    std::size_t position = name.find(L"MAC");
    while (position != std::wstring::npos) {
        const std::size_t after = position + 3u;
        if (after == name.size() || name[after] == L'_' || name[after] == L'-' ||
            name[after] == L' ' || name.compare(after, 7u, L"ADDRESS") == 0 ||
            name.compare(after, 4u, L"ADDR") == 0) {
            return true;
        }
        position = name.find(L"MAC", after);
    }
    return false;
}

bool IsIdentifierLead(const std::wstring& raw_name)
{
    const std::wstring name = UpperAscii(raw_name);
    static constexpr std::array<std::wstring_view, 6> hints = {
        L"UUID", L"SERIAL", L"BOARD", L"SYSTEM", L"PRODUCT", L"MACHINE"};
    return ContainsMacMarker(name) ||
           std::any_of(hints.begin(), hints.end(), [&](std::wstring_view hint) {
               return Contains(name, hint);
           });
}

constexpr std::string_view kDroppedVariableLeadReason =
    "Name hints do not verify EFI payload semantics or value correspondence.";

void AppendLine(std::string& output, std::string_view line)
{
    output.append(line.data(), line.size());
    output.append("\r\n");
}

std::string Quote(std::string_view text)
{
    std::string output;
    output.reserve(text.size() + 2u);
    output.push_back('"');
    output.append(text.data(), text.size());
    output.push_back('"');
    return output;
}

using EvidenceMap = std::map<std::string, std::vector<std::string>>;

void AddEvidence(EvidenceMap& evidence,
                 const std::string& field,
                 std::string source)
{
    evidence[field].push_back(std::move(source));
}

bool IsKnownTarget(const std::string& field)
{
    return std::any_of(kTargets.begin(), kTargets.end(), [&](const Target& target) {
        return field == target.field;
    });
}

std::string Join(const std::vector<std::string>& values, std::string_view separator)
{
    std::string joined;
    for (const std::string& value : values) {
        if (!joined.empty()) {
            joined.append(separator.data(), separator.size());
        }
        joined.append(value);
    }
    return joined;
}

std::string FindSmbiosValue(
    const std::vector<std::pair<std::string, std::string>>& smbios,
    std::string_view label)
{
    const std::string wanted = LowerAscii(std::string(label));
    for (const auto& field : smbios) {
        if (LowerAscii(field.first) == wanted &&
            !IsPlaceholderValue(field.second) &&
            (wanted != "system uuid" || IsCanonicalValidUuid(field.second))) {
            return field.second;
        }
    }
    return {};
}

struct DecisionRow {
    std::string category;
    std::string source;
    std::string decision;
    std::string target;
    std::string reason;
};

void AddSmbiosTargetMappings(
    const std::vector<std::pair<std::string, std::string>>& smbios,
    EvidenceMap& confirmed,
    std::vector<DecisionRow>& decisions)
{
    static constexpr std::array<std::pair<const char*, const char*>, 8> mappings = {{
        {"System serial number", "FakeSMBIOSSerial"},
        {"System manufacturer", "FakeSMBIOSManufacturer"},
        {"System product", "FakeSMBIOSProduct"},
        {"Baseboard serial number", "FakeMotherboardSerial"},
        {"Baseboard manufacturer", "FakeMotherboardManufacturer"},
        {"Baseboard product", "FakeMotherboardProduct"},
        {"Processor serial number", "FakeCPUSerial"},
        {"System UUID", "FakeSMBIOSUUID"},
    }};
    for (const auto& mapping : mappings) {
        if (FindSmbiosValue(smbios, mapping.first).empty()) {
            continue;
        }
        const std::string target(mapping.second);
        const std::string source(mapping.first);
        AddEvidence(confirmed, target,
                    "Windows raw SMBIOS field: " + source);
        decisions.push_back({
            "SMBIOS", source, "CONFIRMED -> MAPPED", target,
            source == "System UUID"
                ? "Validated System UUID observation for review; collector does not initialize a replacement value or enable spoofing."
                : "Observed SMBIOS identifier for review; collector does not initialize a replacement value or enable spoofing."});
    }
}
std::string VariableLeadLabel(const EfiVariable& variable)
{
    return "EFI name lead " + GuidText(variable.guid) + "/" +
           Quote(EscapeWide(variable.name));
}

}  // namespace

std::string FormatRuntimeIdentifierInventory(
    const std::vector<EfiVariable>& variables,
    const std::vector<std::pair<std::string, std::string>>& smbios,
    const std::vector<DeviceIdentifier>& devices,
    const std::wstring& supplied_smbios_path,
    const std::wstring& runtime_metadata_report_path)
{
    std::string output;
    output.reserve(16u * 1024u);

    std::size_t runtime_count = 0u;
    std::size_t excluded_count = 0u;
    for (const EfiVariable& variable : variables) {
        if ((variable.attributes & kEfiVariableRuntimeAccess) != 0u) {
            ++runtime_count;
        } else {
            ++excluded_count;
        }
    }

    AppendLine(output, "HWID runtime identifier inventory");
    AppendLine(output,
               "Access: read-only; EFI variable payloads are not included in reports or retained in identifier records.");
    AppendLine(output,
               "Confidence: confirmed means a source explicitly exposes this semantic field; it does not prove factory truth or consumer use.");
    AppendLine(output,
               "Spoofing policy: collected observations are review evidence only; this collector does not initialize replacement values or enable spoofing.");
    if (supplied_smbios_path.empty()) {
        AppendLine(output,
                   "SMBIOS provenance: Current Windows GetSystemFirmwareTable('RSMB') capture.");
    } else {
        AppendLine(output,
                   "SMBIOS provenance: supplied capture verified against current Windows GetSystemFirmwareTable('RSMB') at " +
                       Quote(EscapeWide(supplied_smbios_path)));
    }
    AppendLine(output, "SMBIOS fields (" + std::to_string(smbios.size()) +
                           "); every supplied field is retained below:");
    for (const auto& field : smbios) {
        AppendLine(output, "  [Windows raw SMBIOS table] " +
                               Quote(EscapeBytes(field.first)) + " = " +
                               Quote(EscapeBytes(field.second)));
    }

    if (runtime_metadata_report_path.empty()) {
        AppendLine(output,
                   "EFI metadata provenance: current Windows runtime-visible variable enumeration.");
    } else {
        AppendLine(output,
                   "EFI metadata provenance: existing same-host runtime metadata snapshot at " +
                       Quote(EscapeWide(runtime_metadata_report_path)) +
                       "; native runtime enumeration was not repeated; same-host status was asserted by the caller; EFI payload correspondence was not verified.");
    }
    AppendLine(output,
               "EFI variable analysis: runtime-access variables only; full HVEFI001 metadata records are in the separate variable report.");
    AppendLine(output, "Runtime-access EFI variable count: " +
                           std::to_string(runtime_count));
    AppendLine(output, "Excluded EFI variables without EFI_VARIABLE_RUNTIME_ACCESS: " +
                           std::to_string(excluded_count));
    std::vector<DecisionRow> decisions;
    std::size_t lead_count = 0u;
    for (const EfiVariable& variable : variables) {
        if ((variable.attributes & kEfiVariableRuntimeAccess) == 0u ||
            !IsIdentifierLead(variable.name)) {
            continue;
        }

        ++lead_count;
        const std::string lead = VariableLeadLabel(variable);
        AppendLine(output, "  EFI name lead: GUID=" + GuidText(variable.guid) +
                               "; NAME=" + Quote(EscapeWide(variable.name)) +
                               "; ATTRS=0x" + Hex(variable.attributes, 8) +
                               "; SIZE=0x" + Hex(variable.size, 16) +
                               "; STATUS=0x" + Hex(variable.status, 16) +
                               "; decision=CANDIDATE -> DROPPED; target=-; reason=" +
                               std::string(kDroppedVariableLeadReason));
        decisions.push_back({"EFI name lead", lead,
                             "CANDIDATE -> DROPPED", "-",
                             std::string(kDroppedVariableLeadReason)});
    }
    AppendLine(output, "EFI name lead count: " + std::to_string(lead_count));
    AppendLine(output,
               "EFI names provide heuristic hints only; every matching name lead is dropped because its name cannot verify payload semantics or value correspondence.");
    if (lead_count == 0u) {
        AppendLine(output, "  No runtime EFI variable names matched the identifier hints.");
    }

    AppendLine(output,
               "Windows device identifier observations: each returned device remains a separate record; no device is selected when several match.");
    EvidenceMap confirmed;
    for (const DeviceIdentifier& device : devices) {
        const bool known_target = IsKnownTarget(device.field);
        const bool has_observation = !IsPlaceholderValue(device.value);
        const bool confirmed_observation =
            device.confirmed && known_target && has_observation;
        std::string state;
        std::string target;
        std::string reason;
        if (confirmed_observation) {
            state = "CONFIRMED";
            target = device.field;
            reason = "Observed device identifier for review; collector does not initialize a replacement value or enable spoofing.";
            const std::string source =
                "Windows device observation: " + EscapeBytes(device.device) +
                " (" + EscapeBytes(device.source) + ")";
            AddEvidence(confirmed, device.field, source);
            decisions.push_back({"Windows device", device.device + " (" +
                                     device.source + ")", "CONFIRMED -> MAPPED",
                                 device.field, reason});
        } else if (!device.value.empty() && !IsPlaceholderValue(device.value)) {
            state = "DROPPED";
            target = "-";
            reason = device.note.empty()
                         ? "Unconfirmed observation has no verified semantic mapping to a configuration field."
                         : device.note + " Unconfirmed observation is dropped; it is not mapped to a configuration field.";
            decisions.push_back({"Windows device", device.device + " (" +
                                     device.source + ")", "DROPPED", "-",
                                 reason});
        } else {
            state = "UNCOVERED";
            target = known_target ? device.field : "-";
            reason = device.note.empty()
                         ? "No usable identifier value was exposed by this observation."
                         : device.note;
            decisions.push_back({"Windows device", device.device + " (" +
                                    device.source + ")", "UNCOVERED", target,
                            reason});
        }
        AppendLine(output,
                   "  DEVICE: field=" + Quote(EscapeBytes(device.field)) +
                       "; device=" + Quote(EscapeBytes(device.device)) +
                       "; value=" + Quote(EscapeBytes(device.value)) +
                       "; source=" + Quote(EscapeBytes(device.source)) +
                       "; state=" + state +
                       "; target=" + target +
                       "; note=" + Quote(EscapeBytes(reason)));
    }
    AddSmbiosTargetMappings(smbios, confirmed, decisions);

    AppendLine(output,
               "HWID_SPOOF_CONFIG coverage (confirmed values are observations for review only):");
    for (const Target& target : kTargets) {
        const auto found_confirmed = confirmed.find(target.field);
        const bool is_confirmed =
            found_confirmed != confirmed.end() && !found_confirmed->second.empty();
        std::string line = "  " + std::string(target.field) + ": " +
                           (is_confirmed ? "CONFIRMED" : "UNCOVERED");
        if (is_confirmed) {
            line.append("; sources=");
            line.append(Join(found_confirmed->second, " | "));
            line.append("; observed source retained for review; replacement value remains uninitialized and spoofing disabled by this collector");
        } else {
            line.append("; policy=leave replacement value zero and spoofing disabled");
        }
        if (std::string_view(target.field) == "FakeMotherboardProduct") {
            line.append("; SMBIOS Type 2 supplies Baseboard product but no distinct BoardId value or member");
        }
        if (!is_confirmed &&
            std::string_view(target.field) == "FakeGPUSerial") {
            line.append("; not obtainable at runtime by this Windows-only collector");
        }
        AppendLine(output, line);
    }
    AppendLine(output,
               "SMBIOS Type 2 has no separate board identifier in the captured field set; no BoardId member is added.");
    AppendLine(output,
               "Uncovered fields remain unknown; values from another field or device class are not substituted.");

    AppendLine(output, "Decision table: source | observation | decision | target | reason/policy");
    for (const DecisionRow& row : decisions) {
        AppendLine(output, row.category + " | " + Quote(EscapeBytes(row.source)) +
                               " | " + row.decision + " | " + row.target +
                               " | " + Quote(EscapeBytes(row.reason)));
    }
    for (const Target& target : kTargets) {
        const auto found = confirmed.find(target.field);
        if (found != confirmed.end() && !found->second.empty()) {
            continue;
        }
        std::string reason =
            "No confirmed source; leave replacement value zero and spoofing disabled.";
        if (std::string_view(target.field) == "FakeGPUSerial") {
            reason = "Not obtainable at runtime by this Windows-only collector; leave replacement value zero and spoofing disabled.";
        } else if (std::string_view(target.field) == "FakeCPUSerial") {
            reason = "No confirmed CPU serial source; leave replacement value zero and spoofing disabled.";
        }
        AppendLine(output, "HWID config field | " + std::string(target.field) +
                               " | UNCOVERED | " + target.field + " | " +
                               Quote(EscapeBytes(reason)));
    }
    return output;
}

}  // namespace hwid_spoofer
