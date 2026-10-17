#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "smbios_table.h"
#include "smbios_values.h"

#include <Windows.h>
#include <Wbemidl.h>
#include <OleAuto.h>
#include <sddl.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr DWORD kRawSmbiosProvider = 0x52534D42u;
constexpr std::size_t kRawSmbiosHeaderBytes = 8u;
constexpr std::size_t kMaximumWmiTextCharacters = 4096u;
constexpr std::size_t kMaximumReportBytes = 16u * 1024u * 1024u;
constexpr auto kWmiQueryBudget = std::chrono::seconds(6);
constexpr auto kWmiPollInterval = std::chrono::milliseconds(250);

using Observations = std::array<std::optional<std::string>, SM_FIELD_COUNT>;
using ExpectedValues = std::array<std::string, SM_FIELD_COUNT>;

static_assert(SM_FIELD_COUNT == 7, "The read-back report contract requires seven fields.");
static_assert(SM_ACTIVE_BOOT_MASK == SM_MASK_ALL,
              "The boot profile must select the seven confirmed SMBIOS fields.");

struct NativeRead final {
    Observations fields{};
    std::optional<unsigned> major;
    std::optional<unsigned> minor;
    std::optional<std::size_t> table_bytes;
    std::optional<std::size_t> parsed_bytes_consumed;
    std::optional<unsigned> structure_count;
    std::optional<unsigned> maximum_structure_size;
    std::string error;
};

struct WmiRead final {
    Observations fields{};
    std::size_t system_product_instances = 0;
    std::size_t baseboard_instances = 0;
    bool system_product_count_exact = false;
    bool baseboard_count_exact = false;
    std::string system_product_error;
    std::string baseboard_error;
};

struct Probe final {
    std::string mode;
    bool valid_arguments = true;
    ExpectedValues expected{};
    NativeRead native;
    WmiRead wmi;
    std::vector<std::string> errors;
    std::uint64_t uptime_ms = 0;
    std::string approximate_boot_utc;
    bool passed = false;
};

enum class ExpectMode {
    Original,
    Serial,
    Seven,
    Invalid
};

template <typename T>
class ComOwner final {
public:
    ComOwner() noexcept = default;
    explicit ComOwner(T* value) noexcept : value_(value) {}
    ~ComOwner() {
        if (value_ != nullptr) {
            value_->Release();
        }
    }
    ComOwner(const ComOwner&) = delete;
    ComOwner& operator=(const ComOwner&) = delete;
    T* get() const noexcept { return value_; }
    T** put() noexcept {
        reset();
        return &value_;
    }
    T* operator->() const noexcept { return value_; }
    void reset(T* value = nullptr) noexcept {
        if (value_ != nullptr) {
            value_->Release();
        }
        value_ = value;
    }

private:
    T* value_ = nullptr;
};

class ScopedBstr final {
public:
    explicit ScopedBstr(const wchar_t* value) : value_(SysAllocString(value)) {}
    ~ScopedBstr() { SysFreeString(value_); }
    ScopedBstr(const ScopedBstr&) = delete;
    ScopedBstr& operator=(const ScopedBstr&) = delete;
    BSTR get() const noexcept { return value_; }

private:
    BSTR value_ = nullptr;
};

class ScopedVariant final {
public:
    ScopedVariant() noexcept { VariantInit(&value_); }
    ~ScopedVariant() { VariantClear(&value_); }
    ScopedVariant(const ScopedVariant&) = delete;
    ScopedVariant& operator=(const ScopedVariant&) = delete;
    VARIANT* put() noexcept {
        VariantClear(&value_);
        VariantInit(&value_);
        return &value_;
    }
    VARIANT* get() noexcept { return &value_; }

private:
    VARIANT value_{};
};

class ScopedComInitialization final {
public:
    ScopedComInitialization() noexcept
        : result_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)),
          must_uninitialize_(result_ == S_OK || result_ == S_FALSE) {}
    ~ScopedComInitialization() {
        if (must_uninitialize_) {
            CoUninitialize();
        }
    }
    HRESULT result() const noexcept { return result_; }
    bool usable() const noexcept {
        return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE;
    }

private:
    HRESULT result_;
    bool must_uninitialize_;
};

std::string HresultText(const char* operation, HRESULT value) {
    std::ostringstream text;
    text << operation << " failed (HRESULT=0x" << std::uppercase << std::hex
         << std::setw(8) << std::setfill('0')
         << static_cast<unsigned long>(value) << ").";
    return text.str();
}

