#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2ipdef.h>
#include <windows.h>
#include <winioctl.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <objbase.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

static std::string Utf8(const wchar_t* value) {
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) throw std::runtime_error("Invalid native UTF-16 text.");
    std::vector<char> bytes(static_cast<std::size_t>(size));
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, bytes.data(), size, nullptr, nullptr) != size)
        throw std::runtime_error("Native text conversion failed.");
    return std::string(bytes.data());
}
static std::string Json(const std::string& value) {
    std::ostringstream out; out << '"';
    const char hex[] = "0123456789ABCDEF";
    for (const unsigned char c : value) {
        if (c == '"' || c == '\\') out << '\\' << static_cast<char>(c);
        else if (c < 32) out << "\\u00" << hex[c >> 4] << hex[c & 15];
        else out << static_cast<char>(c);
    }
    out << '"'; return out.str();
}
static std::string Mac(const UCHAR* bytes, ULONG size) {
    if (size > IF_MAX_PHYS_ADDRESS_LENGTH) throw std::runtime_error("Invalid native address length.");
    const char hex[] = "0123456789ABCDEF"; std::string out;
    for (ULONG i = 0; i < size; ++i) {
        if (i) out += ':';
        out += hex[bytes[i] >> 4]; out += hex[bytes[i] & 15];
    }
    return out;
}
struct File {
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit File(HANDLE handle) : value(handle) {}
    ~File() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    File(const File&) = delete; File& operator=(const File&) = delete;
};
struct DiskResult { DWORD error = 0, descriptor_bytes = 0, bus = 0; std::string serial; };
static DiskResult ReadDisk(const wchar_t* path) {
    DiskResult result;
    File file(CreateFileW(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
    if (file.value == INVALID_HANDLE_VALUE) { result.error = GetLastError(); return result; }
    STORAGE_PROPERTY_QUERY query{};
    query.PropertyId = StorageDeviceProperty; query.QueryType = PropertyStandardQuery;
    STORAGE_DESCRIPTOR_HEADER header{}; DWORD returned = 0;
    if (!DeviceIoControl(file.value, IOCTL_STORAGE_QUERY_PROPERTY, &query, static_cast<DWORD>(sizeof(query)), &header, static_cast<DWORD>(sizeof(header)), &returned, nullptr)) {
        result.error = GetLastError(); return result;
    }
    if (returned < sizeof(header) || header.Size < sizeof(STORAGE_DEVICE_DESCRIPTOR) || header.Size > 1024 * 1024) {
        result.error = ERROR_INVALID_DATA; return result;
    }
    std::vector<BYTE> buffer(header.Size);
    if (!DeviceIoControl(file.value, IOCTL_STORAGE_QUERY_PROPERTY, &query, static_cast<DWORD>(sizeof(query)), buffer.data(), static_cast<DWORD>(buffer.size()), &returned, nullptr)) {
        result.error = GetLastError(); return result;
    }
    if (returned < sizeof(STORAGE_DEVICE_DESCRIPTOR) || returned > buffer.size()) { result.error = ERROR_INVALID_DATA; return result; }
    STORAGE_DEVICE_DESCRIPTOR descriptor{}; std::memcpy(&descriptor, buffer.data(), sizeof(descriptor));
    result.descriptor_bytes = returned; result.bus = static_cast<DWORD>(descriptor.BusType);
    if (descriptor.Size > returned || descriptor.SerialNumberOffset >= descriptor.Size) { result.error = ERROR_INVALID_DATA; return result; }
    if (descriptor.SerialNumberOffset == 0) { result.error = ERROR_NOT_FOUND; return result; }
    const char* serial = reinterpret_cast<const char*>(buffer.data() + descriptor.SerialNumberOffset);
    const void* end = std::memchr(serial, 0, descriptor.Size - descriptor.SerialNumberOffset);
    if (!end) { result.error = ERROR_INVALID_DATA; return result; }
    result.serial.assign(serial, static_cast<const char*>(end) - serial);
    return result;
}
int wmain(int argc, wchar_t** argv) {
    if (argc < 3 || std::wstring(argv[1]) != L"--output") { std::cerr << "Use --output NEW_FILE [PHYSICAL_DRIVE_PATH ...].\n"; return 2; }
    try {
        std::ostringstream out; bool complete = true;
        out << "{\"schema\":\"native-device-originals.v1\",\"readOnly\":true,\"replacementEnabled\":false,\"disks\":[";
        for (int i = 3; i < argc; ++i) {
            const auto disk = ReadDisk(argv[i]); if (disk.error) complete = false;
            if (i != 3) out << ',';
            out << "{\"path\":" << Json(Utf8(argv[i])) << ",\"source\":\"DeviceIoControl(IOCTL_STORAGE_QUERY_PROPERTY,StorageDeviceProperty)\",\"win32Error\":" << disk.error
                << ",\"descriptorBytes\":" << disk.descriptor_bytes << ",\"busType\":" << disk.bus << ",\"serial\":";
            if (disk.error) out << "null"; else out << Json(disk.serial);
            out << '}';
        }
        PMIB_IF_TABLE2 table = nullptr;
        const auto error = GetIfTable2(&table);
        if (error != NO_ERROR) complete = false;
        out << "],\"nicSource\":\"GetIfTable2(MIB_IF_ROW2.PhysicalAddress)\",\"nicWin32Error\":" << error << ",\"interfaces\":[";
        if (error == NO_ERROR && table) {
            if (table->NumEntries > 16384) { FreeMibTable(table); throw std::runtime_error("Unexpected native interface count."); }
            for (ULONG i = 0; i < table->NumEntries; ++i) {
                const auto& row = table->Table[i]; wchar_t guid[40]{};
                if (StringFromGUID2(row.InterfaceGuid, guid, 40) == 0) { FreeMibTable(table); throw std::runtime_error("Native interface GUID conversion failed."); }
                if (i) out << ',';
                out << "{\"interfaceIndex\":" << row.InterfaceIndex << ",\"interfaceGuid\":" << Json(Utf8(guid))
                    << ",\"alias\":" << Json(Utf8(row.Alias)) << ",\"description\":" << Json(Utf8(row.Description))
                    << ",\"type\":" << row.Type << ",\"addressBytes\":" << row.PhysicalAddressLength
                    << ",\"currentMac\":" << Json(Mac(row.PhysicalAddress, row.PhysicalAddressLength))
                    << ",\"permanentMac\":" << Json(Mac(row.PermanentPhysicalAddress, row.PhysicalAddressLength)) << '}';
            }
            FreeMibTable(table);
        }
        out << "],\"captureComplete\":" << (complete ? "true" : "false") << "}\n";
        const std::string bytes = out.str();
        File output(CreateFileW(argv[2], GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (output.value == INVALID_HANDLE_VALUE) throw std::runtime_error("The new output file could not be created.");
        DWORD written = 0;
        if (bytes.size() > MAXDWORD || !WriteFile(output.value, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) || written != bytes.size() || !FlushFileBuffers(output.value))
            throw std::runtime_error("Native report write failed.");
        std::cout << "Native original capture complete=" << (complete ? "true" : "false") << '\n';
        return complete ? 0 : 1;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
