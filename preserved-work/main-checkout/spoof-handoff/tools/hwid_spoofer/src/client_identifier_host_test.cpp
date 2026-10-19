#include "client_reads.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using hwid_spoofer::client::Identifier;
using Key = std::pair<HWID_SPOOF_FIELD, std::wstring>;
using Values = std::map<Key, std::wstring>;

struct Baseline {
    HWID_SPOOF_FIELD field;
    std::string member;
    std::wstring device;
    std::wstring original;
    std::wstring replacement;
};

struct Cleanup final {
    ~Cleanup() { HwidSpooferCleanup(); }
};

void Require(bool condition, const std::string& message) {
    if (!condition) { throw std::runtime_error(message); }
}

std::wstring Wide(const std::string& text) {
    if (text.empty()) { return {}; }
    Require(text.size() <= 4096u, "Text exceeds the bounded input size.");
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                        text.data(), static_cast<int>(text.size()),
                                        nullptr, 0);
    Require(size > 0, "Text is not valid UTF-8.");
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    Require(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                static_cast<int>(text.size()), result.data(),
                                size) == size, "UTF-8 conversion failed.");
    return result;
}

std::string Utf8(const std::wstring& text) {
    if (text.empty()) { return {}; }
    Require(text.size() <= 4096u, "Native text exceeds the bounded output size.");
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                        text.data(), static_cast<int>(text.size()),
                                        nullptr, 0, nullptr, nullptr);
    Require(size > 0, "Native text is not valid UTF-16.");
    std::string result(static_cast<std::size_t>(size), '\0');
    Require(WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                                static_cast<int>(text.size()), result.data(),
                                size, nullptr, nullptr) == size,
            "UTF-16 conversion failed.");
    return result;
}

void CheckedStatus(NTSTATUS status, const char* operation) {
    if (status < 0) {
        std::ostringstream message;
        message << operation << " failed: NTSTATUS=0x" << std::uppercase
                << std::hex << std::setw(8) << std::setfill('0')
                << static_cast<unsigned long>(status);
        throw std::runtime_error(message.str());
    }
}

std::vector<Baseline> LoadBaseline(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    Require(input.is_open(), "Could not open the captured-original baseline.");
    std::string line;
    Require(static_cast<bool>(std::getline(input, line)), "Baseline is empty.");
    if (!line.empty() && line.back() == '\r') { line.pop_back(); }
    Require(line == "member\tdevice\toriginal\treplacement",
            "Baseline header is invalid.");
    SIZE_T mapCount = 0;
    const auto* fields = HwidSpooferGetFieldMap(&mapCount);
    Require(fields != nullptr && mapCount == 9u, "Confirmed field map is invalid.");
    std::vector<Baseline> result;
    std::set<Key> unique;
    std::set<HWID_SPOOF_FIELD> present;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') { line.pop_back(); }
        Require(!line.empty() && line.size() <= 4096u,
                "Baseline row is empty or oversized.");
        std::vector<std::string> columns;
        std::size_t start = 0;
        for (;;) {
            const auto end = line.find('\t', start);
            columns.push_back(line.substr(start, end - start));
            if (end == std::string::npos) { break; }
            start = end + 1u;
        }
        Require(columns.size() == 4u, "Baseline row must contain four columns.");
        const HWID_SPOOF_FIELD_INFO* found = nullptr;
        for (SIZE_T i = 0; i < mapCount; ++i) {
            if (columns[0] == fields[i].Member) { found = &fields[i]; break; }
        }
        Require(found != nullptr, "Baseline contains an unconfirmed field.");
        Baseline row{found->Field, columns[0], Wide(columns[1]),
                     Wide(columns[2]), Wide(columns[3])};
        Require(!row.device.empty() && !row.original.empty() &&
                    !row.replacement.empty() && row.original != row.replacement,
                "Baseline must specify a device, original and distinct replacement.");
        Require(unique.insert({row.field, row.device}).second,
                "Baseline contains a duplicate field/device identity.");
        present.insert(row.field);
        result.push_back(std::move(row));
        Require(result.size() <= HWID_MAX_DEVICE_BINDINGS + 7u,
                "Baseline contains too many selected devices.");
    }
    Require(input.eof() && present.size() == 9u,
            "Baseline does not cover exactly the nine confirmed fields.");
    return result;
}

