#include "read_only_inventory.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using hwid_spoofer::DeviceIdentifier;

void Require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool Has(std::string_view text, std::string_view value)
{
    return text.find(value) != std::string_view::npos;
}

std::size_t Count(std::string_view text, std::string_view value)
{
    std::size_t count = 0u;
    std::size_t position = 0u;
    while ((position = text.find(value, position)) != std::string_view::npos) {
        ++count;
        position += value.size();
    }
    return count;
}

EfiVariable MakeVariable(const wchar_t* name,
                         std::uint32_t attributes,
                         std::uint64_t size,
                         std::uint64_t status)
{
    EfiVariable variable;
    variable.name = name;
    variable.guid.Data1 = 0x12345678u;
    variable.guid.Data2 = 0x9ABCu;
    variable.guid.Data3 = 0xDEF0u;
    variable.guid.Data4[0] = 0x12u;
    variable.guid.Data4[1] = 0x34u;
    variable.guid.Data4[2] = 0x56u;
    variable.guid.Data4[3] = 0x78u;
    variable.guid.Data4[4] = 0x9Au;
    variable.guid.Data4[5] = 0xBCu;
    variable.guid.Data4[6] = 0xDEu;
    variable.guid.Data4[7] = 0xF0u;
    variable.attributes = attributes;
    variable.size = size;
    variable.status = status;
    return variable;
}

void RequireAsciiCrlf(const std::string& report)
{
    Require(!report.empty(), "report must not be empty");
    for (std::size_t i = 0u; i < report.size(); ++i) {
        const auto ch = static_cast<unsigned char>(report[i]);
        Require(ch <= 0x7Fu, "report must contain ASCII bytes only");
        if (ch == static_cast<unsigned char>('\n')) {
            Require(i > 0u && report[i - 1u] == '\r',
                    "every line feed must be preceded by carriage return");
        }
        if (ch == static_cast<unsigned char>('\r')) {
            Require(i + 1u < report.size() && report[i + 1u] == '\n',
                    "every carriage return must be followed by line feed");
        }
    }
    Require(report.size() >= 2u &&
                report[report.size() - 2u] == '\r' &&
                report[report.size() - 1u] == '\n',
            "report must end with CRLF");
}

std::string BuildUuidReport(const std::string& uuid)
{
    return hwid_spoofer::FormatRuntimeIdentifierInventory(
        {}, {{"System UUID", uuid}}, {}, L"C:\\capture\\host-smbios.txt");
}