std::string Win32Text(const char* operation, DWORD value) {
    std::ostringstream text;
    text << operation << " failed (Win32=0x" << std::uppercase << std::hex
         << std::setw(8) << std::setfill('0') << value << ").";
    return text.str();
}

std::string WideToUtf8(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
    if (text.size() > kMaximumWmiTextCharacters) {
        throw std::runtime_error("WMI text exceeds the 4096-character limit.");
    }
    const int input_size = static_cast<int>(text.size());
    const int required = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), input_size,
        nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        throw std::runtime_error("WMI text is not valid UTF-16.");
    }
    std::string result(static_cast<std::size_t>(required), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                            input_size, result.data(), required,
                            nullptr, nullptr) != required) {
        throw std::runtime_error("WMI UTF-8 conversion failed.");
    }
    return result;
}

bool IsValidWideText(const std::wstring& text) {
    if (text.empty() || text.size() > kMaximumWmiTextCharacters ||
        text.find(L'\0') != std::wstring::npos) {
        return false;
    }
    for (std::size_t index = 0; index < text.size(); ++index) {
        const wchar_t code = text[index];
        if (code < 0x20 || code == 0x7f) {
            return false;
        }
        if (code >= 0xd800 && code <= 0xdbff) {
            if (index + 1u >= text.size() || text[index + 1u] < 0xdc00 ||
                text[index + 1u] > 0xdfff) {
                return false;
            }
            ++index;
        } else if (code >= 0xdc00 && code <= 0xdfff) {
            return false;
        }
    }
    return true;
}

std::uint32_t ReadLe32(const std::uint8_t* value) {
    return static_cast<std::uint32_t>(value[0]) |
           (static_cast<std::uint32_t>(value[1]) << 8u) |
           (static_cast<std::uint32_t>(value[2]) << 16u) |
           (static_cast<std::uint32_t>(value[3]) << 24u);
}

bool NativeText(const char (&value)[SM_VALUE_CHARS],
                std::string& output,
                std::string& error) {
    const void* terminator = std::memchr(value, '\0', sizeof(value));
    if (terminator == nullptr) {
        error = "SMBIOS parser returned an unterminated field.";
        return false;
    }
    const auto length = static_cast<const char*>(terminator) - value;
    if (length <= 0) {
        error = "SMBIOS parser returned an empty confirmed field.";
        return false;
    }
    output.assign(value, static_cast<std::size_t>(length));
    for (const unsigned char byte : output) {
        if (byte < 0x20u || byte > 0x7eu) {
            error = "SMBIOS parser returned a field outside printable ASCII.";
            output.clear();
            return false;
        }
    }
    return true;
}

bool ReadNative(NativeRead& result) {
    result = NativeRead{};
    const DWORD required = GetSystemFirmwareTable(
        kRawSmbiosProvider, 0u, nullptr, 0u);
    if (required == 0u) {
        result.error = Win32Text("GetSystemFirmwareTable RSMB size", GetLastError());
        return false;
    }
    if (required < kRawSmbiosHeaderBytes ||
        required > SM_MAX_TABLE_BYTES + kRawSmbiosHeaderBytes) {
        result.error = "Windows RSMB result is outside the supported size bounds.";
        return false;
    }

    std::vector<std::uint8_t> raw(required);
    const DWORD received = GetSystemFirmwareTable(
        kRawSmbiosProvider, 0u, raw.data(), required);
    if (received == 0u) {
        result.error = Win32Text("GetSystemFirmwareTable RSMB", GetLastError());
        return false;
    }
    if (received != required) {
        result.error = "Windows RSMB result size changed during the read.";
        return false;
    }

    result.major = raw[1u];
    result.minor = raw[2u];
    const std::uint32_t table_length = ReadLe32(raw.data() + 4u);
    if (table_length == 0u ||
        table_length != raw.size() - kRawSmbiosHeaderBytes ||
        table_length > SM_MAX_TABLE_BYTES) {
        result.error = "Windows RSMB header length does not match its returned table.";
        return false;
    }
    result.table_bytes = table_length;

    SM_VALUES parsed{};
    if (sm_read_table(raw.data() + kRawSmbiosHeaderBytes,
                      static_cast<std::size_t>(table_length),
                      raw[1u], raw[2u], &parsed) != 1) {
        result.error = "The shared SMBIOS table parser rejected the current RSMB table.";
        return false;
    }
    if (parsed.table_bytes == 0u || parsed.table_bytes > table_length ||
        parsed.structure_count == 0u ||
        parsed.maximum_structure_size == 0u) {
        result.error = "The shared SMBIOS parser returned inconsistent table metadata.";
        return false;
    }
    result.parsed_bytes_consumed = parsed.table_bytes;
    result.structure_count = parsed.structure_count;
    result.maximum_structure_size = parsed.maximum_structure_size;

    for (std::size_t field = 0; field < SM_FIELD_COUNT; ++field) {
        std::string value;
        if (!NativeText(parsed.field[field], value, result.error)) {
            result.fields = {};
            return false;
        }
        result.fields[field] = std::move(value);
    }
    return true;
}

