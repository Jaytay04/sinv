#include "device_identifiers.h"

#include <Windows.h>
#include <Wbemidl.h>
#include <OleAuto.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace hwid_spoofer {
namespace {

constexpr std::size_t kMaximumInstancesPerClass = 128;
constexpr std::size_t kMaximumWmiTextCharacters = 4096;
constexpr auto kQueryBudget = std::chrono::seconds(6);
constexpr auto kEnumerationPoll = std::chrono::milliseconds(250);

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

    T* get() const noexcept {
        return value_;
    }

    T** put() noexcept {
        reset();
        return &value_;
    }

    void reset(T* value = nullptr) noexcept {
        if (value_ != nullptr) {
            value_->Release();
        }
        value_ = value;
    }

    T* operator->() const noexcept {
        return value_;
    }

private:
    T* value_ = nullptr;
};

class ScopedBstr final {
public:
    explicit ScopedBstr(const wchar_t* value) : value_(SysAllocString(value)) {}
    ~ScopedBstr() {
        SysFreeString(value_);
    }

    ScopedBstr(const ScopedBstr&) = delete;
    ScopedBstr& operator=(const ScopedBstr&) = delete;

    BSTR get() const noexcept {
        return value_;
    }

private:
    BSTR value_ = nullptr;
};

class ScopedVariant final {
public:
    ScopedVariant() noexcept {
        VariantInit(&value_);
    }
    ~ScopedVariant() {
        VariantClear(&value_);
    }

    ScopedVariant(const ScopedVariant&) = delete;
    ScopedVariant& operator=(const ScopedVariant&) = delete;

    VARIANT* put() noexcept {
        VariantClear(&value_);
        VariantInit(&value_);
        return &value_;
    }

    const VARIANT* get() const noexcept {
        return &value_;
    }

    VARIANT* get() noexcept {
        return &value_;
    }

private:
    VARIANT value_{};
};

class ScopedComInitialization final {
public:
    ScopedComInitialization() noexcept
        : result_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)),
          initialized_(SUCCEEDED(result_)) {}

    ~ScopedComInitialization() {
        if (initialized_) {
            CoUninitialize();
        }
    }

    HRESULT result() const noexcept {
        return result_;
    }

private:
    HRESULT result_;
    bool initialized_;
};

std::wstring HresultText(const wchar_t* operation, HRESULT result) {
    std::wostringstream text;
    text << operation << L" failed (HRESULT=0x"
         << std::uppercase << std::hex << std::setw(8) << std::setfill(L'0')
         << static_cast<unsigned long>(result) << L")";
    return text.str();
}

std::string HresultNote(const char* operation, HRESULT result) {
    std::ostringstream text;
    text << operation << " failed (HRESULT=0x"
         << std::uppercase << std::hex << std::setw(8) << std::setfill('0')
         << static_cast<unsigned long>(result) << ").";
    return text.str();
}

bool WideToUtf8(const std::wstring& input, std::string& output) {
    output.clear();
    if (input.empty()) {
        return true;
    }
    if (input.size() > kMaximumWmiTextCharacters) {
        return false;
    }

    const int input_length = static_cast<int>(input.size());
    const int required = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        input.data(),
        input_length,
        nullptr,
        0,
        nullptr,
        nullptr);
    if (required <= 0) {
        return false;
    }

    output.resize(static_cast<std::size_t>(required));
    const int written = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        input.data(),
        input_length,
        output.data(),
        required,
        nullptr,
        nullptr);
    if (written != required) {
        output.clear();
        return false;
    }
    return true;
}

HRESULT ReadTextProperty(
    IWbemClassObject* object,
    const wchar_t* property_name,
    std::wstring& output) {
    output.clear();
    ScopedVariant value;
    const HRESULT read_result =
        object->Get(property_name, 0, value.put(), nullptr, nullptr);
    if (FAILED(read_result)) {
        return read_result;
    }
    if (value.get()->vt == VT_EMPTY || value.get()->vt == VT_NULL) {
        return S_OK;
    }

    if (value.get()->vt != VT_BSTR) {
        return DISP_E_TYPEMISMATCH;
    }

    const BSTR text = value.get()->bstrVal;
    const UINT length = SysStringLen(text);
    if (length > kMaximumWmiTextCharacters) {
        return HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
    }
    if (length != 0) {
        output.assign(text, static_cast<std::size_t>(length));
    }
    return S_OK;
}