template <std::size_t N>
void Copy(WCHAR (&destination)[N], const std::wstring& text) {
    Require(text.size() < N, "Configuration text exceeds its member capacity.");
    Require(wcscpy_s(destination, N, text.c_str()) == 0,
            "Configuration copy failed.");
}

std::array<BYTE, 6> ParseMac(const std::wstring& text) {
    Require(text.size() == 17u, "MAC replacement must use six colon-separated octets.");
    std::array<BYTE, 6> bytes{};
    auto digit = [](wchar_t c) -> unsigned {
        if (c >= L'0' && c <= L'9') { return static_cast<unsigned>(c - L'0'); }
        if (c >= L'A' && c <= L'F') { return static_cast<unsigned>(c - L'A') + 10u; }
        throw std::runtime_error("MAC replacement must use uppercase hexadecimal.");
    };
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        const auto offset = i * 3u;
        bytes[i] = static_cast<BYTE>((digit(text[offset]) << 4u) | digit(text[offset + 1u]));
        if (i != bytes.size() - 1u) { Require(text[offset + 2u] == L':', "MAC separator is invalid."); }
    }
    return bytes;
}

void Configure(const std::vector<Baseline>& rows) {
    HWID_SPOOF_CONFIG config{};
    config.SpoofDiskSerial = TRUE;
    config.SpoofMacAddress = TRUE;
    config.SpoofSMBIOS = TRUE;
    config.SpoofMotherboard = TRUE;
    config.SpoofSMBIOSUUID = TRUE;
    std::vector<HWID_SPOOF_DEVICE_BINDING> bindings;
    std::set<HWID_SPOOF_FIELD> configured;
    for (const auto& row : rows) {
        if (row.field == HwidFieldDiskSerial || row.field == HwidFieldMacAddress) {
            HWID_SPOOF_DEVICE_BINDING binding{};
            binding.Field = row.field;
            Copy(binding.DeviceKey, row.device);
            Copy(binding.OriginalValue, row.original);
            Copy(binding.ReplacementValue, row.replacement);
            bindings.push_back(binding);
        }
        if (!configured.insert(row.field).second) {
            Require(row.field == HwidFieldDiskSerial || row.field == HwidFieldMacAddress,
                    "SMBIOS field is ambiguous in the captured baseline.");
            continue;
        }
        switch (row.field) {
        case HwidFieldDiskSerial: Copy(config.FakeDiskSerial, row.replacement); break;
        case HwidFieldMacAddress: {
            const auto mac = ParseMac(row.replacement);
            std::copy(mac.begin(), mac.end(), config.FakeMacAddress);
            break;
        }
        case HwidFieldSystemSerial: Copy(config.FakeSMBIOSSerial, row.replacement); break;
        case HwidFieldSystemManufacturer: Copy(config.FakeSMBIOSManufacturer, row.replacement); break;
        case HwidFieldSystemProduct: Copy(config.FakeSMBIOSProduct, row.replacement); break;
        case HwidFieldBoardSerial: Copy(config.FakeMotherboardSerial, row.replacement); break;
        case HwidFieldBoardManufacturer: Copy(config.FakeMotherboardManufacturer, row.replacement); break;
        case HwidFieldBoardProduct: Copy(config.FakeMotherboardProduct, row.replacement); break;
        case HwidFieldSystemUuid: Copy(config.FakeSMBIOSUUID, row.replacement); break;
        default: throw std::runtime_error("Configuration field is outside the confirmed map.");
        }
    }
    CheckedStatus(HwidSpooferValidateConfig(&config), "ValidateConfig");
    CheckedStatus(HwidSpooferInitialize(&config), "Initialize");
    CheckedStatus(HwidSpooferSetDeviceBindings(bindings.data(), bindings.size()), "SetDeviceBindings");
}

Values Read(bool client, DWORD mask) {
    std::vector<Identifier> identifiers;
    std::wstring error;
    const bool ok = client ? hwid_spoofer::client::ReadClientIdentifiers(identifiers, error, mask)
                           : hwid_spoofer::client::ReadNativeIdentifiers(identifiers, error, mask);
    Require(ok, Utf8(error));
    Values values;
    for (const auto& row : identifiers) {
        Require(values.emplace(Key{row.field, row.device}, row.value).second,
                "Native source returned a duplicate field/device identity.");
    }
    return values;
}

