#if !defined(_WIN64)
#error nic_native_read must be built for native Windows x64.
#endif

#include <winsock2.h>
#include <ws2ipdef.h>
#include <windows.h>
#include <winioctl.h>
#include <iphlpapi.h>
#include <objbase.h>
#include <combaseapi.h>
#include <setupapi.h>
#include <initguid.h>
#include <ndisguid.h>
#include <ntddndis.h>
#include <windot11.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr DWORD kMaxAdapters = 512;
constexpr std::size_t kMaxDetailBytes = 16 * 1024;
constexpr std::size_t kMaxInstanceIdChars = 8192;
constexpr std::size_t kMaxAddressTableBytes = 16 * 1024 * 1024;
constexpr std::size_t kMaxReportBytes = 48 * 1024 * 1024;
constexpr std::size_t kOidOutputBytes = 256;

std::string JsonEscape(std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size() + 8);
    for (const unsigned char byte : value) {
        switch (byte) {
        case '"': escaped += "\\\""; break;
        case '\\': escaped += "\\\\"; break;
        case '\b': escaped += "\\b"; break;
        case '\f': escaped += "\\f"; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default:
            if (byte < 0x20) {
                constexpr char hex[] = "0123456789ABCDEF";
                escaped += "\\u00";
                escaped += hex[(byte >> 4) & 0x0f];
                escaped += hex[byte & 0x0f];
            } else {
                escaped += static_cast<char>(byte);
            }
            break;
        }
    }
    return escaped;
}

std::string Quote(std::string_view value) {
    return "\"" + JsonEscape(value) + "\"";
}

std::string Utf8(const wchar_t* value) {
    if (value == nullptr || *value == L'\0') return {};
    const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, nullptr, 0, nullptr, nullptr);
    if (required <= 0) return {};
    std::vector<char> bytes(static_cast<std::size_t>(required));
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, bytes.data(), required, nullptr, nullptr) != required) return {};
    return std::string(bytes.data());
}

std::string Win32Text(DWORD code) {
    if (code == ERROR_SUCCESS) return {};
    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    if (length == 0 || buffer == nullptr) {
        return "FormatMessageW failed with Win32 error " + std::to_string(GetLastError());
    }
    std::wstring message(buffer, length);
    LocalFree(buffer);
    while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' ')) message.pop_back();
    return Utf8(message.c_str());
}

std::string HexBytes(const BYTE* bytes, std::size_t count) {
    std::ostringstream out;
    out << std::uppercase << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < count; ++i) {
        if (i != 0) out << ':';
        out << std::setw(2) << static_cast<unsigned int>(bytes[i]);
    }
    return out.str();
}

std::string MacIfSixBytes(const BYTE* bytes, std::size_t count) {
    return count == 6 ? Quote(HexBytes(bytes, count)) : "null";
}

std::string GuidText(const GUID& value) {
    std::array<wchar_t, 40> buffer{};
    const int written = StringFromGUID2(value, buffer.data(), static_cast<int>(buffer.size()));
    return written > 0 ? Utf8(buffer.data()) : std::string{};
}

std::string HexCode(DWORD code) {
    std::ostringstream out;
    out << "0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << code;
    return out.str();
}

struct Status {
    DWORD code = ERROR_SUCCESS;
    std::string text;

    void Set(DWORD value) {
        code = value;
        text = Win32Text(value);
    }
};

struct AddressEntry {
    std::string adapterName;
    std::string friendlyName;
    std::string description;
    ULONG interfaceIndex = 0;
    ULONG ipv6InterfaceIndex = 0;
    ULONG interfaceType = 0;
    ULONG operationalStatus = 0;
    std::vector<BYTE> physicalAddress;
};

struct AddressCapture {
    ULONG status = ERROR_SUCCESS;
    std::string errorText;
    ULONG requiredOrReturnedBytes = 0;
    std::size_t attempts = 0;
    std::vector<AddressEntry> entries;
};

