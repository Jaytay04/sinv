#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winioctl.h>
#include <ntddndis.h>
#include <setupapi.h>
#include <initguid.h>
#include <ndisguid.h>

#include "device_native_io_core.h"

#include <cstdint>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using device_native_io::JsonEscape;

struct AdapterBinding {
    const wchar_t* pnpDeviceId;
    const wchar_t* interfaceGuid;
    const char* original;
    const char* replacement;
};

constexpr AdapterBinding kAdapters[] = {
    {L"ROOT\\VMWARE\\0001", L"{E89EFD0E-E452-43D4-8E93-45DA2AF75831}", "00:50:56:C0:00:08", "02:90:10:20:30:40"},
    {L"ROOT\\VMWARE\\0000", L"{219E20CD-E7F3-409C-9AB4-99FF1E29203E}", "00:50:56:C0:00:01", "02:90:10:20:30:41"},
    {L"PCI\\VEN_8086&DEV_7AF0&SUBSYS_00748086&REV_11\\3&11583659&1&A3", L"{1D78C3CF-2BF6-453B-ABAF-2320AF9137F1}", "C4:BD:E5:32:06:D1", "02:90:10:20:30:42"},
    {L"PCI\\VEN_10EC&DEV_8125&SUBSYS_7D371462&REV_05\\4&3FA6681&0&00E2", L"{1291F611-0C4F-4ACF-99A6-E6A22D2F9026}", "D8:BB:C1:D8:57:20", "02:90:10:20:30:43"}
};

constexpr wchar_t kDiskPath[] = L"\\\\.\\PHYSICALDRIVE0";
constexpr wchar_t kDiskPnpId[] = L"SCSI\\DISK&VEN_NVME&PROD_INTEL_SSDPEKNU01\\5&8432851&0&000000";
constexpr char kDiskOriginal[] = "0000_0000_0100_0000_E4D2_5CDF_3227_5501.";
constexpr char kDiskReplacement[] = "1111_1111_1111_1111_A1B2_C3D4_5566_7788.";

std::string Utf8(const wchar_t* value) {
    if (value == nullptr) return {};
    const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, nullptr, 0, nullptr, nullptr);
    if (required <= 0) return {};
    std::vector<char> bytes(static_cast<std::size_t>(required));
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, bytes.data(), required, nullptr, nullptr) != required) return {};
    return std::string(bytes.data());
}

std::string Quote(std::string_view value) {
    return "\"" + JsonEscape(value) + "\"";
}

std::string Win32Text(DWORD code) {
    if (code == ERROR_SUCCESS) return {};
    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    if (length == 0 || buffer == nullptr) return "FormatMessageW failed: " + std::to_string(GetLastError());
    std::wstring message(buffer, length);
    LocalFree(buffer);
    while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' ')) message.pop_back();
    return Utf8(message.c_str());
}

struct Win32Failure {
    DWORD code = ERROR_SUCCESS;
    std::string text;
    void Set(DWORD value) { code = value; text = Win32Text(value); }
};

struct AdapterQuery {
    const AdapterBinding* binding = nullptr;
    std::string stage = "interface_enumeration";
    std::string interfacePath;
    std::wstring interfacePathWide;
    std::string actualPnpDeviceId;
    std::string actualInterfaceGuid;
    DWORD returnedBytes = 0;
    std::array<std::uint8_t, 6> mac{};
    bool hasMac = false;
    Win32Failure failure;
};

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

bool SameText(const wchar_t* left, const wchar_t* right) {
    return left != nullptr && right != nullptr && CompareStringOrdinal(left, -1, right, -1, TRUE) == CSTR_EQUAL;
}