bool ReadBooleanProperty(
    IWbemClassObject* object,
    const wchar_t* property_name,
    bool& output) {
    output = false;
    ScopedVariant value;
    if (FAILED(object->Get(property_name, 0, value.put(), nullptr, nullptr)) ||
        value.get()->vt == VT_EMPTY || value.get()->vt == VT_NULL) {
        return false;
    }

    ScopedVariant converted;
    if (FAILED(VariantChangeType(converted.put(), value.get(), 0, VT_BOOL))) {
        return false;
    }
    output = converted.get()->boolVal != VARIANT_FALSE;
    return true;
}

void AddUncovered(
    std::vector<DeviceIdentifier>& identifiers,
    const char* field,
    const std::string& device,
    const char* source,
    const std::string& note,
    const std::string& raw_value = {}) {
    identifiers.push_back(DeviceIdentifier{
        field,
        device,
        raw_value,
        source,
        note,
        false});
}

void AddSerialValue(
    std::vector<DeviceIdentifier>& identifiers,
    const char* field,
    const std::string& device,
    const std::wstring& raw,
    const char* source,
    const char* semantic_note) {
    std::string utf8;
    if (!WideToUtf8(raw, utf8)) {
        AddUncovered(
            identifiers,
            field,
            device,
            source,
            "Uncovered: source text is missing, oversized, or not valid UTF-16.");
        return;
    }
    if (detail::IsPlaceholderSerial(raw)) {
        AddUncovered(
            identifiers,
            field,
            device,
            source,
            "Uncovered: WMI returned an empty or placeholder serial.");
        return;
    }

    identifiers.push_back(DeviceIdentifier{
        field,
        device,
        std::move(utf8),
        source,
        semantic_note,
        true});
}

void AddCpuSerialValue(
    std::vector<DeviceIdentifier>& identifiers,
    const std::string& device,
    const std::wstring& raw) {
    std::string utf8;
    if (!WideToUtf8(raw, utf8)) {
        AddUncovered(
            identifiers,
            "FakeCPUSerial",
            device,
            "WMI:Win32_Processor.SerialNumber",
            "Uncovered: CPU serial text is oversized or not valid UTF-16.");
        return;
    }

    if (detail::ClassifyCpuSerialObservation(raw) ==
        detail::CpuSerialObservation::NotExposedByFirmware) {
        AddUncovered(
            identifiers,
            "FakeCPUSerial",
            device,
            "WMI:Win32_Processor.SerialNumber",
            "Uncovered: not exposed by firmware: Win32_Processor.SerialNumber "
            "(SMBIOS Type 4 processor serial) is empty, NULL, or a placeholder.");
        return;
    }

    identifiers.push_back(DeviceIdentifier{
        "FakeCPUSerial",
        device,
        std::move(utf8),
        "WMI:Win32_Processor.SerialNumber",
        "Confirmed semantic source: Win32_Processor.SerialNumber, which Windows "
        "maps to the SMBIOS Type 4 serial; raw text preserved. ProcessorId is "
        "not used as a serial.",
        true});
}

struct EnumerationResult {
    HRESULT result = S_OK;
    bool complete = false;
    std::size_t rows = 0;
};