bool InitializeWmi(ScopedComInitialization& com,
                   ComOwner<IWbemServices>& services,
                   std::string& error) {
    if (!com.usable()) {
        error = HresultText("CoInitializeEx", com.result());
        return false;
    }

    const HRESULT security = CoInitializeSecurity(
        nullptr, -1, nullptr, nullptr,
        RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE,
        nullptr, EOAC_NONE, nullptr);
    if (FAILED(security) && security != RPC_E_TOO_LATE) {
        error = HresultText("CoInitializeSecurity", security);
        return false;
    }

    ComOwner<IWbemLocator> locator;
    HRESULT status = CoCreateInstance(
        CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
        IID_IWbemLocator, reinterpret_cast<void**>(locator.put()));
    if (FAILED(status)) {
        error = HresultText("Create WMI locator", status);
        return false;
    }

    ScopedBstr name_space(L"ROOT\\CIMV2");
    if (name_space.get() == nullptr) {
        error = "Allocate WMI namespace failed.";
        return false;
    }
    status = locator->ConnectServer(name_space.get(), nullptr, nullptr,
                                    nullptr, 0, nullptr, nullptr,
                                    services.put());
    if (FAILED(status)) {
        error = HresultText("Connect to ROOT/CIMV2", status);
        return false;
    }
    status = CoSetProxyBlanket(
        services.get(), RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
        RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr,
        EOAC_NONE);
    if (FAILED(status)) {
        error = HresultText("Set WMI proxy security", status);
        services.reset();
        return false;
    }
    return true;
}

bool ReadWmiTextProperty(IWbemClassObject* object,
                         const wchar_t* property,
                         std::string& output,
                         std::string& error) {
    ScopedVariant value;
    const HRESULT status = object->Get(property, 0, value.put(), nullptr, nullptr);
    if (FAILED(status)) {
        error = HresultText("Read WMI property", status);
        return false;
    }
    if (value.get()->vt != VT_BSTR || value.get()->bstrVal == nullptr) {
        error = "WMI property is missing or is not text.";
        return false;
    }
    const UINT length = SysStringLen(value.get()->bstrVal);
    if (length == 0u || length > kMaximumWmiTextCharacters) {
        error = "WMI property is empty or exceeds the 4096-character limit.";
        return false;
    }
    const std::wstring text(value.get()->bstrVal,
                            static_cast<std::size_t>(length));
    if (!IsValidWideText(text)) {
        error = "WMI property contains invalid or non-printable UTF-16 text.";
        return false;
    }
    try {
        output = WideToUtf8(text);
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
    return true;
}

template <std::size_t N>
bool ReadOneWmiClass(IWbemServices* services,
                     const wchar_t* query_text,
                     const std::array<const wchar_t*, N>& properties,
                     const std::array<std::size_t, N>& field_indexes,
                     Observations& fields,
                     std::size_t& instances,
                     bool& count_exact,
                     std::string& error) {
    instances = 0u;
    count_exact = false;
    error.clear();
    const auto deadline = std::chrono::steady_clock::now() + kWmiQueryBudget;
    ScopedBstr language(L"WQL");
    ScopedBstr query(query_text);
    if (language.get() == nullptr || query.get() == nullptr) {
        error = "Allocate WMI query text failed.";
        return false;
    }

    ComOwner<IEnumWbemClassObject> enumerator;
    const HRESULT query_status = services->ExecQuery(
        language.get(), query.get(),
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
        nullptr, enumerator.put());
    if (FAILED(query_status)) {
        error = HresultText("Execute WMI query", query_status);
        return false;
    }
    if (std::chrono::steady_clock::now() >= deadline) {
        error = "WMI class query exceeded its 6-second time budget.";
        return false;
    }

    bool complete = false;
    while (!complete) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            error = "WMI class enumeration exceeded its 6-second time budget.";
            return false;
        }
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - now);
        const auto wait = std::min(remaining, kWmiPollInterval);
        IWbemClassObject* raw_instance = nullptr;
        ULONG returned = 0u;
        const HRESULT next_status = enumerator->Next(
            static_cast<LONG>(wait.count()), 1u, &raw_instance, &returned);
        ComOwner<IWbemClassObject> instance(raw_instance);

        if (next_status == WBEM_S_TIMEDOUT) {
            continue;
        }
        if (FAILED(next_status)) {
            error = HresultText("Read WMI query result", next_status);
            return false;
        }
        if (returned == 0u) {
            if (next_status == WBEM_S_FALSE) {
                complete = true;
                count_exact = true;
            }
            continue;
        }
        if (instance.get() == nullptr) {
            error = "WMI returned an instance count without an object.";
            return false;
        }
        ++instances;
        if (instances > 1u) {
            count_exact = false;
            error = "WMI class returned more than one instance; no instance was selected.";
            return false;
        }

        for (std::size_t index = 0; index < properties.size(); ++index) {
            if (std::chrono::steady_clock::now() >= deadline) {
                count_exact = false;
                error = "WMI class property reads exceeded the 6-second time budget.";
                return false;
            }
            std::string value;
            if (!ReadWmiTextProperty(instance.get(), properties[index], value, error)) {
                error = "WMI required property read failed: " + error;
                return false;
            }
            fields[field_indexes[index]] = std::move(value);
            if (std::chrono::steady_clock::now() >= deadline) {
                count_exact = false;
                error = "WMI class property reads exceeded the 6-second time budget.";
                return false;
            }
        }
        if (next_status == WBEM_S_FALSE) {
            complete = true;
            count_exact = true;
        }
    }

    if (instances != 1u) {
        error = "WMI class must return exactly one instance; observed zero.";
        return false;
    }
    return true;
}