std::size_t Prove(const std::vector<Baseline>& baseline, DWORD mask, bool combined) {
    const Values originals = Read(false, mask);
    Values expected = originals;
    std::vector<const Baseline*> targets;
    for (const auto& row : baseline) {
        if ((mask & HWID_FIELD_MASK(row.field)) == 0u) { continue; }
        const Key key{row.field, row.device};
        const auto found = originals.find(key);
        Require(found != originals.end(), row.member + " selected device is absent: " + Utf8(row.device));
        Require(found->second == row.original,
                row.member + " captured-original mismatch for " + Utf8(row.device) +
                    "; captured=" + Utf8(row.original) + "; native=" + Utf8(found->second));
        expected[key] = row.replacement;
        targets.push_back(&row);
    }
    Require(!targets.empty(), "No confirmed targets were selected.");
    CheckedStatus(HwidSpooferSetFieldMask(mask), "SetFieldMask");
    Require(Read(true, mask) == originals, "Inactive client read changed a native value.");
    CheckedStatus(HwidSpooferApply(), "Apply");
    Require(HwidSpooferIsActive() != FALSE, "Apply did not activate the process-local interceptor.");
    const Values applied = Read(true, mask);
    Require(applied == expected, "Applied client read differs from the explicit replacement map.");
    Require(Read(false, mask) == originals, "A direct native read changed while the client interceptor was active.");
    CheckedStatus(HwidSpooferRemove(), "Remove");
    Require(HwidSpooferIsActive() == FALSE, "Remove left the process-local interceptor active.");
    const Values restored = Read(true, mask);
    Require(restored == originals && Read(false, mask) == originals,
            "Remove did not restore the freshly read original values.");
    if (!combined) {
        for (const auto* target : targets) {
            const Key key{target->field, target->device};
            Require(applied.at(key) != originals.at(key), "Replacement equals original.");
            std::cout << "PROOF " << target->member << " device=" << std::quoted(Utf8(target->device))
                      << " original=" << std::quoted(Utf8(originals.at(key)))
                      << " spoofed=" << std::quoted(Utf8(applied.at(key)))
                      << " restored=" << std::quoted(Utf8(restored.at(key)))
                      << " native_while_active=original PASS\n" << std::flush;
        }
    }
    return targets.size();
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
    Cleanup cleanup;
    try {
        std::filesystem::path baseline;
        bool firstOnly = false;
        for (int i = 1; i < argc; ++i) {
            const std::wstring option = argv[i];
            if (option == L"--baseline" && i + 1 < argc) { baseline = argv[++i]; }
            else if (option == L"--first-field") { firstOnly = true; }
            else { throw std::runtime_error("Usage: client_identifier_host_test --baseline <client-originals.tsv> [--first-field]"); }
        }
        Require(!baseline.empty(), "An explicit captured-original baseline is required.");
        const auto rows = LoadBaseline(baseline);
        Configure(rows);
        std::cout << "Machine identifier spoofing for a client application; process=" << GetCurrentProcessId()
                  << "; explicit read wrappers; native Windows values remain observable.\n";
        std::cout << "BEGIN first field: SMBIOS system serial\n" << std::flush;
        std::size_t proofs = Prove(rows, HWID_FIELD_MASK(HwidFieldSystemSerial), false);
        std::cout << "FIRST FIELD PASS: APPLY/read/REMOVE/read; native reads unchanged.\n" << std::flush;
        if (firstOnly) { return 0; }
        constexpr std::array<HWID_SPOOF_FIELD, 8> remaining{
            HwidFieldSystemManufacturer, HwidFieldSystemProduct,
            HwidFieldBoardSerial, HwidFieldBoardManufacturer, HwidFieldBoardProduct,
            HwidFieldSystemUuid, HwidFieldDiskSerial, HwidFieldMacAddress};
        for (const auto field : remaining) { proofs += Prove(rows, HWID_FIELD_MASK(field), false); }
        const auto combined = Prove(rows, HWID_ALL_CONFIRMED_FIELDS, true);
        Require(combined == rows.size() && proofs == rows.size(), "A selected target lacks read-back proof.");
        std::cout << "COMBINED PASS: all nine fields; " << combined << " selected field/device rows; native reads unchanged.\n"
                  << "PASS: nine confirmed fields; " << proofs << " individual original/spoofed/restored proofs; process state removed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "BOUNDARY: " << error.what() << "; no passing read-back is claimed for the failed field.\n";
        return 1;
    }
}