template <typename RowHandler>
EnumerationResult EnumerateWmi(
    IWbemServices* services,
    const wchar_t* query_text,
    RowHandler&& on_row) {
    EnumerationResult result;
    ScopedBstr language(L"WQL");
    ScopedBstr query(query_text);
    if (language.get() == nullptr || query.get() == nullptr) {
        result.result = E_OUTOFMEMORY;
        return result;
    }

    ComOwner<IEnumWbemClassObject> enumerator;
    result.result = services->ExecQuery(
        language.get(),
        query.get(),
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
        nullptr,
        enumerator.put());
    if (FAILED(result.result)) {
        return result;
    }

    const auto deadline = std::chrono::steady_clock::now() + kQueryBudget;
    while (result.rows < kMaximumInstancesPerClass) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            result.result = WBEM_S_TIMEDOUT;
            return result;
        }

        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        const auto wait_time = std::min(remaining, kEnumerationPoll);
        const LONG timeout = static_cast<LONG>(wait_time.count());

        IWbemClassObject* raw_instance = nullptr;
        ULONG returned = 0;
        const HRESULT next_result =
            enumerator->Next(timeout, 1, &raw_instance, &returned);
        ComOwner<IWbemClassObject> instance(raw_instance);

        if (next_result == WBEM_S_TIMEDOUT) {
            if (std::chrono::steady_clock::now() >= deadline) {
                result.result = WBEM_S_TIMEDOUT;
                return result;
            }
            continue;
        }
        if (FAILED(next_result)) {
            result.result = next_result;
            return result;
        }
        if (returned == 0) {
            if (next_result == WBEM_S_FALSE) {
                result.complete = true;
                result.result = S_OK;
                return result;
            }
            continue;
        }

        on_row(instance.get(), result.rows);
        ++result.rows;
        if (next_result == WBEM_S_FALSE) {
            result.complete = true;
            result.result = S_OK;
            return result;
        }
    }

    result.result = HRESULT_FROM_WIN32(ERROR_MORE_DATA);
    return result;
}

std::string IndexedDevice(const char* kind, std::size_t index) {
    return std::string(kind) + "#" + std::to_string(index);
}

std::string DeviceLabel(
    const std::wstring& primary,
    const std::wstring& secondary,
    const char* kind,
    std::size_t index) {
    std::string primary_utf8;
    std::string secondary_utf8;
    const bool has_primary =
        !primary.empty() && WideToUtf8(primary, primary_utf8);
    const bool has_secondary =
        !secondary.empty() && WideToUtf8(secondary, secondary_utf8);
    if (has_primary && has_secondary) {
        return primary_utf8 + " [" + secondary_utf8 + "]";
    }
    if (has_primary) {
        return primary_utf8;
    }
    if (has_secondary) {
        return secondary_utf8;
    }
    return IndexedDevice(kind, index);
}

std::string QueryFailureNote(const EnumerationResult& result) {
    if (result.result == WBEM_S_TIMEDOUT) {
        return "Uncovered: WMI enumeration exceeded its 6 second per-class time budget.";
    }
    if (result.result == HRESULT_FROM_WIN32(ERROR_MORE_DATA)) {
        return "Uncovered: WMI enumeration reached the 128 instance safety limit.";
    }
    std::ostringstream text;
    text << "Uncovered: WMI class query or enumeration failed (HRESULT=0x"
         << std::uppercase << std::hex << std::setw(8) << std::setfill('0')
         << static_cast<unsigned long>(result.result) << ").";
    return text.str();
}

void AddEnumerationFailure(
    std::vector<DeviceIdentifier>& identifiers,
    const char* field,
    const char* class_name,
    const EnumerationResult& result) {
    AddUncovered(
        identifiers,
        field,
        "*",
        class_name,
        QueryFailureNote(result));
}