bool ReadWmi(WmiRead& result) {
    result = WmiRead{};
    ScopedComInitialization com;
    ComOwner<IWbemServices> services;
    std::string initialization_error;
    if (!InitializeWmi(com, services, initialization_error)) {
        result.system_product_error = initialization_error;
        result.baseboard_error = initialization_error;
        return false;
    }

    const std::array<const wchar_t*, 4> system_properties{
        L"IdentifyingNumber", L"Vendor", L"Name", L"UUID"};
    const std::array<std::size_t, 4> system_fields{
        SM_FIELD_SYSTEM_SERIAL, SM_FIELD_SYSTEM_VENDOR,
        SM_FIELD_SYSTEM_PRODUCT, SM_FIELD_SYSTEM_UUID};
    const bool system_ok = ReadOneWmiClass(
        services.get(),
        L"SELECT IdentifyingNumber, Vendor, Name, UUID "
        L"FROM Win32_ComputerSystemProduct",
        system_properties, system_fields, result.fields,
        result.system_product_instances, result.system_product_count_exact,
        result.system_product_error);

    const std::array<const wchar_t*, 3> board_properties{
        L"SerialNumber", L"Manufacturer", L"Product"};
    const std::array<std::size_t, 3> board_fields{
        SM_FIELD_BOARD_SERIAL, SM_FIELD_BOARD_VENDOR,
        SM_FIELD_BOARD_PRODUCT};
    const bool board_ok = ReadOneWmiClass(
        services.get(),
        L"SELECT SerialNumber, Manufacturer, Product FROM Win32_BaseBoard",
        board_properties, board_fields,
        result.fields, result.baseboard_instances,
        result.baseboard_count_exact, result.baseboard_error);

    return system_ok && board_ok;
}

ExpectedValues KnownValues(const SM_VALUES& values) {
    ExpectedValues result{};
    for (std::size_t field = 0; field < SM_FIELD_COUNT; ++field) {
        const void* terminator = std::memchr(
            values.field[field], '\0', SM_VALUE_CHARS);
        if (terminator == nullptr) {
            throw std::runtime_error("Locked SMBIOS value is not terminated.");
        }
        const auto length = static_cast<const char*>(terminator) -
                            values.field[field];
        if (length <= 0) {
            throw std::runtime_error("Locked SMBIOS value is empty.");
        }
        result[field].assign(values.field[field], static_cast<std::size_t>(length));
    }
    return result;
}

std::string ModeName(ExpectMode mode) {
    switch (mode) {
    case ExpectMode::Original: return "original";
    case ExpectMode::Serial: return "serial";
    case ExpectMode::Seven: return "seven";
    default: return "invalid";
    }
}

bool ParseMode(const std::wstring& text, ExpectMode& mode) {
    if (text == L"original") {
        mode = ExpectMode::Original;
    } else if (text == L"serial") {
        mode = ExpectMode::Serial;
    } else if (text == L"seven") {
        mode = ExpectMode::Seven;
    } else {
        mode = ExpectMode::Invalid;
        return false;
    }
    return true;
}

