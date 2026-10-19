#include "client_reads.h"

#include <Windows.h>
#include <Wbemidl.h>
#include <OleAuto.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iterator>
#include <set>
#include <sstream>
#include <string>
#include <utility>

namespace hwid_spoofer::client {
namespace {

constexpr std::size_t kMaximumRawSmbiosBytes = 16u * 1024u * 1024u;
constexpr std::size_t kMaximumTextCharacters = 4096u;
constexpr std::size_t kMaximumWmiRows = 128u;
constexpr auto kWmiQueryBudget = std::chrono::seconds(6);
constexpr auto kWmiPollInterval = std::chrono::milliseconds(250);
// GetSystemFirmwareTable specifies the provider as the numeric 'RSMB' signature.
constexpr DWORD kRawSmbiosProvider = 0x52534D42u;

constexpr DWORD kSmbiosFields =
    HWID_FIELD_MASK(HwidFieldSystemSerial) |
    HWID_FIELD_MASK(HwidFieldSystemManufacturer) |
    HWID_FIELD_MASK(HwidFieldSystemProduct) |
    HWID_FIELD_MASK(HwidFieldBoardSerial) |
    HWID_FIELD_MASK(HwidFieldBoardManufacturer) |
    HWID_FIELD_MASK(HwidFieldBoardProduct) |
    HWID_FIELD_MASK(HwidFieldSystemUuid);

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
    T* operator->() const noexcept {
        return value_;
    }
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
    VARIANT* get() noexcept {
        return &value_;
    }
    const VARIANT* get() const noexcept {
        return &value_;
    }

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
    HRESULT result() const noexcept {
        return result_;
    }
    bool usable() const noexcept {
        return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE;
    }

private:
    HRESULT result_;
    bool must_uninitialize_;
};

std::wstring FormatHresult(const wchar_t* operation, HRESULT result) {
    std::wostringstream text;
    text << operation << L" failed (HRESULT=0x"
         << std::uppercase << std::hex << std::setw(8) << std::setfill(L'0')
         << static_cast<unsigned long>(result) << L").";
    return text.str();
}

std::wstring FormatWin32Error(const wchar_t* operation, DWORD result) {
    std::wostringstream text;
    text << operation << L" failed (Win32=0x"
         << std::uppercase << std::hex << std::setw(8) << std::setfill(L'0')
         << result << L").";
    return text.str();
}

bool IsValidWideText(const std::wstring& value) {
    if (value.size() > kMaximumTextCharacters ||
        value.find(L'\0') != std::wstring::npos) {
        return false;
    }
    for (std::size_t index = 0; index < value.size(); ++index) {
        const wchar_t code = value[index];
        if (code >= 0xD800 && code <= 0xDBFF) {
            if (index + 1u >= value.size() ||
                value[index + 1u] < 0xDC00 || value[index + 1u] > 0xDFFF) {
                return false;
            }
            ++index;
        } else if (code >= 0xDC00 && code <= 0xDFFF) {
            return false;
        }
    }
    return true;
}

const wchar_t* FieldName(HWID_SPOOF_FIELD field) {
    switch (field) {
    case HwidFieldDiskSerial: return L"FakeDiskSerial";
    case HwidFieldMacAddress: return L"FakeMacAddress";
    case HwidFieldSystemSerial: return L"FakeSMBIOSSerial";
    case HwidFieldSystemManufacturer: return L"FakeSMBIOSManufacturer";
    case HwidFieldSystemProduct: return L"FakeSMBIOSProduct";
    case HwidFieldBoardSerial: return L"FakeMotherboardSerial";
    case HwidFieldBoardManufacturer: return L"FakeMotherboardManufacturer";
    case HwidFieldBoardProduct: return L"FakeMotherboardProduct";
    case HwidFieldSystemUuid: return L"FakeSMBIOSUUID";
    default: return L"unknown field";
    }
}

bool ValidateFieldMask(DWORD field_mask, std::wstring& error) {
    if (field_mask == 0 || (field_mask & ~HWID_ALL_CONFIRMED_FIELDS) != 0) {
        error = L"Field mask must select at least one confirmed identifier and contain no unknown fields.";
        return false;
    }
    return true;
}

bool HasDuplicateKey(const std::vector<Identifier>& values) {
    std::set<std::pair<unsigned, std::wstring>> keys;
    for (const Identifier& value : values) {
        if (value.device.empty() || !IsValidWideText(value.device) ||
            !IsValidWideText(value.value)) {
            return true;
        }
        const auto key = std::make_pair(
            static_cast<unsigned>(value.field), value.device);
        if (!keys.insert(key).second) {
            return true;
        }
    }
    return false;
}

std::uint32_t ReadLittleEndian32(const std::uint8_t* value) {
    return static_cast<std::uint32_t>(value[0]) |
           (static_cast<std::uint32_t>(value[1]) << 8u) |
           (static_cast<std::uint32_t>(value[2]) << 16u) |
           (static_cast<std::uint32_t>(value[3]) << 24u);
}

bool DecodeSmbiosString(
    const std::vector<std::string>& strings,
    std::uint8_t index,
    const wchar_t* field,
    std::wstring& output,
    std::wstring& error) {
    output.clear();
    if (index == 0) {
        error = std::wstring(L"SMBIOS ") + field +
                L" has no referenced string value.";
        return false;
    }
    if (static_cast<std::size_t>(index) > strings.size()) {
        error = std::wstring(L"SMBIOS ") + field +
                L" string index is outside the structure string set.";
        return false;
    }
    const std::string& raw = strings[static_cast<std::size_t>(index) - 1u];
    if (raw.empty() || raw.size() > kMaximumTextCharacters) {
        error = std::wstring(L"SMBIOS ") + field +
                L" string is empty or exceeds the text limit.";
        return false;
    }
    output.reserve(raw.size());
    for (const unsigned char byte : raw) {
        if (byte < 0x20u || byte == 0x7Fu) {
            error = std::wstring(L"SMBIOS ") + field +
                    L" contains a control byte.";
            output.clear();
            return false;
        }
        output.push_back(static_cast<wchar_t>(byte));
    }
    return true;
}

bool FormatSmbiosUuid(
    const std::uint8_t* raw,
    std::uint8_t major,
    std::uint8_t minor,
    std::wstring& output) {
    std::array<std::uint8_t, 16> bytes{};
    const bool mixed_endian = major > 2 || (major == 2 && minor >= 6);
    if (mixed_endian) {
        bytes = {raw[3], raw[2], raw[1], raw[0], raw[5], raw[4],
                 raw[7], raw[6], raw[8], raw[9], raw[10], raw[11],
                 raw[12], raw[13], raw[14], raw[15]};
    } else {
        std::copy(raw, raw + bytes.size(), bytes.begin());
    }
    const bool all_zero = std::all_of(bytes.begin(), bytes.end(),
        [](std::uint8_t byte) { return byte == 0; });
    const bool all_ones = std::all_of(bytes.begin(), bytes.end(),
        [](std::uint8_t byte) { return byte == 0xFFu; });
    if (all_zero || all_ones) {
        return false;
    }

    static constexpr wchar_t kHex[] = L"0123456789ABCDEF";
    output.clear();
    output.reserve(36u);
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        if (index == 4u || index == 6u || index == 8u || index == 10u) {
            output.push_back(L'-');
        }
        output.push_back(kHex[(bytes[index] >> 4u) & 0x0Fu]);
        output.push_back(kHex[bytes[index] & 0x0Fu]);
    }
    return true;
}