void CollectDisks(
    IWbemServices* services,
    std::vector<DeviceIdentifier>& identifiers) {
    const auto result = EnumerateWmi(
        services,
        L"SELECT SerialNumber, DeviceID, Model FROM Win32_DiskDrive",
        [&identifiers](IWbemClassObject* object, std::size_t index) {
            std::wstring device_id;
            std::wstring model;
            std::wstring serial;
            const HRESULT device_result =
                ReadTextProperty(object, L"DeviceID", device_id);
            const HRESULT model_result =
                ReadTextProperty(object, L"Model", model);
            const HRESULT serial_result =
                ReadTextProperty(object, L"SerialNumber", serial);
            const std::string device =
                DeviceLabel(device_id, model, "disk", index);
            if (FAILED(serial_result)) {
                AddUncovered(
                    identifiers,
                    "FakeDiskSerial",
                    device,
                    "WMI:Win32_DiskDrive.SerialNumber",
                    HresultNote("Read SerialNumber", serial_result));
                return;
            }
            if (FAILED(device_result) || FAILED(model_result)) {
                AddSerialValue(
                    identifiers,
                    "FakeDiskSerial",
                    device,
                    serial,
                    "WMI:Win32_DiskDrive.SerialNumber",
                    "Confirmed semantic source: Win32_DiskDrive.SerialNumber; "
                    "device context was partially unavailable.");
                return;
            }

            AddSerialValue(
                identifiers,
                "FakeDiskSerial",
                device,
                serial,
                "WMI:Win32_DiskDrive.SerialNumber",
                "Confirmed semantic source: Win32_DiskDrive.SerialNumber "
                "(manufacturer identifier for the physical disk; raw text preserved).");
        });
    if (result.rows == 0 && result.complete) {
        AddUncovered(
            identifiers,
            "FakeDiskSerial",
            "*",
            "WMI:Win32_DiskDrive.SerialNumber",
            "Uncovered: WMI returned no physical disk instances.");
    }
    if (!result.complete) {
        AddEnumerationFailure(
            identifiers,
            "FakeDiskSerial",
            "WMI:Win32_DiskDrive",
            result);
    }
}

void CollectAdapters(
    IWbemServices* services,
    std::vector<DeviceIdentifier>& identifiers) {
    std::size_t physical_rows = 0;
    const auto result = EnumerateWmi(
        services,
        L"SELECT MACAddress, PhysicalAdapter, DeviceID, Name, PNPDeviceID "
        L"FROM Win32_NetworkAdapter WHERE PhysicalAdapter = TRUE",
        [&identifiers, &physical_rows](IWbemClassObject* object, std::size_t index) {
            bool physical = false;
            if (!ReadBooleanProperty(object, L"PhysicalAdapter", physical) ||
                !physical) {
                return;
            }
            ++physical_rows;

            std::wstring device_id;
            std::wstring name;
            std::wstring pnp_id;
            std::wstring mac;
            const HRESULT device_result =
                ReadTextProperty(object, L"DeviceID", device_id);
            const HRESULT name_result =
                ReadTextProperty(object, L"Name", name);
            const HRESULT pnp_result =
                ReadTextProperty(object, L"PNPDeviceID", pnp_id);
            const HRESULT mac_result =
                ReadTextProperty(object, L"MACAddress", mac);
            std::wstring adapter_context = name;
            if (!pnp_id.empty()) {
                if (!adapter_context.empty()) {
                    adapter_context += L" | ";
                }
                adapter_context += pnp_id;
            }
            const std::string device =
                DeviceLabel(device_id, adapter_context, "network-adapter", index);
            if (FAILED(mac_result)) {
                AddUncovered(
                    identifiers,
                    "FakeMacAddress",
                    device,
                    "WMI:Win32_NetworkAdapter.MACAddress",
                    HresultNote("Read MACAddress", mac_result));
                return;
            }

            std::string utf8_mac;
            if (!WideToUtf8(mac, utf8_mac)) {
                AddUncovered(
                    identifiers,
                    "FakeMacAddress",
                    device,
                    "WMI:Win32_NetworkAdapter.MACAddress",
                    "Uncovered: MAC text is oversized or not valid UTF-16.");
                return;
            }
            if (mac.empty() || detail::IsPlaceholderSerial(mac)) {
                AddUncovered(
                    identifiers,
                    "FakeMacAddress",
                    device,
                    "WMI:Win32_NetworkAdapter.MACAddress",
                    "Uncovered: WMI returned an empty or placeholder MAC value.");
                return;
            }
            if (!detail::IsValidMacAddress(mac)) {
                AddUncovered(
                    identifiers,
                    "FakeMacAddress",
                    device,
                    "WMI:Win32_NetworkAdapter.MACAddress",
                    "Uncovered: invalid or reserved MAC value; raw WMI text is retained.",
                    utf8_mac);
                return;
            }

            std::string note =
                "Confirmed semantic source: physical Win32_NetworkAdapter.MACAddress; "
                "this is the OS-observed address and is not authenticated as the "
                "factory or permanent address.";
            if (FAILED(device_result) || FAILED(name_result) || FAILED(pnp_result)) {
                note += " Some adapter context properties were unavailable.";
            }
            identifiers.push_back(DeviceIdentifier{
                "FakeMacAddress",
                device,
                std::move(utf8_mac),
                "WMI:Win32_NetworkAdapter.MACAddress",
                std::move(note),
                true});
        });
    if (physical_rows == 0 && result.complete) {
        AddUncovered(
            identifiers,
            "FakeMacAddress",
            "*",
            "WMI:Win32_NetworkAdapter.MACAddress",
            "Uncovered: WMI returned no physical network adapter instances.");
    }
    if (!result.complete) {
        AddEnumerationFailure(
            identifiers,
            "FakeMacAddress",
            "WMI:Win32_NetworkAdapter",
            result);
    }
}