AddressCapture ReadAdaptersAddresses() {
    AddressCapture capture;
    ULONG bytes = 16 * 1024;
    std::vector<BYTE> buffer(bytes);
    constexpr ULONG flags = GAA_FLAG_INCLUDE_ALL_INTERFACES;
    for (std::size_t attempt = 0; attempt < 4; ++attempt) {
        capture.attempts = attempt + 1;
        capture.status = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr,
            reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data()), &bytes);
        capture.requiredOrReturnedBytes = bytes;
        if (capture.status != ERROR_BUFFER_OVERFLOW) break;
        if (bytes <= buffer.size() || bytes > kMaxAddressTableBytes) {
            capture.status = ERROR_INVALID_DATA;
            capture.errorText = "GetAdaptersAddresses returned an invalid or over-limit buffer size.";
            return capture;
        }
        buffer.assign(bytes, 0);
    }
    if (capture.status != NO_ERROR) {
        if (capture.errorText.empty()) capture.errorText = Win32Text(capture.status);
        return capture;
    }

    const auto* item = reinterpret_cast<const IP_ADAPTER_ADDRESSES*>(buffer.data());
    std::size_t count = 0;
    while (item != nullptr) {
        if (++count > kMaxAdapters || item->PhysicalAddressLength > sizeof(item->PhysicalAddress)) {
            capture.status = ERROR_INVALID_DATA;
            capture.errorText = "GetAdaptersAddresses returned an invalid entry count or physical address length.";
            capture.entries.clear();
            return capture;
        }
        AddressEntry entry;
        if (item->AdapterName != nullptr) entry.adapterName = item->AdapterName;
        entry.friendlyName = Utf8(item->FriendlyName);
        entry.description = Utf8(item->Description);
        entry.interfaceIndex = item->IfIndex;
        entry.ipv6InterfaceIndex = item->Ipv6IfIndex;
        entry.interfaceType = item->IfType;
        entry.operationalStatus = static_cast<ULONG>(item->OperStatus);
        entry.physicalAddress.assign(item->PhysicalAddress,
            item->PhysicalAddress + item->PhysicalAddressLength);
        capture.entries.push_back(std::move(entry));
        item = item->Next;
    }
    return capture;
}

struct IfTableEntry {
    std::string interfaceGuid;
    std::string alias;
    std::string description;
    ULONG interfaceIndex = 0;
    ULONG interfaceType = 0;
    ULONG operationalStatus = 0;
    std::uint64_t interfaceLuid = 0;
    std::vector<BYTE> physicalAddress;
};

struct IfTableCapture {
    DWORD status = NO_ERROR;
    std::string errorText;
    std::vector<IfTableEntry> entries;
};

IfTableCapture ReadIfTable2() {
    IfTableCapture capture;
    PMIB_IF_TABLE2 table = nullptr;
    capture.status = GetIfTable2(&table);
    if (capture.status != NO_ERROR || table == nullptr) {
        if (capture.status == NO_ERROR) capture.status = ERROR_INVALID_DATA;
        capture.errorText = Win32Text(capture.status);
        if (table != nullptr) FreeMibTable(table);
        return capture;
    }
    if (table->NumEntries > kMaxAdapters) {
        capture.status = ERROR_INVALID_DATA;
        capture.errorText = "GetIfTable2 returned more interfaces than the bounded capture limit.";
        FreeMibTable(table);
        return capture;
    }
    capture.entries.reserve(table->NumEntries);
    for (ULONG i = 0; i < table->NumEntries; ++i) {
        const MIB_IF_ROW2& row = table->Table[i];
        if (row.PhysicalAddressLength > sizeof(row.PhysicalAddress)) {
            capture.status = ERROR_INVALID_DATA;
            capture.errorText = "GetIfTable2 returned an invalid physical address length.";
            capture.entries.clear();
            FreeMibTable(table);
            return capture;
        }
        IfTableEntry entry;
        entry.interfaceGuid = GuidText(row.InterfaceGuid);
        entry.alias = Utf8(row.Alias);
        entry.description = Utf8(row.Description);
        entry.interfaceIndex = row.InterfaceIndex;
        entry.interfaceType = row.Type;
        entry.operationalStatus = static_cast<ULONG>(row.OperStatus);
        entry.interfaceLuid = row.InterfaceLuid.Value;
        entry.physicalAddress.assign(row.PhysicalAddress,
            row.PhysicalAddress + row.PhysicalAddressLength);
        capture.entries.push_back(std::move(entry));
    }
    FreeMibTable(table);
    return capture;
}