void TestSmbiosMappingsAndUuidMember()
{
    const std::vector<std::pair<std::string, std::string>> smbios = {
        {"System manufacturer", "MAINGEAR"},
        {"System product", "VYBE"},
        {"System serial number", "3080-26"},
        {"System UUID", "C88B4631-C88C-871B-A6D7-D8BBC1D85720"},
        {"Baseboard manufacturer", "Micro-Star International Co., Ltd."},
        {"Baseboard product", "PRO B660M-A CEC WIFI DDR4 (MS-7D37)"},
        {"Baseboard serial number", "07D3710_LC1E186916"},
        {"Processor serial number", "CPU-SERIAL-01"},
        {"Processor ID", "72060900FFFBEBBF"},
        {"Memory serial number", "00000000"},
    };

    const std::string report =
        hwid_spoofer::FormatRuntimeIdentifierInventory(
            {}, smbios, {}, L"C:\\capture\\host-smbios.txt");

    Require(Has(report,
                "SMBIOS provenance: supplied capture verified against current Windows GetSystemFirmwareTable('RSMB') at"),
            "report must identify the supplied SMBIOS capture and its verification");
    Require(Has(report, "SMBIOS fields (10); every supplied field is retained below:"),
            "report must preserve the full SMBIOS field count");
    Require(Has(report, "\"Processor ID\" = \"72060900FFFBEBBF\""),
            "unmapped SMBIOS fields must remain visible");
    Require(Has(report, "FakeSMBIOSSerial: CONFIRMED; sources=Windows raw SMBIOS field: System serial number"),
            "system serial maps to the SMBIOS serial config field");
    Require(Has(report, "FakeSMBIOSManufacturer: CONFIRMED; sources=Windows raw SMBIOS field: System manufacturer"),
            "system manufacturer maps to the SMBIOS manufacturer config field");
    Require(Has(report, "FakeSMBIOSProduct: CONFIRMED; sources=Windows raw SMBIOS field: System product"),
            "system product maps to the SMBIOS product config field");
    Require(Has(report, "FakeMotherboardSerial: CONFIRMED; sources=Windows raw SMBIOS field: Baseboard serial number"),
            "baseboard serial maps to the motherboard serial config field");
    Require(Has(report, "FakeMotherboardManufacturer: CONFIRMED; sources=Windows raw SMBIOS field: Baseboard manufacturer"),
            "baseboard manufacturer maps to the motherboard manufacturer config field");
    Require(Has(report, "FakeMotherboardProduct: CONFIRMED; sources=Windows raw SMBIOS field: Baseboard product"),
            "baseboard product maps to motherboard product");
    Require(Has(report, "FakeCPUSerial: CONFIRMED; sources=Windows raw SMBIOS field: Processor serial number"),
            "SMBIOS Type 4 processor serial maps only when populated");
    Require(Has(report, "FakeSMBIOSUUID: CONFIRMED; sources=Windows raw SMBIOS field: System UUID"),
            "validated System UUID maps to the appended UUID config member");
    Require(Has(report, "SMBIOS Type 2 has no separate board identifier"),
            "report must state that no distinct BoardId is available");
    Require(Has(report, "Spoofing policy: collected observations are review evidence only"),
            "report must distinguish collected values from spoof replacement values");
    Require(Has(report,
                "SMBIOS | \"System UUID\" | CONFIRMED -> MAPPED | FakeSMBIOSUUID"),
            "decision table must map the confirmed UUID source to the exact field");
    Require(!Has(report, "no HWID_SPOOF_CONFIG member"),
            "confirmed UUID must not retain a schema-gap assertion");
    RequireAsciiCrlf(report);
}

void TestInvalidUuidRemainsUncovered()
{
    for (const std::string& invalid : {
             "00000000-0000-0000-0000-000000000000",
             "FFFFFFFF-FFFF-FFFF-FFFF-FFFFFFFFFFFF",
             "C88B4631-C88C-871B-A6D7-D8BBC1D8572Z"}) {
        const std::string report = BuildUuidReport(invalid);
        Require(Has(report, "FakeSMBIOSUUID: UNCOVERED"),
                "zero, all-ones, or malformed System UUID must be uncovered");
        Require(Has(report,
                    "HWID config field | FakeSMBIOSUUID | UNCOVERED | FakeSMBIOSUUID"),
                "unconfirmed UUID must be in the final uncovered decision table");
        Require(!Has(report, "SMBIOS | \"System UUID\" | CONFIRMED -> MAPPED"),
                "invalid System UUID must not be a confirmed source");
        RequireAsciiCrlf(report);
    }
}

void TestRuntimeMetadataProvenance()
{
    const std::vector<EfiVariable> variables = {
        MakeVariable(L"SystemUUID", 0x00000004u, 16u, 0u),
    };
    const std::wstring snapshot_path =
        L"C:\\captures\\same-host\\vars\t\u00E9.txt";
    const std::string snapshot_report =
        hwid_spoofer::FormatRuntimeIdentifierInventory(
            variables, {}, {}, L"", snapshot_path);

    Require(Has(snapshot_report,
                "EFI metadata provenance: existing same-host runtime metadata snapshot at \"C:\\\\captures\\\\same-host\\\\vars\\u0009\\u00E9.txt\""),
            "snapshot provenance path must be escaped in ASCII");
    Require(Has(snapshot_report, "native runtime enumeration was not repeated"),
            "snapshot mode must say native runtime enumeration was not repeated");
    Require(Has(snapshot_report, "same-host status was asserted by the caller"),
            "snapshot mode must attribute the same-host assertion to its caller");
    Require(Has(snapshot_report, "EFI payload correspondence was not verified"),
            "snapshot mode must say payload correspondence was not verified");
    Require(Has(snapshot_report,
                "NAME=\"SystemUUID\"; ATTRS=0x00000004; SIZE=0x0000000000000010; STATUS=0x0000000000000000; decision=CANDIDATE -> DROPPED; target=-"),
            "snapshot provenance must not confirm an EFI name lead");
    RequireAsciiCrlf(snapshot_report);

    const std::string current_report =
        hwid_spoofer::FormatRuntimeIdentifierInventory(
            variables, {}, {}, L"");
    Require(Has(current_report,
                "EFI metadata provenance: current Windows runtime-visible variable enumeration."),
            "default mode must retain current-runtime provenance");
    Require(!Has(current_report, "native runtime enumeration was not repeated"),
            "default mode must not claim to reuse a snapshot");
    RequireAsciiCrlf(current_report);
}