void CollectProcessors(
    IWbemServices* services,
    std::vector<DeviceIdentifier>& identifiers) {
    const auto result = EnumerateWmi(
        services,
        L"SELECT SerialNumber, DeviceID, Name FROM Win32_Processor",
        [&identifiers](IWbemClassObject* object, std::size_t index) {
            std::wstring device_id;
            std::wstring name;
            std::wstring serial;
            const HRESULT device_result =
                ReadTextProperty(object, L"DeviceID", device_id);
            const HRESULT name_result =
                ReadTextProperty(object, L"Name", name);
            const HRESULT serial_result =
                ReadTextProperty(object, L"SerialNumber", serial);
            const std::string device =
                DeviceLabel(device_id, name, "processor", index);
            if (FAILED(serial_result)) {
                AddUncovered(
                    identifiers,
                    "FakeCPUSerial",
                    device,
                    "WMI:Win32_Processor.SerialNumber",
                    HresultNote("Read SerialNumber", serial_result));
                return;
            }

            AddCpuSerialValue(identifiers, device, serial);
            if (FAILED(device_result) || FAILED(name_result)) {
                identifiers.back().note +=
                    " Some processor context properties were unavailable.";
            }
        });
    if (result.rows == 0 && result.complete) {
        AddUncovered(
            identifiers,
            "FakeCPUSerial",
            "*",
            "WMI:Win32_Processor.SerialNumber",
            "Uncovered: WMI returned no processor instances.");
    }
    if (!result.complete) {
        AddEnumerationFailure(
            identifiers,
            "FakeCPUSerial",
            "WMI:Win32_Processor",
            result);
    }
}

void CollectVideoControllers(
    IWbemServices* services,
    std::vector<DeviceIdentifier>& identifiers) {
    const auto result = EnumerateWmi(
        services,
        L"SELECT Name, PNPDeviceID FROM Win32_VideoController",
        [&identifiers](IWbemClassObject* object, std::size_t index) {
            std::wstring name;
            std::wstring pnp_id;
            const HRESULT name_result =
                ReadTextProperty(object, L"Name", name);
            const HRESULT pnp_result =
                ReadTextProperty(object, L"PNPDeviceID", pnp_id);
            const std::string device =
                DeviceLabel(pnp_id, name, "video-controller", index);
            std::string note =
                "Uncovered: not obtainable at runtime by this Windows-only "
                "collector. PCIe DSN configuration-space access requires a "
                "supported kernel-driver interface, which this collector does "
                "not use. Standard Win32_VideoController exposes Name and "
                "PNPDeviceID context only; neither is substituted for a serial.";
            if (FAILED(name_result) || FAILED(pnp_result)) {
                note += " Some controller context properties were unavailable.";
            }
            AddUncovered(
                identifiers,
                "FakeGPUSerial",
                device,
                "WMI:Win32_VideoController(Name,PNPDeviceID)",
                note);
        });
    if (result.rows == 0 && result.complete) {
        AddUncovered(
            identifiers,
            "FakeGPUSerial",
            "*",
            "WMI:Win32_VideoController",
            "Uncovered: WMI returned no video-controller instances.");
    }
    if (!result.complete) {
        AddEnumerationFailure(
            identifiers,
            "FakeGPUSerial",
            "WMI:Win32_VideoController",
            result);
    }
}

}  // namespace