struct OidRead {
    const char* name = "";
    DWORD oid = 0;
    bool ioctlSucceeded = false;
    DWORD win32Error = ERROR_SUCCESS;
    std::string errorText;
    DWORD bytesReturned = 0;
    std::vector<BYTE> bytes;
};

OidRead QueryOid(HANDLE adapter, const char* name, DWORD oid) {
    OidRead result;
    result.name = name;
    result.oid = oid;
    std::array<BYTE, kOidOutputBytes> output{};
    DWORD bytesReturned = 0;
    const BOOL succeeded = DeviceIoControl(adapter, IOCTL_NDIS_QUERY_GLOBAL_STATS,
        &result.oid, sizeof(result.oid), output.data(), static_cast<DWORD>(output.size()),
        &bytesReturned, nullptr);
    result.ioctlSucceeded = succeeded != FALSE;
    result.bytesReturned = bytesReturned;
    const std::size_t bytesToCopy = (std::min)(static_cast<std::size_t>(bytesReturned), output.size());
    result.bytes.assign(output.begin(), output.begin() + static_cast<std::ptrdiff_t>(bytesToCopy));
    if (!result.ioctlSucceeded) {
        result.win32Error = GetLastError();
        result.errorText = Win32Text(result.win32Error);
    }
    return result;
}

class DeviceInfoSet final {
public:
    explicit DeviceInfoSet(HDEVINFO value) : value_(value) {}
    ~DeviceInfoSet() { if (value_ != INVALID_HANDLE_VALUE) SetupDiDestroyDeviceInfoList(value_); }
    DeviceInfoSet(const DeviceInfoSet&) = delete;
    DeviceInfoSet& operator=(const DeviceInfoSet&) = delete;
    HDEVINFO Get() const { return value_; }
private:
    HDEVINFO value_;
};

class DeviceFile final {
public:
    explicit DeviceFile(HANDLE value) : value_(value) {}
    ~DeviceFile() { if (value_ != INVALID_HANDLE_VALUE) CloseHandle(value_); }
    DeviceFile(const DeviceFile&) = delete;
    DeviceFile& operator=(const DeviceFile&) = delete;
    HANDLE Get() const { return value_; }
private:
    HANDLE value_;
};

struct RegistryString {
    LONG firstStatus = ERROR_SUCCESS;
    LONG secondStatus = ERROR_SUCCESS;
    DWORD registryType = 0;
    DWORD requiredBytes = 0;
    std::string value;
    std::string errorText;
};

