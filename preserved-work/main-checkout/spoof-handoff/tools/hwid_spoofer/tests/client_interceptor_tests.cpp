#include "hwid_spoofer.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <iostream>

static void Require(bool condition, const char* reason)
{
    if (!condition) {
        std::cerr << reason << "\n";
        std::exit(1);
    }
}

static HWID_SPOOF_CONFIG ValidConfig()
{
    HWID_SPOOF_CONFIG config{};
    config.SpoofDiskSerial = TRUE;
    wcscpy_s(config.FakeDiskSerial, _countof(config.FakeDiskSerial), L"DISK-NEW1");
    config.SpoofMacAddress = TRUE;
    const BYTE mac[6] = { 0x02, 0x12, 0x23, 0x34, 0x45, 0x56 };
    std::memcpy(config.FakeMacAddress, mac, sizeof(mac));
    config.SpoofSMBIOS = TRUE;
    wcscpy_s(config.FakeSMBIOSSerial, _countof(config.FakeSMBIOSSerial), L"SYS-NEW1");
    wcscpy_s(config.FakeSMBIOSManufacturer,
             _countof(config.FakeSMBIOSManufacturer), L"Example Systems");
    wcscpy_s(config.FakeSMBIOSProduct,
             _countof(config.FakeSMBIOSProduct), L"Example Workstation");
    config.SpoofMotherboard = TRUE;
    wcscpy_s(config.FakeMotherboardSerial,
             _countof(config.FakeMotherboardSerial), L"BRD-NEW1");
    wcscpy_s(config.FakeMotherboardManufacturer,
             _countof(config.FakeMotherboardManufacturer), L"Example Boards");
    wcscpy_s(config.FakeMotherboardProduct,
             _countof(config.FakeMotherboardProduct), L"Example Board");
    config.SpoofSMBIOSUUID = TRUE;
    wcscpy_s(config.FakeSMBIOSUUID, _countof(config.FakeSMBIOSUUID),
             L"00112233-4455-6677-8899-AABBCCDDEEFF");
    return config;
}

static HWID_SPOOF_DEVICE_BINDING DiskBinding(
    const WCHAR* key = L"disk-001",
    const WCHAR* original = L"DISK-OLD1",
    const WCHAR* replacement = L"DISK-NEW1")
{
    HWID_SPOOF_DEVICE_BINDING binding{};
    binding.Field = HwidFieldDiskSerial;
    wcscpy_s(binding.DeviceKey, _countof(binding.DeviceKey), key);
    wcscpy_s(binding.OriginalValue, _countof(binding.OriginalValue), original);
    wcscpy_s(binding.ReplacementValue, _countof(binding.ReplacementValue), replacement);
    return binding;
}

static HWID_SPOOF_DEVICE_BINDING MacBinding(
    const WCHAR* key = L"adapter-001",
    const WCHAR* original = L"00:11:22:33:44:55",
    const WCHAR* replacement = L"02:12:23:34:45:66")
{
    HWID_SPOOF_DEVICE_BINDING binding{};
    binding.Field = HwidFieldMacAddress;
    wcscpy_s(binding.DeviceKey, _countof(binding.DeviceKey), key);
    wcscpy_s(binding.OriginalValue, _countof(binding.OriginalValue), original);
    wcscpy_s(binding.ReplacementValue, _countof(binding.ReplacementValue), replacement);
    return binding;
}