bool ReadNetCfgInstanceGuid(HDEVINFO devices, SP_DEVINFO_DATA* device, std::wstring* guid, Win32Failure* failure) {
    HKEY key = SetupDiOpenDevRegKey(devices, device, DICS_FLAG_GLOBAL, 0, DIREG_DRV, KEY_QUERY_VALUE);
    if (key == INVALID_HANDLE_VALUE) {
        failure->Set(GetLastError());
        return false;
    }
    DWORD type = 0;
    DWORD bytes = 0;
    LONG status = RegQueryValueExW(key, L"NetCfgInstanceId", nullptr, &type, nullptr, &bytes);
    if (status != ERROR_SUCCESS || type != REG_SZ || bytes < sizeof(wchar_t) || bytes > 4096) {
        RegCloseKey(key);
        failure->Set(status == ERROR_SUCCESS ? ERROR_INVALID_DATA : static_cast<DWORD>(status));
        return false;
    }
    std::vector<wchar_t> value(bytes / sizeof(wchar_t) + 1, L'\0');
    status = RegQueryValueExW(key, L"NetCfgInstanceId", nullptr, &type, reinterpret_cast<BYTE*>(value.data()), &bytes);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS || type != REG_SZ || bytes < sizeof(wchar_t) || value.empty() || value[(bytes / sizeof(wchar_t)) - 1] != L'\0') {
        failure->Set(status == ERROR_SUCCESS ? ERROR_INVALID_DATA : static_cast<DWORD>(status));
        return false;
    }
    *guid = value.data();
    return true;
}

bool FindAdapterInterface(const AdapterBinding& binding, AdapterQuery* result) {
    result->stage = "setupdi_enumerate_interfaces";
    DeviceInfoSet devices(SetupDiGetClassDevsW(&GUID_NDIS_LAN_CLASS, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE));
    if (devices.Get() == INVALID_HANDLE_VALUE) {
        result->failure.Set(GetLastError());
        return false;
    }
    bool sawPnpMatch = false;
    for (DWORD index = 0;; ++index) {
        SP_DEVICE_INTERFACE_DATA interfaceData{};
        interfaceData.cbSize = sizeof(interfaceData);
        if (!SetupDiEnumDeviceInterfaces(devices.Get(), nullptr, &GUID_NDIS_LAN_CLASS, index, &interfaceData)) {
            const DWORD error = GetLastError();
            if (error != ERROR_NO_MORE_ITEMS) result->failure.Set(error);
            break;
        }

        SP_DEVINFO_DATA deviceData{};
        deviceData.cbSize = sizeof(deviceData);
        DWORD required = 0;
        SetupDiGetDeviceInterfaceDetailW(devices.Get(), &interfaceData, nullptr, 0, &required, &deviceData);
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || required < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)) continue;
        std::vector<BYTE> detailBuffer(required, 0);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(detailBuffer.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(devices.Get(), &interfaceData, detail, required, nullptr, &deviceData)) continue;

        DWORD pnpRequired = 0;
        SetupDiGetDeviceInstanceIdW(devices.Get(), &deviceData, nullptr, 0, &pnpRequired);
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || pnpRequired == 0 || pnpRequired > 32768) continue;
        std::vector<wchar_t> pnpValue(pnpRequired, L'\0');
        if (!SetupDiGetDeviceInstanceIdW(devices.Get(), &deviceData, pnpValue.data(), pnpRequired, nullptr)) continue;
        if (!SameText(pnpValue.data(), binding.pnpDeviceId)) continue;

        sawPnpMatch = true;
        result->actualPnpDeviceId = Utf8(pnpValue.data());
        result->interfacePathWide = detail->DevicePath;
        result->interfacePath = Utf8(detail->DevicePath);
        result->stage = "read_interface_guid_binding";
        std::wstring interfaceGuid;
        if (!ReadNetCfgInstanceGuid(devices.Get(), &deviceData, &interfaceGuid, &result->failure)) return false;
        result->actualInterfaceGuid = Utf8(interfaceGuid.c_str());
        if (!SameText(interfaceGuid.c_str(), binding.interfaceGuid)) {
            result->failure.Set(ERROR_INVALID_DATA);
            return false;
        }
        return true;
    }
    if (!sawPnpMatch && result->failure.code == ERROR_SUCCESS) result->failure.Set(ERROR_NOT_FOUND);
    return false;
}