void SetExpected(Probe& probe, ExpectMode mode) {
    const ExpectedValues original = KnownValues(sm_host_originals);
    const ExpectedValues replacement = KnownValues(sm_host_replacements);
    probe.expected = original;
    if (mode == ExpectMode::Seven) {
        probe.expected = replacement;
    } else if (mode == ExpectMode::Serial) {
        for (std::size_t field = 0; field < SM_FIELD_COUNT; ++field) {
            if ((SM_MASK_SERIAL & (UINT32_C(1) << field)) != 0u) {
                probe.expected[field] = replacement[field];
            }
        }
    }
}

void CaptureBootIdentity(Probe& probe) {
    probe.uptime_ms = GetTickCount64();
    FILETIME current_file_time{};
    GetSystemTimeAsFileTime(&current_file_time);
    ULARGE_INTEGER now{};
    now.LowPart = current_file_time.dwLowDateTime;
    now.HighPart = current_file_time.dwHighDateTime;
    const std::uint64_t elapsed_100ns = probe.uptime_ms * 10000u;
    if (now.QuadPart < elapsed_100ns) {
        probe.approximate_boot_utc.clear();
        return;
    }
    ULARGE_INTEGER boot{};
    boot.QuadPart = now.QuadPart - elapsed_100ns;
    FILETIME boot_file_time{boot.LowPart, boot.HighPart};
    SYSTEMTIME boot_system_time{};
    if (!FileTimeToSystemTime(&boot_file_time, &boot_system_time)) {
        probe.approximate_boot_utc.clear();
        return;
    }
    std::ostringstream text;
    text << std::setfill('0') << std::setw(4) << boot_system_time.wYear << '-'
         << std::setw(2) << boot_system_time.wMonth << '-'
         << std::setw(2) << boot_system_time.wDay << 'T'
         << std::setw(2) << boot_system_time.wHour << ':'
         << std::setw(2) << boot_system_time.wMinute << ':'
         << std::setw(2) << boot_system_time.wSecond << '.'
         << std::setw(3) << boot_system_time.wMilliseconds << 'Z';
    probe.approximate_boot_utc = text.str();
}

std::string JsonEscape(const std::string& text) {
    static constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(text.size() + 8u);
    for (const unsigned char value : text) {
        switch (value) {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\b': result += "\\b"; break;
        case '\f': result += "\\f"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:
            if (value < 0x20u) {
                result += "\\u00";
                result.push_back(hex[(value >> 4u) & 0x0fu]);
                result.push_back(hex[value & 0x0fu]);
            } else {
                result.push_back(static_cast<char>(value));
            }
            break;
        }
    }
    return result;
}

void AppendJsonString(std::ostringstream& output, const std::string& text) {
    output << '"' << JsonEscape(text) << '"';
}

void AppendJsonOptional(std::ostringstream& output,
                        const std::optional<std::string>& value) {
    if (value.has_value()) {
        AppendJsonString(output, *value);
    } else {
        output << "null";
    }
}

void AppendValueMap(std::ostringstream& output,
                    const ExpectedValues& values) {
    output << '{';
    for (std::size_t field = 0; field < SM_FIELD_COUNT; ++field) {
        if (field != 0u) output << ',';
        AppendJsonString(output, sm_field_names[field]);
        output << ':';
        AppendJsonString(output, values[field]);
    }
    output << '}';
}

void AppendObservationMap(std::ostringstream& output,
                          const Observations& values) {
    output << '{';
    for (std::size_t field = 0; field < SM_FIELD_COUNT; ++field) {
        if (field != 0u) output << ',';
        AppendJsonString(output, sm_field_names[field]);
        output << ':';
        AppendJsonOptional(output, values[field]);
    }
    output << '}';
}