static void TestFieldMapAndValidation()
{
    static const char* expected_members[HwidFieldCount] = {
        "FakeDiskSerial",
        "FakeMacAddress",
        "FakeSMBIOSSerial",
        "FakeSMBIOSManufacturer",
        "FakeSMBIOSProduct",
        "FakeMotherboardSerial",
        "FakeMotherboardManufacturer",
        "FakeMotherboardProduct",
        "FakeSMBIOSUUID"
    };
    static const char* expected_sources[HwidFieldCount] = {
        "Win32_DiskDrive.SerialNumber",
        "Win32_NetworkAdapter.MACAddress",
        "SMBIOS Type 1.SerialNumber",
        "SMBIOS Type 1.Manufacturer",
        "SMBIOS Type 1.Product",
        "SMBIOS Type 2.SerialNumber",
        "SMBIOS Type 2.Manufacturer",
        "SMBIOS Type 2.Product",
        "SMBIOS Type 1.UUID"
    };
    SIZE_T count = 0;
    const HWID_SPOOF_FIELD_INFO* map = HwidSpooferGetFieldMap(&count);
    Require(map != nullptr && count == HwidFieldCount, "Field map has nine confirmed entries");
    for (SIZE_T i = 0; i < count; ++i) {
        Require(map[i].Field == static_cast<HWID_SPOOF_FIELD>(i), "Field map enum order is stable");
        Require(std::strcmp(map[i].Member, expected_members[i]) == 0, "Field map member matches README");
        Require(std::strcmp(map[i].Source, expected_sources[i]) == 0, "Field map source matches README");
    }

    auto config = ValidConfig();
    Require(HwidSpooferValidateConfig(&config) == 0, "Canonical enabled values validate");
    config.FakeSMBIOSUUID[8] = L'0';
    Require(HwidSpooferValidateConfig(&config) < 0, "UUID hyphen placement is checked");
    config = ValidConfig();
    wcscpy_s(config.FakeSMBIOSUUID, _countof(config.FakeSMBIOSUUID),
             L"00000000-0000-0000-0000-000000000000");
    Require(HwidSpooferValidateConfig(&config) < 0, "Nil UUID is rejected");
    config = ValidConfig();
    wcscpy_s(config.FakeSMBIOSUUID, _countof(config.FakeSMBIOSUUID),
             L"FFFFFFFF-FFFF-FFFF-FFFF-FFFFFFFFFFFF");
    Require(HwidSpooferValidateConfig(&config) < 0, "All-ones UUID is rejected");
    config = ValidConfig();
    config.FakeSMBIOSManufacturer[0] = static_cast<WCHAR>(0x00e9);
    Require(HwidSpooferValidateConfig(&config) < 0, "Manufacturer text is ASCII only");
    config = ValidConfig();
    std::fill_n(config.FakeDiskSerial, _countof(config.FakeDiskSerial), L'X');
    Require(HwidSpooferValidateConfig(&config) < 0, "Enabled serial requires in-bounds terminator");
    config = ValidConfig();
    config.FakeMacAddress[0] = 0x03;
    Require(HwidSpooferValidateConfig(&config) < 0, "Multicast MAC is rejected");

    config = ValidConfig();
    config.SpoofCPU = TRUE;
    Require(HwidSpooferValidateConfig(&config) < 0, "CPU spoofing is unsupported");
    config = ValidConfig();
    config.FakeGPUSerial[0] = L'X';
    Require(HwidSpooferValidateConfig(&config) < 0, "Unavailable GPU serial remains zero");

    HWID_SPOOF_CONFIG generated{};
    HwidSpooferGenerateRandomConfig(&generated);
    Require(generated.SpoofDiskSerial && generated.SpoofMacAddress &&
            generated.SpoofSMBIOS && generated.SpoofMotherboard,
            "Default generation preserves the supported legacy selection");
    Require(!generated.SpoofCPU && !generated.SpoofGPU &&
            !generated.SpoofSMBIOSUUID, "Unavailable fields remain disabled by default");
    Require(std::wcslen(generated.FakeDiskSerial) == 63u &&
            std::wcslen(generated.FakeSMBIOSSerial) == 63u &&
            std::wcslen(generated.FakeMotherboardSerial) == 63u,
            "Default serial lengths remain 63 characters");
    Require((generated.FakeMacAddress[0] & 0x03u) == 0x02u,
            "Generated MAC is locally administered unicast");
    Require(HwidSpooferValidateConfig(&generated) == 0, "Generated default config validates");
}