bool ReadTextProperty(
    IWbemClassObject* object,
    const wchar_t* property_name,
    std::wstring& output,
    std::wstring& error) {
    output.clear();
    ScopedVariant value;
    const HRESULT read_result =
        object->Get(property_name, 0, value.put(), nullptr, nullptr);
    if (FAILED(read_result)) {
        error = FormatHresult(property_name, read_result);
        return false;
    }
    if (value.get()->vt == VT_EMPTY || value.get()->vt == VT_NULL) {
        return true;
    }
    if (value.get()->vt != VT_BSTR) {
        error = std::wstring(property_name) + L" returned a non-text WMI value.";
        return false;
    }
    const BSTR text = value.get()->bstrVal;
    const UINT length = SysStringLen(text);
    if (length > kMaximumTextCharacters) {
        error = std::wstring(property_name) + L" exceeded the 4096-character limit.";
        return false;
    }
    if (length != 0u) {
        output.assign(text, static_cast<std::size_t>(length));
    }
    if (!IsValidWideText(output)) {
        error = std::wstring(property_name) + L" returned invalid UTF-16 text.";
        output.clear();
        return false;
    }
    return true;
}

bool ReadPhysicalProperty(
    IWbemClassObject* object,
    bool& output,
    std::wstring& error) {
    ScopedVariant value;
    const HRESULT read_result =
        object->Get(L"PhysicalAdapter", 0, value.put(), nullptr, nullptr);
    if (FAILED(read_result)) {
        error = FormatHresult(L"PhysicalAdapter", read_result);
        return false;
    }
    if (value.get()->vt == VT_EMPTY || value.get()->vt == VT_NULL) {
        error = L"PhysicalAdapter returned no WMI value.";
        return false;
    }
    ScopedVariant converted;
    const HRESULT convert_result =
        VariantChangeType(converted.put(), value.get(), 0, VT_BOOL);
    if (FAILED(convert_result)) {
        error = FormatHresult(L"PhysicalAdapter conversion", convert_result);
        return false;
    }
    output = converted.get()->boolVal != VARIANT_FALSE;
    return true;
}