std::string BuildReport(Probe& probe) {
    bool all_fields_match = true;
    std::array<bool, SM_FIELD_COUNT> native_matches{};
    std::array<bool, SM_FIELD_COUNT> wmi_matches{};
    for (std::size_t field = 0; field < SM_FIELD_COUNT; ++field) {
        const bool is_system_field = field == SM_FIELD_SYSTEM_SERIAL ||
            field == SM_FIELD_SYSTEM_VENDOR ||
            field == SM_FIELD_SYSTEM_PRODUCT ||
            field == SM_FIELD_SYSTEM_UUID;
        const bool wmi_source_ok = is_system_field
            ? probe.wmi.system_product_error.empty()
            : probe.wmi.baseboard_error.empty();
        native_matches[field] = probe.native.error.empty() &&
            probe.native.fields[field].has_value() &&
            *probe.native.fields[field] == probe.expected[field];
        wmi_matches[field] = wmi_source_ok &&
            probe.wmi.fields[field].has_value() &&
            *probe.wmi.fields[field] == probe.expected[field];
        all_fields_match = all_fields_match && native_matches[field] &&
                           wmi_matches[field];
        if (!native_matches[field]) {
            probe.errors.emplace_back(
                std::string("Native value missing or mismatched for field ") +
                sm_field_names[field] + ".");
        }
        if (!wmi_matches[field]) {
            probe.errors.emplace_back(
                std::string("WMI value missing, ambiguous, or mismatched for field ") +
                sm_field_names[field] + ".");
        }
    }
    probe.passed = probe.valid_arguments && probe.errors.empty() &&
        probe.native.error.empty() && probe.wmi.system_product_error.empty() &&
        probe.wmi.baseboard_error.empty() && all_fields_match;

    std::ostringstream output;
    output << "{\n  \"schema\":\"smbios-windows-readback.v1\",\n"
           << "  \"mode\":";
    AppendJsonString(output, probe.mode);
    output << ",\n  \"result\":\"" << (probe.passed ? "PASS" : "FAIL") << "\",\n"
           << "  \"originalKnown\":";
    AppendValueMap(output, KnownValues(sm_host_originals));
    output << ",\n  \"expected\":";
    AppendValueMap(output, probe.expected);
    output << ",\n  \"observedNative\":";
    AppendObservationMap(output, probe.native.fields);
    output << ",\n  \"observedWmi\":";
    AppendObservationMap(output, probe.wmi.fields);

    output << ",\n  \"native\":{\"source\":\"GetSystemFirmwareTable(RSMB)\",\"major\":";
    if (probe.native.major.has_value()) output << *probe.native.major;
    else output << "null";
    output << ",\"minor\":";
    if (probe.native.minor.has_value()) output << *probe.native.minor;
    else output << "null";
    output << ",\"tableBytes\":";
    if (probe.native.table_bytes.has_value()) output << *probe.native.table_bytes;
    else output << "null";
    output << ",\"parsedBytesConsumed\":";
    if (probe.native.parsed_bytes_consumed.has_value()) output << *probe.native.parsed_bytes_consumed;
    else output << "null";
    output << ",\"structureCount\":";
    if (probe.native.structure_count.has_value()) output << *probe.native.structure_count;
    else output << "null";
    output << ",\"maximumStructureSize\":";
    if (probe.native.maximum_structure_size.has_value()) output << *probe.native.maximum_structure_size;
    else output << "null";
    output << ",\"error\":";
    if (probe.native.error.empty()) output << "null";
    else AppendJsonString(output, probe.native.error);
    output << "},\n  \"wmi\":{\"source\":\"ROOT\\\\CIMV2\","
           << "\"systemProductClass\":\"Win32_ComputerSystemProduct\","
           << "\"systemProductInstancesObserved\":" << probe.wmi.system_product_instances
           << ",\"systemProductExactlyOneInstance\":"
           << (probe.wmi.system_product_count_exact &&
                       probe.wmi.system_product_instances == 1u ? "true" : "false")
           << ",\"systemProductInstanceCountExact\":"
           << (probe.wmi.system_product_count_exact ? "true" : "false")
           << ",\"systemProductError\":";
    if (probe.wmi.system_product_error.empty()) output << "null";
    else AppendJsonString(output, probe.wmi.system_product_error);
    output << ",\"baseboardClass\":\"Win32_BaseBoard\","
           << "\"baseboardInstancesObserved\":" << probe.wmi.baseboard_instances
           << ",\"baseboardExactlyOneInstance\":"
           << (probe.wmi.baseboard_count_exact &&
                       probe.wmi.baseboard_instances == 1u ? "true" : "false")
           << ",\"baseboardInstanceCountExact\":"
           << (probe.wmi.baseboard_count_exact ? "true" : "false")
           << ",\"baseboardError\":";
    if (probe.wmi.baseboard_error.empty()) output << "null";
    else AppendJsonString(output, probe.wmi.baseboard_error);
    output << "},\n  \"fields\":{\n";

    for (std::size_t field = 0; field < SM_FIELD_COUNT; ++field) {
        if (field != 0u) output << ",\n";
        output << "    ";
        AppendJsonString(output, sm_field_names[field]);
        output << ":{\"originalKnown\":";
        AppendJsonString(output, KnownValues(sm_host_originals)[field]);
        output << ",\"expected\":";
        AppendJsonString(output, probe.expected[field]);
        output << ",\"observedNative\":";
        AppendJsonOptional(output, probe.native.fields[field]);
        output << ",\"observedWmi\":";
        AppendJsonOptional(output, probe.wmi.fields[field]);
        output << ",\"nativeMatches\":"
               << (native_matches[field] ? "true" : "false")
               << ",\"wmiMatches\":"
               << (wmi_matches[field] ? "true" : "false")
               << ",\"pass\":"
               << (probe.valid_arguments && native_matches[field] &&
                           wmi_matches[field] ? "true" : "false")
               << '}';
    }
    output << "\n  },\n  \"bootIdentity\":{\"uptimeMilliseconds\":"
           << probe.uptime_ms << ",\"approximateBootUtc\":";
    if (probe.approximate_boot_utc.empty()) output << "null";
    else AppendJsonString(output, probe.approximate_boot_utc);
    output << "},\n  \"errors\":[";
    bool first_error = true;
    for (const std::string& error : probe.errors) {
        if (!first_error) output << ',';
        AppendJsonString(output, error);
        first_error = false;
    }
    if (!probe.native.error.empty()) {
        if (!first_error) output << ',';
        AppendJsonString(output, "native: " + probe.native.error);
        first_error = false;
    }
    if (!probe.wmi.system_product_error.empty()) {
        if (!first_error) output << ',';
        AppendJsonString(output, "Win32_ComputerSystemProduct: " +
                                 probe.wmi.system_product_error);
        first_error = false;
    }
    if (!probe.wmi.baseboard_error.empty()) {
        if (!first_error) output << ',';
        AppendJsonString(output, "Win32_BaseBoard: " + probe.wmi.baseboard_error);
    }
    output << "]\n}\n";
    return output.str();
}