namespace detail {
namespace {

wchar_t AsciiLower(wchar_t value) noexcept {
    if (value >= L'A' && value <= L'Z') {
        return static_cast<wchar_t>(value - L'A' + L'a');
    }
    return value;
}

bool EqualsAsciiInsensitive(
    const std::wstring& left,
    const wchar_t* right) noexcept {
    std::size_t right_length = 0;
    while (right[right_length] != L'\0') {
        ++right_length;
    }
    if (left.size() != right_length) {
        return false;
    }
    for (std::size_t index = 0; index < right_length; ++index) {
        if (AsciiLower(left[index]) != AsciiLower(right[index])) {
            return false;
        }
    }
    return true;
}

bool IsAsciiAlphanumeric(wchar_t value) noexcept {
    return (value >= L'A' && value <= L'Z') ||
           (value >= L'a' && value <= L'z') ||
           (value >= L'0' && value <= L'9');
}

std::wstring NormalizeToken(const std::wstring& value) {
    std::wstring normalized;
    normalized.reserve(value.size());
    for (const wchar_t character : value) {
        if (IsAsciiAlphanumeric(character)) {
            normalized.push_back(AsciiLower(character));
        }
    }
    return normalized;
}

bool IsHex(wchar_t value) noexcept {
    const wchar_t lowered = AsciiLower(value);
    return (value >= L'0' && value <= L'9') ||
           (lowered >= L'a' && lowered <= L'f');
}

}  // namespace

bool IsPlaceholderSerial(const std::wstring& value) {
    const std::wstring normalized = NormalizeToken(value);
    if (normalized.empty()) {
        return std::all_of(
            value.begin(),
            value.end(),
            [](wchar_t character) {
                return character == L' ' || character == L'\t' ||
                       character == L'\r' || character == L'\n';
            });
    }

    constexpr std::array<const wchar_t*, 14> placeholders = {
        L"notprovided",
        L"notspecified",
        L"defaultstring",
        L"tobefilledbyoem",
        L"oemfill",
        L"systemserialnumber",
        L"unknown",
        L"none",
        L"null",
        L"na",
        L"notavailable",
        L"notapplicable",
        L"invalid",
        L"notset"};
    for (const wchar_t* placeholder : placeholders) {
        if (normalized == placeholder) {
            return true;
        }
    }

    return std::all_of(
        normalized.begin(),
        normalized.end(),
        [](wchar_t character) { return character == L'0'; });
}

CpuSerialObservation ClassifyCpuSerialObservation(const std::wstring& value) {
    return IsPlaceholderSerial(value)
               ? CpuSerialObservation::NotExposedByFirmware
               : CpuSerialObservation::Present;
}

bool IsValidMacAddress(const std::wstring& value) {
    std::size_t first = 0;
    std::size_t last = value.size();
    while (first < last && (value[first] == L' ' || value[first] == L'\t')) {
        ++first;
    }
    while (last > first && (value[last - 1] == L' ' || value[last - 1] == L'\t')) {
        --last;
    }
    const std::wstring_view mac(value.data() + first, last - first);

    std::wstring digits;
    digits.reserve(12);
    if (mac.size() == 12) {
        for (const wchar_t character : mac) {
            if (!IsHex(character)) {
                return false;
            }
            digits.push_back(AsciiLower(character));
        }
    } else if (mac.size() == 17) {
        const wchar_t separator = mac[2];
        if (separator != L':' && separator != L'-') {
            return false;
        }
        for (std::size_t index = 0; index < mac.size(); ++index) {
            if (index % 3 == 2) {
                if (mac[index] != separator) {
                    return false;
                }
            } else {
                if (!IsHex(mac[index])) {
                    return false;
                }
                digits.push_back(AsciiLower(mac[index]));
            }
        }
    } else {
        return false;
    }

    const bool all_zero = std::all_of(
        digits.begin(), digits.end(),
        [](wchar_t character) { return character == L'0'; });
    const bool all_ff = std::all_of(
        digits.begin(), digits.end(),
        [](wchar_t character) { return character == L'f'; });
    return !all_zero && !all_ff;
}

std::string MapWmiPropertyToField(
    const std::wstring& class_name,
    const std::wstring& property_name) {
    if (EqualsAsciiInsensitive(class_name, L"Win32_DiskDrive") &&
        EqualsAsciiInsensitive(property_name, L"SerialNumber")) {
        return "FakeDiskSerial";
    }
    if (EqualsAsciiInsensitive(class_name, L"Win32_NetworkAdapter") &&
        EqualsAsciiInsensitive(property_name, L"MACAddress")) {
        return "FakeMacAddress";
    }
    if (EqualsAsciiInsensitive(class_name, L"Win32_Processor") &&
        EqualsAsciiInsensitive(property_name, L"SerialNumber")) {
        return "FakeCPUSerial";
    }
    return {};
}

}  // namespace detail