HRESULT InitializeWmi(
    ScopedComInitialization& com,
    ComOwner<IWbemServices>& services,
    std::wstring& error) {
    if (!com.usable()) {
        error = FormatHresult(L"CoInitializeEx", com.result());
        return com.result();
    }

    const HRESULT security = CoInitializeSecurity(
        nullptr,
        -1,
        nullptr,
        nullptr,
        RPC_C_AUTHN_LEVEL_DEFAULT,
        RPC_C_IMP_LEVEL_IMPERSONATE,
        nullptr,
        EOAC_NONE,
        nullptr);
    if (FAILED(security) && security != RPC_E_TOO_LATE) {
        error = FormatHresult(L"CoInitializeSecurity", security);
        return security;
    }

    ComOwner<IWbemLocator> locator;
    HRESULT result = CoCreateInstance(
        CLSID_WbemLocator,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_IWbemLocator,
        reinterpret_cast<void**>(locator.put()));
    if (FAILED(result)) {
        error = FormatHresult(L"Create WMI locator", result);
        return result;
    }

    ScopedBstr name_space(L"ROOT\\CIMV2");
    if (name_space.get() == nullptr) {
        error = L"Allocate WMI namespace failed.";
        return E_OUTOFMEMORY;
    }
    result = locator->ConnectServer(
        name_space.get(),
        nullptr,
        nullptr,
        nullptr,
        0,
        nullptr,
        nullptr,
        services.put());
    if (FAILED(result)) {
        error = FormatHresult(L"Connect to ROOT/CIMV2", result);
        return result;
    }

    result = CoSetProxyBlanket(
        services.get(),
        RPC_C_AUTHN_WINNT,
        RPC_C_AUTHZ_NONE,
        nullptr,
        RPC_C_AUTHN_LEVEL_CALL,
        RPC_C_IMP_LEVEL_IMPERSONATE,
        nullptr,
        EOAC_NONE);
    if (FAILED(result)) {
        error = FormatHresult(L"Set WMI proxy security", result);
        services.reset();
        return result;
    }
    return S_OK;
}

struct EnumerationResult {
    HRESULT status = S_OK;
    bool complete = false;
    std::size_t rows = 0;
};

