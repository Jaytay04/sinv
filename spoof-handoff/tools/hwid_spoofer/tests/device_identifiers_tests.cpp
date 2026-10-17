#include "device_identifiers.h"

#include <cassert>
#include <string>

int main() {
    using hwid_spoofer::detail::IsPlaceholderSerial;
    using hwid_spoofer::detail::IsValidMacAddress;
    using hwid_spoofer::detail::MapWmiPropertyToField;
    using hwid_spoofer::detail::ClassifyCpuSerialObservation;
    using hwid_spoofer::detail::CpuSerialObservation;

    assert(IsPlaceholderSerial(L""));
    assert(IsPlaceholderSerial(L"  Not provided  "));
    assert(IsPlaceholderSerial(L"Default string"));
    assert(IsPlaceholderSerial(L"To be filled by O.E.M."));
    assert(IsPlaceholderSerial(L"OEM fill"));
    assert(IsPlaceholderSerial(L"000000000000"));
    assert(!IsPlaceholderSerial(L"WD-WCC6Y7ABC123"));
    assert(!IsPlaceholderSerial(L"CPU-000123"));
    assert(ClassifyCpuSerialObservation(L"") ==
           CpuSerialObservation::NotExposedByFirmware);
    assert(ClassifyCpuSerialObservation(L"Not provided") ==
           CpuSerialObservation::NotExposedByFirmware);
    assert(ClassifyCpuSerialObservation(L"CPU-000123") ==
           CpuSerialObservation::Present);
    assert(ClassifyCpuSerialObservation(L"\u03A9-123") ==
           CpuSerialObservation::Present);

    assert(IsValidMacAddress(L"00:11:22:33:44:55"));
    assert(IsValidMacAddress(L"00-11-22-33-44-55"));
    assert(IsValidMacAddress(L"001122334455"));
    assert(IsValidMacAddress(L" 00:11:22:33:44:55 "));
    assert(!IsValidMacAddress(L"00:11:22:33:44"));
    assert(!IsValidMacAddress(L"00:11:22:33:44:5G"));
    assert(!IsValidMacAddress(L"00-11:22-33:44-55"));
    assert(!IsValidMacAddress(L"00:00:00:00:00:00"));
    assert(!IsValidMacAddress(L"FF:FF:FF:FF:FF:FF"));

    assert(MapWmiPropertyToField(L"Win32_DiskDrive", L"SerialNumber") ==
           "FakeDiskSerial");
    assert(MapWmiPropertyToField(L"win32_diskdrive", L"serialnumber") ==
           "FakeDiskSerial");
    assert(MapWmiPropertyToField(L"Win32_NetworkAdapter", L"MACAddress") ==
           "FakeMacAddress");
    assert(MapWmiPropertyToField(L"Win32_Processor", L"SerialNumber") ==
           "FakeCPUSerial");
    assert(MapWmiPropertyToField(L"Win32_Processor", L"ProcessorId").empty());
    assert(MapWmiPropertyToField(L"Win32_VideoController", L"PNPDeviceID").empty());
    assert(MapWmiPropertyToField(L"Win32_VideoController", L"SerialNumber").empty());

    return 0;
}