static void TestLifecycleAndMaskResolution()
{
    HwidSpooferCleanup();
    auto config = ValidConfig();
    Require(HwidSpooferSetFieldMask(HWID_FIELD_MASK(HwidFieldSystemSerial)) < 0,
            "Field mask requires initialized state");
    Require(HwidSpooferInitialize(&config) == 0, "Configuration initializes");
    Require(HwidSpooferApply() < 0,
            "Default all-confirmed mask requires explicit disk and adapter bindings");
    Require(HwidSpooferSetFieldMask(HWID_FIELD_MASK(HwidFieldSystemSerial)) == 0,
            "One-field serial mask is accepted");
    Require(HwidSpooferSetFieldMask(HWID_ALL_CONFIRMED_FIELDS | (1u << 9)) < 0,
            "Unknown field-mask bits are rejected");
    Require(HwidSpooferApply() == 0 && HwidSpooferIsActive(),
            "System-serial-only selection applies without device bindings");

    WCHAR output[64]{};
    Require(HwidSpooferReadOverride(HwidFieldSystemSerial, nullptr,
                L"SYS-OLD1", output, _countof(output)) == 0 &&
            std::wcscmp(output, L"SYS-NEW1") == 0,
            "Selected system serial resolves to its configured member");
    Require(HwidSpooferReadOverride(HwidFieldSystemSerial, nullptr,
                L"SYS-TOO-LONG", output, _countof(output)) < 0 && output[0] == L'\0',
            "Serial length mismatch fails without producing a value");
    Require(HwidSpooferReadOverride(HwidFieldSystemManufacturer, nullptr,
                L"Native Manufacturer", output, _countof(output)) == 0 &&
            std::wcscmp(output, L"Native Manufacturer") == 0,
            "Unselected SMBIOS field passes the current source through");
    Require(HwidSpooferInitialize(&config) < 0,
            "Active configuration cannot be reinitialized");
    Require(HwidSpooferSetFieldMask(HWID_ALL_CONFIRMED_FIELDS) < 0,
            "Field selection is immutable while active");

    WCHAR alias[64] = L"fresh native value";
    Require(HwidSpooferReadOverride(HwidFieldSystemProduct, nullptr,
                alias, alias, _countof(alias)) == 0 &&
            std::wcscmp(alias, L"fresh native value") == 0,
            "Aliased source and destination pass through safely");
    WCHAR short_output[4] = L"x";
    Require(HwidSpooferReadOverride(HwidFieldSystemProduct, nullptr,
                L"Native Product", short_output, _countof(short_output)) < 0 &&
            short_output[0] == L'\0', "Insufficient output capacity returns an empty result");

    Require(HwidSpooferRemove() == 0 && !HwidSpooferIsActive(),
            "Remove disables replacement state");
    Require(HwidSpooferReadOverride(HwidFieldSystemSerial, nullptr,
                L"SYS-OLD1", output, _countof(output)) == 0 &&
            std::wcscmp(output, L"SYS-OLD1") == 0,
            "Removed state reads through to a fresh native value");

    Require(HwidSpooferSetFieldMask(HWID_FIELD_MASK(HwidFieldSystemUuid)) == 0,
            "UUID field can be selected independently");
    Require(HwidSpooferApply() == 0, "Selected UUID field applies without device bindings");
    Require(HwidSpooferReadOverride(HwidFieldSystemUuid, nullptr,
                L"11223344-5566-7788-99AA-BBCCDDEEFF00",
                output, _countof(output)) == 0 &&
            std::wcscmp(output, L"00112233-4455-6677-8899-AABBCCDDEEFF") == 0,
            "Selected UUID resolves through its append-only config member");
    Require(HwidSpooferRemove() == 0, "UUID selection can be disabled");
    Require(HwidSpooferSetFieldMask(HWID_FIELD_MASK(HwidFieldSystemSerial)) == 0,
            "Serial-only selection can be restored after Remove");
    Require(HwidSpooferApply() == 0 && HwidSpooferIsActive(),
            "Retained configuration can be applied again");

    WCHAR long_native[4097];
    WCHAR long_output[4097]{};
    std::fill_n(long_native, 4096, L'N');
    long_native[4096] = L'\0';
    Require(HwidSpooferReadOverride(HwidFieldSystemManufacturer, nullptr,
                long_native, long_output, _countof(long_output)) == 0 &&
            std::wcslen(long_output) == 4096u,
            "Unselected native text passes through up to the documented bound");
    Require(HwidSpooferReadOverride(HwidFieldMacAddress, nullptr,
                L"", output, _countof(output)) == 0 && output[0] == L'\0',
            "Empty native MAC observation passes through unchanged");

    HwidSpooferCleanup();
    Require(!HwidSpooferIsActive() && HwidSpooferApply() < 0,
            "Cleanup clears state and prevents applying it");
    Require(HwidSpooferReadOverride(HwidFieldSystemSerial, nullptr,
                L"after cleanup", output, _countof(output)) == 0 &&
            std::wcscmp(output, L"after cleanup") == 0,
            "Cleanup leaves reads on the native path");
}