template <typename RowHandler>
EnumerationResult EnumerateQuery(
    IWbemServices* services,
    const wchar_t* query_text,
    RowHandler&& on_row,
    std::wstring& error) {
    EnumerationResult result;
    const auto deadline = std::chrono::steady_clock::now() + kWmiQueryBudget;
    ScopedBstr language(L"WQL");
    ScopedBstr query(query_text);
    if (language.get() == nullptr || query.get() == nullptr) {
        result.status = E_OUTOFMEMORY;
        error = L"Allocate WMI query text failed.";
        return result;
    }

    ComOwner<IEnumWbemClassObject> enumerator;
    result.status = services->ExecQuery(
        language.get(),
        query.get(),
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
        nullptr,
        enumerator.put());
    if (FAILED(result.status)) {
        error = FormatHresult(L"Execute WMI class query", result.status);
        return result;
    }
    if (std::chrono::steady_clock::now() >= deadline) {
        result.status = WBEM_S_TIMEDOUT;
        error = L"WMI class query exceeded its 6-second time budget.";
        return result;
    }

    for (;;) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            result.status = WBEM_S_TIMEDOUT;
            error = L"WMI class enumeration exceeded its 6-second time budget.";
            return result;
        }
        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        const auto wait = std::min(remaining, kWmiPollInterval);
        IWbemClassObject* raw_instance = nullptr;
        ULONG returned = 0;
        const HRESULT next_result = enumerator->Next(
            static_cast<LONG>(wait.count()), 1, &raw_instance, &returned);
        ComOwner<IWbemClassObject> instance(raw_instance);

        if (next_result == WBEM_S_TIMEDOUT) {
            continue;
        }
        if (FAILED(next_result)) {
            result.status = next_result;
            error = FormatHresult(L"Read WMI query result", next_result);
            return result;
        }
        if (returned == 0u) {
            if (next_result == WBEM_S_FALSE) {
                result.complete = true;
                result.status = S_OK;
                return result;
            }
            continue;
        }
        if (result.rows == kMaximumWmiRows) {
            result.status = HRESULT_FROM_WIN32(ERROR_MORE_DATA);
            error = L"WMI class enumeration exceeded the 128-instance limit.";
            return result;
        }
        if (!on_row(instance.get(), result.rows, error)) {
            result.status = E_INVALIDARG;
            return result;
        }
        ++result.rows;
        if (next_result == WBEM_S_FALSE) {
            result.complete = true;
            result.status = S_OK;
            return result;
        }
    }
}

bool CollectDisks(
    IWbemServices* services,
    std::vector<Identifier>& values,
    std::wstring& error) {
    const EnumerationResult result = EnumerateQuery(
        services,
        L"SELECT SerialNumber, DeviceID FROM Win32_DiskDrive",
        [&values](IWbemClassObject* object, std::size_t, std::wstring& row_error) {
            std::wstring device_id;
            std::wstring serial;
            if (!ReadTextProperty(object, L"DeviceID", device_id, row_error) ||
                !ReadTextProperty(object, L"SerialNumber", serial, row_error)) {
                row_error = L"Win32_DiskDrive: " + row_error;
                return false;
            }
            if (device_id.empty()) {
                row_error = L"Win32_DiskDrive returned a missing DeviceID identity.";
                return false;
            }
            values.push_back(Identifier{
                HwidFieldDiskSerial,
                L"disk:" + device_id,
                std::move(serial)});
            return true;
        },
        error);
    if (!result.complete) {
        if (error.empty()) {
            error = L"Win32_DiskDrive enumeration was incomplete.";
        }
        return false;
    }
    if (result.rows == 0u) {
        error = L"Win32_DiskDrive returned no disk identities.";
        return false;
    }
    return true;
}

bool CollectAdapters(
    IWbemServices* services,
    std::vector<Identifier>& values,
    std::wstring& error) {
    std::size_t physical_rows = 0u;
    const EnumerationResult result = EnumerateQuery(
        services,
        L"SELECT MACAddress, PhysicalAdapter, DeviceID, PNPDeviceID "
        L"FROM Win32_NetworkAdapter WHERE PhysicalAdapter = TRUE",
        [&values, &physical_rows](IWbemClassObject* object, std::size_t, std::wstring& row_error) {
            bool physical = false;
            if (!ReadPhysicalProperty(object, physical, row_error)) {
                row_error = L"Win32_NetworkAdapter: " + row_error;
                return false;
            }
            if (!physical) {
                row_error = L"Win32_NetworkAdapter returned a nonphysical row for the physical-adapter query.";
                return false;
            }
            ++physical_rows;

            std::wstring device_id;
            std::wstring pnp_id;
            std::wstring mac;
            if (!ReadTextProperty(object, L"DeviceID", device_id, row_error) ||
                !ReadTextProperty(object, L"PNPDeviceID", pnp_id, row_error) ||
                !ReadTextProperty(object, L"MACAddress", mac, row_error)) {
                row_error = L"Win32_NetworkAdapter: " + row_error;
                return false;
            }
            if (device_id.empty() || pnp_id.empty()) {
                row_error = L"Win32_NetworkAdapter returned a missing device identity.";
                return false;
            }
            values.push_back(Identifier{
                HwidFieldMacAddress,
                L"adapter:" + device_id + L"|" + pnp_id,
                std::move(mac)});
            return true;
        },
        error);
    if (!result.complete) {
        if (error.empty()) {
            error = L"Win32_NetworkAdapter enumeration was incomplete.";
        }
        return false;
    }
    if (result.rows == 0u || physical_rows == 0u) {
        error = L"Win32_NetworkAdapter returned no physical adapter identities.";
        return false;
    }
    return true;
}