void TestEveryRuntimeEfiLeadIsDropped()
{
    const std::vector<EfiVariable> variables = {
        MakeVariable(L"SystemSerialNumber", 0x00000004u, 0x12u, 0u),
        MakeVariable(L"SystemProductName", 0x00000006u, 0x20u, 7u),
        MakeVariable(L"BoardId", 0x00000004u, 4u, 0u),
        MakeVariable(L"SerialNumber", 0x00000004u, 8u, 0x8000000000000002ull),
        MakeVariable(L"MacAddress", 0x00000004u, 6u, 0u),
        MakeVariable(L"MachineProduct", 0x00000004u, 10u, 0u),
        MakeVariable(L"DiskSerial", 0x00000001u, 9u, 0u),
    };
    const std::vector<std::pair<std::string, std::string>> smbios = {
        {"System serial number", "REAL-SYSTEM-SERIAL"},
        {"System product", "REAL-SYSTEM-PRODUCT"},
        {"Baseboard product", "REAL-BOARD-PRODUCT"},
    };
    const std::string report = hwid_spoofer::FormatRuntimeIdentifierInventory(
        variables, smbios, {}, L"C:\\capture\\host-smbios.txt");

    Require(Has(report, "Runtime-access EFI variable count: 6"),
            "runtime-access variables must be counted");
    Require(Has(report, "Excluded EFI variables without EFI_VARIABLE_RUNTIME_ACCESS: 1"),
            "non-runtime variables must be counted as excluded");
    Require(Has(report, "EFI name lead count: 6"),
            "only runtime EFI name leads must be analyzed");
    Require(Count(report, "CANDIDATE -> DROPPED") == 12u,
            "each of six leads must be dropped in both analysis and final decision table");
    Require(Has(report, "NAME=\"SystemSerialNumber\"") &&
                Has(report, "NAME=\"BoardId\"") &&
                Has(report, "NAME=\"MacAddress\""),
            "each matching EFI name must remain visible as metadata");
    Require(Count(report, "EFI name lead | ") == 6u &&
                Count(report, " | CANDIDATE -> DROPPED | - | ") == 6u,
            "final decision table must explicitly drop every EFI lead with no target");
    Require(Has(report,
                "Name hints do not verify EFI payload semantics or value correspondence."),
            "every lead drop must state why its name is insufficient");
    Require(!Has(report, "CANDIDATE ONLY") && !Has(report, "unresolved") &&
                !Has(report, "targets=Fake"),
            "the final report must not leave EFI leads pending or mapped");
    Require(Has(report, "FakeSMBIOSSerial: CONFIRMED") &&
                Has(report, "FakeSMBIOSProduct: CONFIRMED"),
            "matching SMBIOS values must remain confirmed independently of dropped EFI names");
    Require(!Has(report, "FakeDiskSerial: CANDIDATE"),
            "EFI name heuristics must not leave a config field pending");
    Require(!Has(report, "NAME=\"DiskSerial\""),
            "non-runtime EFI variables must be excluded from lead analysis");
    RequireAsciiCrlf(report);
}