static void TestExplicitDeviceBindings()
{
    HwidSpooferCleanup();
    auto config = ValidConfig();
    Require(HwidSpooferInitialize(&config) == 0, "Binding configuration initializes");
    const DWORD disk_mask = HWID_FIELD_MASK(HwidFieldDiskSerial);
    const DWORD both_mask = disk_mask | HWID_FIELD_MASK(HwidFieldMacAddress);
    Require(HwidSpooferApply() < 0,
            "Default mask does not auto-select a disk or adapter binding");
    Require(HwidSpooferSetFieldMask(disk_mask) == 0, "Disk serial can be selected alone");
    Require(HwidSpooferApply() < 0,
            "Apply refuses a selected disk template without explicit binding");

    HWID_SPOOF_DEVICE_BINDING disk = DiskBinding();
    Require(HwidSpooferSetDeviceBindings(&disk, 1) == 0,
            "Explicit disk binding is accepted");
    Require(HwidSpooferSetDeviceBindings(nullptr, HWID_MAX_DEVICE_BINDINGS + 1u) < 0,
            "Binding count is bounded");

    HWID_SPOOF_DEVICE_BINDING duplicate[2] = { disk, disk };
    Require(HwidSpooferSetDeviceBindings(duplicate, 2) < 0,
            "Duplicate field and exact device key are rejected atomically");
    HWID_SPOOF_DEVICE_BINDING malformed = DiskBinding(L"disk-bad", L"ORIGINAL", L"DIFFERENT-LENGTH");
    Require(HwidSpooferSetDeviceBindings(&malformed, 1) < 0,
            "Serial binding replacement must preserve source length");
    malformed = DiskBinding(L"disk-bad", L"ORIGINAL", L"ORIG!NAL");
    Require(HwidSpooferSetDeviceBindings(&malformed, 1) < 0,
            "Serial binding format is validated");
    malformed = DiskBinding();
    malformed.Field = static_cast<HWID_SPOOF_FIELD>(HwidFieldCount);
    Require(HwidSpooferSetDeviceBindings(&malformed, 1) < 0,
            "Unconfirmed field bindings are rejected");
    malformed = MacBinding(L"adapter-bad", L"01:11:22:33:44:55", L"02:12:23:34:45:66");
    Require(HwidSpooferSetDeviceBindings(&malformed, 1) < 0,
            "Multicast original MAC binding is rejected");
    malformed = MacBinding(L"adapter-bad", L"00:11:22:33:44:55", L"001122334455");
    Require(HwidSpooferSetDeviceBindings(&malformed, 1) < 0,
            "Equivalent MAC octets cannot masquerade as a distinct replacement format");
    malformed = DiskBinding();
    std::fill_n(malformed.DeviceKey, _countof(malformed.DeviceKey), L'K');
    Require(HwidSpooferSetDeviceBindings(&malformed, 1) < 0,
            "Device key requires an in-bounds terminator");

    Require(HwidSpooferApply() == 0, "Valid explicit disk binding applies");
    WCHAR output[64]{};
    Require(HwidSpooferReadOverride(HwidFieldDiskSerial, L"disk-001",
                L"DISK-OLD1", output, _countof(output)) == 0 &&
            std::wcscmp(output, L"DISK-NEW1") == 0,
            "Exact device and original value resolve the disk replacement");
    Require(HwidSpooferReadOverride(HwidFieldDiskSerial, L"Disk-001",
                L"DISK-OLD1", output, _countof(output)) == 0 &&
            std::wcscmp(output, L"DISK-OLD1") == 0,
            "Device keys compare exactly and an unbound device passes through");
    Require(HwidSpooferReadOverride(HwidFieldDiskSerial, L"disk-001",
                L"DISK-DRIFT", output, _countof(output)) < 0 &&
            output[0] == L'\0',
            "Source drift for a bound device fails with empty output");
    WCHAR alias[64] = L"DISK-OLD1";
    Require(HwidSpooferReadOverride(HwidFieldDiskSerial, L"disk-001",
                alias, alias, _countof(alias)) == 0 &&
            std::wcscmp(alias, L"DISK-NEW1") == 0,
            "Bound replacement is safe when input and output alias");

    HWID_SPOOF_DEVICE_BINDING bindings[2] = { disk, MacBinding() };
    Require(HwidSpooferSetDeviceBindings(bindings, 2) < 0,
            "Bindings cannot change while active");
    Require(HwidSpooferSetFieldMask(both_mask) < 0,
            "Field mask cannot change while active");
    Require(HwidSpooferRemove() == 0, "Device binding state can be disabled");
    Require(HwidSpooferSetDeviceBindings(bindings, 2) == 0,
            "Disk and adapter bindings can be installed while inactive");
    Require(HwidSpooferSetFieldMask(both_mask) == 0,
            "Disk and MAC fields can be selected together");
    Require(HwidSpooferApply() == 0, "Both selected device fields have bindings");
    Require(HwidSpooferReadOverride(HwidFieldMacAddress, L"adapter-001",
                L"00:11:22:33:44:55", output, _countof(output)) == 0 &&
            std::wcscmp(output, L"02:12:23:34:45:66") == 0,
            "Exact adapter binding returns its configured replacement");
    Require(HwidSpooferReadOverride(HwidFieldMacAddress, L"adapter-other",
                L"00:11:22:33:44:55", output, _countof(output)) == 0 &&
            std::wcscmp(output, L"00:11:22:33:44:55") == 0,
            "Unbound adapter retains its current native address");
    HwidSpooferCleanup();
}

int main()
{
    TestFieldMapAndValidation();
    TestLifecycleAndMaskResolution();
    TestExplicitDeviceBindings();
    std::cout << "client_interceptor_tests: PASS\n";
    return 0;
}