void FilterFields(DWORD field_mask, std::vector<Identifier>& values) {
    values.erase(std::remove_if(values.begin(), values.end(),
        [field_mask](const Identifier& value) {
            return (field_mask & HWID_FIELD_MASK(value.field)) == 0;
        }), values.end());
}

std::wstring FormatStatus(NTSTATUS status) {
    std::wostringstream text;
    text << L"0x" << std::uppercase << std::hex << std::setw(8)
         << std::setfill(L'0') << static_cast<std::uint32_t>(status);
    return text.str();
}

}  // namespace

bool detail::ParseConfirmedSmbios(
    const std::vector<std::uint8_t>& bytes,
    std::vector<Identifier>& values,
    std::wstring& error) {
    values.clear();
    error.clear();
    constexpr std::size_t kRawHeaderSize = 8u;
    constexpr std::size_t kStructureHeaderSize = 4u;
    if (bytes.size() < kRawHeaderSize || bytes.size() > kMaximumRawSmbiosBytes) {
        error = L"Raw SMBIOS result is outside the supported size bounds.";
        return false;
    }
    const std::uint32_t table_length = ReadLittleEndian32(bytes.data() + 4u);
    if (table_length == 0u ||
        table_length != bytes.size() - kRawHeaderSize ||
        table_length > kMaximumRawSmbiosBytes - kRawHeaderSize) {
        error = L"Raw SMBIOS table length does not match the returned buffer.";
        return false;
    }

    const std::uint8_t major = bytes[1u];
    const std::uint8_t minor = bytes[2u];
    const std::size_t table_end = bytes.size();
    std::size_t offset = kRawHeaderSize;
    std::size_t type1_count = 0u;
    std::size_t type2_count = 0u;
    bool found_end = false;
    std::vector<Identifier> parsed;

    while (offset < table_end) {
        if (table_end - offset < kStructureHeaderSize) {
            error = L"Raw SMBIOS structure header is truncated.";
            return false;
        }
        const std::uint8_t type = bytes[offset];
        const std::uint8_t formatted_length = bytes[offset + 1u];
        if (formatted_length < kStructureHeaderSize ||
            formatted_length > table_end - offset) {
            error = L"Raw SMBIOS formatted structure length is invalid.";
            return false;
        }
        const std::size_t strings_start = offset + formatted_length;
        std::size_t cursor = strings_start;
        std::vector<std::string> strings;
        bool found_string_terminator = false;
        while (cursor < table_end) {
            if (bytes[cursor] == 0u) {
                if (cursor == strings_start && cursor + 1u < table_end &&
                    bytes[cursor + 1u] == 0u) {
                    found_string_terminator = true;
                    cursor += 2u;
                    break;
                }
                error = L"Raw SMBIOS structure contains an empty string.";
                return false;
            }
            const std::size_t string_start = cursor;
            while (cursor < table_end && bytes[cursor] != 0u) {
                ++cursor;
            }
            if (cursor >= table_end) {
                break;
            }
            if (cursor == string_start ||
                cursor - string_start > kMaximumTextCharacters) {
                error = L"Raw SMBIOS string is empty or exceeds the text limit.";
                return false;
            }
            strings.emplace_back(
                reinterpret_cast<const char*>(bytes.data() + string_start),
                cursor - string_start);
            if (cursor + 1u < table_end && bytes[cursor + 1u] == 0u) {
                found_string_terminator = true;
                cursor += 2u;
                break;
            }
            ++cursor;
        }
        if (!found_string_terminator) {
            error = L"Raw SMBIOS structure string set is truncated or lacks its double NUL terminator.";
            return false;
        }

        if (type == 1u) {
            ++type1_count;
            if (type1_count != 1u) {
                error = L"Raw SMBIOS contains duplicate Type 1 system structures.";
                return false;
            }
            if (formatted_length < 24u) {
                error = L"SMBIOS Type 1 is too short to contain the confirmed fields and UUID.";
                return false;
            }
            std::wstring manufacturer;
            std::wstring product;
            std::wstring serial;
            std::wstring uuid;
            const std::uint8_t* formatted = bytes.data() + offset;
            if (!DecodeSmbiosString(strings, formatted[4u], L"Type 1 manufacturer", manufacturer, error) ||
                !DecodeSmbiosString(strings, formatted[5u], L"Type 1 product", product, error) ||
                !DecodeSmbiosString(strings, formatted[7u], L"Type 1 serial", serial, error)) {
                return false;
            }
            if (!FormatSmbiosUuid(formatted + 8u, major, minor, uuid)) {
                error = L"SMBIOS Type 1 contains an unavailable UUID value.";
                return false;
            }
            parsed.push_back({HwidFieldSystemSerial, L"SMBIOS Type 1", std::move(serial)});
            parsed.push_back({HwidFieldSystemManufacturer, L"SMBIOS Type 1", std::move(manufacturer)});
            parsed.push_back({HwidFieldSystemProduct, L"SMBIOS Type 1", std::move(product)});
            parsed.push_back({HwidFieldSystemUuid, L"SMBIOS Type 1", std::move(uuid)});
        } else if (type == 2u) {
            ++type2_count;
            if (type2_count != 1u) {
                error = L"Raw SMBIOS contains duplicate Type 2 baseboard structures.";
                return false;
            }
            if (formatted_length < 8u) {
                error = L"SMBIOS Type 2 is too short to contain the confirmed fields.";
                return false;
            }
            const std::uint8_t* formatted = bytes.data() + offset;
            std::wstring manufacturer;
            std::wstring product;
            std::wstring serial;
            if (!DecodeSmbiosString(strings, formatted[4u], L"Type 2 manufacturer", manufacturer, error) ||
                !DecodeSmbiosString(strings, formatted[5u], L"Type 2 product", product, error) ||
                !DecodeSmbiosString(strings, formatted[7u], L"Type 2 serial", serial, error)) {
                return false;
            }
            parsed.push_back({HwidFieldBoardSerial, L"SMBIOS Type 2", std::move(serial)});
            parsed.push_back({HwidFieldBoardManufacturer, L"SMBIOS Type 2", std::move(manufacturer)});
            parsed.push_back({HwidFieldBoardProduct, L"SMBIOS Type 2", std::move(product)});
        } else if (type == 127u) {
            if (formatted_length != kStructureHeaderSize || !strings.empty()) {
                error = L"SMBIOS Type 127 has an invalid end-of-table structure.";
                return false;
            }
            if (cursor != table_end) {
                error = L"Raw SMBIOS contains data after its Type 127 terminator.";
                return false;
            }
            found_end = true;
            offset = cursor;
            break;
        }
        offset = cursor;
    }

    if (!found_end) {
        error = L"Raw SMBIOS table has no Type 127 end-of-table structure.";
        return false;
    }
    if (type1_count != 1u || type2_count != 1u) {
        error = L"Raw SMBIOS must contain exactly one Type 1 and one Type 2 structure.";
        return false;
    }
    if (HasDuplicateKey(parsed)) {
        error = L"Raw SMBIOS produced a missing or duplicate confirmed field key.";
        return false;
    }
    values = std::move(parsed);
    return true;
}