void TestDeviceObservationsAndDroppedValues()
{
    const std::vector<DeviceIdentifier> devices = {
        {"FakeDiskSerial", "Disk 0", "SATA-0-A1", "Windows storage query", "", false},
        {"FakeDiskSerial", "Disk 1", "NVME-1-B2", "Windows storage query", "", false},
        {"FakeDiskSerial", "Disk 2", "NVME-2-C3", "Windows storage query", "stable serial property", true},
        {"FakeDiskSerial", "Disk 3", "NVME-3-D4", "Windows storage query", "stable serial property", true},
        {"FakeMacAddress", "Ethernet adapter", "02-11-22-33-44-55",
         "Windows network adapter query", "locally administered current address", true},
        {"FakeGPUSerial", "Video controller", "",
         "WMI:Win32_VideoController(Name,PNPDeviceID)",
         "not obtainable at runtime by this Windows-only collector", false},
        {"FakeCPUSerial", "CPU package", "",
         "Win32_Processor.SerialNumber",
         "Win32_Processor.SerialNumber returned empty after successful query.", false},
        {"FakeCPUSerial", "CPU package retry", "",
         "Win32_Processor.SerialNumber",
         "WMI query failed with HRESULT 0x8004100E.", false},
        {"unmapped", "Processor", "72060900FFFBEBBF", "Win32_Processor.ProcessorId",
         "Processor ID is not a CPU serial", true},
    };
    const std::vector<std::pair<std::string, std::string>> smbios = {
        {"System serial number", "Default string"},
        {"Processor serial number", "To Be Filled By O.E.M."},
        {"System UUID", "00000000-0000-0000-0000-000000000000"},
    };

    const std::string report = hwid_spoofer::FormatRuntimeIdentifierInventory(
        {}, smbios, devices, L"provided-smbios.txt");

    Require(Has(report, "DEVICE: field=\"FakeDiskSerial\"; device=\"Disk 0\"") &&
                Has(report, "DEVICE: field=\"FakeDiskSerial\"; device=\"Disk 1\""),
            "all physical disk observations must remain separate");
    Require(Has(report, "Disk 0 (Windows storage query)") &&
                Has(report, "Disk 1 (Windows storage query)"),
            "both unconfirmed disk observations must appear in the final decisions");
    Require(Count(report, "Windows device | \"Disk 2 (Windows storage query)\" | CONFIRMED -> MAPPED | FakeDiskSerial") == 1u &&
                Count(report, "Windows device | \"Disk 3 (Windows storage query)\" | CONFIRMED -> MAPPED | FakeDiskSerial") == 1u,
            "multiple confirmed disk sources must map separately without selection");
    Require(Has(report, "FakeDiskSerial: CONFIRMED") &&
                Has(report, "Disk 2 (Windows storage query)") &&
                Has(report, "Disk 3 (Windows storage query)"),
            "confirmed disk values must map to the exact disk serial field");
    Require(Has(report, "FakeMacAddress: CONFIRMED") &&
                Has(report, "Windows device | \"Ethernet adapter (Windows network adapter query)\" | CONFIRMED -> MAPPED | FakeMacAddress"),
            "confirmed current MAC maps to the exact MAC field");
    Require(Has(report, "DEVICE: field=\"FakeGPUSerial\"; device=\"Video controller\"; value=\"\"") &&
                Has(report, "; state=UNCOVERED; target=FakeGPUSerial;"),
            "empty GPU serial remains uncovered");
    Require(Has(report, "FakeGPUSerial: UNCOVERED") &&
                Has(report, "not obtainable at runtime by this Windows-only collector"),
            "GPU uncovered status must state runtime limitation");
    Require(Has(report,
                "HWID config field | FakeGPUSerial | UNCOVERED | FakeGPUSerial"),
            "uncovered GPU field must have an explicit disabled/zero policy");
    Require(Has(report, "FakeCPUSerial: UNCOVERED") &&
                Has(report, "Processor ID is not a CPU serial"),
            "ProcessorId must not be substituted for CPU serial");
    Require(Has(report,
                "Windows device | \"CPU package (Win32_Processor.SerialNumber)\" | UNCOVERED | FakeCPUSerial | \"Win32_Processor.SerialNumber returned empty after successful query.\""),
            "a successful empty CPU result must retain its not-exposed note in the final table");
    Require(Has(report,
                "Windows device | \"CPU package retry (Win32_Processor.SerialNumber)\" | UNCOVERED | FakeCPUSerial | \"WMI query failed with HRESULT 0x8004100E.\""),
            "a CPU query error must retain its distinct note in the final table");
    Require(Has(report, "Windows device | \"Processor (Win32_Processor.ProcessorId)\" | DROPPED | -"),
            "nonempty unconfirmed ProcessorId must be explicitly dropped");
    Require(Has(report, "FakeSMBIOSUUID: UNCOVERED"),
            "all-zero UUID must remain uncovered");
    Require(Has(report, "FakeSMBIOSSerial: UNCOVERED"),
            "placeholder system serial must remain uncovered");
    Require(!Has(report, "CANDIDATE ONLY") && !Has(report, "CANDIDATE |"),
            "unconfirmed nonempty device values must not remain pending");
    const std::vector<DeviceIdentifier> confirmed_gpu = {
        {"FakeGPUSerial", "GPU", "GPU-SERIAL-42", "documented vendor query",
         "stable device serial property", true},
    };
    const std::string gpu_report = hwid_spoofer::FormatRuntimeIdentifierInventory(
        {}, {}, confirmed_gpu, L"");
    Require(Has(gpu_report, "FakeGPUSerial: CONFIRMED"),
            "a future confirmed GPU source must map when supplied");
    Require(!Has(gpu_report,
                 "FakeGPUSerial: CONFIRMED; sources=Windows device observation: GPU (documented vendor query); observed source retained for review; replacement value remains uninitialized and spoofing disabled by this collector; not obtainable at runtime"),
            "GPU runtime-unobtainable wording must not contradict a confirmed source");
    RequireAsciiCrlf(gpu_report);

    RequireAsciiCrlf(report);
}