RegistryString ReadNetCfgInstanceId(HDEVINFO devices, SP_DEVINFO_DATA* device) {
    RegistryString result;
    HKEY key = SetupDiOpenDevRegKey(devices, device, DICS_FLAG_GLOBAL, 0, DIREG_DRV, KEY_QUERY_VALUE);
    if (key == INVALID_HANDLE_VALUE) {
        result.firstStatus = static_cast<LONG>(GetLastError());
        result.errorText = Win32Text(static_cast<DWORD>(result.firstStatus));
        return result;
    }
    DWORD type = 0;
    DWORD bytes = 0;
    result.firstStatus = RegQueryValueExW(key, L"NetCfgInstanceId", nullptr, &type, nullptr, &bytes);
    result.registryType = type;
    result.requiredBytes = bytes;
    if (result.firstStatus != ERROR_SUCCESS || type != REG_SZ || bytes < sizeof(wchar_t) || bytes > 4096) {
        RegCloseKey(key);
        if (result.firstStatus == ERROR_SUCCESS) {
            result.firstStatus = static_cast<LONG>(ERROR_INVALID_DATA);
            result.errorText = "NetCfgInstanceId had an unexpected registry type or bounded size.";
        } else {
            result.errorText = Win32Text(static_cast<DWORD>(result.firstStatus));
        }
        return result;
    }
    std::vector<wchar_t> value(bytes / sizeof(wchar_t) + 1, L'\0');
    result.secondStatus = RegQueryValueExW(key, L"NetCfgInstanceId", nullptr, &type,
        reinterpret_cast<BYTE*>(value.data()), &bytes);
    RegCloseKey(key);
    result.registryType = type;
    result.requiredBytes = bytes;
    if (result.secondStatus != ERROR_SUCCESS || type != REG_SZ || bytes < sizeof(wchar_t) ||
        (bytes % sizeof(wchar_t)) != 0 || value[(bytes / sizeof(wchar_t)) - 1] != L'\0') {
        result.errorText = result.secondStatus == ERROR_SUCCESS
            ? "NetCfgInstanceId was not a terminated REG_SZ value."
            : Win32Text(static_cast<DWORD>(result.secondStatus));
        return result;
    }
    result.value = Utf8(value.data());
    return result;
}

struct DeviceProperty {
    DWORD status = ERROR_SUCCESS;
    std::string value;
};

DeviceProperty ReadFriendlyName(HDEVINFO devices, SP_DEVINFO_DATA* device) {
    DeviceProperty result;
    DWORD type = 0;
    DWORD required = 0;
    SetupDiGetDeviceRegistryPropertyW(devices, device, SPDRP_FRIENDLYNAME, &type, nullptr, 0, &required);
    result.status = GetLastError();
    if (result.status != ERROR_INSUFFICIENT_BUFFER || required == 0 || required > kMaxDetailBytes ||
        (type != REG_SZ && type != REG_EXPAND_SZ)) {
        if (result.status == ERROR_INSUFFICIENT_BUFFER && (required == 0 || required > kMaxDetailBytes || (type != REG_SZ && type != REG_EXPAND_SZ))) {
            result.status = ERROR_INVALID_DATA;
        }
        return result;
    }
    std::vector<BYTE> buffer(required, 0);
    if (!SetupDiGetDeviceRegistryPropertyW(devices, device, SPDRP_FRIENDLYNAME, &type,
        buffer.data(), required, nullptr)) {
        result.status = GetLastError();
        return result;
    }
    result.status = ERROR_SUCCESS;
    result.value = Utf8(reinterpret_cast<const wchar_t*>(buffer.data()));
    return result;
}

struct NdisInterface {
    std::string interfacePath;
    std::string pnpDeviceId;
    std::string friendlyName;
    DWORD friendlyNameError = ERROR_SUCCESS;
    std::string netCfgInstanceId;
    LONG netCfgFirstStatus = ERROR_SUCCESS;
    LONG netCfgSecondStatus = ERROR_SUCCESS;
    DWORD netCfgType = 0;
    DWORD netCfgBytes = 0;
    std::string netCfgErrorText;
    DWORD detailError = ERROR_SUCCESS;
    DWORD detailSizingError = ERROR_SUCCESS;
    DWORD detailRequiredBytes = 0;
    DWORD pnpSizingError = ERROR_SUCCESS;
    DWORD pnpRequiredChars = 0;
    DWORD openError = ERROR_SUCCESS;
    OidRead ethernetAddress;
    OidRead dot11Address;
};

struct NdisCapture {
    DWORD enumerationError = ERROR_SUCCESS;
    std::string enumerationErrorText;
    std::size_t enumeratedInterfaces = 0;
    bool complete = true;
    std::vector<NdisInterface> interfaces;
};