bool ReadNativeIdentifiers(
    std::vector<Identifier>& values,
    std::wstring& error,
    DWORD field_mask) {
    values.clear();
    error.clear();
    if (!ValidateFieldMask(field_mask, error)) {
        return false;
    }

    if ((field_mask & kSmbiosFields) != 0u) {
        const DWORD required = GetSystemFirmwareTable(
            kRawSmbiosProvider,
            0u,
            nullptr,
            0u);
        if (required == 0u) {
            error = FormatWin32Error(L"Read Windows RSMB table size", GetLastError());
            return false;
        }
        if (required > kMaximumRawSmbiosBytes) {
            error = L"Windows RSMB table exceeds the 16 MiB size limit.";
            return false;
        }
        std::vector<std::uint8_t> raw(required);
        const DWORD received = GetSystemFirmwareTable(
            kRawSmbiosProvider,
            0u,
            raw.data(),
            required);
        if (received == 0u) {
            error = FormatWin32Error(L"Read Windows RSMB table", GetLastError());
            return false;
        }
        if (received != required) {
            error = L"Windows RSMB table size changed during the read; no partial result was retained.";
            return false;
        }
        std::vector<Identifier> smbios;
        if (!detail::ParseConfirmedSmbios(raw, smbios, error)) {
            return false;
        }
        FilterFields(field_mask, smbios);
        values.insert(values.end(),
                      std::make_move_iterator(smbios.begin()),
                      std::make_move_iterator(smbios.end()));
    }

    const DWORD wmi_fields =
        HWID_FIELD_MASK(HwidFieldDiskSerial) |
        HWID_FIELD_MASK(HwidFieldMacAddress);
    if ((field_mask & wmi_fields) != 0u) {
        ScopedComInitialization com;
        ComOwner<IWbemServices> services;
        if (FAILED(InitializeWmi(com, services, error))) {
            values.clear();
            return false;
        }
        if ((field_mask & HWID_FIELD_MASK(HwidFieldDiskSerial)) != 0u &&
            !CollectDisks(services.get(), values, error)) {
            values.clear();
            return false;
        }
        if ((field_mask & HWID_FIELD_MASK(HwidFieldMacAddress)) != 0u &&
            !CollectAdapters(services.get(), values, error)) {
            values.clear();
            return false;
        }
    }

    if (values.empty() || HasDuplicateKey(values)) {
        error = L"Native reader returned no requested identifiers or found a missing/duplicate device key.";
        values.clear();
        return false;
    }
    return true;
}