bool CollectDeviceIdentifiers(
    std::vector<DeviceIdentifier>& identifiers,
    std::wstring& error) {
    identifiers.clear();
    error.clear();

    ScopedComInitialization com;
    if (FAILED(com.result())) {
        error = HresultText(L"CoInitializeEx", com.result());
        return false;
    }

    const HRESULT security_result = CoInitializeSecurity(
        nullptr,
        -1,
        nullptr,
        nullptr,
        RPC_C_AUTHN_LEVEL_DEFAULT,
        RPC_C_IMP_LEVEL_IMPERSONATE,
        nullptr,
        EOAC_NONE,
        nullptr);
    if (FAILED(security_result) && security_result != RPC_E_TOO_LATE) {
        error = HresultText(L"CoInitializeSecurity", security_result);
        return false;
    }

    ComOwner<IWbemLocator> locator;
    const HRESULT locator_result = CoCreateInstance(
        CLSID_WbemLocator,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_IWbemLocator,
        reinterpret_cast<void**>(locator.put()));
    if (FAILED(locator_result)) {
        error = HresultText(L"CoCreateInstance(IWbemLocator)", locator_result);
        return false;
    }

    ScopedBstr namespace_name(L"ROOT\\CIMV2");
    if (namespace_name.get() == nullptr) {
        error = L"Unable to allocate the WMI namespace string.";
        return false;
    }

    ComOwner<IWbemServices> services;
    const HRESULT connect_result = locator->ConnectServer(
        namespace_name.get(),
        nullptr,
        nullptr,
        nullptr,
        0,
        nullptr,
        nullptr,
        services.put());
    if (FAILED(connect_result)) {
        error = HresultText(L"IWbemLocator::ConnectServer(ROOT\\CIMV2)", connect_result);
        return false;
    }

    const HRESULT blanket_result = CoSetProxyBlanket(
        services.get(),
        RPC_C_AUTHN_WINNT,
        RPC_C_AUTHZ_NONE,
        nullptr,
        RPC_C_AUTHN_LEVEL_CALL,
        RPC_C_IMP_LEVEL_IMPERSONATE,
        nullptr,
        EOAC_NONE);
    if (FAILED(blanket_result)) {
        error = HresultText(L"CoSetProxyBlanket(IWbemServices)", blanket_result);
        return false;
    }

    CollectDisks(services.get(), identifiers);
    CollectAdapters(services.get(), identifiers);
    CollectProcessors(services.get(), identifiers);
    CollectVideoControllers(services.get(), identifiers);
    return true;
}

}  // namespace hwid_spoofer