NdisCapture ReadNdisInterfaces() {
    NdisCapture capture;
    DeviceInfoSet devices(SetupDiGetClassDevsW(&GUID_NDIS_LAN_CLASS, nullptr, nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE));
    if (devices.Get() == INVALID_HANDLE_VALUE) {
        capture.enumerationError = GetLastError();
        capture.enumerationErrorText = Win32Text(capture.enumerationError);
        capture.complete = false;
        return capture;
    }

    bool reachedEnd = false;
    for (DWORD index = 0; index < kMaxAdapters; ++index) {
        SP_DEVICE_INTERFACE_DATA interfaceData{};
        interfaceData.cbSize = sizeof(interfaceData);
        if (!SetupDiEnumDeviceInterfaces(devices.Get(), nullptr, &GUID_NDIS_LAN_CLASS, index, &interfaceData)) {
            const DWORD error = GetLastError();
            if (error == ERROR_NO_MORE_ITEMS) {
                reachedEnd = true;
            } else {
                capture.enumerationError = error;
                capture.enumerationErrorText = Win32Text(error);
                capture.complete = false;
            }
            break;
        }
        ++capture.enumeratedInterfaces;

        NdisInterface result;
        SP_DEVINFO_DATA deviceData{};
        deviceData.cbSize = sizeof(deviceData);
        DWORD required = 0;
        SetupDiGetDeviceInterfaceDetailW(devices.Get(), &interfaceData, nullptr, 0, &required, &deviceData);
        const DWORD sizingError = GetLastError();
        result.detailSizingError = sizingError;
        result.detailRequiredBytes = required;
        if (sizingError != ERROR_INSUFFICIENT_BUFFER || required < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) || required > kMaxDetailBytes) {
            result.detailError = sizingError == ERROR_INSUFFICIENT_BUFFER ? ERROR_INVALID_DATA : sizingError;
            capture.complete = false;
            capture.interfaces.push_back(std::move(result));
            continue;
        }
        std::vector<BYTE> detailBuffer(required, 0);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(detailBuffer.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(devices.Get(), &interfaceData, detail, required, nullptr, &deviceData)) {
            result.detailError = GetLastError();
            capture.complete = false;
            capture.interfaces.push_back(std::move(result));
            continue;
        }
        result.interfacePath = Utf8(detail->DevicePath);

        DWORD pnpRequired = 0;
        SetupDiGetDeviceInstanceIdW(devices.Get(), &deviceData, nullptr, 0, &pnpRequired);
        DWORD pnpError = GetLastError();
        result.pnpSizingError = pnpError;
        result.pnpRequiredChars = pnpRequired;
        if (pnpError != ERROR_INSUFFICIENT_BUFFER || pnpRequired == 0 || pnpRequired > kMaxInstanceIdChars) {
            result.detailError = pnpError == ERROR_INSUFFICIENT_BUFFER ? ERROR_INVALID_DATA : pnpError;
            capture.complete = false;
            capture.interfaces.push_back(std::move(result));
            continue;
        }
        std::vector<wchar_t> pnpValue(pnpRequired, L'\0');
        if (!SetupDiGetDeviceInstanceIdW(devices.Get(), &deviceData, pnpValue.data(), pnpRequired, nullptr)) {
            result.detailError = GetLastError();
            capture.complete = false;
            capture.interfaces.push_back(std::move(result));
            continue;
        }
        result.pnpDeviceId = Utf8(pnpValue.data());

        const DeviceProperty friendly = ReadFriendlyName(devices.Get(), &deviceData);
        result.friendlyName = friendly.value;
        result.friendlyNameError = friendly.status;
        const RegistryString guid = ReadNetCfgInstanceId(devices.Get(), &deviceData);
        result.netCfgInstanceId = guid.value;
        result.netCfgFirstStatus = guid.firstStatus;
        result.netCfgSecondStatus = guid.secondStatus;
        result.netCfgType = guid.registryType;
        result.netCfgBytes = guid.requiredBytes;
        result.netCfgErrorText = guid.errorText;

        DeviceFile adapter(CreateFileW(detail->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (adapter.Get() == INVALID_HANDLE_VALUE) {
            result.openError = GetLastError();
            result.ethernetAddress.name = "OID_802_3_CURRENT_ADDRESS";
            result.ethernetAddress.oid = OID_802_3_CURRENT_ADDRESS;
            result.ethernetAddress.win32Error = result.openError;
            result.ethernetAddress.errorText = Win32Text(result.openError);
            result.dot11Address.name = "OID_DOT11_CURRENT_ADDRESS";
            result.dot11Address.oid = OID_DOT11_CURRENT_ADDRESS;
            result.dot11Address.win32Error = result.openError;
            result.dot11Address.errorText = Win32Text(result.openError);
        } else {
            result.ethernetAddress = QueryOid(adapter.Get(), "OID_802_3_CURRENT_ADDRESS", OID_802_3_CURRENT_ADDRESS);
            result.dot11Address = QueryOid(adapter.Get(), "OID_DOT11_CURRENT_ADDRESS", OID_DOT11_CURRENT_ADDRESS);
        }
        capture.interfaces.push_back(std::move(result));
    }
    if (!reachedEnd && capture.enumerationError == ERROR_SUCCESS) {
        capture.enumerationError = ERROR_MORE_DATA;
        capture.enumerationErrorText = "NDIS LAN interface enumeration reached its configured limit.";
        capture.complete = false;
    }
    return capture;
}

std::string AddressEntryJson(const AddressEntry& entry) {
    std::ostringstream out;
    out << "{\"adapterName\":" << Quote(entry.adapterName)
        << ",\"friendlyName\":" << Quote(entry.friendlyName)
        << ",\"description\":" << Quote(entry.description)
        << ",\"interfaceIndex\":" << entry.interfaceIndex
        << ",\"ipv6InterfaceIndex\":" << entry.ipv6InterfaceIndex
        << ",\"interfaceType\":" << entry.interfaceType
        << ",\"operationalStatus\":" << entry.operationalStatus
        << ",\"physicalAddressLength\":" << entry.physicalAddress.size()
        << ",\"physicalAddressBytesHex\":" << Quote(HexBytes(entry.physicalAddress.data(), entry.physicalAddress.size()))
        << ",\"physicalAddress\":" << MacIfSixBytes(entry.physicalAddress.data(), entry.physicalAddress.size()) << '}';
    return out.str();
}

std::string IfTableEntryJson(const IfTableEntry& entry) {
    std::ostringstream out;
    out << "{\"interfaceGuid\":" << Quote(entry.interfaceGuid)
        << ",\"alias\":" << Quote(entry.alias)
        << ",\"description\":" << Quote(entry.description)
        << ",\"interfaceIndex\":" << entry.interfaceIndex
        << ",\"interfaceType\":" << entry.interfaceType
        << ",\"operationalStatus\":" << entry.operationalStatus
        << ",\"interfaceLuid\":" << entry.interfaceLuid
        << ",\"physicalAddressLength\":" << entry.physicalAddress.size()
        << ",\"physicalAddressBytesHex\":" << Quote(HexBytes(entry.physicalAddress.data(), entry.physicalAddress.size()))
        << ",\"physicalAddress\":" << MacIfSixBytes(entry.physicalAddress.data(), entry.physicalAddress.size()) << '}';
    return out.str();
}

std::string OidJson(const OidRead& result) {
    std::ostringstream out;
    out << "{\"name\":" << Quote(result.name)
        << ",\"oid\":" << Quote(HexCode(result.oid))
        << ",\"ioctl\":\"IOCTL_NDIS_QUERY_GLOBAL_STATS\""
        << ",\"ioctlSucceeded\":" << (result.ioctlSucceeded ? "true" : "false")
        << ",\"bytesReturned\":" << result.bytesReturned
        << ",\"bytesCaptured\":" << result.bytes.size()
        << ",\"outputBytesHex\":" << Quote(HexBytes(result.bytes.data(), result.bytes.size()))
        << ",\"mac\":" << MacIfSixBytes(result.bytes.data(), result.bytes.size())
        << ",\"win32Error\":" << result.win32Error
        << ",\"win32ErrorText\":" << (result.win32Error == ERROR_SUCCESS ? "null" : Quote(result.errorText)) << '}';
    return out.str();
}

std::string NdisInterfaceJson(const NdisInterface& item, std::size_t index) {
    std::ostringstream out;
    out << "{\"index\":" << index
        << ",\"interfacePath\":" << (item.interfacePath.empty() ? "null" : Quote(item.interfacePath))
        << ",\"pnpDeviceId\":" << (item.pnpDeviceId.empty() ? "null" : Quote(item.pnpDeviceId))
        << ",\"friendlyName\":" << (item.friendlyName.empty() ? "null" : Quote(item.friendlyName))
        << ",\"friendlyNameWin32Error\":" << item.friendlyNameError
        << ",\"netCfgInstanceId\":" << (item.netCfgInstanceId.empty() ? "null" : Quote(item.netCfgInstanceId))
        << ",\"netCfgFirstStatus\":" << item.netCfgFirstStatus
        << ",\"netCfgSecondStatus\":" << item.netCfgSecondStatus
        << ",\"netCfgRegistryType\":" << item.netCfgType
        << ",\"netCfgBytes\":" << item.netCfgBytes
        << ",\"netCfgErrorText\":" << (item.netCfgErrorText.empty() ? "null" : Quote(item.netCfgErrorText))
        << ",\"detailSizingWin32Error\":" << item.detailSizingError
        << ",\"detailRequiredBytes\":" << item.detailRequiredBytes
        << ",\"pnpSizingWin32Error\":" << item.pnpSizingError
        << ",\"pnpRequiredChars\":" << item.pnpRequiredChars
        << ",\"detailWin32Error\":" << item.detailError
        << ",\"openWin32Error\":" << item.openError
        << ",\"ethernetAddress\":" << OidJson(item.ethernetAddress)
        << ",\"dot11Address\":" << OidJson(item.dot11Address) << '}';
    return out.str();
}

std::string CaptureJson(const AddressCapture& addresses, const IfTableCapture& ifTable, const NdisCapture& ndis) {
    SYSTEMTIME now{};
    GetSystemTime(&now);
    char timestamp[40]{};
    std::snprintf(timestamp, sizeof(timestamp), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
        now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
    const bool apiEnumerationComplete = addresses.status == NO_ERROR && ifTable.status == NO_ERROR && ndis.complete;

    std::ostringstream out;
    out << "{\"schema\":\"nic-native-read.v1\",\"capturedUtc\":" << Quote(timestamp)
        << ",\"processArchitecture\":\"x64\",\"readOnly\":true,\"replacementEnabled\":false"
        << ",\"networkConfigurationChanged\":false"
        << ",\"captureComplete\":" << (apiEnumerationComplete ? "true" : "false")
        << ",\"getAdaptersAddresses\":{\"api\":\"GetAdaptersAddresses(AF_UNSPEC,GAA_FLAG_INCLUDE_ALL_INTERFACES)\""
        << ",\"status\":" << addresses.status << ",\"statusHex\":" << Quote(HexCode(addresses.status))
        << ",\"statusText\":" << (addresses.errorText.empty() ? "null" : Quote(addresses.errorText))
        << ",\"requiredOrReturnedBytes\":" << addresses.requiredOrReturnedBytes
        << ",\"attempts\":" << addresses.attempts << ",\"entries\":[";
    for (std::size_t i = 0; i < addresses.entries.size(); ++i) {
        if (i != 0) out << ',';
        out << AddressEntryJson(addresses.entries[i]);
    }
    out << "]},\"getIfTable2\":{\"api\":\"GetIfTable2\",\"status\":" << ifTable.status
        << ",\"statusHex\":" << Quote(HexCode(ifTable.status))
        << ",\"statusText\":" << (ifTable.errorText.empty() ? "null" : Quote(ifTable.errorText)) << ",\"entries\":[";
    for (std::size_t i = 0; i < ifTable.entries.size(); ++i) {
        if (i != 0) out << ',';
        out << IfTableEntryJson(ifTable.entries[i]);
    }
    out << "]},\"setupDiNdisLanClass\":{\"api\":\"SetupDiEnumDeviceInterfaces(GUID_NDIS_LAN_CLASS)\""
        << ",\"enumerationError\":" << ndis.enumerationError
        << ",\"enumerationErrorHex\":" << Quote(HexCode(ndis.enumerationError))
        << ",\"enumerationErrorText\":" << (ndis.enumerationErrorText.empty() ? "null" : Quote(ndis.enumerationErrorText))
        << ",\"enumeratedInterfaces\":" << ndis.enumeratedInterfaces << ",\"complete\":" << (ndis.complete ? "true" : "false")
        << ",\"interfaces\":[";
    for (std::size_t i = 0; i < ndis.interfaces.size(); ++i) {
        if (i != 0) out << ',';
        out << NdisInterfaceJson(ndis.interfaces[i], i);
    }
    out << "]}}\n";
    return out.str();
}

bool WriteNewFile(const wchar_t* path, std::string_view bytes, DWORD* error) {
    DeviceFile file(CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (file.Get() == INVALID_HANDLE_VALUE) {
        *error = GetLastError();
        return false;
    }
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const std::size_t remaining = bytes.size() - offset;
        const DWORD toWrite = static_cast<DWORD>((std::min)(remaining, static_cast<std::size_t>(MAXDWORD)));
        DWORD written = 0;
        if (!WriteFile(file.Get(), bytes.data() + offset, toWrite, &written, nullptr)) {
            *error = GetLastError();
            return false;
        }
        if (written == 0) {
            *error = ERROR_WRITE_FAULT;
            return false;
        }
        offset += written;
    }
    if (!FlushFileBuffers(file.Get())) {
        *error = GetLastError();
        return false;
    }
    return true;
}

int Capture(const wchar_t* outputPath) {
    const AddressCapture addresses = ReadAdaptersAddresses();
    const IfTableCapture ifTable = ReadIfTable2();
    const NdisCapture ndis = ReadNdisInterfaces();
    std::string report = CaptureJson(addresses, ifTable, ndis);
    if (report.size() > kMaxReportBytes) {
        report = "{\"schema\":\"nic-native-read.v1\",\"readOnly\":true,\"replacementEnabled\":false,\"captureComplete\":false,\"reportLimitExceeded\":true,\"maximumReportBytes\":" + std::to_string(kMaxReportBytes) + "}\n";
    }
    DWORD writeError = ERROR_SUCCESS;
    if (!WriteNewFile(outputPath, report, &writeError)) {
        std::cerr << "report_write win32Error=" << writeError << " text=" << Win32Text(writeError) << '\n';
        return 2;
    }
    const bool complete = addresses.status == NO_ERROR && ifTable.status == NO_ERROR && ndis.complete;
    std::cout << "Read-only NIC capture complete=" << (complete ? "true" : "false")
        << " GAA=" << addresses.entries.size() << " IfTable2=" << ifTable.entries.size()
        << " NDIS=" << ndis.interfaces.size() << " report=" << Utf8(outputPath) << '\n';
    if (!complete) {
        std::cerr << "One or more native enumeration APIs failed or reached a configured bound; exact statuses are in the JSON report.\n";
    }
    return complete ? 0 : 1;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 3 || std::wstring_view(argv[1]) != L"--capture" || argv[2] == nullptr || argv[2][0] == L'\0') {
        std::wcerr << L"Usage: nic_native_read.exe --capture NEW_JSON_PATH\n";
        return 2;
    }
    return Capture(argv[2]);
}