void TestEscapingAndDroppedLead()
{
    const std::vector<EfiVariable> variables = {
        MakeVariable(L"FirmwareProduct", 0x00000004u, 1u, 0u),
        MakeVariable(L"Fan\t\u00E9UUID", 0x00000004u, 3u, 0u),
    };
    const std::vector<std::pair<std::string, std::string>> smbios = {
        {"Odd\x01Label", "Acme\tInc\n\xFF"},
        {"System manufacturer", "Odd\tMaker"},
    };

    const std::string report = hwid_spoofer::FormatRuntimeIdentifierInventory(
        variables, smbios, {}, L"C:\\Users\\Jos\u00E9\\host\n.txt");

    Require(Has(report, "Fan\\u0009\\u00E9UUID"),
            "variable-name controls and non-ASCII code units must be escaped");
    Require(Has(report, "\"Odd\\x01Label\" = \"Acme\\x09Inc\\x0A\\xFF\""),
            "SMBIOS byte controls and non-ASCII bytes must be escaped");
    Require(Has(report, "C:\\\\Users\\\\Jos\\u00E9\\\\host\\u000A.txt"),
            "SMBIOS provenance path must be escaped as ASCII");
    Require(Has(report,
                "EFI name lead: GUID={12345678-9ABC-DEF0-1234-56789ABCDEF0}; NAME=\"FirmwareProduct\""),
            "heuristic product lead must remain visible");
    Require(Has(report, "decision=CANDIDATE -> DROPPED; target=-"),
            "name lead with context-free PRODUCT must be dropped explicitly");
    Require(Has(report, "FakeSMBIOSManufacturer: CONFIRMED"),
            "valid system manufacturer value remains a confirmed source");
    RequireAsciiCrlf(report);
}

}  // namespace

int main()
{
    try {
        TestSmbiosMappingsAndUuidMember();
        TestInvalidUuidRemainsUncovered();
        TestRuntimeMetadataProvenance();
        TestEveryRuntimeEfiLeadIsDropped();
        TestDeviceObservationsAndDroppedValues();
        TestEscapingAndDroppedLead();
        std::cout << "PASS: read-only identifier inventory mapping tests\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