bool ReadClientIdentifiers(
    std::vector<Identifier>& values,
    std::wstring& error,
    DWORD field_mask) {
    values.clear();
    error.clear();
    if (!ValidateFieldMask(field_mask, error)) {
        return false;
    }
    std::vector<Identifier> actual;
    if (!ReadNativeIdentifiers(actual, error, field_mask)) {
        values.clear();
        return false;
    }

    std::vector<Identifier> resolved;
    resolved.reserve(actual.size());
    for (const Identifier& identifier : actual) {
        if (identifier.value.size() > kMaximumTextCharacters) {
            std::wostringstream detail;
            detail << L"Native read for " << FieldName(identifier.field)
                   << L" device '" << identifier.device
                   << L"' exceeds the override buffer limit.";
            error = detail.str();
            values.clear();
            return false;
        }
        std::vector<WCHAR> output(std::max<std::size_t>(
            kMaximumTextCharacters + 1u, identifier.value.size() + 1u), L'\0');
        const NTSTATUS status = HwidSpooferReadOverride(
            identifier.field,
            identifier.device.c_str(),
            identifier.value.c_str(),
            output.data(),
            output.size());
        if (status < 0) {
            std::wostringstream detail;
            detail << L"Read override for " << FieldName(identifier.field)
                   << L" device '" << identifier.device
                   << L"' failed with NTSTATUS " << FormatStatus(status) << L'.';
            error = detail.str();
            values.clear();
            return false;
        }
        const auto terminator = std::find(output.begin(), output.end(), L'\0');
        if (terminator == output.end()) {
            std::wostringstream detail;
            detail << L"Read override for " << FieldName(identifier.field)
                   << L" device '" << identifier.device
                   << L"' returned unterminated text (NTSTATUS "
                   << FormatStatus(status) << L").";
            error = detail.str();
            values.clear();
            return false;
        }
        Identifier result = identifier;
        result.value.assign(output.begin(), terminator);
        if (!IsValidWideText(result.value)) {
            std::wostringstream detail;
            detail << L"Read override for " << FieldName(identifier.field)
                   << L" device '" << identifier.device
                   << L"' returned invalid text (NTSTATUS "
                   << FormatStatus(status) << L").";
            error = detail.str();
            values.clear();
            return false;
        }
        resolved.push_back(std::move(result));
    }
    values = std::move(resolved);
    return true;
}

}  // namespace hwid_spoofer::client