AdapterQuery ReadAdapter(const AdapterBinding& binding) {
    AdapterQuery result;
    result.binding = &binding;
    if (!FindAdapterInterface(binding, &result)) return result;

    result.stage = "open_adapter_interface";
    DeviceFile file(CreateFileW(result.interfacePathWide.c_str(),
        0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
    if (file.Get() == INVALID_HANDLE_VALUE) {
        result.failure.Set(GetLastError());
        return result;
    }

    result.stage = "ioctl_ndis_query_global_stats";
    DWORD oid = OID_802_3_CURRENT_ADDRESS;
    std::array<BYTE, 32> output{};
    DWORD returned = 0;
    if (!DeviceIoControl(file.Get(), IOCTL_NDIS_QUERY_GLOBAL_STATS, &oid, sizeof(oid),
        output.data(), static_cast<DWORD>(output.size()), &returned, nullptr)) {
        result.failure.Set(GetLastError());
        return result;
    }
    result.returnedBytes = returned;
    if (returned != result.mac.size()) {
        result.failure.Set(ERROR_BAD_LENGTH);
        return result;
    }
    for (std::size_t i = 0; i < result.mac.size(); ++i) result.mac[i] = output[i];
    result.hasMac = true;
    result.stage = "complete";
    return result;
}

struct DiskQuery {
    std::string stage = "open_disk";
    DWORD descriptorBytes = 0;
    DWORD busType = 0;
    std::string serial;
    std::string serialHex;
    Win32Failure failure;
};

DiskQuery ReadDisk() {
    DiskQuery result;
    DeviceFile file(CreateFileW(kDiskPath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
    if (file.Get() == INVALID_HANDLE_VALUE) {
        result.failure.Set(GetLastError());
        return result;
    }

    result.stage = "ioctl_storage_query_property_header";
    STORAGE_PROPERTY_QUERY query{};
    query.PropertyId = StorageDeviceProperty;
    query.QueryType = PropertyStandardQuery;
    STORAGE_DESCRIPTOR_HEADER header{};
    DWORD returned = 0;
    if (!DeviceIoControl(file.Get(), IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), &header, sizeof(header), &returned, nullptr)) {
        result.failure.Set(GetLastError());
        return result;
    }
    if (returned < sizeof(header) || header.Size < sizeof(STORAGE_DEVICE_DESCRIPTOR) || header.Size > 1024 * 1024) {
        result.failure.Set(ERROR_INVALID_DATA);
        return result;
    }

    result.stage = "ioctl_storage_query_property_descriptor";
    std::vector<BYTE> bytes(header.Size, 0);
    returned = 0;
    if (!DeviceIoControl(file.Get(), IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), bytes.data(), static_cast<DWORD>(bytes.size()), &returned, nullptr)) {
        result.failure.Set(GetLastError());
        return result;
    }
    result.descriptorBytes = returned;
    if (returned < sizeof(STORAGE_DEVICE_DESCRIPTOR) || returned > bytes.size()) {
        result.failure.Set(ERROR_INVALID_DATA);
        return result;
    }
    STORAGE_DEVICE_DESCRIPTOR descriptor{};
    std::memcpy(&descriptor, bytes.data(), sizeof(descriptor));
    result.busType = static_cast<DWORD>(descriptor.BusType);
    if (descriptor.Size > returned || descriptor.Size < sizeof(STORAGE_DEVICE_DESCRIPTOR)) {
        result.failure.Set(ERROR_INVALID_DATA);
        return result;
    }
    if (descriptor.SerialNumberOffset == 0) {
        result.failure.Set(ERROR_NOT_FOUND);
        return result;
    }
    if (descriptor.SerialNumberOffset >= descriptor.Size || descriptor.SerialNumberOffset >= returned) {
        result.failure.Set(ERROR_INVALID_DATA);
        return result;
    }
    const BYTE* serial = bytes.data() + descriptor.SerialNumberOffset;
    const std::size_t remaining = (std::min)(descriptor.Size, returned) - descriptor.SerialNumberOffset;
    const void* terminator = std::memchr(serial, 0, remaining);
    if (terminator == nullptr) {
        result.failure.Set(ERROR_INVALID_DATA);
        return result;
    }
    const auto* end = static_cast<const BYTE*>(terminator);
    result.serial.assign(reinterpret_cast<const char*>(serial), reinterpret_cast<const char*>(end));
    result.serialHex = device_native_io::HexBytes(serial, static_cast<std::size_t>(end - serial));
    result.stage = "complete";
    return result;
}

std::string AdapterJson(const AdapterQuery& result, std::size_t index) {
    const AdapterBinding& binding = *result.binding;
    std::ostringstream out;
    out << "{\"index\":" << index
        << ",\"pnpDeviceId\":" << Quote(Utf8(binding.pnpDeviceId))
        << ",\"expectedInterfaceGuid\":" << Quote(Utf8(binding.interfaceGuid))
        << ",\"actualPnpDeviceId\":" << (result.actualPnpDeviceId.empty() ? "null" : Quote(result.actualPnpDeviceId))
        << ",\"actualInterfaceGuid\":" << (result.actualInterfaceGuid.empty() ? "null" : Quote(result.actualInterfaceGuid))
        << ",\"devicePath\":" << (result.interfacePath.empty() ? "null" : Quote(result.interfacePath))
        << ",\"oid\":\"OID_802_3_CURRENT_ADDRESS\",\"oidCode\":\"0x01010102\",\"ioctl\":\"IOCTL_NDIS_QUERY_GLOBAL_STATS\""
        << ",\"lockedOriginal\":" << Quote(binding.original)
        << ",\"lockedReplacement\":" << Quote(binding.replacement)
        << ",\"rawMacBytesHex\":" << (result.hasMac ? Quote(device_native_io::HexBytes(result.mac.data(), result.mac.size())) : "null")
        << ",\"mac\":" << (result.hasMac ? Quote(device_native_io::FormatMac(device_native_io::MacAddress{result.mac})) : "null")
        << ",\"bytesReturned\":" << result.returnedBytes
        << ",\"stage\":" << Quote(result.stage)
        << ",\"win32Error\":" << result.failure.code
        << ",\"win32ErrorText\":" << (result.failure.code == ERROR_SUCCESS ? "null" : Quote(result.failure.text))
        << '}';
    return out.str();
}

std::string DiskJson(const DiskQuery& result) {
    std::ostringstream out;
    out << "{\"devicePath\":" << Quote(Utf8(kDiskPath))
        << ",\"pnpDeviceId\":" << Quote(Utf8(kDiskPnpId))
        << ",\"ioctl\":\"IOCTL_STORAGE_QUERY_PROPERTY\",\"property\":\"StorageDeviceProperty\""
        << ",\"serialOffsetMeaning\":\"STORAGE_DEVICE_DESCRIPTOR.SerialNumberOffset\""
        << ",\"lockedOriginal\":" << Quote(kDiskOriginal)
        << ",\"lockedReplacement\":" << Quote(kDiskReplacement)
        << ",\"serial\":" << (result.failure.code == ERROR_SUCCESS ? Quote(result.serial) : "null")
        << ",\"serialBytesHex\":" << (result.failure.code == ERROR_SUCCESS ? Quote(result.serialHex) : "null")
        << ",\"busType\":" << result.busType
        << ",\"descriptorBytes\":" << result.descriptorBytes
        << ",\"stage\":" << Quote(result.stage)
        << ",\"win32Error\":" << result.failure.code
        << ",\"win32ErrorText\":" << (result.failure.code == ERROR_SUCCESS ? "null" : Quote(result.failure.text))
        << '}';
    return out.str();
}

bool WriteNewFile(const wchar_t* path, const std::string& bytes, DWORD* error) {
    DeviceFile file(CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (file.Get() == INVALID_HANDLE_VALUE) {
        *error = GetLastError();
        return false;
    }
    DWORD written = 0;
    if (bytes.size() > MAXDWORD || !WriteFile(file.Get(), bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr)) {
        *error = GetLastError();
        return false;
    }
    if (written != bytes.size()) {
        *error = ERROR_WRITE_FAULT;
        return false;
    }
    if (!FlushFileBuffers(file.Get())) {
        *error = GetLastError();
        return false;
    }
    return true;
}

int Capture(const wchar_t* outputPath) {
    std::vector<AdapterQuery> results;
    bool complete = true;
    for (const auto& binding : kAdapters) {
        results.push_back(ReadAdapter(binding));
        if (results.back().failure.code != ERROR_SUCCESS) complete = false;
    }
    const DiskQuery disk = ReadDisk();
    if (disk.failure.code != ERROR_SUCCESS) complete = false;

    std::ostringstream report;
    SYSTEMTIME now{};
    GetSystemTime(&now);
    char timestamp[40]{};
    std::snprintf(timestamp, sizeof(timestamp), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
        now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
    report << "{\"schema\":\"independent-native-device-io.v1\",\"capturedUtc\":" << Quote(timestamp)
        << ",\"readOnly\":true,\"replacementEnabled\":false,\"successRequiresEverySelectedQuery\":true"
        << ",\"adapterEnumeration\":\"SetupDiEnumDeviceInterfaces(GUID_NDIS_LAN_CLASS); exact PnP and NetCfgInstanceId identity match\""
        << ",\"adapterRead\":\"CreateFileW(interface path) + DeviceIoControl(IOCTL_NDIS_QUERY_GLOBAL_STATS, OID_802_3_CURRENT_ADDRESS)\""
        << ",\"diskRead\":\"CreateFileW(physical drive) + DeviceIoControl(IOCTL_STORAGE_QUERY_PROPERTY, StorageDeviceProperty)\""
        << ",\"adapters\":[";
    for (std::size_t i = 0; i < results.size(); ++i) {
        if (i != 0) report << ',';
        report << AdapterJson(results[i], i);
    }
    report << "],\"disk\":" << DiskJson(disk) << ",\"captureComplete\":" << (complete ? "true" : "false") << "}\n";

    DWORD writeError = ERROR_SUCCESS;
    const std::string output = report.str();
    if (!WriteNewFile(outputPath, output, &writeError)) {
        std::cerr << "report_write win32Error=" << writeError << " text=" << Win32Text(writeError) << '\n';
        return 2;
    }
    std::cout << "Independent native query complete=" << (complete ? "true" : "false")
        << " report=" << Utf8(outputPath) << '\n';
    for (std::size_t i = 0; i < results.size(); ++i) {
        const auto& item = results[i];
        std::cout << "adapter[" << i << "] stage=" << item.stage << " win32Error=" << item.failure.code
            << " text=" << item.failure.text << " mac=" << (item.hasMac ? device_native_io::FormatMac(device_native_io::MacAddress{item.mac}) : "<unavailable>") << '\n';
    }
    std::cout << "disk stage=" << disk.stage << " win32Error=" << disk.failure.code << " text=" << disk.failure.text
        << " serial=" << (disk.failure.code == ERROR_SUCCESS ? disk.serial : "<unavailable>") << '\n';
    return complete ? 0 : 1;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 3 || std::wstring_view(argv[1]) != L"--capture" || std::wstring_view(argv[2]) == L"") {
        std::wcerr << L"Usage: device_native_io.exe --capture NEW_JSON_PATH\n";
        return 2;
    }
    return Capture(argv[2]);
}