bool WriteFileContents(HANDLE file, const std::string& contents, DWORD& error) {
    std::size_t offset = 0u;
    while (offset < contents.size()) {
        const DWORD amount = static_cast<DWORD>(std::min<std::size_t>(
            contents.size() - offset, 1024u * 1024u));
        DWORD written = 0u;
        if (!WriteFile(file, contents.data() + offset, amount, &written, nullptr)) {
            error = GetLastError();
            return false;
        }
        if (written == 0u || written > amount) {
            error = ERROR_WRITE_FAULT;
            return false;
        }
        offset += written;
    }
    if (!FlushFileBuffers(file)) {
        error = GetLastError();
        return false;
    }
    return true;
}

bool WriteNewRestrictedReport(const std::wstring& path,
                              const std::string& report,
                              std::string& error) {
    if (path.empty() || report.size() > kMaximumReportBytes) {
        error = "Output path is empty or report exceeds 16 MiB.";
        return false;
    }
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        error = Win32Text("OpenProcessToken", GetLastError());
        return false;
    }
    DWORD token_bytes = 0u;
    (void)GetTokenInformation(token, TokenUser, nullptr, 0u, &token_bytes);
    DWORD token_error = GetLastError();
    if (token_bytes == 0u || token_error != ERROR_INSUFFICIENT_BUFFER ||
        token_bytes > 1024u * 1024u) {
        CloseHandle(token);
        error = "Could not determine the current user SID buffer size.";
        return false;
    }
    std::vector<BYTE> token_info(token_bytes);
    const bool have_user = GetTokenInformation(
        token, TokenUser, token_info.data(), token_bytes, &token_bytes) != FALSE;
    const DWORD user_error = have_user ? ERROR_SUCCESS : GetLastError();
    CloseHandle(token);
    if (!have_user) {
        error = Win32Text("GetTokenInformation(TokenUser)", user_error);
        return false;
    }

    LPWSTR sid_text = nullptr;
    if (!ConvertSidToStringSidW(
            reinterpret_cast<TOKEN_USER*>(token_info.data())->User.Sid,
            &sid_text)) {
        error = Win32Text("ConvertSidToStringSid", GetLastError());
        return false;
    }
    const std::wstring sddl = L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;" +
                              std::wstring(sid_text) + L")";
    LocalFree(sid_text);

    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr)) {
        error = Win32Text("Create report security descriptor", GetLastError());
        return false;
    }
    SECURITY_ATTRIBUTES security{sizeof(security), descriptor, FALSE};
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0u, &security,
                              CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    const DWORD create_error = GetLastError();
    LocalFree(descriptor);
    if (file == INVALID_HANDLE_VALUE) {
        error = Win32Text("Create new report (existing files are not replaced)",
                          create_error);
        return false;
    }

    DWORD write_error = ERROR_SUCCESS;
    bool ok = WriteFileContents(file, report, write_error);
    if (!ok) {
        LARGE_INTEGER beginning{};
        const std::string fallback =
            "{\"schema\":\"smbios-windows-readback.v1\","
            "\"mode\":\"unknown\",\"result\":\"FAIL\","
            "\"errors\":[\"report write failed with Win32 " +
            std::to_string(write_error) + "\"]}\n";
        if (SetFilePointerEx(file, beginning, nullptr, FILE_BEGIN) &&
            SetEndOfFile(file)) {
            DWORD fallback_error = ERROR_SUCCESS;
            if (WriteFileContents(file, fallback, fallback_error)) {
                error = "Report body write failed (Win32 " +
                        std::to_string(write_error) +
                        "); a minimal failure report was written.";
                CloseHandle(file);
                return false;
            }
        }
        error = "Report write failed (Win32 " + std::to_string(write_error) +
                "); fallback failure report could not be flushed.";
    }
    CloseHandle(file);
    if (!ok) {
        (void)DeleteFileW(path.c_str());
    }
    return ok;
}

bool ParseArguments(int argc,
                    wchar_t** argv,
                    ExpectMode& mode,
                    std::wstring& output_path,
                    std::vector<std::string>& errors) {
    bool have_mode = false;
    bool have_output = false;
    std::wstring mode_text;
    for (int index = 1; index < argc; ++index) {
        const std::wstring option(argv[index]);
        if (option == L"--expect") {
            if (have_mode || index + 1 >= argc) {
                errors.emplace_back("--expect must be supplied once with a mode.");
                continue;
            }
            have_mode = true;
            mode_text = argv[++index];
        } else if (option == L"--output") {
            if (index + 1 >= argc) {
                errors.emplace_back("--output requires a path.");
                continue;
            }
            const std::wstring candidate(argv[++index]);
            if (!have_output) {
                output_path = candidate;
                have_output = true;
            } else {
                errors.emplace_back("--output must be supplied once.");
            }
        } else {
            errors.emplace_back("Unknown command-line option.");
        }
    }
    if (!have_mode) {
        errors.emplace_back("--expect is required.");
    } else if (!ParseMode(mode_text, mode)) {
        errors.emplace_back("--expect must be original, serial, or seven.");
    }
    if (!have_output || output_path.empty()) {
        errors.emplace_back("--output requires a non-empty new report path.");
    }
    return errors.empty();
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    Probe probe;
    ExpectMode mode = ExpectMode::Invalid;
    std::wstring output_path;
    probe.valid_arguments = ParseArguments(
        argc, argv, mode, output_path, probe.errors);
    probe.mode = ModeName(mode);
    try {
        SetExpected(probe, mode);
    } catch (const std::exception& exception) {
        probe.valid_arguments = false;
        probe.errors.emplace_back(exception.what());
        probe.expected = KnownValues(sm_host_originals);
    }
    CaptureBootIdentity(probe);

    try {
        (void)ReadNative(probe.native);
    } catch (const std::exception& exception) {
        probe.native.error = std::string("Native read raised an exception: ") +
                             exception.what();
    } catch (...) {
        probe.native.error = "Native read raised an unknown exception.";
    }
    try {
        (void)ReadWmi(probe.wmi);
    } catch (const std::exception& exception) {
        if (probe.wmi.system_product_error.empty()) {
            probe.wmi.system_product_error =
                std::string("WMI read raised an exception: ") + exception.what();
        }
        if (probe.wmi.baseboard_error.empty()) {
            probe.wmi.baseboard_error = "WMI read did not complete.";
        }
    } catch (...) {
        probe.wmi.system_product_error = "WMI read raised an unknown exception.";
        probe.wmi.baseboard_error = "WMI read did not complete.";
    }

    const std::string report = BuildReport(probe);
    if (output_path.empty()) {
        std::cerr << "FAIL: no report path is available; "
                  << "supply --output with a new path.\n";
        return 1;
    }
    std::string write_error;
    if (!WriteNewRestrictedReport(output_path, report, write_error)) {
        std::cerr << "FAIL: " << write_error << '\n';
        return 1;
    }
    if (!probe.passed) {
        std::cerr << "FAIL: native/WMI read-back did not match the selected expectation; report written.\n";
        return 1;
    }
    std::cout << "PASS: all seven native and WMI fields match " << probe.mode
              << "; report written.\n";
    return 0;
}
